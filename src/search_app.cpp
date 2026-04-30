#include "search_app.h"

#include <algorithm>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#endif

namespace search {
namespace {

std::string trim(std::string text) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
    text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
    return text;
}

std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) {
        return "";
    }
#ifdef _WIN32
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), size, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') {
        out.pop_back();
    }
    return out;
#else
    return std::string(text.begin(), text.end());
#endif
}

}  // namespace

QueryFilters make_query_filters(const DbSettings& settings, const QueryInput& input) {
    QueryFilters filters;
    filters.connection_string = wide_to_utf8(build_connection_string_w(settings));
    filters.patient_id = input.patient_id;
    filters.barcode = input.barcode;
    filters.patient_name = input.patient_name;
    filters.bed_code = input.bed_code;
    filters.oper_no = input.oper_no;
    filters.start_date = input.start_date;
    filters.end_date = input.end_date;
    filters.room_code = input.room_code;
    filters.patient_type = input.patient_type;
    filters.report_status = input.report_status;
    filters.mach_code = input.mach_code;
    filters.group_code = input.group_code;
    filters.item_code = input.item_code;
    filters.limit = input.limit;
    return filters;
}

std::string display_conf(const std::string& conf) {
    const auto value = trim(conf);
    if (value == "T" || value == "S") {
        return "已审核";
    }
    return "未审核";
}

std::string display_chk_flag(const std::string& chk_flag) {
    return trim(chk_flag) == "T" ? "已发送" : "未发送";
}

std::string display_binary_print_flag(const std::string& value_text) {
    return trim(value_text) == "1" ? "已打印" : "未打印";
}

std::string make_query_count_status(size_t count) {
    return "当前查询总数：" + std::to_string(count) + " 条";
}

ReportRowTone report_row_tone(const ReportRow& row) {
    if (trim(row.zymz_print) == "1") {
        return ReportRowTone::Printed;
    }
    const auto value = trim(row.conf);
    if (value == "T" || value == "S") {
        return ReportRowTone::Reviewed;
    }
    return ReportRowTone::Pending;
}

ResultRowTone result_row_tone(const ResultRow& row) {
    // Empirical mapping from live LIS data: 1=high, 5=low, 3/NULL=default.
    const auto value = trim(row.normal);
    if (value == "1") {
        return ResultRowTone::High;
    }
    if (value == "5") {
        return ResultRowTone::Low;
    }
    return ResultRowTone::Default;
}

}  // namespace search
