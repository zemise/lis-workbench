#include "login_source_guard.h"
#include "login_adoption.h"
#include "login_source_win32.h"
#include "window_task.h"

#include <atomic>
#include <cstdlib>
#include <iostream>

namespace {
enum class Scenario { ready, locked, newHelper, newUser, exited, databaseFailure,
                      restarted, ambiguousRestart, incomplete };

std::atomic<Scenario> scenario{Scenario::ready};
std::atomic<int> accountChecks{0};
std::atomic<bool> uniqueInstanceRequired{false};
int businessCommands = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

login::Source source(Scenario state) {
    login::Source source;
    source.main_pid = state == Scenario::restarted || state == Scenario::ambiguousRestart ? 2 : 1;
    source.main_created = source.main_pid == 2 ? 101 : 100;
    source.main_hwnd = 20;
    source.helper_pid = state == Scenario::newHelper ? 11 : 10;
    source.helper_created = state == Scenario::newHelper ? 300 : 200;
    source.employee_id = state == Scenario::newUser || source.main_pid == 2 ? 41 : 40;
    source.port = 4310;
    source.machine = L"PC";
    return source;
}

LRESULT CALLBACK business_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_COMMAND)
        ++businessCommands;
    return DefWindowProcW(window, message, wp, lp);
}

template<class Predicate>
void pump_until(Predicate done, const char* message) {
    const auto deadline = GetTickCount64() + 3000;
    while (!done() && GetTickCount64() < deadline) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!done())
            Sleep(5);
    }
    check(done(), message);
}

struct Fixture {
    ULONGLONG ticks = 1000;
    int clockReads = 0;
    int verified = 0;
    int expired = 0;
    login::Identity identity{1, 40, L"CODE", L"NAME", source(Scenario::ready)};
    login::SourceGuard guard{identity, {}, L"synthetic",
        [this](const login::Identity& current) { identity = current; ++verified; },
        [this] { ++expired; },
        [this] { ++clockReads; return ticks; }};
    HWND monitor = nullptr;

    Fixture() {
        scenario = Scenario::ready;
        accountChecks = 0;
        uniqueInstanceRequired = false;
        check(guard.start(), "guard starts");
        monitor = FindWindowExW(HWND_MESSAGE, nullptr, L"LISWorkbenchSourceGuard", nullptr);
        check(monitor && KillTimer(monitor, 1), "disable the periodic timer for deterministic probes");
        pump_until([&] { return clockReads >= 3; }, "initial observation completed");
    }

    void probe(Scenario state) {
        scenario = state;
        SendMessageW(monitor, WM_APP + 1, 0, 0);
        if (!expired) {
            const int reads = clockReads;
            pump_until([&] { return clockReads > reads; }, "source probe completed");
        }
    }
};
} // namespace

namespace login {
SourceScan scan_sources(const std::wstring&, const std::function<bool()>&) {
    const auto state = scenario.load();
    if (state == Scenario::exited)
        return {true, {}, {}};
    if (state == Scenario::incomplete)
        return {};
    const auto observed = source(state);
    SourceScan scan{true, {{observed.main_pid, observed.main_created, state == Scenario::locked}},
                    {observed}};
    if (state == Scenario::ambiguousRestart)
        scan.unreadable_mains.push_back(3);
    return scan;
}

Result adopt_source(const search::DbSettings&, const std::wstring&, const Source& source,
                    const std::function<bool()>& cancelled, bool, bool requireUniqueInstance) {
    ++accountChecks;
    uniqueInstanceRequired = requireUniqueInstance;
    if (cancelled() || scenario == Scenario::databaseFailure)
        return {};
    Result result;
    result.status = Status::success;
    result.identity = Identity{source.employee_id == 40 ? 1 : 2, source.employee_id,
                               L"CODE", L"NAME", source};
    return result;
}
} // namespace login

int main() {
    WNDCLASSW wc{};
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = business_proc;
    wc.lpszClassName = L"OwnedSourceGuardBusinessTest";
    check(RegisterClassW(&wc) != 0, "register business window");
    HWND main = CreateWindowW(wc.lpszClassName, L"Synthetic main", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             0, 0, 400, 300, nullptr, nullptr, wc.hInstance, nullptr);
    check(main != nullptr, "create business window");

    {
        Fixture test;
        test.probe(Scenario::locked);
        test.ticks += 599999;
        test.probe(Scenario::incomplete);
        check(test.expired == 0 && test.verified == 0, "no action before the deadline");
        check(IsWindowVisible(main) && IsWindowEnabled(main), "business window remains usable");
        PostMessageW(main, WM_COMMAND, 42, 0);
        pump_until([] { return businessCommands == 1; }, "business input still dispatched");
        app::WindowTask task;
        bool completed = false;
        check(task.start<int>([] { return 1; }, [&](std::optional<int>, std::exception_ptr) {
            completed = true;
        }), "business tasks can still start");
        pump_until([&] { return completed; }, "business task completes in grace period");

        test.probe(Scenario::newHelper);
        check(test.verified == 1 && test.identity.user_id == 1 && accountChecks == 1,
              "same user reverified within grace period");
        test.ticks += 1;
        test.probe(Scenario::exited);
        check(!test.expired, "recovery resets the deadline");
        test.ticks += 599999;
        test.probe(Scenario::incomplete);
        check(!test.expired, "failure does not cause early exit");
        test.ticks += 1;
        test.probe(Scenario::ready);
        check(test.expired == 1 && test.verified == 1, "deadline takes priority over late recovery");
        SendMessageW(test.monitor, WM_APP + 1, 0, 0);
        check(test.expired == 1, "exit reported only once");
    }
    {
        Fixture test;
        test.probe(Scenario::exited);
        test.ticks += 300000;
        test.probe(Scenario::ambiguousRestart);
        check(test.verified == 0 && accountChecks == 0, "ambiguous replacement is ignored");
        test.probe(Scenario::restarted);
        check(test.verified == 1 && test.identity.user_id == 2 &&
              test.identity.adopted_source->main_pid == 2 && uniqueInstanceRequired,
              "unique restarted source can switch to a verified new user");
    }
    {
        Fixture test;
        test.probe(Scenario::locked);
        test.probe(Scenario::databaseFailure);
        check(test.verified == 0 && accountChecks == 1, "account verification failure keeps waiting");
        test.ticks += 60000;
        test.probe(Scenario::newUser);
        check(test.verified == 1 && test.identity.user_id == 2 && !uniqueInstanceRequired,
              "a verified employee change in the same source switches automatically");
    }
    {
        Fixture test;
        test.probe(Scenario::locked);
        test.ticks += 599999;
        scenario = Scenario::newUser;
        SendMessageW(test.monitor, WM_APP + 1, 0, 0);
        ++test.ticks;
        pump_until([&] { return test.expired || test.verified; }, "in-flight verification finished");
        check(test.expired == 1 && test.verified == 0, "late verification cannot reopen an expired session");
    }

    DestroyWindow(main);
    std::cout << "Silent grace period, source recovery, account switching and expiry passed\n";
}
