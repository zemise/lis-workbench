#include "single_instance.h"
#include <windows.h>
#include <shellapi.h>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <memory>

namespace {
void check(bool ok, const char *message) {
    if (!ok) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() {
        if (value)
            CloseHandle(value);
    }
};
struct Child {
    PROCESS_INFORMATION info{};
    ~Child() {
        if (info.hProcess) {
            if (WaitForSingleObject(info.hProcess, 0) == WAIT_TIMEOUT) {
                TerminateProcess(info.hProcess, 99);
                WaitForSingleObject(info.hProcess, 5000);
            }
            CloseHandle(info.hProcess);
        }
        if (info.hThread)
            CloseHandle(info.hThread);
    }
    void start(const std::wstring &arguments) {
        wchar_t exe[32768]{};
        check(GetModuleFileNameW(nullptr, exe, 32768) != 0, "locate test executable");
        auto command = L"\"" + std::wstring(exe) + L"\" " + arguments;
        STARTUPINFOW startup{sizeof(startup)};
        check(CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                             nullptr, nullptr, &startup, &info) != FALSE,
              "start test child");
    }
    DWORD exit_code() {
        check(WaitForSingleObject(info.hProcess, 10000) == WAIT_OBJECT_0, "test child exits");
        DWORD code = 99;
        check(GetExitCodeProcess(info.hProcess, &code) != FALSE, "read test child result");
        return code;
    }
};
} // namespace
int main() {
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    check(argv != nullptr, "parse test command");
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);
    LocalFree(argv);
    if (args.size() == 2 && args[0] == L"--probe") {
        app::SingleInstance instance(args[1].c_str());
        return instance.primary() ? 1 : instance.already_running() ? 0 : 2;
    }
    if (args.size() == 4 && args[0] == L"--contend") {
        app::SingleInstance instance(args[1].c_str());
        if (!instance.primary() && !instance.already_running())
            return 2;
        Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, args[2].c_str()));
        Handle release(OpenEventW(SYNCHRONIZE, FALSE, args[3].c_str()));
        if (!ready.value || !release.value)
            return 2;
        SetEvent(ready.value);
        if (instance.primary() && WaitForSingleObject(release.value, 15000) != WAIT_OBJECT_0)
            return 2;
        return instance.primary() ? 1 : 0;
    }
    const auto name = L"Local\\LISWorkbench.InstanceTest." + std::to_wstring(GetCurrentProcessId());
    {
        app::SingleInstance primary(name.c_str());
        check(primary.primary(), "first instance admitted");
        app::SingleInstance duplicate(name.c_str());
        check(duplicate.already_running() && !duplicate.primary(),
              "same-process duplicate rejected");
        Child child;
        child.start(L"--probe " + name);
        check(child.exit_code() == 0, "second process rejected");
    }
    {
        Child restart;
        restart.start(L"--probe " + name);
        check(restart.exit_code() == 1, "restart after normal exit admitted");
    }
    {
        auto primary = std::make_unique<app::SingleInstance>(name.c_str());
        check(primary->primary(), "race fixture admitted");
        app::SingleInstance duplicate(name.c_str());
        check(duplicate.already_running(), "race duplicate detected");
        primary.reset();
        app::SingleInstance restart(name.c_str());
        check(restart.primary(),
              "duplicate activation wait does not leave a stale instance marker");
    }

    const auto releaseName = name + L".release";
    Handle release(CreateEventW(nullptr, TRUE, FALSE, releaseName.c_str()));
    check(release.value != nullptr, "create release event");
    std::vector<std::unique_ptr<Child>> children;
    std::vector<std::unique_ptr<Handle>> ready;
    std::vector<HANDLE> waits;
    for (int i = 0; i < 8; ++i) {
        const auto readyName = name + L".ready." + std::to_wstring(i);
        ready.emplace_back(
            std::make_unique<Handle>(CreateEventW(nullptr, TRUE, FALSE, readyName.c_str())));
        check(ready.back()->value != nullptr, "create ready event");
        waits.push_back(ready.back()->value);
        auto child = std::make_unique<Child>();
        child->start(L"--contend " + name + L" " + readyName + L" " + releaseName);
        children.push_back(std::move(child));
    }
    check(WaitForMultipleObjects(DWORD(waits.size()), waits.data(), TRUE, 10000) == WAIT_OBJECT_0,
          "all concurrent launches decide");
    SetEvent(release.value);
    int primaries = 0;
    for (auto &child : children) {
        auto code = child->exit_code();
        check(code == 0 || code == 1, "all contenders complete");
        primaries += code == 1;
    }
    check(primaries == 1, "exactly one of eight concurrent launches admitted");
    children.clear();
    ready.clear();
    waits.clear();

    ResetEvent(release.value);
    const auto crashReadyName = name + L".crashReady";
    Handle crashReady(CreateEventW(nullptr, TRUE, FALSE, crashReadyName.c_str()));
    Child crash;
    crash.start(L"--contend " + name + L" " + crashReadyName + L" " + releaseName);
    check(WaitForSingleObject(crashReady.value, 10000) == WAIT_OBJECT_0,
          "owned crash fixture running");
    check(TerminateProcess(crash.info.hProcess, 77) != FALSE && crash.exit_code() == 77,
          "terminate only owned test process");
    {
        app::SingleInstance recovered(name.c_str());
        check(recovered.primary(), "abnormal exit leaves no stale lock");
    }
    std::cout << "Single instance, concurrent launch and exit recovery passed\n";
}
