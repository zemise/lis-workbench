#include "login_source_win32.h"
#include "search_text.h"
#include "log.h"
#include "login_database.h"
#include "app_settings_io.h"
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <cstdlib>

namespace {
void check(bool ok, const char *message) {
    if (!ok) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
struct Fixture {
    std::wstring directory, exe;
    PROCESS_INFORMATION process{};
    ~Fixture() {
        if (process.hProcess) {
            TerminateProcess(process.hProcess, 0);
            WaitForSingleObject(process.hProcess, 5000);
            CloseHandle(process.hProcess);
        }
        if (process.hThread)
            CloseHandle(process.hThread);
        if (!exe.empty())
            DeleteFileW(exe.c_str());
        if (!directory.empty())
            RemoveDirectoryW(directory.c_str());
    }
};
} // namespace
int main() {
    int argc = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &argc);
    check(args != nullptr, "command line available");
    bool fixture = argc == 2 && std::wstring(args[1]) == L"--fixture";
    bool verify = argc >= 3 && std::wstring(args[1]) == L"--verify-config";
    bool live = verify || (argc >= 2 && std::wstring(args[1]) == L"--scan");
    std::wstring directory = argc >= 3 ? args[2] : L"";
    const auto config = verify ? directory : std::wstring{};
    if (verify)
        directory.clear();
    LocalFree(args);
    if (fixture) {
        Sleep(30000);
        return 0;
    }
    if (live) {
        wchar_t temp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, temp);
        applog::init(std::wstring(temp) + L"LIS-login-source-diagnostics");
        auto s = login::scan_sources(directory, [] { return false; });
        std::cout << "complete=" << s.complete << " mains=" << s.mains.size()
                  << " sources=" << s.sources.size() << " unreadable=" << s.unreadable_mains.size()
                  << '\n';
        for (const auto &reason : s.diagnostics)
            std::cout << search::wide_to_utf8(reason) << '\n';
        bool accountVerified = !verify;
        if (verify) {
            // Read the deployed configuration through an expendable copy. The
            // settings reader may normalize legacy INI encodings in place.
            wchar_t copy[MAX_PATH]{};
            check(GetTempFileNameW(temp, L"lis", 0, copy) != 0, "temporary configuration path");
            if (!CopyFileW(config.c_str(), copy, FALSE)) {
                DeleteFileW(copy);
                check(false, "cannot copy verification configuration");
            }
            auto settings = search::load_settings(copy);
            DeleteFileW(copy);
            for (const auto &source : s.sources) {
                auto account =
                    login::validate_source_account(settings.db, source, [] { return false; });
                accountVerified = accountVerified || account.status == login::Status::success;
                std::cout << "account_status=" << int(account.status)
                          << " reason=" << search::wide_to_utf8(account.diagnostic) << '\n';
            }
        }
        std::cout
            << "Detailed file hashes/errors: temporary directory LIS-login-source-diagnostics\n";
        return s.complete && !s.sources.empty() && accountVerified ? 0 : 1;
    }
    auto missing =
        login::scan_sources(L"Z:\\NoSuchInstallationForSourceTest", [] { return false; });
    check(missing.sources.empty(), "excluded installation cannot produce identity");
    auto cancelled = login::scan_sources(L"", [] { return true; });
    check(!cancelled.complete && cancelled.sources.empty(),
          "cancelled discovery cannot publish identity");
    Fixture f;
    wchar_t temp[MAX_PATH]{}, unique[MAX_PATH]{}, self[MAX_PATH]{};
    check(GetTempPathW(MAX_PATH, temp) != 0 && GetTempFileNameW(temp, L"lis", 0, unique) != 0,
          "fixture temporary path");
    DeleteFileW(unique);
    f.directory = std::wstring(unique) + L" 来源测试";
    check(CreateDirectoryW(f.directory.c_str(), nullptr) != FALSE, "unicode fixture directory");
    f.exe = f.directory + L"\\SourceFixture.exe";
    check(GetModuleFileNameW(nullptr, self, MAX_PATH) != 0 &&
              CopyFileW(self, f.exe.c_str(), TRUE) != FALSE,
          "fixture executable");
    auto command = L"\"" + f.exe + L"\" --fixture";
    STARTUPINFOW startup{sizeof(startup)};
    check(CreateProcessW(f.exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                         nullptr, f.directory.c_str(), &startup, &f.process) != FALSE,
          "fixture process");
    auto rejected = login::scan_sources(f.directory, [] { return false; });
    check(rejected.complete && rejected.sources.empty() && !rejected.diagnostics.empty(),
          "unverified executable cannot establish source identity");
    auto automatic = login::scan_sources(L"", [] { return false; });
    bool accepted = false;
    for (const auto &source : automatic.sources)
        if (source.main_pid == f.process.dwProcessId)
            accepted = true;
    check(!accepted, "automatic discovery rejects unrelated executable in a unicode directory");
    std::cout << "Source discovery, cancellation and unverified release rejection passed\n";
}
