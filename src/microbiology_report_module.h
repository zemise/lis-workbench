#pragma once

#ifdef _WIN32

#include "module_registry.h"
#include "window_task.h"

#include <string>
#include <vector>

struct MicrobiologyReportOpenTarget {
    std::string rep_no;
    std::string oper_no;
    std::string inspect_date;
    std::string mach_code;
    std::string mach_name;
    std::string room_code;
    // Item codes to highlight with an orange background in the result list
    // after the report is opened.
    std::vector<std::string> highlight_item_codes;
    std::uint64_t session = app::window_task_detail::sessionGeneration();
};

constexpr UINT WM_MICROBIOLOGY_OPEN_REPORT = WM_APP + 176;

HWND create_microbiology_report_module(const ModuleContext& ctx);

#endif
