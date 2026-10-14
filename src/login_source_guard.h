#pragma once
#ifdef _WIN32
#include "app_settings.h"
#include "login_core.h"
#include <windows.h>
#include <functional>
#include <memory>

namespace login {

// Owns monitoring and business suspension for one adopted identity on the UI thread.
// Manual login does not create a SourceGuard.
class SourceGuard {
public:
    using StateChanged = std::function<void(bool, const Identity&)>;
    // action: 1=recommend another source, 2=manual login, 3=exit.
    using Leave = std::function<void(int)>;

    SourceGuard(HWND main, Identity identity, search::DbSettings db,
                std::wstring directory, StateChanged changed, Leave leave);
    ~SourceGuard();

    bool start();
    bool paused() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace login
#endif
