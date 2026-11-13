#include "login_source_guard.h"

#ifdef _WIN32
#include "login_adoption.h"
#include "login_source_win32.h"
#include "window_task.h"

#include <algorithm>
#include <utility>

namespace login {
namespace {

constexpr ULONGLONG kGraceMs = 10 * 60 * 1000;
constexpr ULONGLONG kProbeTimeoutMs = 10 * 1000;
constexpr ULONGLONG kAccountRecheckMs = 60 * 1000;
constexpr UINT kPollIntervalMs = 2000;
constexpr UINT_PTR kPollTimer = 1;
constexpr UINT kPollNow = WM_APP + 1;

struct Probe {
    bool verified = false;
    std::optional<Identity> identity;
};

Probe verify_source(const search::DbSettings& db, const std::wstring& directory,
                    const Source& bound, bool recheck, app::WindowTaskContext task) {
    const auto cancelled = [task] { return task.cancelled(); };
    const auto scan = scan_sources(directory, cancelled);
    const auto match = match_source(scan, bound);
    if (match.status == SourceStatus::ready && !recheck &&
        same_observation(*match.source, bound))
        return {true, {}};

    const Source* candidate = nullptr;
    const bool replacingInstance = match.status == SourceStatus::exited;
    if (match.status == SourceStatus::ready) {
        candidate = &*match.source;
    } else if (match.status == SourceStatus::changed) {
        // match_source already established that this main has one unlocked observation.
        const auto found = std::find_if(scan.sources.begin(), scan.sources.end(), [&](const Source& source) {
            return source.main_pid == bound.main_pid && source.main_created == bound.main_created;
        });
        if (found != scan.sources.end())
            candidate = &*found;
    } else if (replacingInstance && scan.mains.size() == 1 && scan.sources.size() == 1 &&
               scan.unreadable_mains.empty()) {
        candidate = &scan.sources.front();
    }

    if (!candidate)
        return {};
    const bool requireEnabled = match.status != SourceStatus::ready;
    auto result = adopt_source(db, directory, *candidate, cancelled, requireEnabled, replacingInstance);
    if (result.status != Status::success || !result.identity || !result.identity->adopted_source)
        return {};
    return {true, std::move(result.identity)};
}

} // namespace

struct SourceGuard::Impl {
    Identity identity;
    search::DbSettings db;
    std::wstring directory;
    Verified verified;
    Expired expired;
    Clock now;
    HWND monitor = nullptr;
    app::WindowTask task;
    bool finished = false;
    ULONGLONG lastAccountCheck = 0;
    ULONGLONG probeStarted = 0;
    std::optional<ULONGLONG> unverifiedSince;

    void stop() {
        finished = true;
        KillTimer(monitor, kPollTimer);
        task.cancel();
    }

    bool expire_if_due(ULONGLONG time) {
        if (!unverifiedSince || time - *unverifiedSince < kGraceMs)
            return false;
        stop();
        expired();
        return true;
    }

    void note_failure(ULONGLONG time) {
        if (!unverifiedSince)
            unverifiedSince = time;
    }

    void poll() {
        if (finished)
            return;
        const auto time = now();
        if (expire_if_due(time))
            return;
        if (task.active()) {
            if (time - probeStarted < kProbeTimeoutMs)
                return;
            task.cancel();
            note_failure(time);
        }

        probeStarted = time;
        const bool recheck = unverifiedSince || time - lastAccountCheck >= kAccountRecheckMs;
        const bool queued = task.start<Probe>(
            [settings = db, path = directory, bound = *identity.adopted_source, recheck]
            (app::WindowTaskContext context) {
                return verify_source(settings, path, bound, recheck, context);
            },
            [this](std::optional<Probe> result, std::exception_ptr error) {
                const auto time = now();
                if (expire_if_due(time))
                    return;
                if (error || !result || !result->verified) {
                    note_failure(time);
                    return;
                }

                unverifiedSince.reset();
                if (result->identity) {
                    lastAccountCheck = time;
                    const auto& refreshed = *result->identity;
                    const bool differentUser = refreshed.user_id != identity.user_id ||
                                               refreshed.employee_id != identity.employee_id;
                    if (differentUser)
                        stop();
                    identity = std::move(*result->identity);
                    verified(identity);
                }
            });
        if (!queued)
            note_failure(time);
    }

    static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self && (message == kPollNow || (message == WM_TIMER && wp == kPollTimer))) {
            self->poll();
            return 0;
        }
        return DefWindowProcW(window, message, wp, lp);
    }

    bool start() {
        if (!identity.adopted_source || monitor)
            return false;

        WNDCLASSW wc{};
        wc.lpfnWndProc = proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"LISWorkbenchSourceGuard";
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
        monitor = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0,
                                  HWND_MESSAGE, nullptr, wc.hInstance, this);
        if (!monitor)
            return false;
        lastAccountCheck = now();
        return SetTimer(monitor, kPollTimer, kPollIntervalMs, nullptr) != 0 &&
               PostMessageW(monitor, kPollNow, 0, 0);
    }

    ~Impl() {
        task.cancel();
        if (monitor)
            DestroyWindow(monitor);
    }
};

SourceGuard::SourceGuard(Identity identity, search::DbSettings db, std::wstring directory,
                         Verified verified, Expired expired, Clock now)
    : impl_(new Impl{std::move(identity), std::move(db), std::move(directory),
                     std::move(verified), std::move(expired), std::move(now)}) {}
SourceGuard::~SourceGuard() = default;

bool SourceGuard::start() { return impl_->start(); }

} // namespace login
#endif
