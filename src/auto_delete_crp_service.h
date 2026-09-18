#pragma once

#ifdef _WIN32

#include "app_settings.h"
#include "window_task.h"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <string>

constexpr UINT_PTR IDT_AUTO_DELETE_CRP = 5003;
constexpr UINT AUTO_DELETE_CRP_INTERVAL_MS = 60U * 1000U;

class AutoDeleteCrpService {
public:
    void initialize(HWND main_window, const search::DbSettings& settings);
    void shutdown();

    bool enabled() const noexcept { return enabled_; }
    void setEnabled(bool enabled);
    void updateDbSettings(const search::DbSettings& settings);
    void onTimer();

private:
    struct PendingReport {
        std::chrono::steady_clock::time_point deadline;
    };

    void resetDayState();
    void startTimer();
    void stopTimer();

    HWND main_window_ = nullptr;
    search::DbSettings db_settings_;
    app::WindowTask task_;
    bool enabled_ = false;
    bool initialized_ = false;
    std::string active_date_;
    std::string last_scanned_rep_no_ = "0";
    bool catch_up_required_ = true;
    std::uint64_t state_generation_ = 0;
    std::map<std::string, PendingReport> pending_reports_;
};

AutoDeleteCrpService& auto_delete_crp_service();

#endif
