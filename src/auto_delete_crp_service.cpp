#include "auto_delete_crp_service.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "log.h"
#include "search_core.h"
#include "search_text.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <exception>
#include <optional>
#include <utility>
#include <vector>

namespace {

constexpr const wchar_t* kConfigSection = L"RegularReport";
constexpr const wchar_t* kConfigKey = L"AutoDeleteCrpEnabled";

struct DateRange {
    std::string date;
    std::string start;
    std::string end;
};

DateRange currentDateRange() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);

    char date[16]{};
    std::snprintf(date, sizeof(date), "%04d-%02d-%02d",
                  local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);

    std::tm next = local;
    next.tm_hour = 0;
    next.tm_min = 0;
    next.tm_sec = 0;
    next.tm_mday += 1;
    next.tm_isdst = -1;
    std::mktime(&next);
    char end_date[16]{};
    std::snprintf(end_date, sizeof(end_date), "%04d-%02d-%02d",
                  next.tm_year + 1900, next.tm_mon + 1, next.tm_mday);

    DateRange range;
    range.date = date;
    range.start = range.date + " 00:00:00";
    range.end = std::string(end_date) + " 00:00:00";
    return range;
}

struct CycleDone {
    bool ok = false;
    bool catch_up = false;
    std::string date;
    std::uint64_t state_generation = 0;
    search::AutoDeleteCrpCycleResult result;
    std::string error;
};

}  // namespace

AutoDeleteCrpService& auto_delete_crp_service() {
    static AutoDeleteCrpService service;
    return service;
}

void AutoDeleteCrpService::initialize(HWND main_window,
                                      const search::DbSettings& settings) {
    if (initialized_) return;
    initialized_ = true;
    main_window_ = main_window;
    db_settings_ = settings;
    enabled_ = search::load_module_int(kConfigSection, kConfigKey, 0) == 1;
    if (!enabled_) return;

    LOG_INFO("auto delete CRP restored as enabled");
    resetDayState();
    startTimer();
    onTimer();
}

void AutoDeleteCrpService::shutdown() {
    if (!initialized_) return;
    enabled_ = false;
    ++state_generation_;
    stopTimer();
    task_.cancel();
    pending_reports_.clear();
    initialized_ = false;
    main_window_ = nullptr;
}

void AutoDeleteCrpService::setEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    search::save_module_int(kConfigSection, kConfigKey, enabled ? 1 : 0);
    if (!enabled) {
        stopTimer();
        resetDayState();
        LOG_INFO("auto delete CRP disabled");
        return;
    }

    resetDayState();
    startTimer();
    LOG_INFO("auto delete CRP enabled");
    onTimer();
}

void AutoDeleteCrpService::updateDbSettings(const search::DbSettings& settings) {
    db_settings_ = settings;
    if (!enabled_) return;
    resetDayState();
    if (!task_.active()) onTimer();
}

void AutoDeleteCrpService::resetDayState() {
    ++state_generation_;
    active_date_.clear();
    last_scanned_rep_no_ = "0";
    catch_up_required_ = true;
    pending_reports_.clear();
}

void AutoDeleteCrpService::startTimer() {
    if (main_window_ &&
        SetTimer(main_window_, IDT_AUTO_DELETE_CRP, AUTO_DELETE_CRP_INTERVAL_MS, nullptr) == 0) {
        LOG_ERROR("auto delete CRP timer start failed");
    }
}

void AutoDeleteCrpService::stopTimer() {
    if (main_window_) KillTimer(main_window_, IDT_AUTO_DELETE_CRP);
}

void AutoDeleteCrpService::onTimer() {
    if (!enabled_ || !initialized_ || !main_window_) return;

    const DateRange range = currentDateRange();
    if (active_date_ != range.date) {
        ++state_generation_;
        active_date_ = range.date;
        last_scanned_rep_no_ = "0";
        catch_up_required_ = true;
        pending_reports_.clear();
        LOG_INFO("auto delete CRP date switched to " + active_date_);
    }
    if (task_.active()) return;

    const auto now = std::chrono::steady_clock::now();
    for (auto it = pending_reports_.begin(); it != pending_reports_.end();) {
        if (now >= it->second.deadline) it = pending_reports_.erase(it);
        else ++it;
    }

    search::AutoDeleteCrpCycleQuery query;
    query.connection_string = search::wide_to_utf8(search::build_connection_string_w(db_settings_));
    query.day_start = range.start;
    query.day_end = range.end;
    query.last_scanned_rep_no = last_scanned_rep_no_;
    query.catch_up = catch_up_required_;
    query.pending_rep_nos.reserve(pending_reports_.size());
    for (const auto& entry : pending_reports_) query.pending_rep_nos.push_back(entry.first);

    const std::string cycle_date = active_date_;
    const bool cycle_catch_up = catch_up_required_;
    const std::uint64_t cycle_generation = state_generation_;
    const bool started = task_.start<CycleDone>(
        [query, cycle_date, cycle_catch_up, cycle_generation] {
            CycleDone done;
            done.date = cycle_date;
            done.catch_up = cycle_catch_up;
            done.state_generation = cycle_generation;
            done.ok = search::run_auto_delete_crp_cycle(query, done.result, done.error);
            return done;
        },
        [this](std::optional<CycleDone> done, std::exception_ptr exception) {
            if (!enabled_) return;
            if (exception || !done) {
                LOG_ERROR("auto delete CRP background task failed");
                return;
            }
            if (done->state_generation != state_generation_) {
                onTimer();
                return;
            }
            if (done->date != active_date_) return;
            if (!done->ok) {
                LOG_ERROR("auto delete CRP database cycle failed: " + done->error);
                return;
            }

            const auto completed_at = std::chrono::steady_clock::now();
            for (const auto& row : done->result.discovered_reports) {
                if (pending_reports_.find(row.rep_no) != pending_reports_.end()) continue;
                PendingReport pending;
                pending.deadline = completed_at + (row.patient_info_complete
                    ? std::chrono::minutes(60) : std::chrono::minutes(30));
                pending_reports_.emplace(row.rep_no, pending);
            }
            for (auto it = pending_reports_.begin(); it != pending_reports_.end();) {
                if (completed_at >= it->second.deadline) it = pending_reports_.erase(it);
                else ++it;
            }

            last_scanned_rep_no_ = done->result.last_scanned_rep_no;
            if (done->catch_up) {
                catch_up_required_ = false;
                LOG_INFO("auto delete CRP catch-up completed date=" + active_date_ +
                         " watermark=" + last_scanned_rep_no_ +
                         " pending=" + std::to_string(pending_reports_.size()));
            }
            if (done->result.updated_entry_count > 0) {
                LOG_INFO("auto delete CRP updated entries=" +
                         std::to_string(done->result.updated_entry_count) +
                         " date=" + active_date_);
            }
        });
    if (!started) LOG_ERROR("auto delete CRP failed to queue background task");
}

#endif
