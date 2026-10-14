#include "window_task.h"

#ifdef _WIN32

#include <memory>
#include <algorithm>

namespace app {
namespace window_task_detail {
namespace { std::atomic<std::uint64_t> sessionEpoch{0}; }
std::uint64_t sessionGeneration() noexcept {return sessionEpoch.load(std::memory_order_acquire);}
void invalidateSession() noexcept {sessionEpoch.fetch_add(1,std::memory_order_acq_rel);}
namespace {thread_local std::vector<SuspendedTask> recoveries;thread_local bool sessionPaused=false;}
bool sessionSuspended() noexcept {return sessionPaused;}
void discardSuspendedTasks() noexcept {sessionPaused=false;}
void registerRecovery(std::weak_ptr<State> state,std::uint64_t generation,std::function<void()> recovery) {
    recoveries.erase(std::remove_if(recoveries.begin(),recoveries.end(),[](const SuspendedTask& t){
        auto s=t.state.lock();return !s||!s->active.load()||s->generation.load()!=t.generation;
    }),recoveries.end());
    recoveries.push_back({std::move(state),generation,std::move(recovery)});
}
std::vector<SuspendedTask> suspendSessionTasks() {
    const auto previous=sessionGeneration();sessionPaused=true;invalidateSession();
    std::vector<SuspendedTask> suspended;
    for(auto& task:recoveries){auto s=task.state.lock();
        if(s&&s->active.load()&&s->session.load()==previous&&s->generation.load()==task.generation){
            s->active.store(false);task.generation=s->generation.fetch_add(1)+1;task.suspension_epoch=sessionGeneration();suspended.push_back(std::move(task));
        }
    }
    recoveries.clear();return suspended;
}
void recoverSuspendedTasks(std::vector<SuspendedTask> tasks) {
    sessionPaused=false;
    for(auto& t:tasks){auto s=t.state.lock();
        // Page destruction, explicit cancel and newly started work take precedence.
        if(s&&t.suspension_epoch==sessionGeneration()&&s->generation.load()==t.generation&&!s->active.load()){
            try {t.recovery();}catch(...){}
        }
    }
}
namespace {

UINT completionMessage() noexcept {
    static const UINT message = RegisterWindowMessageW(L"LISWorkbench.WindowTask.Completion.v1");
    return message;
}

constexpr const wchar_t* DISPATCHER_CLASS = L"LISWorkbenchWindowTaskDispatcher";

LRESULT CALLBACK dispatcherProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == completionMessage()) {
        std::unique_ptr<Completion> completion(reinterpret_cast<Completion*>(lParam));
        if (completion) completion->deliver();
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

ATOM dispatcherClass() noexcept {
    static const ATOM atom = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = dispatcherProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = DISPATCHER_CLASS;
        return RegisterClassExW(&wc);
    }();
    return atom;
}

DWORD WINAPI runWork(void* parameter) noexcept {
    std::unique_ptr<Work> work(static_cast<Work*>(parameter));
    work->execute();
    return 0;
}

class CallbackCompletion final : public Completion {
public:
    CallbackCompletion(std::weak_ptr<State> state,
                       std::uint64_t generation,
                       std::function<void()> callback)
        : state_(std::move(state)),
          generation_(generation),
          callback_(std::move(callback)) {}

    void deliver() noexcept override {
        auto state = state_.lock();
        if (!state || !state->active.load(std::memory_order_acquire) ||
            state->session.load(std::memory_order_acquire) != sessionGeneration() ||
            state->generation.load(std::memory_order_acquire) != generation_) {
            return;
        }
        try {
            callback_();
        } catch (...) {
            // Progress callbacks must not escape the Win32 message loop.
        }
    }

private:
    std::weak_ptr<State> state_;
    std::uint64_t generation_ = 0;
    std::function<void()> callback_;
};

}  // namespace

bool queue(std::unique_ptr<Work> work) noexcept {
    if (!work) return false;
    Work* raw = work.release();
    if (QueueUserWorkItem(runWork, raw, WT_EXECUTELONGFUNCTION)) {
        return true;
    }
    delete raw;
    return false;
}

HWND dispatcher() noexcept {
    thread_local HWND window = nullptr;
    if (window && IsWindow(window)) return window;
    if (!dispatcherClass() || completionMessage() == 0) return nullptr;

    window = CreateWindowExW(0, DISPATCHER_CLASS, L"", 0,
                             0, 0, 0, 0, HWND_MESSAGE, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    return window;
}

bool post(HWND dispatcherWindow, std::unique_ptr<Completion> completion) noexcept {
    if (!dispatcherWindow || !completion || completionMessage() == 0) return false;
    Completion* raw = completion.release();
    if (PostMessageW(dispatcherWindow, completionMessage(), 0,
                     reinterpret_cast<LPARAM>(raw))) {
        return true;
    }
    delete raw;
    return false;
}

bool postCallback(std::weak_ptr<State> state,
                  std::uint64_t generation,
                  HWND dispatcherWindow,
                  std::function<void()> callback) noexcept {
    if (!callback) return false;
    try {
        return post(dispatcherWindow, std::make_unique<CallbackCompletion>(
            std::move(state), generation, std::move(callback)));
    } catch (...) {
        return false;
    }
}

}  // namespace window_task_detail

bool WindowTaskContext::cancelled() const noexcept {
    auto state = state_.lock();
    return !state || !state->active.load(std::memory_order_acquire) ||
           state->session.load(std::memory_order_acquire) != window_task_detail::sessionGeneration() ||
           state->generation.load(std::memory_order_acquire) != generation_;
}

bool WindowTaskContext::post(std::function<void()> callback) const noexcept {
    if (cancelled()) return false;
    return window_task_detail::postCallback(
        state_, generation_, dispatcherWindow_, std::move(callback));
}

WindowTask::WindowTask()
    : state_(std::make_shared<window_task_detail::State>()) {}

WindowTask::~WindowTask() {
    cancel();
}

void WindowTask::cancel() noexcept {
    state_->active.store(false, std::memory_order_release);
    state_->generation.fetch_add(1, std::memory_order_acq_rel);
}

bool WindowTask::active() const noexcept {
    return state_->active.load(std::memory_order_acquire) &&
           state_->session.load(std::memory_order_acquire) == window_task_detail::sessionGeneration();
}

}  // namespace app

#endif
