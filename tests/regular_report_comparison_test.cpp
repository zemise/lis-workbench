#include "regular_report_comparison.h"
#include <cstdlib>
#include <iostream>

void check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
search::ResultRow item(const char* code, const char* name, const char* value,
                       const char* low = "1", const char* high = "5") {
    search::ResultRow row;
    row.item_code = code; row.item_name = name; row.result = value;
    row.downbound = low; row.upbound = high;
    return row;
}
int main() {
    using Phase = regular_comparison::LoadPhase;
    regular_comparison::ViewCache cache;
    const regular_comparison::CacheKey patientA = {"db-a", "100", "patient-a", "room-a", "machine-a", "2026-10-11"};
    check(cache.select(patientA), "first selected report creates cache context");
    cache.current = Phase::Loading;
    check(!cache.select(patientA) && cache.current == Phase::Loading,
          "tab switching while loading preserves in-flight current query");
    cache.current = Phase::Ready; // Empty details are also successful loads.
    cache.comparison = Phase::Loading;
    for (int tabSwitch = 0; tabSwitch < 5; ++tabSwitch)
        check(!cache.select(patientA) && cache.current == Phase::Ready &&
              cache.comparison == Phase::Loading, "tab switching never resets current or pending history");
    cache.comparison = Phase::Ready;
    check(!cache.select(patientA) && cache.comparison == Phase::Ready,
          "loaded comparison survives repeated tab entry");
    for (size_t field = 0; field < patientA.size(); ++field) {
        auto changed = patientA;
        changed[field] += "-changed";
        check(cache.select(changed) && cache.current == Phase::Empty && cache.comparison == Phase::Empty,
              "database, report, patient, room, instrument or date change invalidates both views");
        cache.select(patientA);
        cache.current = Phase::Ready;
        cache.comparison = Phase::Ready;
    }
    cache.comparison = Phase::Failed;
    check(!cache.select(patientA) && cache.current == Phase::Ready && cache.comparison == Phase::Failed,
          "failed history does not discard current results or retry on tab switch");
    cache.invalidate();
    check(!cache.key && cache.current == Phase::Empty && cache.comparison == Phase::Empty,
          "explicit refresh invalidates unchanged report results");
    cache.select(patientA);
    check(cache.current == Phase::Empty && cache.comparison == Phase::Empty,
          "same report may load again after explicit refresh");

    std::array<std::vector<search::ResultRow>, 4> reports;
    check(regular_comparison::merge_items(reports).empty(), "empty reports");
    reports[0] = {item("B", "Current B", "2"), item("A", "Current A", "3")};
    reports[0][0].normal = "5";
    reports[1] = {item("A", "Old name", "4", "0", "9"), item("C", "History C", "6"),
                  item("C", "Duplicate", "999")};
    reports[1][1].normal = "1";
    reports[2] = {item("D", "History D", "7"), item("C", "Older C", "8", "2", "8")};
    const auto merged = regular_comparison::merge_items(reports);
    check(merged.size() == 4, "union of item codes");
    check(merged[0].reference.item_code == "B" && merged[1].reference.item_code == "A" &&
          merged[2].reference.item_code == "C" && merged[3].reference.item_code == "D",
          "current order followed by newest historical extras");
    check(merged[1].reference.item_name == "Current A" && merged[1].reference.downbound == "1" &&
          merged[1].reference.upbound == "5", "current name and range take precedence");
    check(!merged[2].results[0] && merged[2].results[1]->result == "6" &&
          merged[2].results[2]->result == "8" && !merged[2].results[3],
          "history-only items aligned and missing cells empty; first duplicate wins");
    check(merged[2].reference.downbound == "1" && merged[2].reference.upbound == "5",
          "newest history range for history-only item");
    check(merged[0].results[0]->normal == "5" && merged[2].results[1]->normal == "1",
          "each cell keeps its own abnormal flag");
    reports = {};
    reports[0] = {item("X", "Same name", "阳性")};
    reports[1] = {item("Y", "Same name", "阴性")};
    check(regular_comparison::merge_items(reports).size() == 2, "codes distinguish same names");
    reports[2] = {item("", "Unknown one", "1"), item("", "Unknown two", "2")};
    check(regular_comparison::merge_items(reports).size() == 4, "empty codes never coalesce");
    std::cout << "Comparison merge tests passed\n";
}
