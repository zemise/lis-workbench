#pragma once
#ifdef _WIN32
#include <windows.h>

namespace app {
// One workbench per interactive Windows session, independent of install path.
inline constexpr const wchar_t *kWorkbenchInstanceMutex = L"Local\\LISWorkbench.SingleInstance.v1";

class SingleInstance {
  public:
    explicit SingleInstance(const wchar_t *name = kWorkbenchInstanceMutex) noexcept;
    ~SingleInstance();
    SingleInstance(const SingleInstance &) = delete;
    SingleInstance &operator=(const SingleInstance &) = delete;
    bool primary() const noexcept { return primary_; }
    bool already_running() const noexcept { return already_running_; }

  private:
    HANDLE mutex_ = nullptr;
    bool primary_ = false, already_running_ = false;
};

// Restores the existing main/login/modal window. Never exposes a hidden business
// window when the source guard is displaying its session privacy cover.
bool activate_workbench() noexcept;
} // namespace app
#endif
