#include "hiv_statistics_module.h"

#ifdef _WIN32

#include "main_app.h"
#include "resource.h"
#include "search_core.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"

#include <commctrl.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"HivStatisticsModuleChild";
constexpr const wchar_t* WINDOW_TITLE = L"HIV 抗体检测统计";
constexpr const wchar_t* PROP_STATE = L"HivStatisticsSt";
constexpr UINT WM_HIV_STATS_LOADED = WM_APP + 0x551;
const COLORREF COLOR_POSITIVE_ROW = RGB(0xFA, 0xC0, 0xCB);

enum DetailColumn {
    DetailMachine = 0,
    DetailLabDepartment,
    DetailItemCode,
    DetailItemName,
    DetailReportNo,
    DetailBarcode,
    DetailSampleNo,
    DetailPatientNo,
    DetailName,
    DetailCompletedBloodApplyForms,
    DetailPatientType,
    DetailDept,
    DetailResult,
    DetailLowerBound,
    DetailUpperBound,
    DetailPositive,
    DetailReportTime,
};

struct ListColumn {
    const wchar_t* title;
    int width;
};

constexpr ListColumn SUMMARY_COLUMNS[] = {
    {L"样本来源分类", 360},
    {L"初筛检测数", 190},
    {L"初筛阳性数", 190},
    {L"复检数", 160},
    {L"复检阳性数", 190},
    {L"报告疫情检测数", 245},
    {L"报告疫情阳性数", 245},
};

constexpr ListColumn DETAIL_COLUMNS[] = {
    {L"仪器", 240},
    {L"检验科", 92},
    {L"项目代码", 112},
    {L"项目名称", 300},
    {L"报告号", 130},
    {L"条码号", 150},
    {L"样本号", 118},
    {L"病人号", 130},
    {L"姓名", 120},
    {L"已完结输血单申请号", 190},
    {L"病人类型", 120},
    {L"科室", 220},
    {L"结果", 170},
    {L"下限", 102},
    {L"上限", 102},
    {L"阳性", 82},
    {L"报告时间", 190},
};

constexpr const wchar_t* SUMMARY_ROWS[] = {
    L"术前检测",
    L"受血（制品）前检测",
    L"性病门诊",
    L"其他就诊检测",
    L"婚前检查（含涉外婚检）",
    L"孕产期检查",
    L"检测咨询",
    L"阳性者配偶或性伴检测",
    L"女性阳性者子女检测",
    L"职业暴露检测",
    L"娱乐场所人员检测",
    L"有偿供血（浆）人员检测",
    L"无偿供血人员检测",
    L"出入境人员体检",
    L"新兵体检",
    L"强制／劳教戒毒人员检测",
    L"妇教所／女劳收教人员检测",
    L"其他羁押人员体检",
    L"专题调育或其他",
    L"合计",
};

constexpr int SUMMARY_ROW_PREOPERATIVE = 0;
constexpr int SUMMARY_ROW_TRANSFUSION = 1;
constexpr int SUMMARY_ROW_STI_CLINIC = 2;
constexpr int SUMMARY_ROW_OTHER_VISIT = 3;
constexpr int SUMMARY_ROW_PRENATAL = 5;

enum ControlId {
    IDC_YEAR = 6401,
    IDC_MONTH = 6402,
    IDC_QUERY = 6403,
    IDC_SUMMARY = 6404,
    IDC_DETAILS = 6405,
    IDC_STATUS = 6406,
    IDC_SOURCE = 6407,
};

struct HivStatisticsState {
    ModuleContext ctx;
    HWND year = nullptr;
    HWND month = nullptr;
    HWND source = nullptr;
    HWND query = nullptr;
    HWND summary = nullptr;
    HWND details = nullptr;
    HWND status = nullptr;
    HBRUSH bgBrush = nullptr;
    bool querying = false;
    int detailSortColumn = -1;
    bool detailSortAscending = true;
    search::HivStatSummary statSummary;
    std::vector<search::HivStatDetailRow> rows;
};

struct HivQueryResult {
    bool ok = false;
    std::string error;
    search::HivStatSummary summary;
    std::vector<search::HivStatDetailRow> rows;
};

int S(HWND hwnd, int value) {
    return static_cast<int>(value * search::dpi_scale_factor(hwnd));
}


HWND label(HWND parent, const wchar_t* text, int x, int y, int w, int h, DWORD align = SS_RIGHT) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | align,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND edit(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                           x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND combo(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                           x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

std::wstring textOf(HWND hwnd) {
    wchar_t buf[128]{};
    GetWindowTextW(hwnd, buf, 128);
    return buf;
}

int intText(HWND hwnd, int fallback) {
    const auto text = textOf(hwnd);
    if (text.empty()) return fallback;
    return _wtoi(text.c_str());
}

int selectedMonth(HWND combo) {
    const int idx = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    return idx >= 0 ? idx + 1 : 1;
}

std::wstring selectedComboText(HWND combo) {
    const int idx = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (idx < 0) return {};
    wchar_t buf[64]{};
    SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(idx), reinterpret_cast<LPARAM>(buf));
    return buf;
}

void setStatus(HivStatisticsState* st, const std::wstring& text) {
    if (st && st->status) SetWindowTextW(st->status, text.c_str());
}

template <size_t N>
void addColumns(HWND list, const ListColumn (&columns)[N]) {
    for (int i = 0; i < static_cast<int>(N); ++i) {
        const auto& col = columns[static_cast<size_t>(i)];
        search::add_list_column(list, i, col.title, col.width);
    }
}

void setCell(HWND list, int row, int col, const std::wstring& text) {
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(text.c_str()));
}

void setCellUtf8(HWND list, int row, int col, const std::string& text) {
    setCell(list, row, col, search::utf8_to_wide(text));
}

void populateDetails(HivStatisticsState* st);

std::string detailSortValue(const search::HivStatDetailRow& row, int col) {
    switch (col) {
        case DetailMachine: return row.machine_name.empty() ? row.mach_code : row.machine_name;
        case DetailLabDepartment: return row.lab_department;
        case DetailItemCode: return row.item_code;
        case DetailItemName: return row.item_name;
        case DetailReportNo: return row.rep_no;
        case DetailBarcode: return row.txm_no;
        case DetailSampleNo: return row.oper_no;
        case DetailPatientNo: return row.patient_no;
        case DetailName: return row.name;
        case DetailCompletedBloodApplyForms: return row.completed_blood_apply_forms;
        case DetailPatientType: return row.patient_type;
        case DetailDept: return row.dept_name;
        case DetailResult: return row.result;
        case DetailLowerBound: return row.lower_bound;
        case DetailUpperBound: return row.upper_bound;
        case DetailPositive: return row.positive;
        case DetailReportTime: return row.report_time;
        default: return {};
    }
}

bool parseSortNumber(const std::string& value, double& out) {
    const std::string text = search::trim(value);
    if (text.empty()) return false;
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return end && *end == '\0';
}

int compareDetailSortValue(const search::HivStatDetailRow& a, const search::HivStatDetailRow& b, int col) {
    const std::string left = search::trim(detailSortValue(a, col));
    const std::string right = search::trim(detailSortValue(b, col));
    double leftNum = 0.0;
    double rightNum = 0.0;
    const bool leftNumeric = parseSortNumber(left, leftNum);
    const bool rightNumeric = parseSortNumber(right, rightNum);
    if (leftNumeric && rightNumeric) {
        if (leftNum < rightNum) return -1;
        if (leftNum > rightNum) return 1;
        return 0;
    }
    if (left < right) return -1;
    if (left > right) return 1;
    return 0;
}

void sortDetailRowsForDisplay(HivStatisticsState* st) {
    if (!st || st->detailSortColumn < 0) return;
    const int col = st->detailSortColumn;
    const bool ascending = st->detailSortAscending;
    std::stable_sort(st->rows.begin(), st->rows.end(),
                     [col, ascending](const search::HivStatDetailRow& a, const search::HivStatDetailRow& b) {
                         const int cmp = compareDetailSortValue(a, b, col);
                         return ascending ? cmp < 0 : cmp > 0;
                     });
}

void sortDetailsByColumn(HivStatisticsState* st, int col) {
    if (!st || !st->details || st->rows.empty() || col < 0 || col >= static_cast<int>(std::size(DETAIL_COLUMNS))) {
        return;
    }
    if (st->detailSortColumn == col) {
        st->detailSortAscending = !st->detailSortAscending;
    } else {
        st->detailSortColumn = col;
        st->detailSortAscending = true;
    }

    std::string selectedKey;
    const int selected = ListView_GetNextItem(st->details, -1, LVNI_SELECTED);
    if (selected >= 0 && selected < static_cast<int>(st->rows.size())) {
        const auto& row = st->rows[static_cast<size_t>(selected)];
        selectedKey = row.rep_no + "|" + row.mach_code + "|" + row.item_code;
    }

    sortDetailRowsForDisplay(st);
    populateDetails(st);

    if (!selectedKey.empty()) {
        for (int i = 0; i < static_cast<int>(st->rows.size()); ++i) {
            const auto& row = st->rows[static_cast<size_t>(i)];
            const std::string key = row.rep_no + "|" + row.mach_code + "|" + row.item_code;
            if (key == selectedKey) {
                ListView_SetItemState(st->details, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(st->details, i, FALSE);
                break;
            }
        }
    }
}

void initSummaryList(HWND list) {
    ListView_DeleteAllItems(list);
    while (ListView_DeleteColumn(list, 0)) {}
    addColumns(list, SUMMARY_COLUMNS);
}

void initDetailList(HWND list) {
    ListView_DeleteAllItems(list);
    while (ListView_DeleteColumn(list, 0)) {}
    addColumns(list, DETAIL_COLUMNS);
}

void setSummaryCounts(HWND list, int row, int screeningCount, int positiveCount) {
    wchar_t buf[32]{};
    swprintf(buf, 32, L"%d", screeningCount);
    setCell(list, row, 1, buf);
    swprintf(buf, 32, L"%d", positiveCount);
    setCell(list, row, 2, buf);
}

void populateSummary(HivStatisticsState* st) {
    ListView_DeleteAllItems(st->summary);
    for (int i = 0; i < static_cast<int>(std::size(SUMMARY_ROWS)); ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<wchar_t*>(SUMMARY_ROWS[i]);
        ListView_InsertItem(st->summary, &item);
        if (i == SUMMARY_ROW_PREOPERATIVE) {
            setSummaryCounts(st->summary, i,
                             st->statSummary.preoperative_screening_count,
                             st->statSummary.preoperative_positive_count);
        } else if (i == SUMMARY_ROW_TRANSFUSION) {
            setSummaryCounts(st->summary, i,
                             st->statSummary.transfusion_screening_count,
                             st->statSummary.transfusion_positive_count);
        } else if (i == SUMMARY_ROW_STI_CLINIC) {
            setSummaryCounts(st->summary, i,
                             st->statSummary.sti_clinic_screening_count,
                             st->statSummary.sti_clinic_positive_count);
        } else if (i == SUMMARY_ROW_OTHER_VISIT) {
            setSummaryCounts(st->summary, i,
                             st->statSummary.other_visit_screening_count,
                             st->statSummary.other_visit_positive_count);
        } else if (i == SUMMARY_ROW_PRENATAL) {
            setSummaryCounts(st->summary, i,
                             st->statSummary.prenatal_screening_count,
                             st->statSummary.prenatal_positive_count);
        } else if (i == static_cast<int>(std::size(SUMMARY_ROWS)) - 1) {
            setSummaryCounts(st->summary, i,
                             st->statSummary.screening_count,
                             st->statSummary.positive_count);
        }
    }
}

void populateDetails(HivStatisticsState* st) {
    ListView_DeleteAllItems(st->details);
    for (int i = 0; i < static_cast<int>(st->rows.size()); ++i) {
        const auto& row = st->rows[static_cast<size_t>(i)];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        auto first = search::utf8_to_wide(row.machine_name.empty() ? row.mach_code : row.machine_name);
        item.pszText = const_cast<wchar_t*>(first.c_str());
        ListView_InsertItem(st->details, &item);
        setCellUtf8(st->details, i, 1, row.lab_department);
        setCellUtf8(st->details, i, 2, row.item_code);
        setCellUtf8(st->details, i, 3, row.item_name);
        setCellUtf8(st->details, i, 4, row.rep_no);
        setCellUtf8(st->details, i, 5, row.txm_no);
        setCellUtf8(st->details, i, 6, row.oper_no);
        setCellUtf8(st->details, i, 7, row.patient_no);
        setCellUtf8(st->details, i, 8, row.name);
        setCellUtf8(st->details, i, 9, row.completed_blood_apply_forms);
        setCellUtf8(st->details, i, 10, row.patient_type);
        setCellUtf8(st->details, i, 11, row.dept_name);
        setCellUtf8(st->details, i, 12, row.result);
        setCellUtf8(st->details, i, 13, row.lower_bound);
        setCellUtf8(st->details, i, 14, row.upper_bound);
        setCellUtf8(st->details, i, 15, row.positive);
        setCellUtf8(st->details, i, 16, row.report_time);
    }
}

void resizeLayout(HWND hwnd, HivStatisticsState* st) {
    if (!st) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const int pad = S(hwnd, 10);
    const int topH = S(hwnd, 42);
    const int summaryH = S(hwnd, 232);
    const int statusH = S(hwnd, 24);

    MoveWindow(st->summary, pad, topH + pad, w - pad * 2, summaryH, TRUE);
    MoveWindow(st->details, pad, topH + summaryH + pad * 2, w - pad * 2,
               (std::max)(S(hwnd, 80), h - topH - summaryH - pad * 3 - statusH), TRUE);
    MoveWindow(st->status, pad, h - statusH - S(hwnd, 4), w - pad * 2, statusH, TRUE);
}

void runQuery(HWND hwnd, HivStatisticsState* st) {
    if (!st || st->querying) return;
    const int year = intText(st->year, 0);
    const int month = selectedMonth(st->month);
    if (year < 1900 || year > 9999) {
        MessageBoxW(hwnd, L"请输入有效年份。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }

    const auto connection = search::build_connection_string_w(st->ctx.dbSettings);
    if (connection.empty()) {
        MessageBoxW(hwnd, L"请先在系统设置中配置数据库连接。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }

    st->querying = true;
    EnableWindow(st->query, FALSE);
    setStatus(st, L"正在查询 HIV 抗体检测统计...");

    search::HivStatQuery query;
    query.connection_string = search::wide_to_utf8(connection);
    query.year = year;
    query.month = month;
    query.lab_department = search::wide_to_utf8(selectedComboText(st->source));

    std::thread([hwnd, query]() {
        auto* result = new HivQueryResult();
        result->ok = search::query_hiv_statistics(query, result->summary, result->rows, result->error);
        if (!PostMessageW(hwnd, WM_HIV_STATS_LOADED, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<HivStatisticsState*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            st = reinterpret_cast<HivStatisticsState*>(mcs->lParam);
            SetPropW(hwnd, PROP_STATE, st);

            st->bgBrush = CreateSolidBrush(RGB(0xF0, 0xF0, 0xF0));
            SYSTEMTIME now{};
            GetLocalTime(&now);

            const int pad = S(hwnd, 10);
            label(hwnd, L"年份：", pad, S(hwnd, 12), S(hwnd, 52), S(hwnd, 24));
            wchar_t yearText[16]{};
            swprintf(yearText, 16, L"%u", now.wYear);
            st->year = edit(hwnd, IDC_YEAR, yearText, S(hwnd, 64), S(hwnd, 10), S(hwnd, 72), S(hwnd, 24));
            label(hwnd, L"月份：", S(hwnd, 148), S(hwnd, 12), S(hwnd, 52), S(hwnd, 24));
            st->month = combo(hwnd, IDC_MONTH, S(hwnd, 202), S(hwnd, 10), S(hwnd, 78), S(hwnd, 160));
            for (int m = 1; m <= 12; ++m) {
                wchar_t text[16]{};
                swprintf(text, 16, L"%d月", m);
                SendMessageW(st->month, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
            }
            SendMessageW(st->month, CB_SETCURSEL, now.wMonth - 1, 0);
            label(hwnd, L"检验科：", S(hwnd, 292), S(hwnd, 12), S(hwnd, 70), S(hwnd, 24));
            st->source = combo(hwnd, IDC_SOURCE, S(hwnd, 364), S(hwnd, 10), S(hwnd, 82), S(hwnd, 160));
            SendMessageW(st->source, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"全部"));
            SendMessageW(st->source, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"新院"));
            SendMessageW(st->source, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"老院"));
            SendMessageW(st->source, CB_SETCURSEL, 0, 0);
            st->query = search::create_button(hwnd, IDC_QUERY, L"查询", S(hwnd, 456), S(hwnd, 9), S(hwnd, 86), S(hwnd, 26));
            label(hwnd, L"第一版仅统计合计行的初筛检测数和初筛阳性数；来源分类、复检和自动填表暂未启用。",
                  S(hwnd, 556), S(hwnd, 12), S(hwnd, 650), S(hwnd, 24), SS_LEFT);

            st->summary = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                          WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                          0, 0, 0, 0, hwnd, win32_control_id(IDC_SUMMARY), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->summary, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            initSummaryList(st->summary);

            st->details = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                          WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                          0, 0, 0, 0, hwnd, win32_control_id(IDC_DETAILS), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->details, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            initDetailList(st->details);

            st->status = label(hwnd, L"请选择年份和月份后查询。", 0, 0, 0, 0, SS_LEFT);
            search::apply_font_to_children(hwnd, st->ctx.uiFont);
            populateSummary(st);
            resizeLayout(hwnd, st);
            return 0;
        }
        case WM_SIZE:
            resizeLayout(hwnd, st);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_QUERY) {
                runQuery(hwnd, st);
                return 0;
            }
            break;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (nm->idFrom == IDC_DETAILS && nm->code == NM_CUSTOMDRAW) {
                auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const int idx = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (st && idx >= 0 && idx < static_cast<int>(st->rows.size()) &&
                        search::trim(st->rows[static_cast<size_t>(idx)].positive) == "是") {
                        cd->clrTextBk = COLOR_POSITIVE_ROW;
                    }
                    return CDRF_NEWFONT;
                }
            }
            if (st && nm->idFrom == IDC_DETAILS && nm->code == LVN_COLUMNCLICK) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                sortDetailsByColumn(st, lv->iSubItem);
                return 0;
            }
            break;
        }
        case WM_HIV_STATS_LOADED: {
            std::unique_ptr<HivQueryResult> result(reinterpret_cast<HivQueryResult*>(lp));
            if (!st) return 0;
            st->querying = false;
            EnableWindow(st->query, TRUE);
            if (!result->ok) {
                setStatus(st, L"查询失败：" + search::utf8_to_wide(result->error));
                MessageBoxW(hwnd, search::utf8_to_wide(result->error).c_str(), WINDOW_TITLE, MB_ICONERROR);
                return 0;
            }
            st->statSummary = result->summary;
            st->rows = std::move(result->rows);
            sortDetailRowsForDisplay(st);
            populateSummary(st);
            populateDetails(st);
            wchar_t status[160]{};
            swprintf(status, 160, L"查询完成：初筛检测数 %d，初筛阳性数 %d，明细 %d 行。",
                     st->statSummary.screening_count, st->statSummary.positive_count,
                     static_cast<int>(st->rows.size()));
            setStatus(st, status);
            return 0;
        }
        case app::WM_APP_SETTINGS_CHANGED:
            if (st) {
                search::apply_font_to_children(hwnd, st->ctx.uiFont);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        case WM_CTLCOLORSTATIC:
            SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
            return reinterpret_cast<LRESULT>(st ? st->bgBrush : nullptr);
        case WM_ERASEBKGND: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, st ? st->bgBrush : reinterpret_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH)));
            return 1;
        }
        case WM_DESTROY:
            if (st) {
                if (st->bgBrush) DeleteObject(st->bgBrush);
                RemovePropW(hwnd, PROP_STATE);
                delete st;
            }
            return 0;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

void registerClass(HINSTANCE inst) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = WND_CLASS;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP),
                                               IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);
    registered = true;
}

}  // namespace

HWND create_hiv_statistics_module(const ModuleContext& ctx) {
    if (HWND existing = activate_existing_mdi_child_by_title(ctx.mdiClient, WINDOW_TITLE)) {
        return existing;
    }

    registerClass(ctx.instance);
    auto* st = new HivStatisticsState();
    st->ctx = ctx;
    MDICREATESTRUCTW mcs{};
    mcs.szClass = WND_CLASS;
    mcs.szTitle = WINDOW_TITLE;
    mcs.hOwner = ctx.instance;
    mcs.x = CW_USEDEFAULT;
    mcs.y = CW_USEDEFAULT;
    mcs.cx = CW_USEDEFAULT;
    mcs.cy = CW_USEDEFAULT;
    mcs.lParam = reinterpret_cast<LPARAM>(st);
    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&mcs)));
    if (!child) {
        delete st;
        return nullptr;
    }
    SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    return child;
}

#endif
