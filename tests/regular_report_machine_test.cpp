#include "regular_report_machine.h"

#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << "check failed line " << __LINE__ << ": " #x "\n"; return 1; } } while (false)

int main() {
    CHECK(regular_machine::query_code(" 666 ") == "626");
    CHECK(regular_machine::query_code("626") == "626");
    CHECK(regular_machine::query_code("11102") == "11102");
    std::vector<search::MachineOption> machines(1);
    machines[0].room_code = "11";
    machines[0].mach_code = "626";
    machines[0].mach_name = "原仪器";
    machines[0].group_code = "123";
    machines[0].group_name = "原组合";
    machines[0].sample_code = "1";
    machines[0].sample_name = "全血";
    regular_machine::append_blood_manual(machines);
    CHECK(machines.size() == 2);
    CHECK(machines[0].mach_name == "原仪器" && machines[0].sample_name == "全血");
    CHECK(machines[1].mach_code == "666" && machines[1].mach_name == "血库手工");
    CHECK(machines[1].room_code == "11" && machines[1].py_code == "XKSG");
    CHECK(machines[1].group_name == "PRP" && machines[1].group_code.empty());
    CHECK(machines[1].sample_code.empty() && machines[1].sample_name.empty());
    regular_machine::append_blood_manual(machines);
    CHECK(machines.size() == 2);

    std::vector<search::ReportRow> rows(1);
    auto& row = rows[0];
    row.id = "5"; row.rep_no = "REP1"; row.mach_code = "626";
    row.oper_no = "012"; row.txm_no = "B012";
    row.group_name = "原组合"; row.sample_name = "全血";
    row.sex = "男"; row.age = "30岁"; row.order_text = "医嘱内容";
    regular_machine::present_reports(rows, "626");
    CHECK(row.group_name == "原组合" && row.sample_name == "全血");
    const auto original = row;
    regular_machine::present_reports(rows, "666");
    CHECK(row.group_name == "PRP" && row.sample_name.empty());
    CHECK(row.view_machine_code == "666");
    CHECK(row.id == "5" && row.rep_no == "REP1" && row.mach_code == "626");
    CHECK(row.oper_no == "012" && row.txm_no == "B012");

    search::BarcodeLabelPayload label;
    label.test_item = "原组合"; label.specimen_type = "全血";
    regular_machine::configure_label(label, original, "626");
    CHECK(label.label_template == search::BarcodeLabelTemplate::Standard);
    CHECK(label.test_item == "原组合" && label.specimen_type == "全血");
    regular_machine::configure_label(label, row, "666");
    CHECK(label.label_template == search::BarcodeLabelTemplate::Microbiology);
    CHECK(label.test_item == "PRP" && label.specimen_type.empty());
    CHECK(label.order_text.empty() && label.sex == row.sex && label.age == row.age);

    // A queried report keeps source instrument 626. Printing must retain the
    // virtual template when the UI/source instrument is 626 or no longer set.
    for (const auto& code : {"626", ""}) {
        auto actual = regular_machine::barcode_payload(row, row.group_name, "2026/11/6", code);
        CHECK(actual.view_machine_code == "666");
        CHECK(search::uses_microbiology_label(actual));
        CHECK(actual.test_item == "PRP" && actual.specimen_type.empty());
        CHECK(actual.sample_no == row.oper_no && actual.barcode_value == row.txm_no);
        CHECK(actual.order_text.empty() && actual.sex == row.sex && actual.age == row.age);
        actual.label_template = search::BarcodeLabelTemplate::Standard;
        CHECK(search::uses_microbiology_label(actual));
    }
    const auto standard = regular_machine::barcode_payload(original, original.group_name, "2026/11/6", "626");
    CHECK(!search::uses_microbiology_label(standard));
    CHECK(standard.test_item == "原组合" && standard.specimen_type == "全血");
    return 0;
}
