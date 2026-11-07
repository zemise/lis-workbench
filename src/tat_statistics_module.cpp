#include "tat_statistics_module.h"

#ifdef _WIN32

#include "query_diagnostics.h"
#include "main_app.h"
#include "page_feedback.h"
#include "resource.h"
#include "search_core.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"
#include "window_task.h"
#include "xlsx_writer.h"

#include <commctrl.h>
#include <commdlg.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"TatStatisticsModuleChild";
constexpr const wchar_t* WINDOW_TITLE = L"检验周转时间统计";
constexpr const wchar_t* PROP_STATE = L"TatStatisticsState";

constexpr COLORREF COLOR_NORMAL = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF COLOR_OVERTIME = RGB(0xFF, 0xF5, 0x7A);
constexpr COLORREF COLOR_ABNORMAL = RGB(0xFF, 0xCD, 0xD2);

enum ControlId {
    IDC_START = 7401,
    IDC_END = 7402,
    IDC_ROOM = 7403,
    IDC_PATIENT_TYPE = 7404,
    IDC_EMERGENCY = 7405,
    IDC_DEPARTMENT = 7406,
    IDC_ORDER = 7407,
    IDC_TAT_STATUS = 7408,
    IDC_QUERY = 7409,
    IDC_RESET = 7410,
    IDC_EXPORT = 7411,
    IDC_MODE = 7412,
    IDC_SUMMARY = 7413,
    IDC_DETAILS = 7414,
    IDC_STATUS = 7415,
    IDC_RESERVED_CYCLE = 7416,
    IDC_RESERVED_CALC = 7417,
    IDC_RESERVED_HISTORY = 7418,
    IDC_RESERVED_PRINT = 7419,
    IDC_REFRESH = 7420,
    IDC_SIGN_DEPT = 7421,
    IDC_ORDER_GROUP = 7422,
};

enum DetailColumn {
    COL_PATIENT_TYPE,
    COL_BARCODE,
    COL_REG_NO,
    COL_NAME,
    COL_SEX,
    COL_DIAGNOSIS,
    COL_BED_NO,
    COL_AGE,
    COL_SAMPLE,
    COL_DEPARTMENT,
    COL_ROOM,
    COL_ORDER,
    COL_COLLECTION_TIME,
    COL_COLLECTION_RECEIVE,
    COL_RECEIVE_TIME,
    COL_RECEIVE_MACHINE,
    COL_MACHINE_TIME,
    COL_RECEIVE_REVIEW,
    COL_RECEIVER,
    COL_REVIEWER,
    COL_SEND_TIME,
    COL_COLLECTION_REVIEW,
    COL_QUALITY,
    COL_TAT_STATUS,
    COL_RECEIVE_SOURCE,
    COL_SEND_SOURCE,
    COL_REPORT_COUNT,
    DETAIL_COLUMN_COUNT,
};

struct ListColumn { const wchar_t* title; int width; };

constexpr ListColumn SUMMARY_COLUMNS[] = {
    {L"条码总数", 100}, {L"明细总数", 100}, {L"正常候选明细", 120}, {L"超时明细", 100},
    {L"有效样本", 100}, {L"超时率", 90}, {L"缺少时间", 100}, {L"缺少标记", 100},
    {L"时间异常", 100}, {L"显示条码", 100}, {L"显示明细", 100},
    {L"采集-接收超时率", 150}, {L"接收-发送超时率", 150}, {L"采集-发送超时率", 150},
};

constexpr ListColumn DETAIL_COLUMNS[] = {
    {L"病人类型", 80}, {L"条码号", 125}, {L"病人号", 130}, {L"姓名", 85},
    {L"性别", 50}, {L"诊断", 150}, {L"床号", 65}, {L"年龄", 60},
    {L"标本", 90}, {L"申请科室", 150}, {L"专业组", 120}, {L"医嘱项目", 220},
    {L"采集时间", 145}, {L"采集-接收", 155}, {L"接收时间", 145},
    {L"接收-上机", 155}, {L"上机时间", 145}, {L"接收-发送", 165},
    {L"签收人", 90}, {L"审核人", 90}, {L"发送时间", 145},
    {L"采集-发送", 165}, {L"数据提示", 260}, {L"TAT状态", 130},
    {L"签收时间来源", 120}, {L"发送时间来源", 120}, {L"同条码报告数", 130},
};
static_assert(std::size(DETAIL_COLUMNS) == DETAIL_COLUMN_COUNT);

using DetailRow = search::TatStatDetailRow;

struct TatState {
    ModuleContext ctx;
    HWND start = nullptr;
    HWND end = nullptr;
    HWND room = nullptr;
    HWND patientType = nullptr;
    HWND emergency = nullptr;
    HWND department = nullptr;
    HWND order = nullptr;
    HWND tatStatus = nullptr;
    HWND query = nullptr;
    HWND reset = nullptr;
    HWND exportExcel = nullptr;
    HWND mode = nullptr;
    HWND refresh = nullptr;
    HWND signDepartment = nullptr;
    HWND orderGroup = nullptr;
    HWND summary = nullptr;
    HWND details = nullptr;
    HWND status = nullptr;
    search::PageFeedback feedback;
    HBRUSH bgBrush = nullptr;
    app::WindowTask queryTask;
    app::WindowTask roomTask;
    app::WindowTask exportTask;
    bool querying = false;
    bool exporting = false;
    bool hasLoaded = false;
    int sortColumn = COL_RECEIVE_TIME;
    bool sortAscending = false;
    std::string loadedAt;
    search::TatStatSummary statSummary;
    search::TatPickerOptions pickers;
    std::vector<DetailRow> allRows;
    std::vector<DetailRow> rows;
    std::string loadedStart;
    std::string loadedEnd;
};

struct QueryResult {
    bool ok = false;
    std::string error;
    std::string start;
    std::string end;
    search::TatStatSummary summary;
    std::vector<DetailRow> rows;
};

struct RoomResult {
    bool ok = false;
    search::TatPickerOptions options;
    std::string error;
};

struct ExportResult {
    bool ok = false;
    std::wstring path;
    std::string error;
};

int S(HWND hwnd, int value) {
    return static_cast<int>(value * search::dpi_scale_factor(hwnd));
}

HWND label(HWND parent, const wchar_t* text) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT,
                           0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND edit(HWND parent, int id) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                           0, 0, 0, 0, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND combo(HWND parent, int id) {
    return CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                           CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0, parent,
                           win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND dateTimePicker(HWND parent, int id) {
    HWND control = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_VISIBLE |
        WS_TABSTOP | DTS_SHORTDATECENTURYFORMAT, 0, 0, 0, 0, parent,
        win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
    DateTime_SetFormat(control, L"yyyy-MM-dd");
    return control;
}

void addCombo(HWND control, const wchar_t* text) {
    SendMessageW(control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
}

std::wstring comboText(HWND control) {
    const int index = static_cast<int>(SendMessageW(control, CB_GETCURSEL, 0, 0));
    if (index < 0) return {};
    wchar_t text[128]{};
    SendMessageW(control, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text));
    return text;
}

std::wstring controlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0) GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<size_t>(length));
    return value;
}

std::string dateTimeText(HWND control) {
    SYSTEMTIME value{};
    if (DateTime_GetSystemtime(control, &value) != GDT_VALID) return {};
    char text[32]{};
    sprintf_s(text, "%04u-%02u-%02u", value.wYear, value.wMonth, value.wDay);
    return text;
}

std::string currentTimeText() {
    SYSTEMTIME value{};
    GetLocalTime(&value);
    char text[32]{};
    sprintf_s(text, "%04u-%02u-%02u %02u:%02u:%02u", value.wYear, value.wMonth,
              value.wDay, value.wHour, value.wMinute, value.wSecond);
    return text;
}

void setToday(HWND start, HWND end) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    SYSTEMTIME first = now;
    first.wHour = first.wMinute = first.wSecond = first.wMilliseconds = 0;
    now.wHour = 23;
    now.wMinute = 59;
    now.wSecond = now.wMilliseconds = 0;
    DateTime_SetSystemtime(start, GDT_VALID, &first);
    DateTime_SetSystemtime(end, GDT_VALID, &now);
}

std::string cellValue(const DetailRow& row, int column) {
    switch (column) {
        case COL_PATIENT_TYPE: return row.patient_type;
        case COL_BARCODE: return row.barcode;
        case COL_REG_NO: return row.reg_no;
        case COL_NAME: return row.name;
        case COL_SEX: return row.sex;
        case COL_DIAGNOSIS: return row.diagnosis;
        case COL_BED_NO: return row.bed_no;
        case COL_AGE: return row.age;
        case COL_SAMPLE: return row.sample_name;
        case COL_DEPARTMENT: return row.department_name;
        case COL_ROOM: return row.room_name;
        case COL_ORDER: return row.order_text;
        case COL_COLLECTION_TIME: return row.collection_time.empty() ? "-" : row.collection_time;
        case COL_COLLECTION_RECEIVE: return row.display_times[0];
        case COL_RECEIVE_TIME: return row.receive_time;
        case COL_RECEIVE_MACHINE: return row.display_times[3];
        case COL_MACHINE_TIME: return row.machine_time.empty() ? "-" : row.machine_time;
        case COL_RECEIVE_REVIEW: return row.display_times[1];
        case COL_RECEIVER: return row.receiver;
        case COL_REVIEWER: return row.reviewer;
        case COL_SEND_TIME: return row.send_time.empty() ? "-" : row.send_time;
        case COL_COLLECTION_REVIEW: return row.display_times[2];
        case COL_QUALITY: return row.quality_note;
        case COL_TAT_STATUS: return row.tat_status;
        case COL_RECEIVE_SOURCE: return row.receive_source;
        case COL_SEND_SOURCE: return row.send_source;
        case COL_REPORT_COUNT: return std::to_string(row.report_count);
        default: return {};
    }
}

long long durationValue(const DetailRow& row, int column) {
    switch (column) {
        case COL_COLLECTION_RECEIVE: return row.duration_seconds[0];
        case COL_RECEIVE_MACHINE: return row.duration_seconds[3];
        case COL_RECEIVE_REVIEW: return row.duration_seconds[1];
        case COL_COLLECTION_REVIEW: return row.duration_seconds[2];
        default: return -2;
    }
}

void sortRows(TatState* state, int column, bool toggle) {
    if (!state) return;
    if (toggle) {
        if (state->sortColumn == column) state->sortAscending = !state->sortAscending;
        else { state->sortColumn = column; state->sortAscending = true; }
    }
    const int sortColumn = state->sortColumn;
    const bool ascending = state->sortAscending;
    std::stable_sort(state->rows.begin(), state->rows.end(), [=](const DetailRow& lhs, const DetailRow& rhs) {
        const long long leftDuration = durationValue(lhs, sortColumn);
        const long long rightDuration = durationValue(rhs, sortColumn);
        if (leftDuration != -2 || rightDuration != -2) {
            return ascending ? leftDuration < rightDuration : leftDuration > rightDuration;
        }
        const auto left = cellValue(lhs, sortColumn);
        const auto right = cellValue(rhs, sortColumn);
        return ascending ? left < right : left > right;
    });
}

void populateDetails(TatState* state) {
    if (!state || !state->details) return;
    ListView_SetItemState(state->details, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemCountEx(state->details, static_cast<int>(state->rows.size()), LVSICF_NOSCROLL);
    InvalidateRect(state->details, nullptr, TRUE);
}

search::TatMode selectedMode(TatState* state) {
    return SendMessageW(state->mode, CB_GETCURSEL, 0, 0) == 1 ?
        search::TatMode::improved : search::TatMode::compatibility;
}

void populateSummary(TatState* state) {
    if (!state || !state->summary) return;
    const auto& s = state->statSummary;
    const bool improved = selectedMode(state) == search::TatMode::improved;
    const wchar_t* titles[] = {improved ? L"正常条码" : L"正常候选明细",
                              improved ? L"超时条码" : L"超时明细"};
    for (int i = 0; i < 2; ++i) {
        LVCOLUMNW column{}; column.mask = LVCF_TEXT; column.pszText = const_cast<wchar_t*>(titles[i]);
        ListView_SetColumn(state->summary, i + 2, &column);
    }
    const std::string values[] = {std::to_string(s.total_count), std::to_string(s.detail_count),
        std::to_string(s.normal_count), std::to_string(s.overtime_count),
        improved ? std::to_string(s.eligible_count) : "-", s.overtime_rate,
        std::to_string(s.missing_time_count), std::to_string(s.missing_marks_count),
        std::to_string(s.time_abnormal_count), std::to_string(s.visible_barcode_count),
        std::to_string(s.visible_count), s.stage_overtime_rate[0], s.stage_overtime_rate[1], s.stage_overtime_rate[2]};
    ListView_DeleteAllItems(state->summary);
    auto first = search::utf8_to_wide(values[0]);
    LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = 0; item.pszText = first.data();
    ListView_InsertItem(state->summary, &item);
    for (int i = 1; i < static_cast<int>(std::size(values)); ++i) {
        auto value = search::utf8_to_wide(values[i]);
        ListView_SetItemText(state->summary, 0, i, value.data());
    }
}

void applyStatusFilter(TatState* state, bool = true) {
    if (!state) return;
    const auto index = static_cast<int>(SendMessageW(state->tatStatus, CB_GETCURSEL, 0, 0));
    const search::TatFilter filters[] = {search::TatFilter::all, search::TatFilter::normal,
        search::TatFilter::overtime, search::TatFilter::missing_time,
        search::TatFilter::missing_marks, search::TatFilter::abnormal};
    const auto filter = index >= 0 && index < 6 ? filters[index] : search::TatFilter::all;
    search::select_tat_statistics(selectedMode(state), filter, state->allRows, state->statSummary, state->rows);
    sortRows(state, state->sortColumn, false);
    populateDetails(state); populateSummary(state);
    EnableWindow(state->exportExcel, state->hasLoaded && !state->rows.empty() && !state->exporting);
    if (state->hasLoaded) {
        const bool improved = selectedMode(state) == search::TatMode::improved;
        search::set_page_status(state->feedback,
            (improved ? std::wstring(L"改良口径：按条码统计，有效样本为正常+超时。") :
                        std::wstring(L"标准兼容：总数按条码、超时按明细；缺少标记不等于正常。")) +
            (improved ? L" 汇总基于全部查询结果。显示 " : L" 总汇总基于查询集合；阶段率按当前明细。显示 ") + std::to_wstring(state->rows.size()) +
            L" 条明细。签收日期 " + search::utf8_to_wide(state->loadedStart) + L" 至 " +
            search::utf8_to_wide(state->loadedEnd) + L"；查询时间 " + search::utf8_to_wide(state->loadedAt));
    }
}

void setQueryControls(TatState* state, bool enabled) {
    const BOOL value = enabled ? TRUE : FALSE;
    HWND controls[] = {state->start, state->end, state->room, state->patientType, state->emergency,
                       state->department, state->order, state->tatStatus, state->query, state->reset,
                       state->mode, state->refresh, state->signDepartment, state->orderGroup};
    for (HWND control : controls) EnableWindow(control, value);
}

void finishQuery(TatState* state, QueryResult result) {
    if (!state) return;
    state->querying = false;
    setQueryControls(state, true);
    search::hide_page_activity(state->feedback);
    if (!result.ok) {
        EnableWindow(state->exportExcel, state->hasLoaded && !state->rows.empty());
        search::show_page_alert(state->feedback,
            L"查询失败：" + search::utf8_to_wide(result.error));
        return;
    }
    state->loadedStart = result.start;
    state->loadedEnd = result.end;
    state->allRows = std::move(result.rows);
    state->statSummary = result.summary;
    state->hasLoaded = true;
    state->loadedAt = currentTimeText();
    state->sortColumn = COL_RECEIVE_TIME;
    state->sortAscending = true;
    populateSummary(state);
    applyStatusFilter(state);
}

void finishRoomLoad(TatState* state, RoomResult result) {
    if (!state) return;
    state->pickers = std::move(result.options);
    const auto populate = [&](HWND control, const auto& options) {
        SendMessageW(control, CB_RESETCONTENT, 0, 0); addCombo(control, L"全部");
        for (const auto& option : options) {
            auto text = search::utf8_to_wide(option.name.empty() ? option.code : option.name);
            addCombo(control, text.c_str());
        }
        SendMessageW(control, CB_SETCURSEL, 0, 0);
    };
    populate(state->room, state->pickers.rooms);
    populate(state->department, state->pickers.departments);
    populate(state->signDepartment, state->pickers.departments);
    populate(state->orderGroup, state->pickers.order_groups);
    if (!result.ok && !state->querying && !state->hasLoaded)
        search::show_page_alert(state->feedback, L"筛选列表加载失败：" + search::utf8_to_wide(result.error));
}

void finishExport(TatState* state, ExportResult result) {
    if (!state) return;
    state->exporting = false;
    setQueryControls(state, true);
    search::hide_page_activity(state->feedback);
    EnableWindow(state->exportExcel, state->hasLoaded && !state->rows.empty());
    if (!result.ok) {
        search::show_page_alert(state->feedback,
            L"导出失败：" + search::utf8_to_wide(result.error));
    } else {
        search::set_page_status(state->feedback, L"已导出：" + result.path);
    }
}

std::string selectedCode(HWND control, const std::vector<search::TatCodeOption>& options) {
    const int index = static_cast<int>(SendMessageW(control, CB_GETCURSEL, 0, 0));
    if (index == 0) return {};
    if (index > 0 && static_cast<size_t>(index - 1) < options.size()) return options[index - 1].code;
    const auto text = search::wide_to_utf8(controlText(control));
    return text == "全部" ? "" : text;
}

void runQuery(HWND hwnd, TatState* state) {
    if (!state || state->querying || state->exporting) return;
    const auto connection = search::build_connection_string_w(state->ctx.dbSettings);
    if (connection.empty()) {
        MessageBoxW(hwnd, L"请先在系统设置中配置数据库连接。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }
    search::TatStatQuery query;
    query.connection_string = search::wide_to_utf8(connection);
    query.start_time = dateTimeText(state->start);
    query.end_time = dateTimeText(state->end);
    query.room_code = selectedCode(state->room, state->pickers.rooms);
    query.department_code = selectedCode(state->department, state->pickers.departments);
    query.order_keyword = search::wide_to_utf8(controlText(state->order));
    query.patient_type = search::wide_to_utf8(comboText(state->patientType));
    query.emergency_only = Button_GetCheck(state->emergency) == BST_CHECKED;
    query.sign_department_code = selectedCode(state->signDepartment, state->pickers.departments);
    query.order_group_id = selectedCode(state->orderGroup, state->pickers.order_groups);
    if (query.start_time.empty() || query.end_time.empty() || query.start_time > query.end_time) {
        MessageBoxW(hwnd, L"签收开始时间不能晚于结束时间。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }
    state->querying = true;
    setQueryControls(state, false);
    EnableWindow(state->exportExcel, FALSE);
    search::set_page_status(state->feedback, L"正在查询检验周转时间...");
    search::show_page_activity(state->feedback, L"正在查询检验周转时间，请稍候…");
    const bool queued = state->queryTask.start<QueryResult>(
        [query] {
            QueryResult result;
            result.start = query.start_time;
            result.end = query.end_time;
            result.ok = search::query_tat_statistics(
                query, result.summary, result.rows, result.error);
            return result;
        },
        [hwnd](std::optional<QueryResult> result, std::exception_ptr error) {
            auto* current = reinterpret_cast<TatState*>(GetPropW(hwnd, PROP_STATE));
            if (!current) return;
            if (error || !result) {
                current->querying = false;
                setQueryControls(current, true);
                search::hide_page_activity(current->feedback);
                EnableWindow(current->exportExcel,
                    current->hasLoaded && !current->rows.empty());
                search::show_page_alert(current->feedback, L"查询发生后台任务异常。");
                return;
            }
            finishQuery(current, std::move(*result));
        });
    if (!queued) {
        state->querying = false;
        setQueryControls(state, true);
        search::hide_page_activity(state->feedback);
        EnableWindow(state->exportExcel, state->hasLoaded && !state->rows.empty());
        search::show_page_alert(state->feedback, L"无法启动 TAT 后台查询。");
    }
}

void resetFilters(TatState* state) {
    if (!state) return;
    setToday(state->start, state->end);
    SendMessageW(state->room, CB_SETCURSEL, 0, 0);
    SendMessageW(state->patientType, CB_SETCURSEL, 0, 0);
    SendMessageW(state->tatStatus, CB_SETCURSEL, 0, 0);
    Button_SetCheck(state->emergency, BST_UNCHECKED);
    SendMessageW(state->department, CB_SETCURSEL, 0, 0);
    SetWindowTextW(state->order, L"");
    SendMessageW(state->signDepartment, CB_SETCURSEL, 0, 0);
    SendMessageW(state->orderGroup, CB_SETCURSEL, 0, 0);
    state->allRows.clear(); state->hasLoaded = false; state->loadedAt.clear();
    applyStatusFilter(state);
    search::set_page_status(state->feedback, L"已重置，请重新查询。");
}

void copyCell(HWND owner, TatState* state) {
    POINT point{};
    GetCursorPos(&point);
    POINT client = point;
    ScreenToClient(state->details, &client);
    LVHITTESTINFO hit{};
    hit.pt = client;
    const int row = ListView_SubItemHitTest(state->details, &hit);
    if (row < 0 || hit.iSubItem < 0 || row >= static_cast<int>(state->rows.size())) return;
    const auto value = search::utf8_to_wide(cellValue(state->rows[static_cast<size_t>(row)], hit.iSubItem));
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    const SIZE_T bytes = (value.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* target = GlobalLock(memory);
        if (target) {
            memcpy(target, value.c_str(), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
    search::set_page_status(state->feedback, L"已复制当前单元格内容。");
}

void exportXlsx(HWND hwnd, TatState* state) {
    if (!state || state->exporting || state->rows.empty()) {
        MessageBoxW(hwnd, L"当前没有可导出的明细。", WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }
    wchar_t path[MAX_PATH] = L"检验周转时间统计.xlsx";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFilter = L"Excel 工作簿 (*.xlsx)\0*.xlsx\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"xlsx";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&dialog)) return;
    std::vector<std::string> headers;
    for (const auto& column : DETAIL_COLUMNS) headers.push_back(search::wide_to_utf8(column.title));
    const std::wstring outputPath(path);
    const auto rows = state->rows;
    state->exporting = true;
    setQueryControls(state, false);
    EnableWindow(state->exportExcel, FALSE);
    search::show_page_activity(state->feedback, L"正在导出检验周转时间明细，请稍候…");
    search::set_page_status(state->feedback, L"正在导出 Excel...");
    const bool queued = state->exportTask.start<ExportResult>(
        [outputPath, headers = std::move(headers), rows] {
            ExportResult result;
            result.path = outputPath;
            result.ok = search::write_xlsx_file(
                outputPath, "检验周转时间统计", headers, rows.size(),
                [&rows](size_t row, size_t column) {
                    return cellValue(rows[row], static_cast<int>(column));
                },
                result.error);
            return result;
        },
        [hwnd](std::optional<ExportResult> result, std::exception_ptr error) {
            auto* current = reinterpret_cast<TatState*>(GetPropW(hwnd, PROP_STATE));
            if (!current) return;
            if (error || !result) {
                current->exporting = false;
                setQueryControls(current, true);
                search::hide_page_activity(current->feedback);
                EnableWindow(current->exportExcel,
                    current->hasLoaded && !current->rows.empty());
                search::show_page_alert(current->feedback, L"导出发生后台任务异常。");
                return;
            }
            finishExport(current, std::move(*result));
        });
    if (!queued) {
        state->exporting = false;
        setQueryControls(state, true);
        search::hide_page_activity(state->feedback);
        EnableWindow(state->exportExcel, state->hasLoaded && !state->rows.empty());
        search::show_page_alert(state->feedback, L"无法启动 Excel 后台导出。");
    }
}

void resizeLayout(HWND hwnd, TatState* state) {
    if (!state) return;
    RECT rc{}; GetClientRect(hwnd, &rc);
    const int pad = S(hwnd, 10);
    const int width = static_cast<int>(rc.right), height = static_cast<int>(rc.bottom);
    int x = pad;
    const auto place = [&](HWND control, int w, int y, bool dropdown = false) {
        MoveWindow(control, x, S(hwnd, y), S(hwnd, w), S(hwnd, dropdown ? 220 : 25), TRUE);
        x += S(hwnd, w + 6);
    };
    place(GetDlgItem(hwnd, 7601), 72, 9); place(state->start, 110, 9);
    place(GetDlgItem(hwnd, 7602), 18, 9); place(state->end, 110, 9);
    place(GetDlgItem(hwnd, 7603), 58, 9); place(state->room, 125, 9, true);
    place(GetDlgItem(hwnd, 7604), 70, 9); place(state->patientType, 86, 9, true);
    place(state->emergency, 76, 9);
    x = pad;
    place(GetDlgItem(hwnd, 7605), 80, 42); place(state->department, 120, 42, true);
    place(GetDlgItem(hwnd, 7608), 80, 42); place(state->signDepartment, 120, 42, true);
    place(GetDlgItem(hwnd, 7606), 96, 42); place(state->order, 160, 42);
    place(GetDlgItem(hwnd, 7607), 50, 42); place(state->tatStatus, 130, 42, true);
    x = pad;
    place(GetDlgItem(hwnd, 7609), 70, 75); place(state->orderGroup, 150, 75, true);
    place(GetDlgItem(hwnd, 7610), 40, 75); place(state->mode, 155, 75, true);
    place(state->query, 64, 75); place(state->refresh, 64, 75);
    place(state->reset, 64, 75); place(state->exportExcel, 96, 75);
    MoveWindow(state->status, pad, S(hwnd, 110), (std::max)(S(hwnd, 300), width - pad * 2), S(hwnd, 40), TRUE);
    MoveWindow(state->summary, pad, S(hwnd, 154), width - pad * 2, S(hwnd, 72), TRUE);
    MoveWindow(state->details, pad, S(hwnd, 236), width - pad * 2,
               (std::max)(S(hwnd, 100), height - S(hwnd, 246)), TRUE);
    search::layout_page_feedback(state->feedback);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<TatState*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            state = reinterpret_cast<TatState*>(mcs->lParam);
            SetPropW(hwnd, PROP_STATE, state);
            state->bgBrush = CreateSolidBrush(RGB(0xF0, 0xF0, 0xF0));

            CreateWindowExW(0, L"STATIC", L"签收日期：", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0,
                            hwnd, win32_control_id(7601), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"STATIC", L"至", WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 0, 0,
                            hwnd, win32_control_id(7602), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"STATIC", L"专业组：", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0,
                            hwnd, win32_control_id(7603), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"STATIC", L"病人类型：", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0,
                            hwnd, win32_control_id(7604), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"STATIC", L"申请科室：", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0,
                            hwnd, win32_control_id(7605), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"STATIC", L"医嘱精确名称：", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0,
                            hwnd, win32_control_id(7606), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"STATIC", L"TAT状态：", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0,
                            hwnd, win32_control_id(7607), GetModuleHandleW(nullptr), nullptr);

            const wchar_t* extraLabels[] = {L"签收科室：", L"医嘱组：", L"口径："};
            const int labelIds[] = {7608, 7609, 7610};
            for (int i = 0; i < 3; ++i) CreateWindowExW(0, L"STATIC", extraLabels[i],
                WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0, hwnd, win32_control_id(labelIds[i]),
                GetModuleHandleW(nullptr), nullptr);
            state->signDepartment = combo(hwnd, IDC_SIGN_DEPT); addCombo(state->signDepartment,L"全部");
            SendMessageW(state->signDepartment,CB_SETCURSEL,0,0);
            state->orderGroup = combo(hwnd, IDC_ORDER_GROUP); addCombo(state->orderGroup,L"全部");
            SendMessageW(state->orderGroup,CB_SETCURSEL,0,0);
            state->mode = combo(hwnd, IDC_MODE);
            addCombo(state->mode, L"标准兼容"); addCombo(state->mode, L"改良统计");
            SendMessageW(state->mode, CB_SETCURSEL, 0, 0);
            state->start = dateTimePicker(hwnd, IDC_START);
            state->end = dateTimePicker(hwnd, IDC_END);
            setToday(state->start, state->end);
            state->room = combo(hwnd, IDC_ROOM); addCombo(state->room, L"全部"); SendMessageW(state->room, CB_SETCURSEL, 0, 0);
            state->patientType = combo(hwnd, IDC_PATIENT_TYPE);
            addCombo(state->patientType, L"全部"); addCombo(state->patientType, L"住院"); addCombo(state->patientType, L"门诊");
            SendMessageW(state->patientType, CB_SETCURSEL, 0, 0);
            state->emergency = CreateWindowExW(0, L"BUTTON", L"仅急诊", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_EMERGENCY), GetModuleHandleW(nullptr), nullptr);
            state->department = combo(hwnd, IDC_DEPARTMENT); addCombo(state->department,L"全部");
            SendMessageW(state->department,CB_SETCURSEL,0,0);
            state->order = edit(hwnd, IDC_ORDER);
            state->tatStatus = combo(hwnd, IDC_TAT_STATUS);
            addCombo(state->tatStatus, L"全部"); addCombo(state->tatStatus, L"正常");
            addCombo(state->tatStatus, L"超时"); addCombo(state->tatStatus, L"缺少时间");
            addCombo(state->tatStatus, L"缺少统计标记"); addCombo(state->tatStatus, L"时间异常");
            SendMessageW(state->tatStatus, CB_SETCURSEL, 0, 0);
            state->query = search::create_button(hwnd, IDC_QUERY, L"查询", 0, 0, 0, 0);
            state->reset = search::create_button(hwnd, IDC_RESET, L"重置", 0, 0, 0, 0);
            state->exportExcel = search::create_button(hwnd, IDC_EXPORT, L"导出Excel", 0, 0, 0, 0);
            state->refresh = search::create_button(hwnd, IDC_REFRESH, L"刷新", 0, 0, 0, 0);
            EnableWindow(state->exportExcel, FALSE);
            state->status = CreateWindowExW(0, L"STATIC", L"请选择签收时间后查询。", WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_STATUS), GetModuleHandleW(nullptr), nullptr);

            state->summary = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE |
                LVS_REPORT | LVS_SINGLESEL, 0, 0, 0, 0, hwnd, win32_control_id(IDC_SUMMARY), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(state->summary, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            for (int i = 0; i < static_cast<int>(std::size(SUMMARY_COLUMNS)); ++i)
                search::add_list_column(state->summary, i, SUMMARY_COLUMNS[i].title, SUMMARY_COLUMNS[i].width);

            state->details = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE |
                LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL, 0, 0, 0, 0, hwnd, win32_control_id(IDC_DETAILS), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(state->details, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
            for (int i = 0; i < DETAIL_COLUMN_COUNT; ++i)
                search::add_list_column(state->details, i, DETAIL_COLUMNS[i].title, DETAIL_COLUMNS[i].width);

            search::initialize_page_feedback(state->feedback, hwnd, state->status, state->details, state->ctx.uiFont);
            search::add_page_tooltip(state->feedback, state->query, L"按当前条件查询已签收条码的检验周转时间。");
            search::add_page_tooltip(state->feedback, state->mode, L"兼容口径保留原计数；改良口径按条码去重并单列缺失、异常。");
            search::add_page_tooltip(state->feedback, state->refresh, L"重新读取数据库，不只重算缓存。");
            search::add_page_tooltip(state->feedback, state->order, L"按完整医嘱名称匹配，多项以顿号分隔；留空为全部。");
            search::add_page_tooltip(state->feedback, state->details, L"单击列标题排序；右键复制单元格。来源列说明日志优先及回退值。");
            search::apply_font_to_children(hwnd, state->ctx.uiFont);
            populateSummary(state);
            resizeLayout(hwnd, state);

            const auto connection = search::build_connection_string_w(state->ctx.dbSettings);
            if (!connection.empty()) {
                state->roomTask.start<RoomResult>(
                    [connection] {
                        querydiag::SilenceScope silence;
                        RoomResult result;
                        result.ok = search::query_tat_picker_options(
                            search::wide_to_utf8(connection), result.options, result.error);
                        return result;
                    },
                    [hwnd](std::optional<RoomResult> result, std::exception_ptr) {
                        auto* current = reinterpret_cast<TatState*>(GetPropW(hwnd, PROP_STATE));
                        if (!current || !result) return;
                        finishRoomLoad(current, std::move(*result));
                    });
            }
            return 0;
        }
        case WM_SIZE:
            resizeLayout(hwnd, state);
            return 0;
        case WM_COMMAND:
            if (state && search::handle_page_feedback_command(state->feedback, lp, WINDOW_TITLE)) return 0;
            if (!state) break;
            if (LOWORD(wp) == IDC_QUERY || LOWORD(wp) == IDC_REFRESH) { runQuery(hwnd, state); return 0; }
            if (LOWORD(wp) == IDC_RESET) { resetFilters(state); return 0; }
            if (LOWORD(wp) == IDC_EXPORT) { exportXlsx(hwnd, state); return 0; }
            if ((LOWORD(wp) == IDC_TAT_STATUS || LOWORD(wp) == IDC_MODE) && HIWORD(wp) == CBN_SELCHANGE) {
                applyStatusFilter(state);
                return 0;
            }
            break;
        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lp);
            if (!state || header->idFrom != IDC_DETAILS) break;
            if (header->code == LVN_GETDISPINFOW) {
                auto* info = reinterpret_cast<NMLVDISPINFOW*>(lp);
                auto& item = info->item;
                if ((item.mask & LVIF_TEXT) && item.pszText && item.cchTextMax > 0) {
                    std::wstring value;
                    if (item.iItem >= 0 && static_cast<size_t>(item.iItem) < state->rows.size() &&
                        item.iSubItem >= 0 && item.iSubItem < DETAIL_COLUMN_COUNT)
                        value = search::utf8_to_wide(cellValue(state->rows[item.iItem], item.iSubItem));
                    lstrcpynW(item.pszText, value.c_str(), item.cchTextMax);
                }
                return 0;
            }
            if (header->code == NM_CUSTOMDRAW) {
                auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const size_t index = static_cast<size_t>(draw->nmcd.dwItemSpec);
                    if (index < state->rows.size()) {
                        draw->clrText = RGB(0, 0, 0);
                        draw->clrTextBk = state->rows[index].time_abnormal ? COLOR_ABNORMAL :
                            state->rows[index].tat_status == "超时" ? COLOR_OVERTIME : COLOR_NORMAL;
                    }
                    return CDRF_NEWFONT;
                }
            }
            if (header->code == LVN_COLUMNCLICK) {
                if (state->querying || state->exporting) return 0;
                const auto* info = reinterpret_cast<NMLISTVIEW*>(lp);
                sortRows(state, info->iSubItem, true);
                populateDetails(state);
                return 0;
            }
            if (header->code == NM_RCLICK) {
                copyCell(hwnd, state);
                return 0;
            }
            break;
        }
        case app::WM_APP_SETTINGS_CHANGED:
        case app::WM_APP_FONT_CHANGED:
            if (state) {
                if (msg == app::WM_APP_FONT_CHANGED && lp) state->ctx.uiFont = reinterpret_cast<HFONT>(lp);
                search::apply_font_to_children(hwnd, state->ctx.uiFont);
                resizeLayout(hwnd, state);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        case WM_CTLCOLORSTATIC:
            if (state) {
                LRESULT result = 0;
                if (search::page_feedback_static_color(state->feedback, reinterpret_cast<HDC>(wp),
                        reinterpret_cast<HWND>(lp), result)) return result;
            }
            SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
            return reinterpret_cast<LRESULT>(state ? state->bgBrush : nullptr);
        case WM_ERASEBKGND: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, state ? state->bgBrush :
                     reinterpret_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH)));
            return 1;
        }
        case WM_DESTROY:
            if (state) {
                RemovePropW(hwnd, PROP_STATE);
                state->queryTask.cancel();
                state->roomTask.cancel();
                state->exportTask.cancel();
                search::destroy_page_feedback(state->feedback);
                if (state->bgBrush) DeleteObject(state->bgBrush);
                delete state;
            }
            return 0;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_tat_statistics_module(const ModuleContext& ctx) {
    if (HWND existing = activate_existing_mdi_child_by_title(ctx.mdiClient, WINDOW_TITLE)) return existing;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = ctx.instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(ctx.instance, MAKEINTRESOURCEW(IDI_APP));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = WND_CLASS;
    RegisterClassExW(&wc);
    auto* state = new TatState();
    state->ctx = ctx;
    MDICREATESTRUCTW mcs{};
    mcs.szTitle = WINDOW_TITLE;
    mcs.szClass = WND_CLASS;
    mcs.hOwner = ctx.instance;
    mcs.x = mcs.y = mcs.cx = mcs.cy = CW_USEDEFAULT;
    mcs.lParam = reinterpret_cast<LPARAM>(state);
    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&mcs)));
    if (!child) {
        delete state;
        MessageBoxW(ctx.mdiClient, L"检验周转时间统计窗口创建失败。", WINDOW_TITLE, MB_ICONERROR);
        return nullptr;
    }
    SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    return child;
}

#endif
