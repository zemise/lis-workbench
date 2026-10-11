#include "regular_report_tracking.h"

#include <cstdio>

namespace regular_tracking {
namespace {

int month_days(int year, int month) {
    static constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

bool blank(const std::string& text) { return text.find_first_not_of(" \t\r\n") == std::string::npos; }
std::string trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

}  // namespace

bool build_filters(const Options& o, search::QueryFilters& filters, std::string& error) {
    filters = {};
    error.clear();
    if (blank(o.patient)) {
        error = "当前报告没有病人号，无法追踪。";
        return false;
    }
    if (o.days < kMinDays || o.days > kMaxDays) {
        error = "追踪天数请输入1至9999。";
        return false;
    }
    int year = 0, month = 0, day = 0;
    const auto& date = o.today;
    bool valid = date.size() == 10 && date[4] == '-' && date[7] == '-';
    for (size_t i = 0; valid && i < date.size(); ++i)
        if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9')) valid = false;
    if (!valid || std::sscanf(date.c_str(), "%d-%d-%d", &year, &month, &day) != 3 ||
        year < 100 || month < 1 || month > 12 || day < 1 || day > month_days(year, month)) {
        error = "系统日期无效。";
        return false;
    }
    // Subtract calendar days, never seconds: DST transitions cannot shift the date range.
    for (int remaining = o.days - 1; remaining > 0; --remaining) {
        if (--day == 0) {
            if (--month == 0) { month = 12; --year; }
            day = month_days(year, month);
        }
    }
    char start[16]{};
    std::snprintf(start, sizeof(start), "%04d-%02d-%02d", year, month, day);
    filters.connection_string = o.connection;
    filters.patient_id = o.patient;  // query_reports maps patient_id directly to r.REG_NO.
    filters.start_date = start;
    filters.end_date = o.today;
    filters.limit = 0;
    filters.skip_order_text = true;
    return true;
}

std::vector<search::ReportRow> filter_reports(const std::vector<search::ReportRow>& rows,
                                            const Options& o) {
    std::vector<search::ReportRow> visible;
    if (o.current_machine && blank(o.origin_machine)) return visible;
    const auto machine = trimmed(o.origin_machine), room = trimmed(o.origin_room);
    for (const auto& row : rows) {
        if (o.current_machine && (trimmed(row.mach_code) != machine ||
            (!room.empty() && trimmed(row.room_code) != room))) continue;
        if (o.reviewed && trimmed(row.chk_flag) != "T") continue;
        visible.push_back(row);
    }
    return visible;
}

}  // namespace regular_tracking
