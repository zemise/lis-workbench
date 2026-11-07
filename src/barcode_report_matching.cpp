#include "barcode_report_matching.h"
#include "search_text.h"
#include <algorithm>
#include <charconv>

namespace search {
std::string barcode_numeric_id(const std::string& value) {
    const auto text = trim(value);
    long long number = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || number <= 0) return {};
    return std::to_string(number);
}
std::string barcode_receive_day(const std::string& value) {
    const auto text = trim(value);
    // This helper rejects impossible dates and times without locale or timezone conversion.
    if (text.size() != 19 || sql_datetime_diff_seconds(text, text) != 0) return {};
    return text.substr(0, 10);
}
std::vector<BarcodeMatchProfile> barcode_match_profiles(
    const BarcodeMatchSource& source, const std::vector<BarcodeOrderMapping>& mappings) {
    const auto room = barcode_numeric_id(source.room);
    const auto machine = barcode_numeric_id(source.machine);
    const auto group = barcode_numeric_id(source.group);
    if (room.empty()) return {};
    if (!machine.empty() && !group.empty()) return {{machine, room, {group}}};
    const auto order = barcode_numeric_id(source.order_code);
    if (order.empty()) return {};
    std::map<std::string, std::set<std::string>> groups;
    for (const auto& mapping : mappings) {
        const auto mapped_machine = barcode_numeric_id(mapping.machine);
        const auto mapped_group = barcode_numeric_id(mapping.group);
        if (barcode_numeric_id(mapping.order_code) == order &&
            barcode_numeric_id(mapping.room) == room &&
            !mapped_machine.empty() && !mapped_group.empty()) groups[mapped_machine].insert(mapped_group);
    }
    std::vector<BarcodeMatchProfile> profiles;
    for (const auto& entry : groups) profiles.push_back({entry.first, room, entry.second});
    return profiles;
}
std::vector<BarcodeReportKey> barcode_report_keys(
    const BarcodeMatchSource& source, const std::vector<BarcodeMatchProfile>& profiles) {
    const auto patient = trim(source.patient_no), type = trim(source.patient_type);
    const auto day = barcode_receive_day(source.receive_time);
    if (patient.empty() || type.empty() || day.empty()) return {};
    std::vector<BarcodeReportKey> keys;
    for (const auto& profile : profiles) keys.push_back({patient, type, profile.machine, profile.room, day});
    return keys;
}
BarcodeReportMatcher::BarcodeReportMatcher(const std::vector<BarcodeExpectedItem>& expected,
                                           const std::vector<BarcodeMatchReport>& reports)
    : reports_(reports) {
    for (const auto& item : expected) {
        const auto group = barcode_numeric_id(item.group), machine = barcode_numeric_id(item.machine);
        const auto room = barcode_numeric_id(item.room), code = barcode_numeric_id(item.item);
        if (!group.empty() && !machine.empty() && !room.empty() && !code.empty())
            expected_[{group, machine, room}].insert(code);
    }
    for (std::size_t i = 0; i < reports.size(); ++i) {
        const auto& report = reports[i];
        const auto& row = report.fields;
        const auto day = barcode_receive_day(row.inspect_date);
        if (report.deleted || trim(row.report_no).empty() || trim(row.barcode).empty() || day.empty()) continue;
        reports_by_key_[{trim(row.reg_no), trim(report.patient_type), barcode_numeric_id(row.machine_code),
                         barcode_numeric_id(row.room_code), day}].push_back(i);
    }
}
BarcodeMatchResult BarcodeReportMatcher::match(
    const BarcodeMatchSource& source, const std::vector<BarcodeMatchProfile>& profiles) const {
    if (trim(source.patient_no).empty() || trim(source.patient_type).empty()) return {BarcodeMatchReason::missing_identity, {}};
    if (barcode_receive_day(source.receive_time).empty()) return {BarcodeMatchReason::missing_date, {}};
    if (profiles.empty()) return {BarcodeMatchReason::missing_mapping, {}};
    const auto keys = barcode_report_keys(source, profiles);
    std::map<std::string, std::size_t> matched;
    bool valid_definition = false, rejected_time = false, rejected_result = false;
    for (std::size_t p = 0; p < profiles.size(); ++p) {
        std::set<std::string> required;
        bool complete_definition = !profiles[p].groups.empty();
        for (const auto& group : profiles[p].groups) {
            const auto found = expected_.find({group, profiles[p].machine, profiles[p].room});
            if (found == expected_.end() || found->second.empty()) { complete_definition = false; break; }
            required.insert(found->second.begin(), found->second.end());
        }
        if (!complete_definition) continue;
        valid_definition = true;
        const auto candidates = reports_by_key_.find(keys[p]);
        if (candidates == reports_by_key_.end()) continue;
        for (const auto index : candidates->second) {
            const auto& report = reports_[index];
            if (trim(report.fields.barcode) == trim(source.barcode)) continue;
            if (!std::includes(report.items.begin(), report.items.end(), required.begin(), required.end())) continue;
            if (report.reviewed || report.sent) {
                if (barcode_receive_day(report.fields.review_time).empty() ||
                    sql_datetime_diff_seconds(source.receive_time, report.fields.review_time) < 0) {
                    rejected_time = true; continue;
                }
                if (!std::includes(report.result_items.begin(), report.result_items.end(), required.begin(), required.end())) {
                    rejected_result = true; continue;
                }
            }
            matched.emplace(trim(report.fields.report_no), index);
        }
    }
    if (matched.size() > 1) return {BarcodeMatchReason::ambiguous, {}};
    if (matched.size() == 1) return {BarcodeMatchReason::matched, matched.begin()->second};
    if (!valid_definition) return {BarcodeMatchReason::missing_items, {}};
    if (rejected_time) return {BarcodeMatchReason::invalid_time, {}};
    if (rejected_result) return {BarcodeMatchReason::incomplete_results, {}};
    return {BarcodeMatchReason::no_report, {}};
}
const char* barcode_match_reason_text(BarcodeMatchReason reason) {
    switch (reason) {
    case BarcodeMatchReason::matched: return "项目匹配关联";
    case BarcodeMatchReason::missing_identity: return "未关联：身份信息不足";
    case BarcodeMatchReason::missing_date: return "未关联：签收时间缺失或无效";
    case BarcodeMatchReason::missing_mapping: return "未关联：缺少仪器映射";
    case BarcodeMatchReason::missing_items: return "未关联：缺少项目定义";
    case BarcodeMatchReason::ambiguous: return "待核实：多份候选报告";
    case BarcodeMatchReason::invalid_time: return "待核实：审核时间异常";
    case BarcodeMatchReason::incomplete_results: return "待核实：项目结果不完整";
    default: return "未找到完整覆盖的报告";
    }
}
void apply_barcode_report_match(BarcodeQueryRow& row, const BarcodeMatchReport& report) {
    const auto& r = report.fields;
    row.report_no = r.report_no;
    row.sample_no = r.sample_no;
    row.machine_code = r.machine_code;
    row.machine_name = r.machine_name;
    row.room_code = r.room_code;
    row.inspect_date = r.inspect_date;
    row.tester = r.tester;
    row.reviewer = r.reviewer;
    row.review_time = r.review_time;
    row.machine_status = report.sent ? "发送完成" : report.reviewed ? "已审核未发送" : "已上机未审核";
    row.report_link = barcode_match_reason_text(BarcodeMatchReason::matched);
    row.review_elapsed_seconds = sql_datetime_diff_seconds(row.receive_time, row.review_time);
    row.review_elapsed = format_duration_seconds_zh(row.review_elapsed_seconds);
}
bool barcode_status_matches(const BarcodeQueryRow& row, const BarcodeQueryFilters& filters) {
    const auto known = [](const std::string& value) {
        return value == "已签收未上机" || value == "已上机未审核" ||
               value == "已审核未发送" || value == "发送完成";
    };
    if (filters.machine_statuses.empty()) {
        const auto status = trim(filters.machine_status);
        return !known(status) || row.machine_status == status;
    }
    bool constrained = false;
    for (const auto& value : filters.machine_statuses) {
        const auto status = trim(value);
        if (!known(status)) continue;
        constrained = true;
        if (row.machine_status == status) return true;
    }
    return !constrained;
}
} // namespace search
