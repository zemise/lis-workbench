#pragma once

#include "app_settings.h"
#include "search_core.h"

#include <string>
#include <vector>

namespace search {

struct ViewState {
    std::wstring ini_path;
    AppSettings settings;
    std::vector<ReportRow> report_rows;
    std::vector<ResultRow> result_rows;
    std::vector<RoomOption> room_options;
    std::vector<PatientTypeOption> patient_type_options;
    std::vector<MachineOption> machine_options;
    std::string connection_string;
};

}  // namespace search
