#include "search_ui_layout.h"
#include "search_ui_columns.h"

#ifdef _WIN32

#include <commctrl.h>

#include <algorithm>

namespace search {

HWND create_label(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND create_groupbox(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND create_edit(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                           x, y, w, h, parent, reinterpret_cast<HMENU>(id), GetModuleHandleW(nullptr), nullptr);
}

HWND create_date_picker(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATEFORMAT,
                           x, y, w, h, parent, reinterpret_cast<HMENU>(id), GetModuleHandleW(nullptr), nullptr);
}

HWND create_combo(HWND parent, int id, int x, int y, int w, int h, bool editable) {
    const DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                        (editable ? CBS_DROPDOWN : CBS_DROPDOWNLIST);
    return CreateWindowExW(0, L"COMBOBOX", L"", style,
                           x, y, w, h, parent, reinterpret_cast<HMENU>(id), GetModuleHandleW(nullptr), nullptr);
}

HWND create_password_edit(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
                           x, y, w, h, parent, reinterpret_cast<HMENU>(id), GetModuleHandleW(nullptr), nullptr);
}

HWND create_button(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                           x, y, w, h, parent, reinterpret_cast<HMENU>(id), GetModuleHandleW(nullptr), nullptr);
}

void add_list_column(HWND list, int index, const wchar_t* title, int width) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.pszText = const_cast<wchar_t*>(title);
    col.cx = width;
    col.iSubItem = index;
    ListView_InsertColumn(list, index, &col);
}

void create_main_controls(HWND hwnd, HFONT font, const MainUiIds& ids, MainUiHandles& ui) {
    constexpr int kReportsX = 220;
    constexpr int kReportsWidth = 820;
    constexpr int kResultsX = 1048;
    constexpr int kResultsWidth = 410;
    constexpr int kListTop = 8;
    constexpr int kListHeight = 685;
    constexpr int kButtonsY = 714;

    int y = 16;
    create_label(hwnd, L"诊疗卡号", 8, y, 72, 22); ui.patient_id = create_edit(hwnd, ids.patient_id, 88, y, 120, 24); y += 28;
    create_label(hwnd, L"条码号", 8, y, 72, 22); ui.barcode = create_edit(hwnd, ids.barcode, 88, y, 120, 24); y += 28;
    create_label(hwnd, L"病人姓名", 8, y, 72, 22); ui.name = create_edit(hwnd, ids.name, 88, y, 120, 24); y += 28;
    create_label(hwnd, L"病人号", 8, y, 72, 22); ui.patient_no = create_edit(hwnd, ids.patient_no, 88, y, 120, 24); y += 28;
    create_label(hwnd, L"样本号", 8, y, 72, 22); ui.oper = create_edit(hwnd, ids.oper, 88, y, 120, 24); y += 28;
    create_label(hwnd, L"开始日期", 8, y, 72, 22); ui.start = create_date_picker(hwnd, ids.start, 88, y, 120, 24); y += 28;
    create_label(hwnd, L"结束日期", 8, y, 72, 22); ui.end = create_date_picker(hwnd, ids.end, 88, y, 120, 24); y += 28;
    ui.group_test = create_groupbox(hwnd, L"检验条件", 8, y - 2, 202, 78);
    create_label(hwnd, L"检验科室", 14, y + 18, 66, 22); ui.room = create_combo(hwnd, ids.room, 88, y + 16, 120, 180, false);
    create_label(hwnd, L"检验仪器", 14, y + 46, 66, 22); ui.mach = create_combo(hwnd, ids.mach, 88, y + 44, 120, 180, false);
    y += 84;
    ui.group_patient = create_groupbox(hwnd, L"病人条件", 8, y - 2, 202, 78);
    create_label(hwnd, L"病人类型", 14, y + 18, 66, 22); ui.patient_type = create_combo(hwnd, ids.patient_type, 88, y + 16, 120, 180, false);
    create_label(hwnd, L"报告状态", 14, y + 46, 66, 22); ui.report_status = create_combo(hwnd, ids.report_status, 88, y + 44, 120, 180, false);
    y += 84;
    create_label(hwnd, L"组合项目", 8, y, 72, 22); ui.group = create_edit(hwnd, ids.group, 88, y, 120, 24); y += 28;
    create_label(hwnd, L"项目代码", 8, y, 72, 22); ui.item = create_edit(hwnd, ids.item, 88, y, 120, 24);

    ui.reports = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                 WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                 kReportsX, kListTop, kReportsWidth, kListHeight,
                                 hwnd, reinterpret_cast<HMENU>(ids.reports), GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(ui.reports, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    add_list_column(ui.reports, report_columns::SampleNo, L"样本号", 70);
    add_list_column(ui.reports, report_columns::Name, L"姓名", 80);
    add_list_column(ui.reports, report_columns::Barcode, L"条码号", 110);
    add_list_column(ui.reports, report_columns::ReportTime, L"上机时间", 130);
    add_list_column(ui.reports, report_columns::Sex, L"性别", 50);
    add_list_column(ui.reports, report_columns::Age, L"年龄", 60);
    add_list_column(ui.reports, report_columns::Bed, L"床号", 60);
    add_list_column(ui.reports, report_columns::PatientType, L"病人类型", 70);
    add_list_column(ui.reports, report_columns::Requester, L"检验者", 80);
    add_list_column(ui.reports, report_columns::Reviewer, L"审核者", 80);
    add_list_column(ui.reports, report_columns::GroupName, L"项目名称", 110);
    add_list_column(ui.reports, report_columns::ReviewStatus, L"审核", 60);
    add_list_column(ui.reports, report_columns::ConfirmStatus, L"确认", 60);
    add_list_column(ui.reports, report_columns::PrintStatus, L"打印", 60);
    add_list_column(ui.reports, report_columns::SelfServicePrintStatus, L"自助机", 60);

    ui.splitter = CreateWindowExW(0, L"ResultSearchSplitter", L"",
                                  WS_CHILD | WS_VISIBLE,
                                  1040, kListTop, 8, kListHeight,
                                  hwnd, reinterpret_cast<HMENU>(ids.splitter), GetModuleHandleW(nullptr), nullptr);

    ui.results = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                 WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                 kResultsX, kListTop, kResultsWidth, kListHeight,
                                 hwnd, reinterpret_cast<HMENU>(ids.results), GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(ui.results, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    add_list_column(ui.results, result_columns::ItemName, L"项目名称", 150);
    add_list_column(ui.results, result_columns::Result, L"结果", 80);
    add_list_column(ui.results, result_columns::LowerBound, L"下限", 70);
    add_list_column(ui.results, result_columns::UpperBound, L"上限", 70);
    add_list_column(ui.results, result_columns::Unit, L"单位", 70);
    add_list_column(ui.results, result_columns::EnglishName, L"英文名称", 80);

    ui.settings_button = create_button(hwnd, ids.settings, L"设置", 620, kButtonsY, 92, 32);
    ui.query_button = create_button(hwnd, ids.query, L"查询(&Q)", 720, kButtonsY, 92, 32);
    ui.export_button = create_button(hwnd, ids.export_, L"导出(&E)", 820, kButtonsY, 92, 32);
    ui.preview_button = create_button(hwnd, ids.preview, L"预览(&V)", 920, kButtonsY, 92, 32);
    ui.print_button = create_button(hwnd, ids.print, L"打印(&P)", 1020, kButtonsY, 92, 32);
    ui.exit_button = create_button(hwnd, ids.exit, L"退出(&X)", 1120, kButtonsY, 92, 32);
    ui.status = CreateWindowExW(0, L"STATIC", L"请输入条件后查询。", WS_CHILD | WS_VISIBLE,
                                8, kButtonsY + 2, 600, 28, hwnd, reinterpret_cast<HMENU>(ids.status), GetModuleHandleW(nullptr), nullptr);

    EnumChildWindows(hwnd, [](HWND child, LPARAM param) -> BOOL {
        SendMessageW(child, WM_SETFONT, param, TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font));
}

void layout_main_window(HWND hwnd, MainUiHandles& ui, int& splitter_x) {
    if (!hwnd || !ui.reports || !ui.results) {
        return;
    }

    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int client_w = rc.right - rc.left;
    const int client_h = rc.bottom - rc.top;

    const int margin = 8;
    const int left_x = 8;
    const int left_input_x = 88;
    const int left_input_w = 120;
    const int left_panel_w = 202;
    const int edit_h = 24;
    const int row_gap = 28;
    const int group_h = 78;
    const int button_w = 92;
    const int button_h = 32;
    const int button_gap = 8;
    const int status_w = 600;
    const int bottom_margin = 10;
    const int splitter_w = 8;
    const int reports_x = 220;
    const int min_reports_w = 520;
    const int min_results_w = 330;
    const int default_results_w = 410;

    const int buttons_y = client_h - bottom_margin - button_h;
    const int status_y = buttons_y + 2;
    const int list_top = 8;
    const int list_bottom = buttons_y - 12;
    const int list_h = std::max(120, list_bottom - list_top);
    const int min_splitter_x = reports_x + min_reports_w;
    const int max_splitter_x = std::max(min_splitter_x, client_w - margin - min_results_w - splitter_w);
    if (splitter_x <= 0) {
        splitter_x = client_w - margin - default_results_w - splitter_w;
    }
    splitter_x = std::max(min_splitter_x, std::min(max_splitter_x, splitter_x));
    const int reports_w = splitter_x - reports_x;
    const int results_x = splitter_x + splitter_w;
    const int results_w = std::max(min_results_w, client_w - margin - results_x);

    int y = 16;
    MoveWindow(ui.patient_id, left_input_x, y, left_input_w, edit_h, TRUE); y += row_gap;
    MoveWindow(ui.barcode, left_input_x, y, left_input_w, edit_h, TRUE); y += row_gap;
    MoveWindow(ui.name, left_input_x, y, left_input_w, edit_h, TRUE); y += row_gap;
    MoveWindow(ui.patient_no, left_input_x, y, left_input_w, edit_h, TRUE); y += row_gap;
    MoveWindow(ui.oper, left_input_x, y, left_input_w, edit_h, TRUE); y += row_gap;
    MoveWindow(ui.start, left_input_x, y, left_input_w, edit_h, TRUE); y += row_gap;
    MoveWindow(ui.end, left_input_x, y, left_input_w, edit_h, TRUE); y += row_gap;

    if (ui.group_test) MoveWindow(ui.group_test, left_x, y - 2, left_panel_w, group_h, TRUE);
    MoveWindow(ui.room, left_input_x, y + 16, left_input_w, edit_h + 200, TRUE);
    MoveWindow(ui.mach, left_input_x, y + 44, left_input_w, edit_h + 200, TRUE);
    y += 84;

    if (ui.group_patient) MoveWindow(ui.group_patient, left_x, y - 2, left_panel_w, group_h, TRUE);
    MoveWindow(ui.patient_type, left_input_x, y + 16, left_input_w, edit_h + 200, TRUE);
    MoveWindow(ui.report_status, left_input_x, y + 44, left_input_w, edit_h + 200, TRUE);
    y += 84;

    MoveWindow(ui.group, left_input_x, y, left_input_w, edit_h, TRUE); y += row_gap;
    MoveWindow(ui.item, left_input_x, y, left_input_w, edit_h, TRUE);

    MoveWindow(ui.reports, reports_x, list_top, reports_w, list_h, TRUE);
    if (ui.splitter) MoveWindow(ui.splitter, splitter_x, list_top, splitter_w, list_h, TRUE);
    MoveWindow(ui.results, results_x, list_top, results_w, list_h, TRUE);

    const int buttons_start_x = client_w - margin - (button_w * 6 + button_gap * 5);
    MoveWindow(ui.settings_button, buttons_start_x, buttons_y, button_w, button_h, TRUE);
    MoveWindow(ui.query_button, buttons_start_x + (button_w + button_gap) * 1, buttons_y, button_w, button_h, TRUE);
    MoveWindow(ui.export_button, buttons_start_x + (button_w + button_gap) * 2, buttons_y, button_w, button_h, TRUE);
    MoveWindow(ui.preview_button, buttons_start_x + (button_w + button_gap) * 3, buttons_y, button_w, button_h, TRUE);
    MoveWindow(ui.print_button, buttons_start_x + (button_w + button_gap) * 4, buttons_y, button_w, button_h, TRUE);
    MoveWindow(ui.exit_button, buttons_start_x + (button_w + button_gap) * 5, buttons_y, button_w, button_h, TRUE);
    MoveWindow(ui.status, margin, status_y, std::min(status_w, buttons_start_x - margin - 8), 28, TRUE);
}

}  // namespace search

#endif
