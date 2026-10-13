#include "window_task.h"

#ifdef _WIN32

#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

// Unlike assert, these checks and their expressions run in Release too.
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void workerCheck(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool pumpUntil(const std::function<bool()>& condition, DWORD timeoutMs = 3000) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    while (!condition() && GetTickCount64() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    }
    return condition();
}

struct State {
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    std::atomic<bool> exited{false};
    std::atomic<bool> cancellationObserved{false};
    int progress = 0;
    int delivered = 0;
    int value = 0;
    std::exception_ptr error;
};

// Last worker capture is destroyed after WorkModel posts its completion.
// Waiting on this fence, then draining messages, avoids timing-based sleeps.
struct WorkerExit {
    explicit WorkerExit(std::shared_ptr<State> value) : state(std::move(value)) {}
    ~WorkerExit() { state->exited.store(true); }
    std::shared_ptr<State> state;
};

auto fencedWorker(const std::shared_ptr<State>& state,
                  std::function<int(app::WindowTaskContext)> worker) {
    return [worker = std::move(worker), fence = std::make_shared<WorkerExit>(state)]
           (app::WindowTaskContext context) { return worker(context); };
}

void awaitRelease(const std::shared_ptr<State>& state) {
    state->entered.store(true);
    const auto deadline = GetTickCount64() + 3000;
    while (!state->release.load() && GetTickCount64() < deadline) Sleep(1);
    workerCheck(state->release.load(), "worker release timed out");
}

auto handler(const std::shared_ptr<State>& state) {
    return [state](std::optional<int> result, std::exception_ptr error) {
        state->error = error;
        state->value = result.value_or(-1);
        ++state->delivered;
    };
}

void drainAfterWorker(const std::shared_ptr<State>& state) {
    const bool finished = pumpUntil([state] { return state->exited.load(); });
    check(finished, "worker exit fence timed out");
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const bool failWorker = argc == 2 && std::string(argv[1]) == "--fail-worker";
    {
        auto state = std::make_shared<State>();
        app::WindowTask task;
        const bool started = task.start<int>(
            fencedWorker(state, [state, failWorker](app::WindowTaskContext context) {
                workerCheck(!failWorker, "intentional worker failure probe");
                workerCheck(!context.cancelled(), "new task unexpectedly cancelled");
                const bool posted = context.post([state] { state->progress = 3; });
                workerCheck(posted, "worker-to-UI post failed");
                return 9;
            }), handler(state));
        check(started, "start progress task");
        const bool delivered = pumpUntil([state] { return state->delivered == 1; });
        check(delivered, "progress completion timed out");
        drainAfterWorker(state);
        check(!state->error, "worker checks must propagate failure to main thread");
        check(state->value == 9 && state->progress == 3, "progress and result delivered");
        check(!task.active(), "completed task inactive");
    }

    {
        auto state = std::make_shared<State>();
        app::WindowTask task;
        const bool started = task.start<int>(
            fencedWorker(state, [](app::WindowTaskContext) -> int {
                throw std::runtime_error("expected");
            }), handler(state));
        check(started, "start throwing task");
        const bool delivered = pumpUntil([state] { return state->delivered == 1; });
        check(delivered, "exception completion timed out");
        drainAfterWorker(state);
        check(state->error && state->value == -1, "exception delivered without result");
        bool expected = false;
        try { std::rethrow_exception(state->error); }
        catch (const std::runtime_error& error) { expected = std::string(error.what()) == "expected"; }
        catch (...) {}
        check(expected, "original worker exception preserved");
        check(!task.active(), "failed task inactive");
    }

    {
        auto old = std::make_shared<State>();
        auto current = std::make_shared<State>();
        app::WindowTask task;
        const bool oldStarted = task.start<int>(
            fencedWorker(old, [old](app::WindowTaskContext) { awaitRelease(old); return 1; }),
            handler(old));
        check(oldStarted, "start old task");
        const bool oldRunning = pumpUntil([old] { return old->entered.load(); });
        check(oldRunning, "old worker entered before replacement");
        const bool newStarted = task.start<int>(
            fencedWorker(current, [](app::WindowTaskContext) { return 2; }), handler(current));
        check(newStarted, "replace task");
        const bool delivered = pumpUntil([current] { return current->delivered == 1; });
        check(delivered, "replacement completion timed out");
        drainAfterWorker(current);
        check(!current->error && current->value == 2, "replacement result delivered");
        old->release.store(true);
        drainAfterWorker(old);
        check(old->delivered == 0, "old completion discarded after worker exit");
    }

    {
        auto state = std::make_shared<State>();
        app::WindowTask task;
        const bool started = task.start<int>(
            fencedWorker(state, [state](app::WindowTaskContext context) {
                awaitRelease(state);
                state->cancellationObserved.store(context.cancelled());
                return 7;
            }), handler(state));
        check(started, "start cancellable task");
        const bool running = pumpUntil([state] { return state->entered.load(); });
        check(running, "worker entered before cancellation");
        task.cancel();
        state->release.store(true);
        drainAfterWorker(state);
        check(state->cancellationObserved.load(), "worker observes cancellation");
        check(state->delivered == 0 && !task.active(), "cancelled completion discarded");
    }

    {
        auto state = std::make_shared<State>();
        {
            app::WindowTask task;
            const bool started = task.start<int>(
                fencedWorker(state, [state](app::WindowTaskContext context) {
                    awaitRelease(state);
                    state->cancellationObserved.store(context.cancelled());
                    return 42;
                }), handler(state));
            check(started, "start task before owner destruction");
            const bool running = pumpUntil([state] { return state->entered.load(); });
            check(running, "worker entered before owner destruction");
        }
        state->release.store(true);
        drainAfterWorker(state);
        check(state->cancellationObserved.load(), "owner destruction cancels worker context");
        check(state->delivered == 0, "destroyed owner receives no completion");
    }
    std::cout << "WindowTask lifecycle checks passed (including Release)\n";
    return 0;
}

#endif
