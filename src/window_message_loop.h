#pragma once
#ifdef _WIN32
#include <windows.h>

namespace app {
// Closing a session window must not quit the thread: another login/session
// window may follow. A sent close can run inside GetMessage while it waits for
// a posted message, so wake that wait with a no-op instead of WM_QUIT.
inline void wake_window_message_loop() noexcept {
    PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
}
} // namespace app
#endif
