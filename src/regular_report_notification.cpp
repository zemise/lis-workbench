#include "regular_report_notification.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "search_text.h"

#include <windows.h>

#include <filesystem>
#include <memory>

namespace {
struct CloseHandleDeleter {
    void operator()(void* handle) const { CloseHandle(handle); }
};
using Handle = std::unique_ptr<void, CloseHandleDeleter>;

// Use the workstation's Trasen assembly for the existing doctor-station message format.
constexpr auto kNotifyCommand = LR"PS(
$ErrorActionPreference='Stop'
try {
    $reader=New-Object IO.StreamReader([Console]::OpenStandardInput(),[Text.Encoding]::UTF8)
    $parts=$reader.ReadToEnd().Split([char]0)
    $assembly=[Reflection.Assembly]::LoadFrom([IO.Path]::Combine($parts[0],'TrasenMessage.dll'))
    $type=$assembly.GetType('TrasenMessage.MessageCommunication',$true)
    $message=[Activator]::CreateInstance($type,@([Guid]::Empty,$parts[1],$parts[2],0,3))
    $message.SendTime=[DateTime]::Now
    $client=New-Object Net.Sockets.UdpClient
    try {
        $client.EnableBroadcast=$true
        $stream=New-Object IO.MemoryStream
        try {
            $formatter=New-Object Runtime.Serialization.Formatters.Binary.BinaryFormatter
            $formatter.Serialize($stream,$message)
            $packet=$stream.GetBuffer()
        } finally { $stream.Dispose() }
        $failed=$false
        for ($i=3;$i -lt $parts.Count;$i+=2) {
            try {
                $ip=[Net.IPAddress]::Parse($parts[$i])
                if ($ip.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork) { throw 'address family' }
                $port=[int]$parts[$i+1]
                if ($port -eq 0) { $port=4667 }
                $target=New-Object Net.IPEndPoint($ip,$port)
                [void]$client.Send($packet,$packet.Length,$target)
            } catch { $failed=$true }
        }
    } finally { $client.Dispose() }
    if ($failed) { exit 1 }
    exit 0
} catch { exit 1 }
)PS";
}

bool regularNotifyDoctorStations(const search::ReportNotification& notification,
                                 const std::wstring& sender) {
    if (notification.recipients.empty()) return true;

    const std::filesystem::path directory(search::trasen_install_directory());
    std::error_code ec;
    if (!directory.is_absolute() ||
        !std::filesystem::is_regular_file(directory / L"TrasenMessage.dll", ec)) return false;
    wchar_t windows[MAX_PATH]{};
    const UINT length = GetWindowsDirectoryW(windows, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return false;
    const std::filesystem::path system = std::filesystem::path(windows);
    auto powershell = system / L"SysWOW64" / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
    if (!std::filesystem::is_regular_file(powershell, ec))
        powershell = system / L"System32" / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";

    std::string input = search::wide_to_utf8(directory.wstring()) + '\0' +
                        notification.summary + '\0' + search::wide_to_utf8(sender);
    for (const auto& [ip, port] : notification.recipients) {
        input += '\0';
        input += ip;
        input += '\0';
        input += port;
    }
    constexpr DWORD pipeCapacity = 64 * 1024;
    if (input.size() > pipeCapacity) return false;

    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &security, pipeCapacity)) return false;
    Handle reader(read), writer(write);
    if (!SetHandleInformation(writer.get(), HANDLE_FLAG_INHERIT, 0)) return false;
    HANDLE output = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                &security, OPEN_EXISTING, 0, nullptr);
    if (output == INVALID_HANDLE_VALUE) return false;
    Handle sink(output);

    std::wstring command = L"\"" + powershell.wstring() +
        L"\" -NoProfile -NonInteractive -Command \"& {" + kNotifyCommand + L"}\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = reader.get();
    startup.hStdOutput = sink.get();
    startup.hStdError = sink.get();
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, directory.c_str(), &startup, &process)) return false;
    Handle child(process.hProcess), thread(process.hThread);
    reader.reset();
    DWORD written = 0;
    const bool fed = WriteFile(writer.get(), input.data(), static_cast<DWORD>(input.size()),
                               &written, nullptr) && written == input.size();
    writer.reset();
    if (WaitForSingleObject(child.get(), 8000) != WAIT_OBJECT_0) {
        TerminateProcess(child.get(), 1);
        return false;
    }
    DWORD code = 1;
    return fed && GetExitCodeProcess(child.get(), &code) && code == 0;
}

#endif
