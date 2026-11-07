#pragma once

#include "barcode_label_printing.h"
#include "blood_bank_manual.h"
#include "search_core.h"
#include "search_text.h"

#include <algorithm>
#include <utility>

// The manual blood-bank entry is a view of 626, never a database instrument.
namespace regular_machine {

inline bool is_blood_manual(const std::string& code) {
    return search::trim(code) == blood_manual::view_code;
}

inline std::string query_code(const std::string& code) {
    return is_blood_manual(code) ? blood_manual::source_code : code;
}

inline bool is_blood_manual_report(const search::ReportRow& row, const std::string& code) {
    return is_blood_manual(row.view_machine_code) || is_blood_manual(code);
}

inline void append_blood_manual(std::vector<search::MachineOption>& machines) {
    const auto source = std::find_if(machines.begin(), machines.end(), [](const auto& m) {
        return search::trim(m.mach_code) == blood_manual::source_code;
    });
    if (source == machines.end()) return;
    auto manual = *source;
    manual.mach_code = blood_manual::view_code;
    manual.mach_name = blood_manual::name;
    manual.py_code = blood_manual::abbreviation;
    manual.group_code.clear();
    manual.group_name = blood_manual::group_name;
    manual.sample_code.clear();
    manual.sample_name.clear();
    const auto existing = std::find_if(machines.begin(), machines.end(), [](const auto& m) {
        return is_blood_manual(m.mach_code);
    });
    if (existing == machines.end()) machines.push_back(std::move(manual));
    else *existing = std::move(manual);
}

inline void present_reports(std::vector<search::ReportRow>& rows, const std::string& code) {
    if (!is_blood_manual(code)) return;
    for (auto& row : rows) {
        // Keep the real report/instrument identity for comparison and navigation.
        row.view_machine_code = blood_manual::view_code;
        row.group_name = blood_manual::group_name;
        row.sample_name.clear();
    }
}

inline void configure_label(search::BarcodeLabelPayload& payload,
                            const search::ReportRow& row, const std::string& code) {
    if (!is_blood_manual_report(row, code)) return;
    payload.view_machine_code = blood_manual::view_code;
    payload.label_template = search::BarcodeLabelTemplate::Microbiology;
    payload.test_item = blood_manual::group_name;
    payload.specimen_type.clear();
    payload.order_text.clear();
    payload.sex = row.sex;
    payload.age = row.age;
}

// All regular-report printing entry points share this factory. The virtual
// identity travels with the report rather than relying on mutable UI selection.
inline search::BarcodeLabelPayload barcode_payload(const search::ReportRow& row,
                                                   const std::string& group_name,
                                                   const std::string& timestamp,
                                                   const std::string& machine_code) {
    search::BarcodeLabelPayload payload;
    payload.sample_no = row.oper_no;
    payload.test_item = group_name;
    payload.barcode_value = row.txm_no;
    payload.patient_name = row.name;
    payload.specimen_type = row.sample_name;
    payload.department = row.dept_name;
    payload.patient_id = row.reg_no;
    payload.timestamp = timestamp;
    configure_label(payload, row, machine_code);
    return payload;
}

}  // namespace regular_machine
