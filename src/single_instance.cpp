#include "single_instance.h"
#ifdef _WIN32
#include <cwchar>

namespace app {
SingleInstance::SingleInstance(const wchar_t *name) noexcept {
    mutex_ = CreateMutexW(nullptr, FALSE, name);
    const DWORD error = GetLastError();
    already_running_ = mutex_ && error == ERROR_ALREADY_EXISTS;
    primary_ = mutex_ && !already_running_;
    // A duplicate must not keep the primary's marker alive while waiting to
    // activate its window: the primary may exit during that wait.
    if (already_running_) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
}
SingleInstance::~SingleInstance() {
    if (mutex_)
        CloseHandle(mutex_);
}

namespace {
struct ExistingWindow {
    HWND window = nullptr;
    int priority = 0;
};
BOOL CALLBACK find_workbench(HWND window, LPARAM parameter) {
    if (!IsWindowVisible(window))
        return TRUE;
    wchar_t name[128]{};
    if (!GetClassNameW(window, name, 128))
        return TRUE;
    int priority = 0;
    if (std::wcscmp(name, L"LISWorkbenchSourceGuard") == 0)
        priority = 3;
    else if (std::wcscmp(name, L"LISWorkbenchLogin") == 0 ||
             std::wcscmp(name, L"LISWorkbenchStartupUpdate") == 0)
        priority = 2;
    else if (std::wcscmp(name, L"LISWorkbenchMainWindow") == 0)
        priority = 1;
    auto &found = *reinterpret_cast<ExistingWindow *>(parameter);
    if (priority > found.priority)
        found = {window, priority};
    return TRUE;
}
} // namespace
bool activate_workbench() noexcept {
    // The first process may still be starting or between authenticated sessions.
    for (int attempt = 0; attempt < 30; ++attempt) {
        ExistingWindow found;
        EnumWindows(find_workbench, reinterpret_cast<LPARAM>(&found));
        if (found.window) {
            HWND target = GetLastActivePopup(found.window);
            if (!target || !IsWindowVisible(target))
                target = found.window;
            if (IsIconic(found.window))
                ShowWindowAsync(found.window, SW_RESTORE);
            DWORD process = 0;
            GetWindowThreadProcessId(target, &process);
            if (process)
                AllowSetForegroundWindow(process);
            if (!SetForegroundWindow(target)) {
                FLASHWINFO flash{sizeof(flash), target, FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0};
                FlashWindowEx(&flash);
            }
            return true;
        }
        Sleep(100);
    }
    return false;
}
} // namespace app
#endif
