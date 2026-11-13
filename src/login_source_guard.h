#pragma once

#ifdef _WIN32
#include "app_settings.h"
#include "login_core.h"
#include <windows.h>
#include <functional>
#include <memory>

namespace login {

// Rechecks an adopted login in the background. Callbacks run on the UI thread.
class SourceGuard {
public:
    using Verified = std::function<void(const Identity&)>;
    using Expired = std::function<void()>;
    using Clock = std::function<ULONGLONG()>;

    SourceGuard(Identity identity, search::DbSettings db, std::wstring directory,
                Verified verified, Expired expired, Clock now = GetTickCount64);
    ~SourceGuard();

    bool start();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace login
#endif
