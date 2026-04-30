#include "search_controller.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace search {
namespace {

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

std::string make_connection_string_utf8(const DbSettings& settings) {
    return wide_to_utf8(build_connection_string_w(settings));
}

}  // namespace

bool test_database_connection(const DbSettings& settings, std::string& error) {
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
    return query_rooms(connection_string, rows, error);
}

bool load_patient_type_options(const DbSettings& settings, std::vector<PatientTypeOption>& rows, std::string& error) {
    const auto connection_string = make_connection_string_utf8(settings);
    if (connection_string.empty()) {
        error.clear();
        rows.clear();
        return true;
    }
    return query_patient_types(connection_string, rows, error);
}

bool load_machine_options(const DbSettings& settings, const std::string& room_code, std::vector<MachineOption>& rows, std::string& error) {
    const auto connection_string = make_connection_string_utf8(settings);
    if (connection_string.empty()) {
        error.clear();
        rows.clear();
        return true;
    }
    return query_machines(connection_string, room_code, rows, error);
}

bool run_report_query(const DbSettings& settings, const QueryInput& input, std::vector<ReportRow>& rows, std::string& connection_string, std::string& error) {
    const auto filters = make_query_filters(settings, input);
    connection_string = filters.connection_string;
    if (connection_string.empty()) {
        error = "missing connection string";
        rows.clear();
        return false;
    }
    return query_reports(filters, rows, error);
}

bool load_result_rows(const std::string& connection_string, const std::string& rep_no, std::vector<ResultRow>& rows, std::string& error) {
    if (connection_string.empty()) {
        error = "missing connection string";
        rows.clear();
        return false;
    }
    return query_results(connection_string, rep_no, rows, error);
}

}  // namespace search
