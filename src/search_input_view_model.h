#pragma once

#ifdef _WIN32

#include "search_app.h"
#include "search_ui_layout.h"
#include "search_view_state.h"

#include <windows.h>

namespace search {

void set_date_picker_today(HWND hwnd);
void initialize_report_status_combo(HWND combo);
void populate_room_combo(HWND combo, const std::vector<RoomOption>& rows);
void populate_patient_type_combo(HWND combo, const std::vector<PatientTypeOption>& rows);
void populate_machine_combo(HWND combo, const std::vector<MachineOption>& rows);

std::string selected_room_code(HWND combo, const std::vector<RoomOption>& rows);
std::string selected_patient_type_code(HWND combo, const std::vector<PatientTypeOption>& rows);
std::string selected_machine_code(HWND combo, const std::vector<MachineOption>& rows);

QueryInput build_query_input(const MainUiHandles& ui, const ViewState& state);

}  // namespace search

#endif
