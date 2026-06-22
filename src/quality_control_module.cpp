#include "quality_control_module.h"

#ifdef _WIN32

#include "main_app.h"
#include "machine_picker_popup.h"
#include "quality_control_chart_renderer.h"
#include "quality_control_store.h"
#include "regular_report_module.h"
#include "resource.h"
#include "search_core.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"

#include <commctrl.h>
#include <commdlg.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"QualityControlModuleChild";
constexpr const wchar_t* CHART_WND_CLASS = L"QualityControlChartWindow";
constexpr const wchar_t* WINDOW_TITLE = L"质控分析";
constexpr const wchar_t* PROP_STATE = L"QualityControlSt";
constexpr const wchar_t* PROP_CHART = L"QualityControlChartData";
constexpr UINT WM_QC_QUERY_DONE = WM_APP + 0x681;

enum ControlId {
    IDC_START_DATE = 6801,
    IDC_END_DATE = 6802,
    IDC_MACH_CODE = 6803,
    IDC_QUERY = 6804,
    IDC_MACH_PICK = 6805,
    IDC_GROUPS = 6806,
    IDC_CARDS = 6807,
    IDC_DETAILS = 6808,
    IDC_STATUS = 6809,
    IDC_ITEM_CODE = 6810,
    IDC_LEVEL = 6811,
    IDC_EXPORT_CSV = 6812,
    IDC_LJ_CHART = 6813,
    IDC_CHART_LIST = 6814,
    IDC_STATUS_FILTER = 6815,
};

struct GroupRow {
    std::string key;
    std::string mach_code;
    std::string mach_name;
    std::string sample_no;
    std::string item_code;
    std::string item_name;
    std::string level;
    std::string qc_name;
    int count = 0;
    std::string latest_time;
    std::string latest_result;
    int numeric_count = 0;
    double sum = 0.0;
    double sum_square = 0.0;
    double qc_mean = 0.0;
    double qc_sd = 0.0;
    bool has_configured_stats = false;
    int evaluated_count = 0;
    int in_control_count = 0;
    int warning_count = 0;
    int out_of_control_count = 0;
};

struct State {
    ModuleContext ctx;
    HWND startLabel = nullptr;
    HWND startDate = nullptr;
    HWND endLabel = nullptr;
    HWND endDate = nullptr;
    HWND machLabel = nullptr;
    HWND machCode = nullptr;
    HWND machPick = nullptr;
    HWND itemLabel = nullptr;
    HWND itemCode = nullptr;
    HWND levelLabel = nullptr;
    HWND level = nullptr;
    HWND statusFilterLabel = nullptr;
    HWND statusFilter = nullptr;
    HWND queryButton = nullptr;
    HWND chartButton = nullptr;
    HWND exportButton = nullptr;
    HWND groups = nullptr;
    HWND cards = nullptr;
    HWND details = nullptr;
    HWND status = nullptr;
    HBRUSH bgBrush = nullptr;
    bool busy = false;
    std::string selectedMachineCode;
    std::vector<qc::Result> rows;
    std::vector<GroupRow> groupsRows;
    int selectedGroup = -1;
};

struct QueryDone {
    bool ok = false;
    std::string error;
    std::vector<qc::Result> rows;
    int config_count = 0;
    int elapsed_ms = 0;
};

struct ChartWindowData {
    std::vector<qc::ChartSeries> series;
    HWND list = nullptr;
    HWND tooltip = nullptr;
    std::vector<qc::ChartHitPoint> hitPoints;
    int selectedIndex = -1;
    int hoverIndex = -1;
    int scrollY = 0;
    bool suppressListNotify = false;
    std::wstring tooltipText;
};

int S(HWND hwnd, int value) {
    return static_cast<int>(value * search::dpi_scale_factor(hwnd));
}

HWND label(HWND parent, const wchar_t* text) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
                           0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND edit(HWND parent, int id) {
    return search::create_edit(parent, id, 0, 0, 10, 24);
}

HWND button(HWND parent, int id, const wchar_t* text) {
    return search::create_button(parent, id, text, 0, 0, 80, 28);
}

HWND combo(HWND parent, int id) {
    return search::create_combo(parent, id, 0, 0, 10, 160, false);
}

HWND datePicker(HWND parent, int id) {
    HWND hwnd = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATECENTURYFORMAT,
                                0, 0, 120, 24, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
    DateTime_SetFormat(hwnd, L"yyyy-MM-dd");
    return hwnd;
}

std::string dateText(HWND hwnd) {
    SYSTEMTIME st{};
    if (DateTime_GetSystemtime(hwnd, &st) != GDT_VALID) return "";
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
    return buffer;
}

std::wstring editText(HWND hwnd) {
    wchar_t buffer[128]{};
    GetWindowTextW(hwnd, buffer, 128);
    return buffer;
}

std::string comboText(HWND hwnd) {
    wchar_t buffer[128]{};
    GetWindowTextW(hwnd, buffer, 128);
    return search::wide_to_utf8(buffer);
}

void setComboItems(HWND hwnd, const std::vector<const wchar_t*>& items) {
    SendMessageW(hwnd, CB_RESETCONTENT, 0, 0);
    for (const auto* item : items) {
        SendMessageW(hwnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    }
    SendMessageW(hwnd, CB_SETCURSEL, 0, 0);
}

int comboSelection(HWND hwnd) {
    return static_cast<int>(SendMessageW(hwnd, CB_GETCURSEL, 0, 0));
}

void setTodayRange(State* st) {
    SYSTEMTIME end{};
    GetLocalTime(&end);
    SYSTEMTIME start = end;
    FILETIME ft{};
    SystemTimeToFileTime(&start, &ft);
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    value.QuadPart -= static_cast<ULONGLONG>(29) * 24 * 60 * 60 * 10000000ULL;
    ft.dwLowDateTime = value.LowPart;
    ft.dwHighDateTime = value.HighPart;
    FileTimeToSystemTime(&ft, &start);
    DateTime_SetSystemtime(st->startDate, GDT_VALID, &start);
    DateTime_SetSystemtime(st->endDate, GDT_VALID, &end);
}

void setStatus(State* st, const std::wstring& text) {
    if (st && st->status) SetWindowTextW(st->status, text.c_str());
}

bool rowVisibleByStatus(const State* st, const qc::Result& row) {
    if (!st || !st->statusFilter) return true;
    switch (comboSelection(st->statusFilter)) {
        case 1:
            return row.qc_status == "warning" || row.qc_status == "out_of_control";
        case 2:
            return row.qc_status == "out_of_control";
        case 3:
            return row.qc_status == "warning";
        default:
            return true;
    }
}

bool hasVisibleRows(const State* st) {
    if (!st) return false;
    for (const auto& row : st->rows) {
        if (rowVisibleByStatus(st, row)) return true;
    }
    return false;
}

void updateExportButton(State* st) {
    if (!st || !st->exportButton) return;
    EnableWindow(st->exportButton, !st->busy && hasVisibleRows(st));
}

void updateChartButton(State* st) {
    if (!st || !st->chartButton) return;
    EnableWindow(st->chartButton, !st->busy && !st->groupsRows.empty());
}

void setCell(HWND list, int row, int col, const std::string& text) {
    const auto wide = search::utf8_to_wide(text);
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(wide.c_str()));
}

void addColumn(HWND list, int col, const wchar_t* text, int width) {
    LVCOLUMNW lvc{};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    lvc.pszText = const_cast<wchar_t*>(text);
    lvc.cx = width;
    lvc.iSubItem = col;
    ListView_InsertColumn(list, col, &lvc);
}

void setupList(HWND list, const std::vector<std::pair<const wchar_t*, int>>& cols) {
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
        addColumn(list, i, cols[static_cast<size_t>(i)].first, cols[static_cast<size_t>(i)].second);
    }
}

std::string formatNumber(double value, int digits = 2) {
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
    return buffer;
}

double groupMean(const GroupRow& group) {
    return group.numeric_count > 0 ? group.sum / group.numeric_count : 0.0;
}

double groupSd(const GroupRow& group) {
    if (group.numeric_count < 2) return 0.0;
    const double mean = groupMean(group);
    const double variance = (group.sum_square - group.numeric_count * mean * mean) / (group.numeric_count - 1);
    return variance > 0.0 ? std::sqrt(variance) : 0.0;
}

std::string groupKey(const std::string& machCode, const std::string& sampleNo, const std::string& itemCode,
                     const std::string& level, const std::string& qcName) {
    return machCode + "\x1f" + sampleNo + "\x1f" + itemCode + "\x1f" + level + "\x1f" + qcName;
}

std::string rowGroupKey(const qc::Result& row) {
    return groupKey(row.mach_code, row.sample_no, row.item_code, row.level, row.qc_name);
}

std::string qcStatusText(const std::string& status) {
    if (status == "out_of_control") return "失控";
    if (status == "warning") return "警告";
    if (status == "in_control") return "在控";
    return "";
}

std::string groupStatusText(const GroupRow& group) {
    if (group.count <= 0) return "无数据";
    if (group.out_of_control_count > 0) return "失控";
    if (group.warning_count > 0) return "警告";
    if (group.evaluated_count <= 0) return "未判定";
    return "在控";
}

std::string joinRules(const std::vector<std::string>& rules) {
    std::string text;
    for (const auto& rule : rules) {
        if (!text.empty()) text += ", ";
        text += rule;
    }
    return text;
}

void appendRule(qc::Result& row, const std::string& rule) {
    std::string remaining = row.qc_rules;
    size_t start = 0;
    while (start <= remaining.size()) {
        const size_t comma = remaining.find(',', start);
        const std::string existing = search::trim(remaining.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (existing == rule) return;
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    if (!row.qc_rules.empty()) row.qc_rules += ", ";
    row.qc_rules += rule;
}

void markOutOfControl(qc::Result& row, const std::string& rule) {
    row.qc_status = "out_of_control";
    appendRule(row, rule);
}

void markWarning(qc::Result& row, const std::string& rule) {
    if (row.qc_status != "out_of_control") row.qc_status = "warning";
    appendRule(row, rule);
}

bool parseNumber(const std::string& text, double& value) {
    const std::string trimmed = search::trim(text);
    if (trimmed.empty()) return false;
    char* end = nullptr;
    value = std::strtod(trimmed.c_str(), &end);
    return end && *end == '\0';
}

bool configuredQcStats(const qc::Result& row, double& mean, double& sd) {
    return parseNumber(row.target_mean, mean) && parseNumber(row.target_sd, sd) && sd > 0.0;
}

double groupQcMean(const GroupRow& group) {
    return group.has_configured_stats ? group.qc_mean : groupMean(group);
}

double groupQcSd(const GroupRow& group) {
    return group.has_configured_stats ? group.qc_sd : groupSd(group);
}

bool groupHasQcStats(const GroupRow& group) {
    return group.has_configured_stats || (group.numeric_count > 1 && groupSd(group) > 0.0);
}

std::string groupStatsSourceText(const GroupRow& group) {
    if (group.has_configured_stats) return "靶值";
    if (groupHasQcStats(group)) return "本期";
    return "";
}

void evaluateWestgardRules(State* st) {
    if (!st) return;
    std::map<std::string, const GroupRow*> groupsByKey;
    for (const auto& group : st->groupsRows) {
        groupsByKey[group.key] = &group;
    }

    for (auto& row : st->rows) {
        row.qc_mean = 0.0;
        row.qc_sd = 0.0;
        row.qc_z = 0.0;
        row.has_qc_stats = false;
        row.has_qc_z = false;
        row.qc_status.clear();
        row.qc_rules.clear();
        if (!row.has_numeric_value) continue;

        double mean = 0.0;
        double sd = 0.0;
        if (!configuredQcStats(row, mean, sd)) {
            const auto found = groupsByKey.find(rowGroupKey(row));
            if (found == groupsByKey.end()) continue;
            mean = groupMean(*found->second);
            sd = groupSd(*found->second);
        }
        if (sd <= 0.0) continue;

        row.qc_mean = mean;
        row.qc_sd = sd;
        row.qc_z = (row.result_value - mean) / sd;
        row.has_qc_stats = true;
        row.has_qc_z = true;

        const double absZ = std::fabs(row.qc_z);
        std::vector<std::string> rules;
        if (absZ >= 3.0) {
            row.qc_status = "out_of_control";
            rules.push_back("1-3s");
        } else if (absZ >= 2.0) {
            row.qc_status = "warning";
            rules.push_back("1-2s");
        } else {
            row.qc_status = "in_control";
        }
        row.qc_rules = joinRules(rules);
    }

    std::map<std::string, std::vector<size_t>> rowsByGroup;
    for (size_t i = 0; i < st->rows.size(); ++i) {
        if (st->rows[i].has_qc_z) rowsByGroup[rowGroupKey(st->rows[i])].push_back(i);
    }
    for (auto& entry : rowsByGroup) {
        auto& indexes = entry.second;
        std::sort(indexes.begin(), indexes.end(), [st](size_t lhs, size_t rhs) {
            const auto& a = st->rows[lhs];
            const auto& b = st->rows[rhs];
            if (a.effective_time != b.effective_time) return a.effective_time < b.effective_time;
            if (a.source_rep_no != b.source_rep_no) return a.source_rep_no < b.source_rep_no;
            return a.source_entry_key < b.source_entry_key;
        });
        for (size_t i = 1; i < indexes.size(); ++i) {
            const qc::Result& prev = st->rows[indexes[i - 1]];
            qc::Result& current = st->rows[indexes[i]];
            const bool sameSide = (prev.qc_z >= 2.0 && current.qc_z >= 2.0) ||
                                  (prev.qc_z <= -2.0 && current.qc_z <= -2.0);
            if (sameSide) markOutOfControl(current, "2-2s");

            const bool oppositeSide = (prev.qc_z > 0.0 && current.qc_z < 0.0) ||
                                      (prev.qc_z < 0.0 && current.qc_z > 0.0);
            if (oppositeSide && std::fabs(current.qc_z - prev.qc_z) > 4.0) {
                markOutOfControl(current, "R-4s");
            }
        }
        for (size_t i = 3; i < indexes.size(); ++i) {
            const qc::Result& p0 = st->rows[indexes[i - 3]];
            const qc::Result& p1 = st->rows[indexes[i - 2]];
            const qc::Result& p2 = st->rows[indexes[i - 1]];
            qc::Result& current = st->rows[indexes[i]];
            const bool highSide = p0.qc_z >= 1.0 && p1.qc_z >= 1.0 && p2.qc_z >= 1.0 && current.qc_z >= 1.0;
            const bool lowSide = p0.qc_z <= -1.0 && p1.qc_z <= -1.0 && p2.qc_z <= -1.0 && current.qc_z <= -1.0;
            if (highSide || lowSide) markWarning(current, "4-1s");
        }
        for (size_t i = 9; i < indexes.size(); ++i) {
            bool highSide = true;
            bool lowSide = true;
            for (size_t offset = 0; offset < 10; ++offset) {
                const qc::Result& point = st->rows[indexes[i - offset]];
                highSide = highSide && point.qc_z > 0.0;
                lowSide = lowSide && point.qc_z < 0.0;
            }
            if (highSide || lowSide) markWarning(st->rows[indexes[i]], "10x");
        }
    }
}

int chartListHeight(const RECT& rc) {
    const int height = static_cast<int>(rc.bottom - rc.top);
    return std::clamp(height / 4, 150, 240);
}

int chartSingleHeightForWidth(int width) {
    const int preferred = static_cast<int>(width * 0.42);
    return std::clamp(preferred, 280, 420);
}

int chartGap() {
    return 12;
}

int chartContentHeight(const ChartWindowData* data, int singleHeight) {
    if (!data || data->series.empty()) return 0;
    return static_cast<int>(data->series.size()) * singleHeight +
           static_cast<int>(data->series.size() - 1) * chartGap();
}

int chartViewportHeight(const RECT& rc) {
    return std::max(0, static_cast<int>(rc.bottom - rc.top));
}

RECT chartAreaRect(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int listH = chartListHeight(rc);
    rc.bottom = std::max(rc.top + 220, rc.bottom - listH - 8);
    return rc;
}

int chartSingleHeightForRect(const RECT& rc) {
    return chartSingleHeightForWidth(std::max(0, static_cast<int>(rc.right - rc.left)));
}

int maxChartScroll(HWND hwnd, const ChartWindowData* data) {
    const RECT rc = chartAreaRect(hwnd);
    return std::max(0, chartContentHeight(data, chartSingleHeightForRect(rc)) - chartViewportHeight(rc));
}

void updateChartScrollBar(HWND hwnd, ChartWindowData* data) {
    if (!data) return;
    const int maxScroll = maxChartScroll(hwnd, data);
    data->scrollY = std::clamp(data->scrollY, 0, maxScroll);
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    const RECT rc = chartAreaRect(hwnd);
    si.nMax = std::max(0, chartContentHeight(data, chartSingleHeightForRect(rc)) - 1);
    si.nPage = static_cast<UINT>(std::max(1, chartViewportHeight(rc)));
    si.nPos = data->scrollY;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

RECT chartListRect(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int listH = chartListHeight(rc);
    rc.top = std::max(rc.top + 220, rc.bottom - listH);
    return rc;
}

std::string zText(const qc::ChartSeries& series, const qc::ChartPoint& point) {
    (void)series;
    if (!point.has_z) return "";
    return formatNumber(point.z);
}

const qc::ChartPoint* findChartPoint(const ChartWindowData* data, int index, const qc::ChartSeries** owner = nullptr) {
    if (!data || index < 0) return nullptr;
    for (const auto& series : data->series) {
        for (const auto& point : series.points) {
            if (static_cast<int>(point.point_index) == index) {
                if (owner) *owner = &series;
                return &point;
            }
        }
    }
    return nullptr;
}

void populateChartList(ChartWindowData* data) {
    if (!data || !data->list) return;
    ListView_DeleteAllItems(data->list);
    int row = 0;
    for (const auto& series : data->series) {
        for (const auto& point : series.points) {
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = row;
            const auto time = search::utf8_to_wide(point.time);
            item.pszText = const_cast<wchar_t*>(time.c_str());
            ListView_InsertItem(data->list, &item);
            setCell(data->list, row, 1, point.sample_no);
            setCell(data->list, row, 2, point.rep_no);
            setCell(data->list, row, 3, point.level);
            setCell(data->list, row, 4, point.result_text);
            setCell(data->list, row, 5, point.unit);
            setCell(data->list, row, 6, zText(series, point));
            setCell(data->list, row, 7, qcStatusText(point.qc_status));
            setCell(data->list, row, 8, point.qc_rules);
            ++row;
        }
    }
}

int hitChartPoint(const ChartWindowData* data, int x, int y) {
    if (!data) return -1;
    POINT pt{x, y};
    for (const auto& hit : data->hitPoints) {
        if (PtInRect(&hit.bounds, pt)) return static_cast<int>(hit.point_index);
    }
    return -1;
}

std::wstring chartTooltipText(const ChartWindowData* data, int index) {
    const qc::ChartSeries* series = nullptr;
    const qc::ChartPoint* found = findChartPoint(data, index, &series);
    if (!found || !series) return L"";
    const auto& point = *found;
    std::wstring text = L"报告号：" + search::utf8_to_wide(point.rep_no);
    text += L"\n样本号：" + search::utf8_to_wide(point.sample_no);
    text += L"\n时间：" + search::utf8_to_wide(point.time);
    text += L"\n项目：" + search::utf8_to_wide(point.item_name);
    text += L"\n结果：" + search::utf8_to_wide(point.result_text);
    if (!point.unit.empty()) text += L" " + search::utf8_to_wide(point.unit);
    const std::string z = zText(*series, point);
    if (!z.empty()) text += L"\nZ值：" + search::utf8_to_wide(z);
    const std::string status = qcStatusText(point.qc_status);
    if (!status.empty()) text += L"\n状态：" + search::utf8_to_wide(status);
    if (!point.qc_rules.empty()) text += L"\n命中规则：" + search::utf8_to_wide(point.qc_rules);
    return text;
}

void updateChartTooltip(HWND hwnd, ChartWindowData* data, int index) {
    if (!data || !data->tooltip) return;
    if (index == data->hoverIndex) return;
    data->hoverIndex = index;
    data->tooltipText = chartTooltipText(data, index);
    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.hwnd = hwnd;
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.uId = reinterpret_cast<UINT_PTR>(hwnd);
    ti.lpszText = data->tooltipText.empty() ? const_cast<wchar_t*>(L"") : data->tooltipText.data();
    SendMessageW(data->tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
}

void selectChartPoint(HWND hwnd, ChartWindowData* data, int index) {
    if (!data || !findChartPoint(data, index)) return;
    data->selectedIndex = index;
    if (data->list) {
        data->suppressListNotify = true;
        ListView_SetItemState(data->list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(data->list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(data->list, index, FALSE);
        data->suppressListNotify = false;
    }
    RECT rc = chartAreaRect(hwnd);
    InvalidateRect(hwnd, &rc, FALSE);
}

void setChartScroll(HWND hwnd, ChartWindowData* data, int position) {
    if (!data) return;
    const int next = std::clamp(position, 0, maxChartScroll(hwnd, data));
    if (next == data->scrollY) return;
    data->scrollY = next;
    updateChartScrollBar(hwnd, data);
    RECT rc = chartAreaRect(hwnd);
    InvalidateRect(hwnd, &rc, FALSE);
}

void drawChartStack(HDC dc, const RECT& rect, ChartWindowData* data) {
    if (!data) return;
    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(dc, &rect, bg);
    DeleteObject(bg);
    data->hitPoints.clear();

    const int singleHeight = chartSingleHeightForRect(rect);
    int top = rect.top - data->scrollY;
    for (const auto& series : data->series) {
        RECT chartRect{rect.left, top, rect.right, top + singleHeight};
        RECT intersection{};
        if (IntersectRect(&intersection, &rect, &chartRect)) {
            std::vector<qc::ChartHitPoint> localHits;
            qc::draw_levey_jennings_chart(dc, chartRect, series, data->selectedIndex, &localHits);
            data->hitPoints.insert(data->hitPoints.end(), localHits.begin(), localHits.end());
        }
        top += singleHeight + chartGap();
        if (top > rect.bottom + singleHeight) break;
    }
}

LRESULT CALLBACK chartWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* data = reinterpret_cast<ChartWindowData*>(GetPropW(hwnd, PROP_CHART));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            data = reinterpret_cast<ChartWindowData*>(cs->lpCreateParams);
            SetPropW(hwnd, PROP_CHART, reinterpret_cast<HANDLE>(data));
            data->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                         WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SINGLESEL,
                                         0, 0, 0, 0, hwnd, win32_control_id(IDC_CHART_LIST), GetModuleHandleW(nullptr), nullptr);
            setupList(data->list, {{L"时间", 140}, {L"样本号", 80}, {L"报告号", 90}, {L"水平", 60},
                                   {L"结果", 90}, {L"单位", 70}, {L"Z值", 70}, {L"状态", 70},
                                   {L"规则", 90}});
            populateChartList(data);
            data->tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (data->tooltip) {
                TOOLINFOW ti{};
                ti.cbSize = sizeof(ti);
                ti.hwnd = hwnd;
                ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                ti.uId = reinterpret_cast<UINT_PTR>(hwnd);
                ti.lpszText = const_cast<wchar_t*>(L"");
                SendMessageW(data->tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
                SendMessageW(data->tooltip, TTM_SETMAXTIPWIDTH, 0, 360);
            }
            updateChartScrollBar(hwnd, data);
            return 0;
        }
        case WM_SIZE: {
            if (data && data->list) {
                RECT listRc = chartListRect(hwnd);
                MoveWindow(data->list, listRc.left + 8, listRc.top, listRc.right - listRc.left - 16,
                           listRc.bottom - listRc.top - 8, TRUE);
                RedrawWindow(data->list, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
                updateChartScrollBar(hwnd, data);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_VSCROLL: {
            if (!data) break;
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int next = data->scrollY;
            switch (LOWORD(wp)) {
                case SB_LINEUP:
                    next -= 40;
                    break;
                case SB_LINEDOWN:
                    next += 40;
                    break;
                case SB_PAGEUP:
                    next -= static_cast<int>(si.nPage);
                    break;
                case SB_PAGEDOWN:
                    next += static_cast<int>(si.nPage);
                    break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION:
                    next = si.nTrackPos;
                    break;
                default:
                    break;
            }
            setChartScroll(hwnd, data, next);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (!data) break;
            const int delta = GET_WHEEL_DELTA_WPARAM(wp);
            setChartScroll(hwnd, data, data->scrollY - delta * 80 / WHEEL_DELTA);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            RECT rc = chartAreaRect(hwnd);
            HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
            RECT topBg{client.left, client.top, client.right, rc.top};
            RECT bottomBg{client.left, rc.bottom, client.right, client.bottom};
            if (topBg.bottom > topBg.top) FillRect(dc, &topBg, bg);
            if (bottomBg.bottom > bottomBg.top) FillRect(dc, &bottomBg, bg);
            DeleteObject(bg);
            if (data && rc.right > rc.left && rc.bottom > rc.top) {
                HDC memDc = CreateCompatibleDC(dc);
                HBITMAP bitmap = CreateCompatibleBitmap(dc, rc.right - rc.left, rc.bottom - rc.top);
                HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);
                RECT localRc{0, 0, rc.right - rc.left, rc.bottom - rc.top};
                drawChartStack(memDc, localRc, data);
                for (auto& hit : data->hitPoints) {
                    OffsetRect(&hit.bounds, rc.left, rc.top);
                }
                BitBlt(dc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, memDc, 0, 0, SRCCOPY);
                SelectObject(memDc, oldBitmap);
                DeleteObject(bitmap);
                DeleteDC(memDc);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!data) break;
            const int x = static_cast<short>(LOWORD(lp));
            const int y = static_cast<short>(HIWORD(lp));
            updateChartTooltip(hwnd, data, hitChartPoint(data, x, y));
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (!data) break;
            const int x = static_cast<short>(LOWORD(lp));
            const int y = static_cast<short>(HIWORD(lp));
            const int index = hitChartPoint(data, x, y);
            if (index >= 0) selectChartPoint(hwnd, data, index);
            return 0;
        }
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (data && nm && nm->idFrom == IDC_CHART_LIST && nm->code == LVN_ITEMCHANGED && !data->suppressListNotify) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED)) {
                    data->selectedIndex = lv->iItem;
                    RECT rc = chartAreaRect(hwnd);
                    InvalidateRect(hwnd, &rc, FALSE);
                }
                return 0;
            }
            break;
        }
        case WM_DESTROY:
            RemovePropW(hwnd, PROP_CHART);
            delete data;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ensureChartWindowClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = chartWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = CHART_WND_CLASS;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    registered = true;
}

void buildGroups(State* st, bool applyStatusFilter = true) {
    st->groupsRows.clear();
    std::map<std::string, size_t> index;
    for (const auto& row : st->rows) {
        if (applyStatusFilter && !rowVisibleByStatus(st, row)) continue;
        const std::string key = rowGroupKey(row);
        auto found = index.find(key);
        if (found == index.end()) {
            index[key] = st->groupsRows.size();
            GroupRow group;
            group.key = key;
            group.mach_code = row.mach_code;
            group.mach_name = row.mach_name;
            group.sample_no = row.sample_no;
            group.item_code = row.item_code;
            group.item_name = row.item_name;
            group.level = row.level;
            group.qc_name = row.qc_name;
            st->groupsRows.push_back(std::move(group));
            found = index.find(key);
        }
        auto& group = st->groupsRows[found->second];
        ++group.count;
        if (row.effective_time >= group.latest_time) {
            group.latest_time = row.effective_time;
            group.latest_result = row.result_text;
        }
        if (row.has_numeric_value) {
            ++group.numeric_count;
            group.sum += row.result_value;
            group.sum_square += row.result_value * row.result_value;
        }
        double configuredMean = 0.0;
        double configuredSd = 0.0;
        if (!group.has_configured_stats && configuredQcStats(row, configuredMean, configuredSd)) {
            group.qc_mean = configuredMean;
            group.qc_sd = configuredSd;
            group.has_configured_stats = true;
        }
        if (!row.qc_status.empty()) {
            ++group.evaluated_count;
        }
        if (row.qc_status == "out_of_control") {
            ++group.out_of_control_count;
        } else if (row.qc_status == "warning") {
            ++group.warning_count;
        } else if (row.qc_status == "in_control") {
            ++group.in_control_count;
        }
    }
}

void populateGroups(State* st) {
    ListView_DeleteAllItems(st->groups);
    for (int i = 0; i < static_cast<int>(st->groupsRows.size()); ++i) {
        const auto& row = st->groupsRows[static_cast<size_t>(i)];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        const auto title = search::utf8_to_wide(row.item_name.empty() ? row.item_code : row.item_name);
        item.pszText = const_cast<wchar_t*>(title.c_str());
        ListView_InsertItem(st->groups, &item);
        setCell(st->groups, i, 1, row.level);
        setCell(st->groups, i, 2, row.sample_no);
        setCell(st->groups, i, 3, std::to_string(row.count));
        setCell(st->groups, i, 4, groupHasQcStats(row) ? formatNumber(groupQcMean(row)) : "");
        setCell(st->groups, i, 5, groupHasQcStats(row) ? formatNumber(groupQcSd(row)) : "");
        const double mean = groupQcMean(row);
        const double sd = groupQcSd(row);
        setCell(st->groups, i, 6, groupHasQcStats(row) && mean != 0.0 ? formatNumber(sd / mean * 100.0) : "");
        setCell(st->groups, i, 7, groupStatsSourceText(row));
        setCell(st->groups, i, 8, groupStatusText(row));
        setCell(st->groups, i, 9, row.out_of_control_count > 0 ? std::to_string(row.out_of_control_count) : "");
        setCell(st->groups, i, 10, row.warning_count > 0 ? std::to_string(row.warning_count) : "");
        setCell(st->groups, i, 11, row.latest_time);
    }
}

bool rowInSelectedGroup(const State* st, const qc::Result& row) {
    if (!st || st->selectedGroup < 0 || st->selectedGroup >= static_cast<int>(st->groupsRows.size())) return true;
    const auto& group = st->groupsRows[static_cast<size_t>(st->selectedGroup)];
    return rowVisibleByStatus(st, row) && rowGroupKey(row) == group.key;
}

void populateCards(State* st) {
    ListView_DeleteAllItems(st->cards);
    int out = 0;
    for (const auto& group : st->groupsRows) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = out;
        const std::string title = group.qc_name.empty() ? group.item_name : group.qc_name;
        const auto titleW = search::utf8_to_wide(title);
        item.pszText = const_cast<wchar_t*>(titleW.c_str());
        ListView_InsertItem(st->cards, &item);
        setCell(st->cards, out, 1, group.mach_name.empty() ? group.mach_code : group.mach_name);
        setCell(st->cards, out, 2, group.sample_no);
        setCell(st->cards, out, 3, group.item_name);
        setCell(st->cards, out, 4, group.level);
        setCell(st->cards, out, 5, group.latest_result);
        setCell(st->cards, out, 6, group.latest_time);
        setCell(st->cards, out, 7, groupStatusText(group));
        ++out;
    }
}

void populateDetails(State* st) {
    ListView_DeleteAllItems(st->details);
    int out = 0;
    for (const auto& row : st->rows) {
        if (!rowVisibleByStatus(st, row)) continue;
        if (!rowInSelectedGroup(st, row)) continue;
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = out;
        const auto timeW = search::utf8_to_wide(row.effective_time);
        item.pszText = const_cast<wchar_t*>(timeW.c_str());
        ListView_InsertItem(st->details, &item);
        setCell(st->details, out, 1, row.sample_no);
        setCell(st->details, out, 2, row.source_rep_no);
        setCell(st->details, out, 3, row.mach_name.empty() ? row.mach_code : row.mach_name);
        setCell(st->details, out, 4, row.tester_name);
        setCell(st->details, out, 5, row.item_name);
        setCell(st->details, out, 6, row.level);
        setCell(st->details, out, 7, row.result_text);
        setCell(st->details, out, 8, row.unit);
        setCell(st->details, out, 9, qcStatusText(row.qc_status));
        setCell(st->details, out, 10, row.qc_rules);
        setCell(st->details, out, 11, row.has_qc_z ? formatNumber(row.qc_z) : "");
        setCell(st->details, out, 12, "LIS");
        ++out;
    }
}

std::vector<const qc::Result*> visibleDetails(State* st) {
    std::vector<const qc::Result*> rows;
    if (!st) return rows;
    for (const auto& row : st->rows) {
        if (!rowVisibleByStatus(st, row)) continue;
        if (rowInSelectedGroup(st, row)) rows.push_back(&row);
    }
    return rows;
}

void refreshVisibleResults(State* st) {
    if (!st) return;
    buildGroups(st);
    if (st->selectedGroup >= static_cast<int>(st->groupsRows.size())) st->selectedGroup = -1;
    populateGroups(st);
    populateCards(st);
    populateDetails(st);
    updateExportButton(st);
    updateChartButton(st);
}

void clearCurrentResults(State* st) {
    if (!st) return;
    st->rows.clear();
    st->groupsRows.clear();
    st->selectedGroup = -1;
    refreshVisibleResults(st);
}

const GroupRow* selectedOrFirstGroup(State* st) {
    if (!st || st->groupsRows.empty()) return nullptr;
    if (st->selectedGroup >= 0 && st->selectedGroup < static_cast<int>(st->groupsRows.size())) {
        return &st->groupsRows[static_cast<size_t>(st->selectedGroup)];
    }
    return &st->groupsRows.front();
}

bool rowInGroup(const GroupRow& group, const qc::Result& row) {
    return rowGroupKey(row) == group.key;
}

bool groupMatchesChartBase(const GroupRow& base, const GroupRow& candidate) {
    if (base.mach_code != candidate.mach_code) return false;
    const std::string baseName = search::trim(base.item_name);
    const std::string candidateName = search::trim(candidate.item_name);
    if (!baseName.empty() && !candidateName.empty()) return baseName == candidateName;
    return search::trim(base.item_code) == search::trim(candidate.item_code);
}

qc::ChartSeries buildChartSeries(State* st, const GroupRow& group, size_t& pointIndex) {
    qc::ChartSeries series;
    const std::string item = group.item_name.empty() ? group.item_code : group.item_name;
    series.title = item.empty() ? "Levey-Jennings 质控图" : item;
    if (!group.level.empty()) series.title += " " + group.level;
    if (!group.sample_no.empty()) series.title += " / 样本号 " + group.sample_no;
    series.subtitle = group.mach_name.empty() ? group.mach_code : group.mach_name;
    if (!group.qc_name.empty()) series.subtitle += " / " + group.qc_name;
    series.mean = groupQcMean(group);
    series.sd = groupQcSd(group);
    series.has_mean = groupHasQcStats(group);
    series.has_sd = groupHasQcStats(group) && series.sd > 0.0;
    for (const auto& row : st->rows) {
        if (!rowVisibleByStatus(st, row)) continue;
        if (!rowInGroup(group, row)) continue;
        qc::ChartPoint point;
        point.time = row.effective_time;
        point.sample_no = row.sample_no;
        point.rep_no = row.source_rep_no;
        point.item_name = row.item_name;
        point.level = row.level;
        point.result_text = row.result_text;
        point.unit = row.unit;
        point.qc_status = row.qc_status;
        point.qc_rules = row.qc_rules;
        point.value = row.result_value;
        point.z = row.qc_z;
        point.has_value = row.has_numeric_value;
        point.has_z = row.has_qc_z;
        if (series.unit.empty()) series.unit = row.unit;
        series.points.push_back(std::move(point));
    }
    std::sort(series.points.begin(), series.points.end(), [](const qc::ChartPoint& a, const qc::ChartPoint& b) {
        if (a.time != b.time) return a.time < b.time;
        return a.rep_no < b.rep_no;
    });
    for (auto& point : series.points) {
        point.point_index = pointIndex++;
    }
    return series;
}

std::vector<qc::ChartSeries> buildChartSeriesList(State* st, const GroupRow& base) {
    std::vector<const GroupRow*> groups;
    for (const auto& group : st->groupsRows) {
        if (groupMatchesChartBase(base, group)) groups.push_back(&group);
    }
    std::sort(groups.begin(), groups.end(), [](const GroupRow* a, const GroupRow* b) {
        if (a->level != b->level) return a->level < b->level;
        if (a->sample_no != b->sample_no) return a->sample_no < b->sample_no;
        return a->qc_name < b->qc_name;
    });

    std::vector<qc::ChartSeries> series;
    size_t pointIndex = 0;
    for (const auto* group : groups) {
        auto item = buildChartSeries(st, *group, pointIndex);
        if (!item.points.empty()) series.push_back(std::move(item));
    }
    return series;
}

RECT centeredChartWindowRect(HWND owner) {
    HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(monitor, &mi);
    const RECT work = mi.rcWork;
    const int workW = static_cast<int>(work.right - work.left);
    const int workH = static_cast<int>(work.bottom - work.top);
    const int maxW = std::max(640, workW - 40);
    const int maxH = std::max(520, workH - 40);
    const int minW = std::min(1280, maxW);
    const int minH = std::min(820, maxH);
    const int width = std::clamp(static_cast<int>(workW * 0.86), minW, maxW);
    const int height = std::clamp(static_cast<int>(workH * 0.84), minH, maxH);
    const int left = work.left + (workW - width) / 2;
    const int top = work.top + (workH - height) / 2;
    return RECT{left, top, left + width, top + height};
}

void openLeveyJenningsChart(HWND hwnd, State* st) {
    const GroupRow* group = selectedOrFirstGroup(st);
    if (!group) {
        MessageBoxW(hwnd, L"请先查询到质控结果。", WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }
    auto* data = new ChartWindowData;
    data->series = buildChartSeriesList(st, *group);
    ensureChartWindowClass();
    const RECT popup = centeredChartWindowRect(hwnd);
    HWND chart = CreateWindowExW(WS_EX_APPWINDOW, CHART_WND_CLASS, L"Levey-Jennings 质控图",
                                 WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL,
                                 popup.left, popup.top, popup.right - popup.left, popup.bottom - popup.top,
                                 hwnd, nullptr, GetModuleHandleW(nullptr), data);
    if (!chart) {
        delete data;
        MessageBoxW(hwnd, L"L-J 图窗口创建失败。", WINDOW_TITLE, MB_ICONERROR);
    }
}

std::string csvEscape(const std::string& text) {
    const bool quote = text.find_first_of(",\"\r\n") != std::string::npos;
    std::string out;
    out.reserve(text.size() + 2);
    if (quote) out.push_back('"');
    for (const char ch : text) {
        if (ch == '"') {
            out += "\"\"";
        } else {
            out.push_back(ch);
        }
    }
    if (quote) out.push_back('"');
    return out;
}

std::string sanitizeFilenamePart(std::string text) {
    text = search::trim(text);
    for (char& ch : text) {
        switch (ch) {
            case '\\':
            case '/':
            case ':':
            case '*':
            case '?':
            case '"':
            case '<':
            case '>':
            case '|':
                ch = '_';
                break;
            default:
                break;
        }
    }
    return text;
}

std::wstring defaultExportFilename(State* st) {
    std::string filename = "quality_control";
    const std::string mach = sanitizeFilenamePart(search::wide_to_utf8(editText(st->machCode)));
    if (!mach.empty()) filename += "-" + mach;
    const std::string start = sanitizeFilenamePart(dateText(st->startDate));
    const std::string end = sanitizeFilenamePart(dateText(st->endDate));
    if (!start.empty()) filename += "-" + start;
    if (!end.empty() && end != start) filename += "-" + end;
    filename += ".csv";
    return search::utf8_to_wide(filename);
}

bool chooseExportPath(HWND owner, State* st, std::wstring& path) {
    wchar_t buffer[MAX_PATH]{};
    const std::wstring defaultName = defaultExportFilename(st);
    lstrcpynW(buffer, defaultName.c_str(), MAX_PATH);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"CSV 文件 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return false;
    path = buffer;
    return true;
}

void exportVisibleDetailsCsv(HWND hwnd, State* st) {
    if (!st || st->busy) return;
    const auto rows = visibleDetails(st);
    if (rows.empty()) {
        MessageBoxW(hwnd, L"当前没有可导出的质控明细。", WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }

    std::wstring path;
    if (!chooseExportPath(hwnd, st, path)) return;

    FILE* file = nullptr;
#ifdef _MSC_VER
    _wfopen_s(&file, path.c_str(), L"wb");
#else
    file = _wfopen(path.c_str(), L"wb");
#endif
    if (!file) {
        MessageBoxW(hwnd, L"导出文件创建失败，请确认目标位置可写。", WINDOW_TITLE, MB_ICONERROR);
        return;
    }

    std::ostringstream csv;
    csv << "\xEF\xBB\xBF";
    csv << "时间,样本号,报告号,仪器,检验者,项目,水平,结果,单位,状态,规则,Z值,来源\n";
    for (const auto* row : rows) {
        csv << csvEscape(row->effective_time) << ','
            << csvEscape(row->sample_no) << ','
            << csvEscape(row->source_rep_no) << ','
            << csvEscape(row->mach_name.empty() ? row->mach_code : row->mach_name) << ','
            << csvEscape(row->tester_name) << ','
            << csvEscape(row->item_name) << ','
            << csvEscape(row->level) << ','
            << csvEscape(row->result_text) << ','
            << csvEscape(row->unit) << ','
            << csvEscape(qcStatusText(row->qc_status)) << ','
            << csvEscape(row->qc_rules) << ','
            << csvEscape(row->has_qc_z ? formatNumber(row->qc_z) : "") << ','
            << "LIS\n";
    }
    const std::string text = csv.str();
    fwrite(text.data(), 1, text.size(), file);
    fclose(file);
    setStatus(st, L"已导出质控明细：" + path);
    MessageBoxW(hwnd, (L"已导出质控明细：\n" + path).c_str(), WINDOW_TITLE, MB_ICONINFORMATION);
}

void resizeLayout(HWND hwnd, State* st) {
    if (!st) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int pad = S(hwnd, 10);
    const int statusH = S(hwnd, 24);
    const int topH = S(hwnd, 74);
    const int bottomH = std::max(S(hwnd, 190), static_cast<int>((rc.bottom - rc.top) / 3));
    const int leftW = std::max(S(hwnd, 260), static_cast<int>((rc.right - rc.left) / 4));
    int x = pad;
    MoveWindow(st->startLabel, x, pad, S(hwnd, 64), S(hwnd, 24), TRUE);
    MoveWindow(st->startDate, x + S(hwnd, 70), pad, S(hwnd, 120), S(hwnd, 24), TRUE);
    MoveWindow(st->endLabel, x + S(hwnd, 195), pad, S(hwnd, 20), S(hwnd, 24), TRUE);
    MoveWindow(st->endDate, x + S(hwnd, 220), pad, S(hwnd, 120), S(hwnd, 24), TRUE);
    MoveWindow(st->machLabel, x + S(hwnd, 345), pad, S(hwnd, 50), S(hwnd, 24), TRUE);
    MoveWindow(st->machCode, x + S(hwnd, 400), pad, S(hwnd, 110), S(hwnd, 24), TRUE);
    MoveWindow(st->machPick, x + S(hwnd, 514), pad - S(hwnd, 2), S(hwnd, 34), S(hwnd, 28), TRUE);
    MoveWindow(st->itemLabel, x, pad + S(hwnd, 34), S(hwnd, 64), S(hwnd, 24), TRUE);
    MoveWindow(st->itemCode, x + S(hwnd, 70), pad + S(hwnd, 34), S(hwnd, 120), S(hwnd, 24), TRUE);
    MoveWindow(st->levelLabel, x + S(hwnd, 195), pad + S(hwnd, 34), S(hwnd, 50), S(hwnd, 24), TRUE);
    MoveWindow(st->level, x + S(hwnd, 250), pad + S(hwnd, 34), S(hwnd, 90), S(hwnd, 160), TRUE);
    MoveWindow(st->statusFilterLabel, x + S(hwnd, 345), pad + S(hwnd, 34), S(hwnd, 50), S(hwnd, 24), TRUE);
    MoveWindow(st->statusFilter, x + S(hwnd, 400), pad + S(hwnd, 34), S(hwnd, 120), S(hwnd, 180), TRUE);
    MoveWindow(st->queryButton, x + S(hwnd, 535), pad + S(hwnd, 32), S(hwnd, 120), S(hwnd, 28), TRUE);
    MoveWindow(st->chartButton, x + S(hwnd, 665), pad + S(hwnd, 32), S(hwnd, 76), S(hwnd, 28), TRUE);
    MoveWindow(st->exportButton, x + S(hwnd, 750), pad + S(hwnd, 32), S(hwnd, 96), S(hwnd, 28), TRUE);
    MoveWindow(st->groups, pad, pad + topH, leftW, rc.bottom - pad * 3 - topH - bottomH - statusH, TRUE);
    MoveWindow(st->cards, pad * 2 + leftW, pad + topH,
               rc.right - leftW - pad * 3, rc.bottom - pad * 3 - topH - bottomH - statusH, TRUE);
    MoveWindow(st->details, pad, rc.bottom - bottomH - statusH - pad,
               rc.right - pad * 2, bottomH, TRUE);
    MoveWindow(st->status, pad, rc.bottom - statusH, rc.right - pad * 2, statusH, TRUE);
}

qc::Query buildQuery(State* st) {
    qc::Query query;
    query.start_date = dateText(st->startDate);
    query.end_date = dateText(st->endDate);
    query.mach_code = search::trim(st->selectedMachineCode);
    query.item_code = search::wide_to_utf8(editText(st->itemCode));
    query.level = comboText(st->level);
    if (query.level == "全部") query.level.clear();
    return query;
}

bool configMatchesItem(const qc::Config& cfg, const search::QualityControlLisRow& item) {
    const std::string configured = search::trim(cfg.item_code);
    return configured.empty() || configured == search::trim(item.item_code);
}

bool rowMatchesQuery(const qc::Query& request, const qc::Config& cfg, const search::QualityControlLisRow& item) {
    if (!configMatchesItem(cfg, item)) return false;
    const std::string requestedItem = search::trim(request.item_code);
    if (!requestedItem.empty() && requestedItem != search::trim(item.item_code)) return false;
    return true;
}

qc::Result toResult(const search::QualityControlLisRow& item, const qc::Config& cfg) {
    qc::Result row;
    row.source_rep_no = item.rep_no;
    row.source_entry_key = item.entry_id;
    row.room_code = item.room_code;
    row.mach_code = item.mach_code;
    row.mach_name = item.mach_name.empty() ? cfg.mach_name : item.mach_name;
    row.sample_no = item.sample_no;
    row.tester_name = item.tester_name;
    row.report_date = item.report_date;
    row.inspect_date = item.inspect_date;
    row.report_time = item.report_time;
    row.effective_time = item.effective_time;
    row.item_code = item.item_code;
    row.item_name = item.item_name.empty() ? cfg.item_name : item.item_name;
    row.result_text = item.result;
    row.has_numeric_value = parseNumber(item.result, row.result_value);
    row.unit = item.unit;
    row.normal = item.normal;
    row.qc_name = cfg.qc_name;
    row.level = cfg.level;
    row.target_mean = cfg.target_mean;
    row.target_sd = cfg.target_sd;
    return row;
}

bool queryLisRows(const ModuleContext& ctx, const qc::Query& request, std::vector<qc::Result>& rows,
                  int& configCount, std::string& error) {
    rows.clear();
    configCount = 0;
    std::vector<qc::Config> configs;
    if (!qc::load_configs(configs, error)) return false;

    std::vector<qc::Config> active;
    const std::string requestedMachine = search::trim(request.mach_code);
    const std::string requestedLevel = search::trim(request.level);
    for (const auto& cfg : configs) {
        if (!cfg.enabled) continue;
        if (search::trim(cfg.mach_code).empty() || search::trim(cfg.sample_no).empty()) continue;
        if (!requestedMachine.empty() && search::trim(cfg.mach_code) != requestedMachine) continue;
        if (!requestedLevel.empty() && search::trim(cfg.level) != requestedLevel) continue;
        active.push_back(cfg);
    }
    configCount = static_cast<int>(active.size());
    if (active.empty()) return true;

    const std::string connection = search::wide_to_utf8(search::build_connection_string_w(ctx.dbSettings));
    if (search::trim(connection).empty()) {
        error = "missing LIS database connection";
        return false;
    }

    std::map<std::string, std::vector<qc::Config>> configsByMachineSample;
    for (const auto& cfg : active) {
        configsByMachineSample[search::trim(cfg.mach_code) + "\x1f" + search::trim(cfg.sample_no)].push_back(cfg);
    }

    std::set<std::string> queriedKeys;
    std::set<std::string> emittedKeys;
    for (const auto& cfg : active) {
        const std::string key = search::trim(cfg.mach_code) + "\x1f" + search::trim(cfg.sample_no);
        if (!queriedKeys.insert(key).second) continue;

        search::QualityControlLisQuery lisQuery;
        lisQuery.connection_string = connection;
        lisQuery.start_date = request.start_date;
        lisQuery.end_date = request.end_date;
        lisQuery.mach_code = search::trim(cfg.mach_code);
        lisQuery.sample_no = search::trim(cfg.sample_no);

        std::vector<search::QualityControlLisRow> items;
        if (!search::query_quality_control_lis_results(lisQuery, items, error)) return false;

        const auto configIt = configsByMachineSample.find(key);
        if (configIt == configsByMachineSample.end()) continue;
        for (const auto& item : items) {
            for (const auto& itemCfg : configIt->second) {
                if (!rowMatchesQuery(request, itemCfg, item)) continue;
                const std::string uniqueKey = search::trim(item.rep_no) + "\x1f" + search::trim(item.entry_id);
                if (!emittedKeys.insert(uniqueKey).second) break;
                rows.push_back(toResult(item, itemCfg));
                break;
            }
        }
    }

    std::sort(rows.begin(), rows.end(), [](const qc::Result& a, const qc::Result& b) {
        if (a.mach_code != b.mach_code) return a.mach_code < b.mach_code;
        if (a.item_code != b.item_code) return a.item_code < b.item_code;
        if (a.level != b.level) return a.level < b.level;
        if (a.effective_time != b.effective_time) return a.effective_time < b.effective_time;
        return a.source_rep_no < b.source_rep_no;
    });
    return true;
}

void showMachinePicker(HWND hwnd, State* st) {
    if (!st) return;
    search::MachinePickerPopupOptions options;
    options.owner = hwnd;
    options.anchor = st->machPick;
    options.font = st->ctx.uiFont;
    options.db_settings = st->ctx.dbSettings;
    options.current_mach_code = search::trim(st->selectedMachineCode);
    options.include_all_rooms = true;
    options.on_accept = [st](const search::MachineOption& machine) {
        st->selectedMachineCode = machine.mach_code;
        SetWindowTextW(st->machCode, search::utf8_to_wide(machine.mach_name.empty() ? machine.mach_code : machine.mach_name).c_str());
        clearCurrentResults(st);
        setStatus(st, L"已选择检验仪器，请点击“查询/刷新 LIS”。");
    };
    search::show_machine_picker_popup(options);
}

void runLisQuery(HWND hwnd, State* st) {
    if (!st || st->busy) return;
    if (search::trim(st->selectedMachineCode).empty()) {
        setStatus(st, L"请先选择检验仪器，再点击“查询/刷新 LIS”。");
        MessageBoxW(hwnd, L"请先选择检验仪器。", WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }
    st->busy = true;
    EnableWindow(st->queryButton, FALSE);
    EnableWindow(st->exportButton, FALSE);
    EnableWindow(st->chartButton, FALSE);
    setStatus(st, L"正在查询 LIS 质控结果...");
    const qc::Query query = buildQuery(st);
    const ModuleContext ctx = st->ctx;
    std::thread([hwnd, ctx, query]() {
        auto* done = new QueryDone;
        const auto started = std::chrono::steady_clock::now();
        done->ok = queryLisRows(ctx, query, done->rows, done->config_count, done->error);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
        done->elapsed_ms = static_cast<int>(elapsed);
        if (!PostMessageW(hwnd, WM_QC_QUERY_DONE, 0, reinterpret_cast<LPARAM>(done))) delete done;
    }).detach();
}

void openRegularReport(HWND hwnd, State* st, int visibleIndex) {
    const auto rows = visibleDetails(st);
    if (visibleIndex < 0 || visibleIndex >= static_cast<int>(rows.size())) return;
    const qc::Result* row = rows[static_cast<size_t>(visibleIndex)];
    if (search::trim(row->source_rep_no).empty() || search::trim(row->mach_code).empty()) {
        MessageBoxW(hwnd, L"当前质控记录缺少报告号或仪器代码，无法跳转。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }
    auto* target = new RegularReportOpenTarget;
    target->rep_no = row->source_rep_no;
    target->oper_no = row->sample_no;
    target->inspect_date = row->inspect_date.empty() ? row->report_date : row->inspect_date;
    target->mach_code = row->mach_code;
    target->mach_name = row->mach_name;
    target->room_code = row->room_code;
    HWND regular = create_regular_report_module(st->ctx);
    if (!regular || !PostMessageW(regular, WM_REGULAR_OPEN_REPORT, 0, reinterpret_cast<LPARAM>(target))) {
        delete target;
        MessageBoxW(hwnd, L"常规报告页面打开失败。", WINDOW_TITLE, MB_ICONERROR);
    }
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<State*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            st = reinterpret_cast<State*>(mcs->lParam);
            SetPropW(hwnd, PROP_STATE, reinterpret_cast<HANDLE>(st));
            st->bgBrush = CreateSolidBrush(RGB(0xF3, 0xF6, 0xFA));
            st->startLabel = label(hwnd, L"开始日期");
            st->startDate = datePicker(hwnd, IDC_START_DATE);
            st->endLabel = label(hwnd, L"至");
            st->endDate = datePicker(hwnd, IDC_END_DATE);
            st->machLabel = label(hwnd, L"仪器");
            st->machCode = edit(hwnd, IDC_MACH_CODE);
            SendMessageW(st->machCode, EM_SETREADONLY, TRUE, 0);
            st->machPick = button(hwnd, IDC_MACH_PICK, L"...");
            st->itemLabel = label(hwnd, L"项目代码");
            st->itemCode = edit(hwnd, IDC_ITEM_CODE);
            st->levelLabel = label(hwnd, L"水平");
            st->level = combo(hwnd, IDC_LEVEL);
            setComboItems(st->level, {L"全部", L"L1", L"L2", L"L3"});
            st->statusFilterLabel = label(hwnd, L"状态");
            st->statusFilter = combo(hwnd, IDC_STATUS_FILTER);
            setComboItems(st->statusFilter, {L"全部", L"警告+失控", L"仅失控", L"仅警告"});
            st->queryButton = button(hwnd, IDC_QUERY, L"查询/刷新 LIS");
            st->chartButton = button(hwnd, IDC_LJ_CHART, L"L-J图");
            EnableWindow(st->chartButton, FALSE);
            st->exportButton = button(hwnd, IDC_EXPORT_CSV, L"导出CSV");
            EnableWindow(st->exportButton, FALSE);
            st->groups = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                         WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                         0, 0, 0, 0, hwnd, win32_control_id(IDC_GROUPS), GetModuleHandleW(nullptr), nullptr);
            st->cards = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                        0, 0, 0, 0, hwnd, win32_control_id(IDC_CARDS), GetModuleHandleW(nullptr), nullptr);
            st->details = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                          WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                          0, 0, 0, 0, hwnd, win32_control_id(IDC_DETAILS), GetModuleHandleW(nullptr), nullptr);
            st->status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                         0, 0, 0, 0, hwnd, win32_control_id(IDC_STATUS), GetModuleHandleW(nullptr), nullptr);
            setupList(st->groups, {{L"项目", 150}, {L"水平", 70}, {L"样本号", 80}, {L"点数", 60},
                                   {L"均值", 70}, {L"SD", 70}, {L"CV%", 70}, {L"基准", 60},
                                   {L"状态", 70}, {L"失控", 56}, {L"警告", 56}, {L"最近时间", 140}});
            setupList(st->cards, {{L"质控名称", 150}, {L"仪器", 160}, {L"样本号", 80}, {L"项目", 180},
                                  {L"水平", 60}, {L"最近结果", 100}, {L"最近时间", 140}, {L"状态", 80}});
            setupList(st->details, {{L"时间", 140}, {L"样本号", 90}, {L"报告号", 90}, {L"仪器", 160},
                                    {L"检验者", 90}, {L"项目", 180}, {L"水平", 60}, {L"结果", 90},
                                    {L"单位", 70}, {L"状态", 70}, {L"规则", 90}, {L"Z值", 70},
                                    {L"来源", 70}});
            setTodayRange(st);
            search::apply_font_to_children(hwnd, st->ctx.uiFont);
            resizeLayout(hwnd, st);
            {
                std::string error;
                if (!qc::ensure_store(error)) {
                    setStatus(st, L"本地质控库初始化失败：" + search::utf8_to_wide(error));
                } else {
                    setStatus(st, L"请选择检验仪器后点击“查询/刷新 LIS”。");
                }
            }
            return 0;
        }
        case WM_SIZE:
            resizeLayout(hwnd, st);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_QUERY) {
                runLisQuery(hwnd, st);
                return 0;
            }
            if (LOWORD(wp) == IDC_MACH_PICK) {
                showMachinePicker(hwnd, st);
                return 0;
            }
            if (LOWORD(wp) == IDC_EXPORT_CSV) {
                exportVisibleDetailsCsv(hwnd, st);
                return 0;
            }
            if (LOWORD(wp) == IDC_LJ_CHART) {
                openLeveyJenningsChart(hwnd, st);
                return 0;
            }
            if (LOWORD(wp) == IDC_STATUS_FILTER && HIWORD(wp) == CBN_SELCHANGE) {
                st->selectedGroup = -1;
                refreshVisibleResults(st);
                const auto rows = visibleDetails(st);
                setStatus(st, L"已应用状态筛选：当前显示 " + std::to_wstring(rows.size()) + L" 条质控明细。");
                return 0;
            }
            break;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (st && nm->idFrom == IDC_GROUPS && nm->code == LVN_ITEMCHANGED) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED)) {
                    st->selectedGroup = lv->iItem;
                    populateDetails(st);
                }
                return 0;
            }
            if (st && nm->idFrom == IDC_GROUPS && nm->code == NM_DBLCLK) {
                auto* item = reinterpret_cast<NMITEMACTIVATE*>(lp);
                if (item->iItem >= 0) st->selectedGroup = item->iItem;
                populateDetails(st);
                openLeveyJenningsChart(hwnd, st);
                return 0;
            }
            if (st && nm->idFrom == IDC_DETAILS && nm->code == NM_DBLCLK) {
                auto* item = reinterpret_cast<NMITEMACTIVATE*>(lp);
                openRegularReport(hwnd, st, item->iItem);
                return 0;
            }
            break;
        }
        case WM_QC_QUERY_DONE: {
            std::unique_ptr<QueryDone> done(reinterpret_cast<QueryDone*>(lp));
            st->busy = false;
            EnableWindow(st->queryButton, TRUE);
            if (!done->ok) {
                updateExportButton(st);
                updateChartButton(st);
                setStatus(st, L"查询失败：" + search::utf8_to_wide(done->error));
                MessageBoxW(hwnd, search::utf8_to_wide(done->error).c_str(), WINDOW_TITLE, MB_ICONERROR);
                return 0;
            }
            st->rows = std::move(done->rows);
            st->selectedGroup = -1;
            buildGroups(st, false);
            evaluateWestgardRules(st);
            refreshVisibleResults(st);
            int warningCount = 0;
            int outOfControlCount = 0;
            int evaluatedCount = 0;
            for (const auto& row : st->rows) {
                if (!row.qc_status.empty()) ++evaluatedCount;
                if (row.qc_status == "warning") ++warningCount;
                if (row.qc_status == "out_of_control") ++outOfControlCount;
            }
            std::wstring status = L"LIS 查询完成：返回 " + std::to_wstring(st->rows.size()) +
                                  L" 条；启用配置 " + std::to_wstring(done->config_count) +
                                  L" 条；已判定 " + std::to_wstring(evaluatedCount) +
                                  L" 条；警告 " + std::to_wstring(warningCount) +
                                  L" 条；失控 " + std::to_wstring(outOfControlCount) +
                                  L" 条；耗时 " + std::to_wstring(done->elapsed_ms) + L" ms";
            if (done->config_count == 0) {
                status = L"没有匹配的启用质控配置，请先在系统设置中维护仪器和固定样本号。";
            } else if (st->rows.empty()) {
                status += L"；当前日期范围内没有匹配 LIS 结果。";
            }
            setStatus(st, status);
            updateExportButton(st);
            updateChartButton(st);
            return 0;
        }
        case app::WM_APP_SETTINGS_CHANGED:
        case app::WM_APP_FONT_CHANGED:
            if (msg == app::WM_APP_FONT_CHANGED && lp) st->ctx.uiFont = reinterpret_cast<HFONT>(lp);
            search::apply_font_to_children(hwnd, st->ctx.uiFont);
            resizeLayout(hwnd, st);
            return 0;
        case WM_CTLCOLORSTATIC:
            SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
            return reinterpret_cast<LRESULT>(st ? st->bgBrush : nullptr);
        case WM_ERASEBKGND: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, st && st->bgBrush ? st->bgBrush : GetSysColorBrush(COLOR_BTNFACE));
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
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);
    registered = true;
}

}  // namespace

HWND create_quality_control_module(const ModuleContext& ctx) {
    if (HWND existing = activate_existing_mdi_child_by_title(ctx.mdiClient, WINDOW_TITLE)) return existing;
    registerClass(ctx.instance);
    auto* st = new State;
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
