#pragma once

#ifdef _WIN32

#include "module_registry.h"

#include <string>

struct RegularReportOpenTarget {
    std::string rep_no;
    std::string oper_no;
    std::string inspect_date;
    std::string mach_code;
    std::string mach_name;
    std::string room_code;
};

constexpr UINT WM_REGULAR_OPEN_REPORT = WM_APP + 175;

HWND create_regular_report_module(const ModuleContext& ctx);

#endif
