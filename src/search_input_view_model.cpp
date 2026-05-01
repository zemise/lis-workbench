#include "search_input_view_model.h"
#include "search_text.h"

#ifdef _WIN32

#include <commctrl.h>

#include <cstdio>

namespace search {
namespace {

std::string text_of(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(len), L'\0');
    GetWindowTextW(hwnd, text.data(), len + 1);
    return wide_to_utf8(text);
}

void combo_reset(HWND hwnd) {
    SendMessageW(hwnd, CB_RESETCONTENT, 0, 0);
}

void combo_add(HWND hwnd, const wchar_t* text) {
    SendMessageW(hwnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
}

void combo_select(HWND hwnd, int index) {
    SendMessageW(hwnd, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
}

std::string normalized_filter_text(HWND hwnd) {
    const auto value = trim(text_of(hwnd));
    return value == "全部" ? "" : value;
}

template <typename T, typename Getter>
std::string selected_code(HWND combo, const std::vector<T>& rows, Getter getter) {
    if (!combo) {
        return "";
    }
    const auto index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index <= 0) {
        return "";
    }
    const auto option_index = static_cast<size_t>(index - 1);
    if (option_index < rows.size()) {
        return getter(rows[option_index]);
    }
    return "";
}

std::string date_picker_value(HWND hwnd) {
    SYSTEMTIME st{};
    if (DateTime_GetSystemtime(hwnd, &st) != GDT_VALID) {
        return "";
    }
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
    return buffer;
}

}  // namespace

void set_date_picker_today(HWND hwnd) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    DateTime_SetSystemtime(hwnd, GDT_VALID, &st);
}

void initialize_report_status_combo(HWND combo) {
    combo_reset(combo);
    combo_add(combo, L"全部");
    combo_add(combo, L"已审核");
    combo_add(combo, L"未审核");
    combo_add(combo, L"已发送");
    combo_add(combo, L"未发送");
    combo_select(combo, 0);
}

void populate_room_combo(HWND combo, const std::vector<RoomOption>& rows) {
    combo_reset(combo);
    combo_add(combo, L"全部");
    combo_select(combo, 0);
    for (const auto& row : rows) {
        combo_add(combo, utf8_to_wide(row.room_name).c_str());
    }
}

void populate_patient_type_combo(HWND combo, const std::vector<PatientTypeOption>& rows) {
    combo_reset(combo);
    combo_add(combo, L"全部");
    combo_select(combo, 0);
    for (const auto& row : rows) {
        combo_add(combo, utf8_to_wide(row.type_code + "-" + row.type_name).c_str());
    }
}

void populate_machine_combo(HWND combo, const std::vector<MachineOption>& rows) {
    combo_reset(combo);
    combo_add(combo, L"全部");
    combo_select(combo, 0);
    for (const auto& row : rows) {
        combo_add(combo, utf8_to_wide(row.mach_name).c_str());
    }
}

std::string selected_room_code(HWND combo, const std::vector<RoomOption>& rows) {
    return selected_code(combo, rows, [](const RoomOption& row) { return row.room_code; });
}

std::string selected_patient_type_code(HWND combo, const std::vector<PatientTypeOption>& rows) {
    return selected_code(combo, rows, [](const PatientTypeOption& row) { return row.type_code; });
}

std::string selected_machine_code(HWND combo, const std::vector<MachineOption>& rows) {
    return selected_code(combo, rows, [](const MachineOption& row) { return row.mach_code; });
}

QueryInput build_query_input(const MainUiHandles& ui, const ViewState& state) {
    QueryInput input;
    input.patient_id = text_of(ui.patient_id);
    input.barcode = text_of(ui.barcode);
    input.patient_name = text_of(ui.name);
    input.patient_no = text_of(ui.patient_no);
    input.oper_no = text_of(ui.oper);
    input.start_date = date_picker_value(ui.start);
    input.end_date = date_picker_value(ui.end);
    input.room_code = selected_room_code(ui.room, state.room_options);
    input.patient_type = selected_patient_type_code(ui.patient_type, state.patient_type_options);
    input.report_status = normalized_filter_text(ui.report_status);
    input.mach_code = selected_machine_code(ui.mach, state.machine_options);
    input.group_code = text_of(ui.group);
    input.item_code = text_of(ui.item);
    input.limit = 0;
    return input;
}

}  // namespace search

#endif
