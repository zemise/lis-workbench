#include "search_controller.h"
#include "query_diagnostics.h"
#include "search_text.h"
#include "regular_report_machine.h"

namespace search {
namespace {

std::string make_connection_string_utf8(const DbSettings& settings) {
    return wide_to_utf8(build_connection_string_w(settings));
}

}  // namespace

bool test_database_connection(const DbSettings& settings, std::string& error) {
    querydiag::RequestScope request(__func__, error);
    std::vector<ReportRow> rows;
    QueryInput input;
    input.limit = 1;
    std::string connection_string;
    return run_report_query(settings, input, rows, connection_string, error);
}

bool load_room_options(const DbSettings& settings, std::vector<RoomOption>& rows, std::string& error) {
    const auto connection_string = make_connection_string_utf8(settings);
    if (connection_string.empty()) {
        error.clear();
        rows.clear();
        return true;
    }
    return query_rooms(connection_string, rows, error, {});
}

bool load_report_machine_picker_room_options(const DbSettings& settings, std::vector<RoomOption>& rows, std::string& error) {
    const auto connection_string = make_connection_string_utf8(settings);
    if (connection_string.empty()) {
        error.clear();
        rows.clear();
        return true;
    }
    return query_report_machine_picker_rooms(connection_string, rows, error, {});
}

bool load_patient_type_options(const DbSettings& settings, std::vector<PatientTypeOption>& rows, std::string& error) {
    const auto connection_string = make_connection_string_utf8(settings);
    if (connection_string.empty()) {
        error.clear();
        rows.clear();
        return true;
    }
    return query_patient_types(connection_string, rows, error, {});
}

bool load_machine_options(const DbSettings& settings, const std::string& room_code, std::vector<MachineOption>& rows, std::string& error) {
    const auto connection_string = make_connection_string_utf8(settings);
    if (connection_string.empty()) {
        error.clear();
        rows.clear();
        return true;
    }
    return query_machines(connection_string, room_code, rows, error, {});
}

bool load_report_machine_picker_machine_options(const DbSettings& settings, const std::string& room_code, std::vector<MachineOption>& rows, std::string& error) {
    const auto connection_string = make_connection_string_utf8(settings);
    if (connection_string.empty()) {
        error.clear();
        rows.clear();
        return true;
    }
    if (!query_report_machine_picker_machines(connection_string, room_code, rows, error, {}))
        return false;
    regular_machine::append_blood_manual(rows);
    return true;
}

bool load_microbiology_report_machine_picker_machine_options(
    const DbSettings& settings, const std::string& room_code,
    std::vector<MachineOption>& rows, std::string& error) {
    const auto connection_string = make_connection_string_utf8(settings);
    if (connection_string.empty()) {
        error.clear();
        rows.clear();
        return true;
    }
    return query_microbiology_report_machine_picker_machines(
        connection_string, room_code, rows, error, {});
}

bool run_report_query(const DbSettings& settings, const QueryInput& input, std::vector<ReportRow>& rows, std::string& connection_string, std::string& error) {
    auto filters = make_query_filters(settings, input);
    filters.mach_code = regular_machine::query_code(input.mach_code);
    connection_string = filters.connection_string;
    if (connection_string.empty()) {
        error = "missing connection string";
        rows.clear();
        return false;
    }
    if (!query_reports(filters, rows, error, {})) return false;
    regular_machine::present_reports(rows, input.mach_code);
    return true;
}

bool load_result_rows(const std::string& connection_string, const std::string& rep_no, std::vector<ResultRow>& rows, std::string& error) {
    if (connection_string.empty()) {
        error = "missing connection string";
        rows.clear();
        return false;
    }
    return query_results(connection_string, rep_no, rows, error, {});
}

bool load_specimen_barcode(const DbSettings& settings, const std::string& barcode, SpecimenBarcodeResult& result, std::string& error) {
    const auto connection_string = make_connection_string_utf8(settings);
    if (connection_string.empty()) {
        error = "missing connection string";
        result = SpecimenBarcodeResult{};
        return false;
    }
    SpecimenBarcodeQuery query;
    query.connection_string = connection_string;
    query.barcode = barcode;
    return query_specimen_barcode(query, result, error, {});
}

bool load_specimen_signed_list(const DbSettings& settings, const SpecimenSignedListQuery& input, std::vector<SpecimenSignedListRow>& rows, std::string& error) {
    const auto connection_string = make_connection_string_utf8(settings);
    if (connection_string.empty()) {
        error = "missing connection string";
        rows.clear();
        return false;
    }
    SpecimenSignedListQuery query = input;
    query.connection_string = connection_string;
    return query_specimen_signed_list(query, rows, error, {});
}

}  // namespace search
