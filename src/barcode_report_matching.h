#pragma once

#include "search_core.h"
#include <map>
#include <set>
#include <tuple>

namespace search {

struct BarcodeMatchSource {
    std::string barcode, patient_no, patient_type, machine, room, group, order_code, receive_time;
};
struct BarcodeMatchProfile {
    std::string machine, room;
    std::set<std::string> groups;
};
struct BarcodeOrderMapping {
    std::string order_code, group, machine, room;
};
struct BarcodeExpectedItem {
    std::string group, machine, room, item;
};
struct BarcodeReportKey {
    std::string patient_no, patient_type, machine, room, day;
    auto values() const { return std::tie(patient_no, patient_type, machine, room, day); }
    bool operator<(const BarcodeReportKey& other) const { return values() < other.values(); }
};
struct BarcodeMatchReport {
    BarcodeQueryRow fields;
    std::string patient_type;
    bool reviewed = false, sent = false, deleted = false;
    std::set<std::string> items, result_items;
};
enum class BarcodeMatchReason {
    matched, missing_identity, missing_date, missing_mapping, missing_items,
    no_report, incomplete_results, invalid_time, ambiguous
};
struct BarcodeMatchResult {
    BarcodeMatchReason reason = BarcodeMatchReason::no_report;
    std::optional<std::size_t> report_index;
};

// Only canonical positive numeric identifiers can enter numeric SQL parameters.
std::string barcode_numeric_id(const std::string& value);
std::string barcode_receive_day(const std::string& value);
std::vector<BarcodeMatchProfile> barcode_match_profiles(
    const BarcodeMatchSource& source, const std::vector<BarcodeOrderMapping>& mappings);
std::vector<BarcodeReportKey> barcode_report_keys(
    const BarcodeMatchSource& source, const std::vector<BarcodeMatchProfile>& profiles);

// Build indexes once per query; matching never scans the whole report collection per row.
class BarcodeReportMatcher {
public:
    BarcodeReportMatcher(const std::vector<BarcodeExpectedItem>& expected,
                         const std::vector<BarcodeMatchReport>& reports);
    BarcodeMatchResult match(const BarcodeMatchSource& source,
                             const std::vector<BarcodeMatchProfile>& profiles) const;
private:
    const std::vector<BarcodeMatchReport>& reports_;
    std::map<std::tuple<std::string, std::string, std::string>, std::set<std::string>> expected_;
    std::map<BarcodeReportKey, std::vector<std::size_t>> reports_by_key_;
};
const char* barcode_match_reason_text(BarcodeMatchReason reason);
void apply_barcode_report_match(BarcodeQueryRow& row, const BarcodeMatchReport& report);
bool barcode_status_matches(const BarcodeQueryRow& row, const BarcodeQueryFilters& filters);

} // namespace search
