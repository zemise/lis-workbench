#pragma once

#include "app_settings.h"
#include "search_core.h"

#include <filesystem>
#include <string>
#include <vector>

namespace search {

struct ViewState {
    std::filesystem::path ini_path;
    AppSettings settings;
    std::vector<ReportRow> report_rows;
    std::vector<ResultRow> result_rows;
    std::vector<RoomOption> room_options;
    std::vector<PatientTypeOption> patient_type_options;
    std::vector<MachineOption> machine_options;
    std::string connection_string;
};

ViewState load_view_state();
bool save_view_state_settings(const ViewState& state);

}  // namespace search
