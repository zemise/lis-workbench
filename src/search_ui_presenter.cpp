#include "search_ui_presenter.h"

#ifdef _WIN32

#include "search_app.h"

#include <commctrl.h>

namespace search {
namespace {

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) {
        return L"";
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
    if (!out.empty() && out.back() == L'\0') {
        out.pop_back();
    }
    return out;
}

void set_cell(HWND list, int row, int col, const std::string& text) {
    const auto wide = utf8_to_wide(text);
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(wide.c_str()));
}

void insert_report_row(HWND list, int index, const ReportRow& row) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = index;
    std::wstring first = utf8_to_wide(row.oper_no);
    item.pszText = first.data();
    ListView_InsertItem(list, &item);
    set_cell(list, index, 1, row.name);
    set_cell(list, index, 2, row.txm_no);
    set_cell(list, index, 3, row.chk_date);
    set_cell(list, index, 4, row.sex);
    set_cell(list, index, 5, row.age);
    set_cell(list, index, 6, row.bed_code);
    set_cell(list, index, 7, row.patient_type);
    set_cell(list, index, 8, row.requester);
    set_cell(list, index, 9, row.group_name);
    set_cell(list, index, 10, display_conf(row.conf));
    set_cell(list, index, 11, display_chk_flag(row.chk_flag));
    set_cell(list, index, 12, display_binary_print_flag(row.zymz_print));
    set_cell(list, index, 13, display_binary_print_flag(row.zzj_print));
}

void insert_result_row(HWND list, int index, const ResultRow& row) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = index;
    std::wstring first = utf8_to_wide(row.item_name);
    item.pszText = first.data();
    ListView_InsertItem(list, &item);
    set_cell(list, index, 1, row.result);
    set_cell(list, index, 2, row.downbound);
    set_cell(list, index, 3, row.upbound);
    set_cell(list, index, 4, row.unit);
    set_cell(list, index, 5, row.item_eng);
}

}  // namespace

void set_status_text(const MainUiHandles& ui, const std::wstring& text) {
    SetWindowTextW(ui.status, text.c_str());
}

void clear_result_lists(const MainUiHandles& ui) {
    ListView_DeleteAllItems(ui.reports);
    ListView_DeleteAllItems(ui.results);
}

void present_report_rows(const MainUiHandles& ui, const std::vector<ReportRow>& rows) {
    ListView_DeleteAllItems(ui.reports);
    for (size_t i = 0; i < rows.size(); ++i) {
        insert_report_row(ui.reports, static_cast<int>(i), rows[i]);
    }
}

void present_result_rows(const MainUiHandles& ui, const std::vector<ResultRow>& rows) {
    ListView_DeleteAllItems(ui.results);
    for (size_t i = 0; i < rows.size(); ++i) {
        insert_result_row(ui.results, static_cast<int>(i), rows[i]);
    }
}

}  // namespace search

#endif
