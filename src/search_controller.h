#pragma once

#include "app_settings.h"
#include "search_app.h"
#include "search_core.h"

#include <string>
#include <vector>

namespace search {

bool test_database_connection(const DbSettings& settings, std::string& error);
bool load_room_options(const DbSettings& settings, std::vector<RoomOption>& rows, std::string& error);
bool load_patient_type_options(const DbSettings& settings, std::vector<PatientTypeOption>& rows, std::string& error);
bool load_machine_options(const DbSettings& settings, const std::string& room_code, std::vector<MachineOption>& rows, std::string& error);
bool run_report_query(const DbSettings& settings, const QueryInput& input, std::vector<ReportRow>& rows, std::string& connection_string, std::string& error);
bool load_result_rows(const std::string& connection_string, const std::string& rep_no, std::vector<ResultRow>& rows, std::string& error);
bool load_specimen_barcode(const DbSettings& settings, const std::string& barcode, SpecimenBarcodeResult& result, std::string& error);
bool load_specimen_signed_list(const DbSettings& settings, const SpecimenSignedListQuery& input, std::vector<SpecimenSignedListRow>& rows, std::string& error);

}  // namespace search
