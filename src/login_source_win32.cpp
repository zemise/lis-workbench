#include "login_source_win32.h"
#ifdef _WIN32
#include "update_manifest.h"
#include "login_source_versions.h"
#include "log.h"
#include "search_text.h"
#include <windows.h>
#include <tlhelp32.h>
#include <wbemidl.h>
#include <shellapi.h>
#include <map>
#include <mutex>
#include <array>
#include <memory>

namespace login {
namespace {
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
};
std::uint64_t ticks(FILETIME f) {
    return (std::uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime;
}
std::wstring canonical_path(const std::wstring &path) {
    Handle file(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (file.value == INVALID_HANDLE_VALUE)
        return {};
    std::array<wchar_t, 32768> buffer{};
    DWORD n = GetFinalPathNameByHandleW(file.value, buffer.data(), DWORD(buffer.size()),
                                        FILE_NAME_NORMALIZED);
    return n && n < buffer.size() ? std::wstring(buffer.data(), n) : std::wstring{};
}
bool equal_path(const std::wstring &a, const std::wstring &b) {
    auto x = canonical_path(a), y = canonical_path(b);
    return !x.empty() && !y.empty() &&
           CompareStringOrdinal(x.c_str(), -1, y.c_str(), -1, TRUE) == CSTR_EQUAL;
}
std::wstring path_join(std::wstring dir, const wchar_t *file) {
    while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/'))
        dir.pop_back();
    return dir + L"\\" + file;
}
std::vector<unsigned char> token_user(HANDLE process) {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &raw))
        return {};
    Handle token(raw);
    DWORD n = 0;
    GetTokenInformation(raw, TokenUser, nullptr, 0, &n);
    if (!n || n > 65536)
        return {};
    std::vector<unsigned char> data(n);
    if (!GetTokenInformation(raw, TokenUser, data.data(), n, &n))
        return {};
    return data;
}
struct Process {
    DWORD pid = 0, parent = 0;
    std::uint64_t created = 0;
    std::wstring path;
};
bool process_info(DWORD pid, DWORD parent, const std::wstring &expected, Process &out,
                  std::wstring *error = nullptr) {
    auto fail = [&](const wchar_t *stage) {
        if (error)
            *error = std::wstring(stage) + L"（错误码 " + std::to_wstring(GetLastError()) + L"）";
        return false;
    };
    Handle h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
    if (!h.value)
        return fail(L"无法打开进程");
    if (WaitForSingleObject(h.value, 0) != WAIT_TIMEOUT)
        return fail(L"进程已退出或无法核实");
    std::array<wchar_t, 32768> path{};
    DWORD count = DWORD(path.size());
    if (!QueryFullProcessImageNameW(h.value, 0, path.data(), &count))
        return fail(L"无法读取进程路径");
    const std::wstring actual(path.data(), count);
    if (!expected.empty() && !equal_path(actual, expected)) {
        if (error)
            *error = L"进程不在指定安装目录";
        return false;
    }
    DWORD ours = 0, theirs = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &ours) || !ProcessIdToSessionId(pid, &theirs))
        return fail(L"无法读取进程会话");
    if (ours != theirs) {
        if (error)
            *error = L"进程属于其他 Windows 会话";
        return false;
    }
    auto a = token_user(GetCurrentProcess()), b = token_user(h.value);
    if (a.empty() || b.empty())
        return fail(L"无法核实 Windows 用户权限");
    if (!EqualSid(reinterpret_cast<TOKEN_USER *>(a.data())->User.Sid,
                  reinterpret_cast<TOKEN_USER *>(b.data())->User.Sid)) {
        if (error)
            *error = L"进程属于其他 Windows 用户";
        return false;
    }
    FILETIME created{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(h.value, &created, &exit, &kernel, &user))
        return fail(L"无法读取进程创建时间");
    out = {pid, parent, ticks(created), actual};
    return true;
}
// SourceGuard scans repeatedly. Cache successful digests by file identity,
// length and modification time; hold a read-only file open while hashing.
bool file_digest(const std::wstring &path, std::string &hash, std::string &error) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) {
        error = "open failed win32=" + std::to_string(GetLastError());
        return false;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.value, &info)) {
        error = "file information failed win32=" + std::to_string(GetLastError());
        return false;
    }
    struct Entry {
        DWORD volume = 0, indexHigh = 0, indexLow = 0;
        std::uint64_t size = 0, modified = 0;
        std::string digest;
    };
    static std::mutex mutex;
    static std::map<std::wstring, Entry> cache;
    std::lock_guard<std::mutex> lock(mutex);
    const auto size = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    auto found = cache.find(path);
    if (found != cache.end()) {
        const auto &e = found->second;
        if (e.volume == info.dwVolumeSerialNumber && e.indexHigh == info.nFileIndexHigh &&
            e.indexLow == info.nFileIndexLow && e.size == size &&
            e.modified == ticks(info.ftLastWriteTime)) {
            hash = e.digest;
            return true;
        }
    }
    if (!lis_update::sha256_file_hex(path, hash, error))
        return false;
    if (cache.size() >= 128)
        cache.clear();
    cache[path] = {info.dwVolumeSerialNumber,   info.nFileIndexHigh,
                   info.nFileIndexLow,          size,
                   ticks(info.ftLastWriteTime), hash};
    return true;
}
void source_warning(const std::string &message) {
    static std::mutex mutex;
    static std::map<std::string, DWORD> recent;
    std::lock_guard<std::mutex> lock(mutex);
    const DWORD now = GetTickCount();
    auto found = recent.find(message);
    if (found != recent.end() && DWORD(now - found->second) < 60000)
        return;
    if (recent.size() >= 256)
        recent.clear();
    recent[message] = now;
    LOG_WARN(message);
}
std::wstring parent_directory(const std::wstring &path) {
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : path.substr(0, slash);
}
bool process_path(DWORD pid, std::wstring &path) {
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process.value)
        return false;
    std::array<wchar_t, 32768> buffer{};
    DWORD length = DWORD(buffer.size());
    if (!QueryFullProcessImageNameW(process.value, 0, buffer.data(), &length))
        return false;
    path.assign(buffer.data(), length);
    return true;
}
bool file_size(const std::wstring &path, std::uint64_t &size) {
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return false;
    size = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    return true;
}
// A verified component's version resource names its installation filename.
// Bind to that path as well as its digest so an unrelated backup copy cannot
// stand in for the component used by the running installation.
bool installed_component_path(const std::wstring &path) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!size || size > 1024 * 1024)
        return false;
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
        return false;
    struct Translation {
        WORD language, codepage;
    };
    Translation *translations = nullptr;
    UINT bytes = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation",
                        reinterpret_cast<void **>(&translations), &bytes))
        return false;
    for (UINT i = 0; i < bytes / sizeof(Translation); ++i) {
        wchar_t key[96]{};
        swprintf(key, 96, L"\\StringFileInfo\\%04x%04x\\OriginalFilename", translations[i].language,
                 translations[i].codepage);
        wchar_t *value = nullptr;
        UINT count = 0;
        if (!VerQueryValueW(data.data(), key, reinterpret_cast<void **>(&value), &count) ||
            !value || count < 2 || count > 32768)
            continue;
        if (value[count - 1] != L'\0')
            continue;
        std::wstring name(value, count - 1);
        if (name.empty() || name.find_first_of(L"\\/:") != std::wstring::npos || name == L"." ||
            name == L"..")
            continue;
        if (equal_path(path, path_join(parent_directory(path), name.c_str())))
            return true;
    }
    return false;
}
struct Installation {
    const SourceVersion *version = nullptr;
    std::array<std::wstring, 4> paths;
};
Installation verified_installation(const std::wstring &executable, std::wstring &error,
                                   const std::function<bool()> &stopped) {
    const auto directory = parent_directory(executable);
    std::string executableHash, why;
    if (!file_digest(executable, executableHash, why)) {
        error = L"无法校验来源程序文件，请检查安装完整性";
        return {};
    }
    for (const auto &version : supported_source_versions) {
        if (executableHash != version.files[0].sha256)
            continue;
        Installation installation;
        installation.paths[0] = executable;
        WIN32_FIND_DATAW file{};
        HANDLE raw = FindFirstFileW(path_join(directory, L"*").c_str(), &file);
        if (raw == INVALID_HANDLE_VALUE) {
            error = L"无法枚举来源安装目录";
            return {};
        }
        struct FindHandle {
            HANDLE value;
            ~FindHandle() { FindClose(value); }
        } find{raw};
        do {
            if (stopped())
                return {};
            if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            const auto size = (std::uint64_t(file.nFileSizeHigh) << 32) | file.nFileSizeLow;
            for (std::size_t i = 1; i < version.files.size(); ++i) {
                const auto &component = version.files[i];
                if (!installation.paths[i].empty() || size != component.size)
                    continue;
                std::string hash;
                auto path = path_join(directory, file.cFileName);
                if (file_digest(path, hash, why) &&
                    source_component_matches(component, size, hash) &&
                    installed_component_path(path))
                    installation.paths[i] = std::move(path);
            }
        } while (FindNextFileW(raw, &file));
        if (GetLastError() != ERROR_NO_MORE_FILES) {
            error = L"来源安装目录枚举未完成";
            return {};
        }
        bool complete = true;
        for (std::size_t i = 0; i < version.files.size(); ++i) {
            if (installation.paths[i].empty()) {
                complete = false;
                source_warning(
                    std::string("Login source component missing or unsupported component=") +
                    version.files[i].component);
            }
        }
        if (complete) {
            installation.version = &version;
            return installation;
        }
    }
    error = L"来源组件缺失或文件版本尚未支持";
    return {};
}
// WMI is used only for one already-scoped helper PID. Never query the main
// program's command line, which may carry credentials in other launch modes.
class Commands {
    bool uninit = false;
    IWbemServices *service = nullptr;
    std::wstring failure = L"WMI 初始化未完成";
    void failed(const wchar_t *stage, HRESULT hr) {
        failure = std::wstring(stage) + L"（HRESULT " +
                  std::to_wstring(static_cast<unsigned long>(hr)) + L"）";
    }

  public:
    Commands() {
        auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        uninit = SUCCEEDED(hr);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            failed(L"COM 初始化失败", hr);
            return;
        }
        IWbemLocator *locator = nullptr;
        hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                              reinterpret_cast<void **>(&locator));
        if (FAILED(hr)) {
            failed(L"WMI 创建失败", hr);
            return;
        }
        BSTR ns = SysAllocString(L"ROOT\\CIMV2");
        hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &service);
        SysFreeString(ns);
        locator->Release();
        if (FAILED(hr)) {
            failed(L"WMI 连接失败", hr);
            service = nullptr;
            return;
        }
        hr = CoSetProxyBlanket(service, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                               RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr,
                               EOAC_NONE);
        if (FAILED(hr)) {
            failed(L"WMI 权限设置失败", hr);
            service->Release();
            service = nullptr;
        }
    }
    ~Commands() {
        if (service)
            service->Release();
        if (uninit)
            CoUninitialize();
    }
    bool read(DWORD pid, std::vector<std::wstring> &out, std::wstring &error) {
        error = L"辅助进程命令行无法读取（WMI 或权限异常）";
        if (!service) {
            error = failure;
            return false;
        }
        auto query =
            L"SELECT CommandLine FROM Win32_Process WHERE ProcessId=" + std::to_wstring(pid);
        BSTR language = SysAllocString(L"WQL"), statement = SysAllocString(query.c_str());
        IEnumWbemClassObject *result = nullptr;
        auto hr = service->ExecQuery(language, statement,
                                     WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
                                     &result);
        SysFreeString(language);
        SysFreeString(statement);
        if (FAILED(hr) || !result) {
            failed(L"WMI 查询失败", hr);
            error = failure;
            return false;
        }
        IWbemClassObject *row = nullptr;
        ULONG count = 0;
        hr = result->Next(3000, 1, &row, &count);
        result->Release();
        if (FAILED(hr) || count != 1 || !row) {
            if (row)
                row->Release();
            failed(L"WMI 查询超时、进程退出或结果不可用", hr);
            error = failure;
            return false;
        }
        VARIANT value;
        VariantInit(&value);
        hr = row->Get(L"CommandLine", 0, &value, nullptr, nullptr);
        row->Release();
        bool ok = false;
        if (FAILED(hr)) {
            failed(L"辅助进程命令行读取失败", hr);
            error = failure;
        }
        if (SUCCEEDED(hr) && value.vt == VT_BSTR && value.bstrVal &&
            SysStringLen(value.bstrVal) < 32768) {
            int argc = 0;
            auto argv = CommandLineToArgvW(value.bstrVal, &argc);
            if (argv) {
                error = L"辅助进程参数格式尚未支持";
                if (argc == 7) {
                    for (int i = 1; i < argc; ++i)
                        out.emplace_back(argv[i]);
                    ok = true;
                }
                LocalFree(argv);
            }
        }
        VariantClear(&value);
        return ok;
    }
};
struct WindowProbe {
    DWORD pid = 0;
    bool locked = false;
};
BOOL CALLBACK labels(HWND child, LPARAM param) {
    if (!IsWindowVisible(child))
        return TRUE;
    wchar_t cls[128]{};
    GetClassNameW(child, cls, 128);
    std::wstring c = cls;
    if (c != L"STATIC" && c != L"BUTTON" && c.find(L"WindowsForms10.STATIC.") != 0 &&
        c.find(L"WindowsForms10.BUTTON.") != 0)
        return TRUE;
    wchar_t text[1024]{};
    GetWindowTextW(child, text, 1024);
    std::wstring s = text;
    if (s.find(L"系统已经自动锁屏") != std::wstring::npos ||
        (s.find(L"当前系统已被用户") != std::wstring::npos &&
         s.find(L"锁定") != std::wstring::npos))
        reinterpret_cast<WindowProbe *>(param)->locked = true;
    return TRUE;
}
BOOL CALLBACK windows(HWND window, LPARAM param) {
    auto *p = reinterpret_cast<WindowProbe *>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == p->pid && IsWindowVisible(window))
        EnumChildWindows(window, labels, param);
    return TRUE;
}
} // namespace
SourceScan scan_sources(const std::wstring &directory, const std::function<bool()> &cancelled) {
    SourceScan scan;
    auto stopped = [&] { return cancelled && cancelled(); };
    auto report = [&](DWORD pid, const std::wstring &reason) {
        scan.diagnostics.push_back(reason);
        source_warning("Login source pid=" + std::to_string(pid) +
                       " reason=" + search::wide_to_utf8(reason));
    };
    try {
        static std::mutex scanMutex;
        std::unique_lock<std::mutex> onlyScan(scanMutex, std::try_to_lock);
        if (!onlyScan.owns_lock()) {
            scan.diagnostics.push_back(L"来源识别正在进行，请稍后刷新");
            return scan;
        }
        if (stopped())
            return scan;
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (snapshot.value == INVALID_HANDLE_VALUE) {
            report(0, L"无法枚举进程");
            return scan;
        }
        struct Candidate {
            DWORD pid, parent;
            std::wstring path;
        };
        std::vector<Candidate> mainCandidates, helperCandidates;
        PROCESSENTRY32W item{};
        item.dwSize = sizeof(item);
        if (!Process32FirstW(snapshot.value, &item)) {
            report(0, L"无法读取进程列表");
            return scan;
        }
        // Read only executable paths here. Size filters avoid hashing unrelated
        // applications; command lines remain restricted to verified helpers.
        do {
            if (stopped())
                return {};
            std::wstring path;
            std::uint64_t size = 0;
            if (!process_path(item.th32ProcessID, path) || !file_size(path, size))
                continue;
            if (source_size_candidate(SourceComponent::main_executable, size))
                mainCandidates.push_back({item.th32ProcessID, item.th32ParentProcessID, path});
            if (source_size_candidate(SourceComponent::session_helper, size))
                helperCandidates.push_back({item.th32ProcessID, item.th32ParentProcessID, path});
        } while (Process32NextW(snapshot.value, &item));
        if (GetLastError() != ERROR_NO_MORE_FILES) {
            report(0, L"进程枚举未完成");
            return scan;
        }
        std::vector<Process> mains, helpers;
        std::map<DWORD, std::wstring> helperErrors;
        std::map<DWORD, Installation> installations;
        for (const auto &candidate : mainCandidates) {
            if (stopped())
                return {};
            const auto dir = parent_directory(candidate.path);
            // A candidate's filename is irrelevant; the executable digest is
            // required before the remaining components or process identity.
            std::string hash, why;
            if (!file_digest(candidate.path, hash, why))
                continue;
            bool known = false;
            for (const auto &version : supported_source_versions)
                known |= hash == version.files[0].sha256;
            if (!known)
                continue;
            if (!directory.empty() && !equal_path(dir, directory)) {
                scan.unreadable_mains.push_back(candidate.pid);
                report(candidate.pid, L"来源程序不在配置的安装目录");
                continue;
            }
            std::wstring error;
            Process process;
            auto installation = verified_installation(candidate.path, error, stopped);
            if (!installation.version ||
                !process_info(candidate.pid, candidate.parent, candidate.path, process, &error)) {
                scan.unreadable_mains.push_back(candidate.pid);
                report(candidate.pid, error.empty() ? L"来源识别未完成" : error);
                continue;
            }
            installations.emplace(process.pid, std::move(installation));
            mains.push_back(std::move(process));
        }
        for (const auto &candidate : helperCandidates) {
            auto main = installations.find(candidate.parent);
            if (main == installations.end() || !equal_path(candidate.path, main->second.paths[3]))
                continue;
            std::string hash, why;
            if (!file_digest(candidate.path, hash, why) ||
                hash != main->second.version->files[3].sha256)
                continue;
            std::wstring error;
            Process process;
            if (process_info(candidate.pid, candidate.parent, candidate.path, process, &error))
                helpers.push_back(std::move(process));
            else
                helperErrors[candidate.parent] = error;
        }
        for (const auto &m : mains) {
            WindowProbe p{m.pid};
            EnumWindows(windows, reinterpret_cast<LPARAM>(&p));
            scan.mains.push_back({m.pid, m.created, p.locked});
        }
        if (mains.empty()) {
            if (scan.unreadable_mains.empty())
                scan.diagnostics.push_back(L"未检测到运行中的来源程序");
            scan.complete = true;
            return scan;
        }
        std::unique_ptr<Commands> commands;
        wchar_t machine[MAX_COMPUTERNAME_LENGTH + 1]{};
        DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
        if (!GetComputerNameW(machine, &size)) {
            report(0, L"无法读取本机名称");
            return scan;
        }
        for (const auto &main : mains) {
            bool sawHelper = false;
            const auto &installation = installations.at(main.pid);
            for (const auto &helper : helpers) {
                if (stopped())
                    return {};
                if (helper.parent != main.pid)
                    continue;
                sawHelper = true;
                if (!equal_path(parent_directory(helper.path), parent_directory(main.path)) ||
                    main.created >= helper.created) {
                    report(helper.pid, L"辅助进程路径或创建时间不匹配");
                    continue;
                }
                std::vector<std::wstring> args;
                Source s;
                std::wstring error;
                if (!commands)
                    commands = std::make_unique<Commands>();
                if (!commands->read(helper.pid, args, error)) {
                    report(helper.pid, error);
                    scan.retryable = true;
                    continue;
                }
                if (installation.version->protocol != SourceProtocol::slash_numeric_v1 ||
                    !parse_source_arguments(args, s)) {
                    report(helper.pid, L"辅助进程登录参数不符合已核实协议");
                    continue;
                }
                if (s.main_pid != main.pid) {
                    report(helper.pid, L"辅助进程声明的主进程不匹配");
                    continue;
                }
                HWND hwnd = reinterpret_cast<HWND>(std::uintptr_t(s.main_hwnd));
                DWORD owner = 0;
                GetWindowThreadProcessId(hwnd, &owner);
                if (!IsWindow(hwnd) || !IsWindowVisible(hwnd) || owner != main.pid ||
                    GetWindow(hwnd, GW_OWNER)) {
                    report(helper.pid, L"登录窗口不存在、不可见或归属不匹配");
                    scan.retryable = true;
                    continue;
                }
                s.main_created = main.created;
                s.helper_pid = helper.pid;
                s.helper_created = helper.created;
                s.machine = machine;
                s.main_enabled = IsWindowEnabled(hwnd) != FALSE;
                Process afterMain, afterHelper;
                if (!process_info(main.pid, main.parent, main.path, afterMain) ||
                    afterMain.created != main.created ||
                    !process_info(helper.pid, helper.parent, helper.path, afterHelper) ||
                    afterHelper.created != helper.created) {
                    report(helper.pid, L"来源进程在识别期间发生变化");
                    scan.retryable = true;
                    continue;
                }
                scan.sources.push_back(std::move(s));
            }
            if (!sawHelper) {
                auto error = helperErrors.find(main.pid);
                report(main.pid, error == helperErrors.end()
                                     ? L"检测到来源程序，但登录辅助进程尚未出现"
                                     : error->second);
                scan.retryable = true;
            }
        }
        scan.complete = !stopped();
        return scan;
    } catch (...) {
        report(0, L"来源识别发生异常");
        scan.complete = false;
        return scan;
    }
}
} // namespace login
#endif
