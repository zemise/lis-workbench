#include "immune_duplicate_statistics_module.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "main_app.h"
#include "page_feedback.h"
#include "regular_report_module.h"
#include "resource.h"
#include "search_core.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"
#include "window_task.h"

#include <commctrl.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <cstring>
#include <exception>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"ImmuneDuplicateStatisticsModuleChild";
constexpr const wchar_t* CATALOG_WND_CLASS = L"ImmuneDuplicateItemCatalogWindow";
constexpr const wchar_t* WINDOW_TITLE = L"免疫重复项目统计";
constexpr const wchar_t* PROP_STATE = L"ImmuneDuplicateStatisticsSt";
constexpr const wchar_t* CONFIG_SECTION = L"ImmuneDuplicateStatistics";
constexpr const wchar_t* CONFIG_BASE_CODES = L"BaseYzxmidCodes";
constexpr const wchar_t* CONFIG_DUPLICATE_CODES = L"DuplicateYzxmidCodes";
constexpr int IDM_COPY_CELL = 9101;

enum ControlId {
    IDC_START_TIME = 6901,
    IDC_END_TIME = 6902,
    IDC_QUERY = 6903,
    IDC_SUMMARY = 6905,
    IDC_DETAILS = 6906,
    IDC_STATUS = 6907,
    IDC_BASE_CODES = 6908,
    IDC_DUPLICATE_CODES = 6909,
    IDC_CATALOG = 6910,
    IDC_CATALOG_LIST = 6951,
    IDC_CATALOG_ADD_BASE = 6952,
    IDC_CATALOG_ADD_DUPLICATE = 6953,
    IDC_CATALOG_CLOSE = 6954,
    IDC_PATIENTS = 6956,
    IDC_PATIENT_SEARCH = 6957,
    IDC_RELATION_FILTER = 6958,
    IDC_RULE_BASE_CODES = 6959,
    IDC_RULE_DUPLICATE_CODES = 6960,
    IDC_RULE_SAVE = 6961,
};

struct ListColumn {
    const wchar_t* title;
    int width;
};

constexpr ListColumn SUMMARY_COLUMNS[] = {
    {L"涉及患者", 180},
    {L"重复医嘱", 180},
    {L"同条码", 180},
    {L"跨条码", 180},
};

constexpr ListColumn PATIENT_COLUMNS[] = {
    {L"姓名", 100},
    {L"病人号", 120},
    {L"科室", 160},
    {L"床号", 70},
    {L"重复医嘱数", 100},
    {L"重复关系", 130},
    {L"最近签收时间", 150},
};

constexpr ListColumn DETAIL_COLUMNS[] = {
    {L"医嘱角色", 100},
    {L"项目名称", 330},
    {L"YZXMID", 110},
    {L"条码", 150},
    {L"签收时间", 160},
    {L"关系", 100},
};

using Summary = search::ImmuneDuplicateStatSummary;
using DetailRow = search::ImmuneDuplicateStatDetailRow;

struct PatientGroup {
    std::string inpatient_id;
    std::string patient_no;
    std::string name;
    std::string department;
    std::string bed_no;
    std::string latest_sign_time;
    int same_count = 0;
    int cross_count = 0;
    std::vector<size_t> row_indices;
};

struct ImmuneDuplicateState {
    ModuleContext ctx;
    HWND startTimeLabel = nullptr;
    HWND endTimeLabel = nullptr;
    HWND startDate = nullptr;
    HWND endDate = nullptr;
    HWND query = nullptr;
    HWND baseCodes = nullptr;
    HWND duplicateCodes = nullptr;
    HWND catalog = nullptr;
    HWND searchLabel = nullptr;
    HWND patientSearch = nullptr;
    HWND relationLabel = nullptr;
    HWND relationFilter = nullptr;
    HWND summaryList = nullptr;
    HWND patientsLabel = nullptr;
    HWND patients = nullptr;
    HWND detailsLabel = nullptr;
    HWND details = nullptr;
    HWND status = nullptr;
    HWND usageHint = nullptr;
    search::PageFeedback feedback;
    HBRUSH bgBrush = nullptr;
    app::WindowTask queryTask;
    app::WindowTask catalogTask;
    HWND catalogWindow = nullptr;
    bool querying = false;
    bool cataloging = false;
    Summary summary;
    std::vector<DetailRow> rows;
    std::vector<PatientGroup> patientGroups;
    std::vector<size_t> visiblePatientIndices;
    std::vector<int> comparisonRowIndices;
};

struct ImmuneDuplicateQueryResult {
    bool ok = false;
    std::string error;
    Summary summary;
    std::vector<DetailRow> rows;
};

struct ImmuneDuplicateCatalogResult {
    bool ok = false;
    std::string error;
    std::vector<search::ImmuneDuplicateItemCatalogRow> rows;
};

struct CatalogWindowState {
    HWND owner = nullptr;
    HWND list = nullptr;
    HWND status = nullptr;
    HWND baseLabel = nullptr;
    HWND duplicateLabel = nullptr;
    HWND baseCodes = nullptr;
    HWND duplicateCodes = nullptr;
    std::vector<search::ImmuneDuplicateItemCatalogRow> rows;
};

int S(HWND hwnd, int value) {
    return static_cast<int>(value * search::dpi_scale_factor(hwnd));
}

HWND label(HWND parent, const wchar_t* text, int x, int y, int w, int h, DWORD align = SS_RIGHT) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | align,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND dateTimePicker(HWND parent, int id, int x, int y, int w, int h) {
    HWND hwnd = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATECENTURYFORMAT,
                                x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
    DateTime_SetFormat(hwnd, L"yyyy-MM-dd HH:mm");
    return hwnd;
}

void setRelativeDay(HWND hwnd, int dayOffset, bool endOfDay) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    FILETIME fileTime{};
    if (dayOffset != 0 && SystemTimeToFileTime(&st, &fileTime)) {
        ULARGE_INTEGER value{};
        value.LowPart = fileTime.dwLowDateTime;
        value.HighPart = fileTime.dwHighDateTime;
        constexpr ULONGLONG ticksPerDay = 24ULL * 60ULL * 60ULL * 10000000ULL;
        if (dayOffset < 0) value.QuadPart -= static_cast<ULONGLONG>(-dayOffset) * ticksPerDay;
        else value.QuadPart += static_cast<ULONGLONG>(dayOffset) * ticksPerDay;
        fileTime.dwLowDateTime = value.LowPart;
        fileTime.dwHighDateTime = value.HighPart;
        FileTimeToSystemTime(&fileTime, &st);
    }
    st.wHour = endOfDay ? 23 : 0;
    st.wMinute = endOfDay ? 59 : 0;
    st.wSecond = 0;
    st.wMilliseconds = 0;
    DateTime_SetSystemtime(hwnd, GDT_VALID, &st);
}

std::string dateTimeText(HWND hwnd) {
    SYSTEMTIME st{};
    if (DateTime_GetSystemtime(hwnd, &st) != GDT_VALID) return "";
    char buf[32]{};
    sprintf_s(buf, "%04u-%02u-%02u %02u:%02u:%02u",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

void setStatus(ImmuneDuplicateState* st, const std::wstring& text) {
    if (st) search::set_page_status(st->feedback, text);
}

void setCellUtf8(HWND list, int row, int col, const std::string& text) {
    const auto wide = search::utf8_to_wide(text);
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(wide.c_str()));
}

void initList(HWND list, const ListColumn* columns, int count) {
    for (int i = 0; i < count; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.fmt = LVCFMT_LEFT;
        col.cx = columns[i].width;
        col.pszText = const_cast<wchar_t*>(columns[i].title);
        ListView_InsertColumn(list, i, &col);
    }
}

std::wstring windowText(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    std::wstring value(static_cast<size_t>((std::max)(0, length)) + 1, L'\0');
    if (length > 0) GetWindowTextW(hwnd, value.data(), length + 1);
    value.resize(static_cast<size_t>((std::max)(0, length)));
    return value;
}

std::optional<std::wstring> normalizeCodeText(const std::wstring& input) {
    std::vector<std::wstring> codes;
    std::wstring token;
    auto flush = [&]() {
        if (token.empty()) return;
        if (std::find(codes.begin(), codes.end(), token) == codes.end()) codes.push_back(token);
        token.clear();
    };
    for (wchar_t ch : input) {
        if (ch >= L'0' && ch <= L'9') {
            token.push_back(ch);
        } else if (ch == L';' || ch == L',' || ch == L'|' || ch == L'/' || iswspace(ch)) {
            flush();
        } else {
            return std::nullopt;
        }
    }
    flush();
    if (codes.empty()) return std::nullopt;
    std::wstring normalized;
    for (const auto& code : codes) {
        if (!normalized.empty()) normalized += L';';
        normalized += code;
    }
    return normalized;
}

std::set<std::wstring> codeSet(const std::wstring& normalized) {
    std::set<std::wstring> result;
    size_t start = 0;
    while (start <= normalized.size()) {
        const size_t end = normalized.find(L';', start);
        const std::wstring code = normalized.substr(start, end == std::wstring::npos ? end : end - start);
        if (!code.empty()) result.insert(code);
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return result;
}

bool validateAndSaveCodes(HWND owner, ImmuneDuplicateState* st, bool showMessage) {
    if (!st) return false;
    const auto base = normalizeCodeText(windowText(st->baseCodes));
    const auto duplicate = normalizeCodeText(windowText(st->duplicateCodes));
    if (!base || !duplicate) {
        if (showMessage) {
            MessageBoxW(owner, L"基准项目和重复项目都必须填写数字 YZXMID；多个代码用分号分隔。",
                        WINDOW_TITLE, MB_ICONWARNING);
        }
        return false;
    }
    const auto baseSet = codeSet(*base);
    const auto duplicateSet = codeSet(*duplicate);
    for (const auto& code : baseSet) {
        if (duplicateSet.count(code) != 0) {
            if (showMessage) {
                MessageBoxW(owner, (L"YZXMID " + code + L" 不能同时作为基准项目和重复项目。").c_str(),
                            WINDOW_TITLE, MB_ICONWARNING);
            }
            return false;
        }
    }
    SetWindowTextW(st->baseCodes, base->c_str());
    SetWindowTextW(st->duplicateCodes, duplicate->c_str());
    search::save_module_str(CONFIG_SECTION, CONFIG_BASE_CODES, *base);
    search::save_module_str(CONFIG_SECTION, CONFIG_DUPLICATE_CODES, *duplicate);
    return true;
}

bool addCatalogCode(HWND owner, HWND target, HWND other, const std::wstring& code) {
    if (!target || !other || code.empty()) return false;
    auto targetValue = normalizeCodeText(windowText(target));
    auto otherValue = normalizeCodeText(windowText(other));
    const auto otherCodes = otherValue ? codeSet(*otherValue) : std::set<std::wstring>{};
    if (otherCodes.count(code) != 0) {
        MessageBoxW(owner, (L"YZXMID " + code + L" 已存在于另一组配置中。").c_str(),
                    WINDOW_TITLE, MB_ICONWARNING);
        return false;
    }
    std::wstring value = targetValue.value_or(L"");
    if (codeSet(value).count(code) == 0) {
        if (!value.empty()) value += L';';
        value += code;
        SetWindowTextW(target, value.c_str());
    }
    return true;
}

void setSummaryValue(HWND list, int col, int value) {
    const auto text = std::to_wstring(value);
    ListView_SetItemText(list, 0, col, const_cast<wchar_t*>(text.c_str()));
}

int summaryValue(const Summary& summary, int col) {
    switch (col) {
        case 0: return summary.duplicate_patient_count;
        case 1: return summary.duplicate_item_count;
        case 2: return summary.same_barcode_duplicate_count;
        case 3: return summary.cross_barcode_duplicate_count;
        default: return 0;
    }
}

void populateSummary(ImmuneDuplicateState* st) {
    if (!st || !st->summaryList) return;
    ListView_DeleteAllItems(st->summaryList);
    const std::wstring firstValue = std::to_wstring(summaryValue(st->summary, 0));
    LVITEMW valueItem{};
    valueItem.mask = LVIF_TEXT;
    valueItem.iItem = 0;
    valueItem.pszText = const_cast<wchar_t*>(firstValue.c_str());
    ListView_InsertItem(st->summaryList, &valueItem);
    for (int i = 1; i < static_cast<int>(std::size(SUMMARY_COLUMNS)); ++i) {
        setSummaryValue(st->summaryList, i, summaryValue(st->summary, i));
    }
}

void populateDetails(ImmuneDuplicateState* st, const PatientGroup* group) {
    if (!st || !st->details) return;
    SendMessageW(st->details, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(st->details);
    st->comparisonRowIndices.clear();
    if (!group || group->row_indices.empty()) {
        SetWindowTextW(st->detailsLabel,
            L"医嘱对照｜选择上方患者后查看　绿色=已有基准　蓝色=同条码重复　橙色=跨条码重复");
        SendMessageW(st->details, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(st->details, nullptr, TRUE);
        return;
    }
    const std::wstring context = L"医嘱对照｜" + search::utf8_to_wide(group->name) + L" / " +
        search::utf8_to_wide(group->patient_no) + L" / " + search::utf8_to_wide(group->department) +
        L"　　绿色=已有基准　蓝色=同条码重复　橙色=跨条码重复";
    SetWindowTextW(st->detailsLabel, context.c_str());
    const auto addRow = [&](const std::vector<std::string>& cells, int sourceIndex) {
        const int index = ListView_GetItemCount(st->details);
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = index;
        const auto first = search::utf8_to_wide(cells[0]);
        item.pszText = const_cast<wchar_t*>(first.c_str());
        ListView_InsertItem(st->details, &item);
        for (int col = 1; col < static_cast<int>(cells.size()); ++col)
            setCellUtf8(st->details, index, col, cells[static_cast<size_t>(col)]);
        st->comparisonRowIndices.push_back(sourceIndex);
    };
    const auto& first = st->rows[group->row_indices.front()];
    addRow({"已有基准医嘱", first.base_order_text, first.base_item_code,
            first.base_barcode, first.base_sign_time, "—"}, -1);
    for (size_t rowIndex : group->row_indices) {
        const auto& row = st->rows[rowIndex];
        addRow({"重复医嘱", row.duplicate_item_name, row.duplicate_item_code,
                row.duplicate_barcode, row.duplicate_sign_time, row.relation},
               static_cast<int>(rowIndex));
    }
    SendMessageW(st->details, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(st->details, nullptr, TRUE);
}

void buildPatientGroups(ImmuneDuplicateState* st) {
    st->patientGroups.clear();
    std::map<std::string, size_t> positions;
    for (size_t i = 0; i < st->rows.size(); ++i) {
        const auto& row = st->rows[i];
        const std::string key = !row.inpatient_id.empty()
            ? row.inpatient_id
            : row.patient_no + "\n" + row.name + "\n" + row.department;
        auto inserted = positions.emplace(key, st->patientGroups.size());
        if (inserted.second) {
            PatientGroup group;
            group.inpatient_id = row.inpatient_id;
            group.patient_no = row.patient_no;
            group.name = row.name;
            group.department = row.department;
            group.bed_no = row.bed_no;
            st->patientGroups.push_back(std::move(group));
        }
        auto& group = st->patientGroups[inserted.first->second];
        group.row_indices.push_back(i);
        if (row.relation == "同条码") ++group.same_count;
        else ++group.cross_count;
        if (row.duplicate_sign_time > group.latest_sign_time)
            group.latest_sign_time = row.duplicate_sign_time;
    }
    std::stable_sort(st->patientGroups.begin(), st->patientGroups.end(), [](const auto& a, const auto& b) {
        return a.latest_sign_time > b.latest_sign_time;
    });
}

bool containsInsensitive(const std::string& value, const std::wstring& needle) {
    std::wstring wide = search::utf8_to_wide(value);
    std::transform(wide.begin(), wide.end(), wide.begin(), towlower);
    return wide.find(needle) != std::wstring::npos;
}

void populatePatients(ImmuneDuplicateState* st) {
    if (!st || !st->patients) return;
    std::wstring searchText = windowText(st->patientSearch);
    std::transform(searchText.begin(), searchText.end(), searchText.begin(), towlower);
    const int relation = ComboBox_GetCurSel(st->relationFilter);
    st->visiblePatientIndices.clear();
    ListView_DeleteAllItems(st->patients);
    for (size_t i = 0; i < st->patientGroups.size(); ++i) {
        const auto& group = st->patientGroups[i];
        if (relation == 1 && group.same_count == 0) continue;
        if (relation == 2 && group.cross_count == 0) continue;
        if (!searchText.empty() &&
            !containsInsensitive(group.name, searchText) &&
            !containsInsensitive(group.patient_no, searchText) &&
            !containsInsensitive(group.department, searchText)) continue;
        const int rowIndex = ListView_GetItemCount(st->patients);
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = rowIndex;
        const auto name = search::utf8_to_wide(group.name);
        item.pszText = const_cast<wchar_t*>(name.c_str());
        ListView_InsertItem(st->patients, &item);
        setCellUtf8(st->patients, rowIndex, 1, group.patient_no);
        setCellUtf8(st->patients, rowIndex, 2, group.department);
        setCellUtf8(st->patients, rowIndex, 3, group.bed_no);
        setCellUtf8(st->patients, rowIndex, 4, std::to_string(group.row_indices.size()));
        std::string relationText;
        if (group.same_count) relationText = "同条码 " + std::to_string(group.same_count);
        if (group.cross_count) {
            if (!relationText.empty()) relationText += " / ";
            relationText += "跨条码 " + std::to_string(group.cross_count);
        }
        setCellUtf8(st->patients, rowIndex, 5, relationText);
        setCellUtf8(st->patients, rowIndex, 6, group.latest_sign_time);
        st->visiblePatientIndices.push_back(i);
    }
    const std::wstring patientTitle = L"患者结果｜当前显示 " +
        std::to_wstring(st->visiblePatientIndices.size()) + L" / " +
        std::to_wstring(st->patientGroups.size()) + L" 位（每位患者一行；橙色表示存在跨条码重复）";
    SetWindowTextW(st->patientsLabel, patientTitle.c_str());
    if (!st->visiblePatientIndices.empty()) {
        ListView_SetItemState(st->patients, 0, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        populateDetails(st, &st->patientGroups[st->visiblePatientIndices.front()]);
    } else {
        populateDetails(st, nullptr);
    }
}

bool copyTextToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        CloseClipboard();
        return false;
    }
    void* ptr = GlobalLock(mem);
    if (!ptr) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }
    memcpy(ptr, text.c_str(), bytes);
    GlobalUnlock(mem);
    if (!SetClipboardData(CF_UNICODETEXT, mem)) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

void showCellContextMenu(HWND hwnd, ImmuneDuplicateState* st, HWND list,
                         const ListColumn* columns, int columnCount) {
    if (!st || !list) return;
    POINT screenPt{};
    GetCursorPos(&screenPt);
    POINT listPt = screenPt;
    ScreenToClient(list, &listPt);

    LVHITTESTINFO hit{};
    hit.pt = listPt;
    const int row = ListView_SubItemHitTest(list, &hit);
    if (row < 0 || hit.iSubItem < 0 || hit.iSubItem >= columnCount) return;

    ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);

    wchar_t buffer[2048]{};
    ListView_GetItemText(list, row, hit.iSubItem, buffer, static_cast<int>(std::size(buffer)));
    const std::wstring text(buffer);
    const std::wstring menuLabel = search::copy_menu_label(text);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, IDM_COPY_CELL, menuLabel.c_str());
    const UINT command = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                        screenPt.x, screenPt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (command != IDM_COPY_CELL) return;

    if (copyTextToClipboard(hwnd, text)) {
        setStatus(st, L"已复制单元格：" + std::wstring(columns[hit.iSubItem].title));
    } else {
        setStatus(st, L"复制单元格失败。");
    }
}

void addSelectedCatalogItem(CatalogWindowState* ps, bool asBase) {
    if (!ps || !ps->list || !IsWindow(ps->owner)) return;
    const int selected = ListView_GetNextItem(ps->list, -1, LVNI_SELECTED);
    if (selected < 0 || selected >= static_cast<int>(ps->rows.size())) {
        SetWindowTextW(ps->status, L"请先选择一条项目记录。");
        return;
    }
    const auto code = search::utf8_to_wide(ps->rows[static_cast<size_t>(selected)].item_code);
    if (!addCatalogCode(ps->owner,
                        asBase ? ps->baseCodes : ps->duplicateCodes,
                        asBase ? ps->duplicateCodes : ps->baseCodes,
                        code)) return;
    const std::wstring message =
        (asBase ? L"已加入基准项目：YZXMID " : L"已加入重复项目：YZXMID ") + code;
    SetWindowTextW(ps->status, message.c_str());
}

bool saveCatalogRules(HWND hwnd, CatalogWindowState* ps) {
    if (!ps || !IsWindow(ps->owner)) return false;
    const auto base = normalizeCodeText(windowText(ps->baseCodes));
    const auto duplicate = normalizeCodeText(windowText(ps->duplicateCodes));
    if (!base || !duplicate) {
        MessageBoxW(hwnd, L"基准项目和重复项目都必须填写数字 YZXMID；多个代码用分号分隔。",
                    WINDOW_TITLE, MB_ICONWARNING);
        return false;
    }
    for (const auto& code : codeSet(*base)) {
        if (codeSet(*duplicate).count(code) != 0) {
            MessageBoxW(hwnd, (L"YZXMID " + code + L" 不能同时属于两组规则。").c_str(),
                        WINDOW_TITLE, MB_ICONWARNING);
            return false;
        }
    }
    auto* st = reinterpret_cast<ImmuneDuplicateState*>(GetPropW(ps->owner, PROP_STATE));
    if (!st) return false;
    SetWindowTextW(st->baseCodes, base->c_str());
    SetWindowTextW(st->duplicateCodes, duplicate->c_str());
    validateAndSaveCodes(hwnd, st, true);
    setStatus(st, L"统计规则已保存；下次查询将使用新的项目范围。");
    return true;
}

LRESULT CALLBACK catalogWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ps = reinterpret_cast<CatalogWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            ps = reinterpret_cast<CatalogWindowState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ps));
            auto* ownerState = reinterpret_cast<ImmuneDuplicateState*>(GetPropW(ps->owner, PROP_STATE));
            ps->baseLabel = label(hwnd, L"基准项目代码：", 0, 0, 0, 0);
            ps->baseCodes = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                ownerState ? windowText(ownerState->baseCodes).c_str() : L"12731",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_RULE_BASE_CODES), GetModuleHandleW(nullptr), nullptr);
            ps->duplicateLabel = label(hwnd, L"重复项目：", 0, 0, 0, 0);
            ps->duplicateCodes = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                ownerState ? windowText(ownerState->duplicateCodes).c_str() : L"8714;8724;7345",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_RULE_DUPLICATE_CODES), GetModuleHandleW(nullptr), nullptr);
            ps->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_CATALOG_LIST), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(ps->list,
                LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            constexpr ListColumn columns[] = {
                {L"YZXMID", 110}, {L"医嘱名称（SQNR）", 390},
                {L"记录数", 90}, {L"最近签收时间", 160},
            };
            initList(ps->list, columns, static_cast<int>(std::size(columns)));
            for (int i = 0; i < static_cast<int>(ps->rows.size()); ++i) {
                const auto& row = ps->rows[static_cast<size_t>(i)];
                LVITEMW item{};
                item.mask = LVIF_TEXT;
                item.iItem = i;
                const auto code = search::utf8_to_wide(row.item_code);
                item.pszText = const_cast<wchar_t*>(code.c_str());
                ListView_InsertItem(ps->list, &item);
                setCellUtf8(ps->list, i, 1, row.order_text);
                setCellUtf8(ps->list, i, 2, row.record_count);
                setCellUtf8(ps->list, i, 3, row.last_sign_time);
            }
            search::create_button(hwnd, IDC_CATALOG_ADD_BASE, L"加入基准项目", 0, 0, 0, 0);
            search::create_button(hwnd, IDC_CATALOG_ADD_DUPLICATE, L"加入重复项目", 0, 0, 0, 0);
            search::create_button(hwnd, IDC_RULE_SAVE, L"保存规则", 0, 0, 0, 0);
            search::create_button(hwnd, IDC_CATALOG_CLOSE, L"关闭", 0, 0, 0, 0);
            ps->status = label(hwnd, L"下表来自当前检索时间段；项目名称用于识别，实际只按 YZXMID 匹配。",
                               0, 0, 0, 0, SS_LEFT);
            search::apply_font_to_children(hwnd, ownerState ? ownerState->ctx.uiFont : nullptr);
            return 0;
        }
        case WM_SIZE:
            if (ps) {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                const int pad = S(hwnd, 10);
                const int buttonY = rc.bottom - S(hwnd, 42);
                MoveWindow(GetDlgItem(hwnd, IDC_RULE_BASE_CODES), S(hwnd, 118), pad,
                           S(hwnd, 250), S(hwnd, 25), TRUE);
                MoveWindow(GetDlgItem(hwnd, IDC_RULE_DUPLICATE_CODES), S(hwnd, 118), pad + S(hwnd, 34),
                           S(hwnd, 360), S(hwnd, 25), TRUE);
                MoveWindow(ps->baseLabel, pad, pad + S(hwnd, 2), S(hwnd, 102), S(hwnd, 23), TRUE);
                MoveWindow(ps->duplicateLabel, pad, pad + S(hwnd, 36), S(hwnd, 102), S(hwnd, 23), TRUE);
                const int listY = pad + S(hwnd, 70);
                MoveWindow(ps->list, pad, listY, (std::max)(0, static_cast<int>(rc.right) - pad * 2),
                           (std::max)(80, buttonY - listY - S(hwnd, 30)), TRUE);
                MoveWindow(GetDlgItem(hwnd, IDC_CATALOG_ADD_BASE), pad, buttonY,
                           S(hwnd, 112), S(hwnd, 28), TRUE);
                MoveWindow(GetDlgItem(hwnd, IDC_CATALOG_ADD_DUPLICATE), pad + S(hwnd, 122), buttonY,
                           S(hwnd, 112), S(hwnd, 28), TRUE);
                MoveWindow(GetDlgItem(hwnd, IDC_RULE_SAVE), rc.right - pad - S(hwnd, 160), buttonY,
                           S(hwnd, 80), S(hwnd, 28), TRUE);
                MoveWindow(GetDlgItem(hwnd, IDC_CATALOG_CLOSE), rc.right - pad - S(hwnd, 70), buttonY,
                           S(hwnd, 70), S(hwnd, 28), TRUE);
                MoveWindow(ps->status, pad, buttonY - S(hwnd, 27),
                           (std::max)(0, static_cast<int>(rc.right) - pad * 2), S(hwnd, 22), TRUE);
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_CATALOG_ADD_BASE) {
                addSelectedCatalogItem(ps, true);
                return 0;
            }
            if (LOWORD(wp) == IDC_CATALOG_ADD_DUPLICATE) {
                addSelectedCatalogItem(ps, false);
                return 0;
            }
            if (LOWORD(wp) == IDC_CATALOG_CLOSE) {
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wp) == IDC_RULE_SAVE) {
                if (saveCatalogRules(hwnd, ps)) DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_NOTIFY:
            if (ps) {
                auto* nm = reinterpret_cast<NMHDR*>(lp);
                if (nm->idFrom == IDC_CATALOG_LIST && nm->code == NM_DBLCLK) {
                    addSelectedCatalogItem(ps, false);
                    return 0;
                }
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (ps) {
                if (IsWindow(ps->owner)) {
                    auto* ownerState = reinterpret_cast<ImmuneDuplicateState*>(GetPropW(ps->owner, PROP_STATE));
                    if (ownerState && ownerState->catalogWindow == hwnd) ownerState->catalogWindow = nullptr;
                }
                delete ps;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void showCatalogWindow(HWND owner, ImmuneDuplicateState* st,
                       std::vector<search::ImmuneDuplicateItemCatalogRow> rows) {
    if (!st) return;
    if (st->catalogWindow && IsWindow(st->catalogWindow)) {
        SetForegroundWindow(st->catalogWindow);
        return;
    }
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = catalogWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = CATALOG_WND_CLASS;
    RegisterClassExW(&wc);

    auto* ps = new CatalogWindowState();
    ps->owner = owner;
    ps->rows = std::move(rows);
    HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW, CATALOG_WND_CLASS,
        L"免疫重复项目 · 统计规则设置", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, S(owner, 820), S(owner, 580),
        owner, nullptr, GetModuleHandleW(nullptr), ps);
    if (!popup) {
        delete ps;
        MessageBoxW(owner, L"无法打开 YZXMID 项目记录窗口。", WINDOW_TITLE, MB_ICONERROR);
        return;
    }
    st->catalogWindow = popup;
    ShowWindow(popup, SW_SHOW);
    UpdateWindow(popup);
}

void resizeLayout(HWND hwnd, ImmuneDuplicateState* st) {
    if (!st) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const int pad = S(hwnd, 10);
    const int labelGap = S(hwnd, 6);
    const int controlGap = S(hwnd, 8);
    const int groupGap = S(hwnd, 18);
    const int controlH = S(hwnd, 25);
    const int labelYOffset = S(hwnd, 2);
    const int firstRow = S(hwnd, 9);
    const int secondRow = S(hwnd, 42);
    const int hintRow = S(hwnd, 73);
    const int topH = S(hwnd, 102);
    const int summaryH = S(hwnd, 62);

    int x = pad;
    int labelW = search::measure_control_text_width(hwnd, st->startTimeLabel, 72);
    MoveWindow(st->startTimeLabel, x, firstRow + labelYOffset, labelW, controlH, TRUE);
    x += labelW + labelGap;
    MoveWindow(st->startDate, x, firstRow, S(hwnd, 172), controlH, TRUE);
    x += S(hwnd, 172) + controlGap;
    labelW = search::measure_control_text_width(hwnd, st->endTimeLabel, 20);
    MoveWindow(st->endTimeLabel, x, firstRow + labelYOffset, labelW, controlH, TRUE);
    x += labelW + controlGap;
    MoveWindow(st->endDate, x, firstRow, S(hwnd, 172), controlH, TRUE);
    x += S(hwnd, 172) + groupGap;
    MoveWindow(st->query, x, firstRow - S(hwnd, 1), S(hwnd, 112), S(hwnd, 27), TRUE);
    x += S(hwnd, 112) + controlGap;
    MoveWindow(st->catalog, x, firstRow - S(hwnd, 1), S(hwnd, 82), S(hwnd, 27), TRUE);
    x += S(hwnd, 82) + groupGap;
    MoveWindow(st->status, x, firstRow + labelYOffset,
               (std::max)(0, w - x - pad), controlH, TRUE);

    x = pad;
    labelW = search::measure_control_text_width(hwnd, st->relationLabel, 72);
    MoveWindow(st->relationLabel, x, secondRow + labelYOffset, labelW, controlH, TRUE);
    x += labelW + labelGap;
    MoveWindow(st->relationFilter, x, secondRow, S(hwnd, 118), S(hwnd, 120), TRUE);
    x += S(hwnd, 118) + groupGap;
    labelW = search::measure_control_text_width(hwnd, st->searchLabel, 46);
    MoveWindow(st->searchLabel, x, secondRow + labelYOffset, labelW, controlH, TRUE);
    x += labelW + labelGap;
    MoveWindow(st->patientSearch, x, secondRow, S(hwnd, 260), controlH, TRUE);
    MoveWindow(st->usageHint, pad, hintRow, (std::max)(0, w - pad * 2), S(hwnd, 22), TRUE);

    MoveWindow(st->summaryList, pad, topH, (std::max)(0, w - pad * 2), summaryH, TRUE);
    const int labelH = S(hwnd, 22);
    const int contentTop = topH + summaryH + pad;
    const int available = (std::max)(S(hwnd, 260), h - contentTop - pad);
    const int patientH = (std::max)(S(hwnd, 120), (available - labelH * 2 - pad) * 45 / 100);
    MoveWindow(st->patientsLabel, pad, contentTop, (std::max)(0, w - pad * 2), labelH, TRUE);
    MoveWindow(st->patients, pad, contentTop + labelH,
               (std::max)(0, w - pad * 2), patientH, TRUE);
    const int detailsLabelY = contentTop + labelH + patientH + pad;
    MoveWindow(st->detailsLabel, pad, detailsLabelY, (std::max)(0, w - pad * 2), labelH, TRUE);
    MoveWindow(st->details, pad, detailsLabelY + labelH,
               (std::max)(0, w - pad * 2),
               (std::max)(S(hwnd, 100), h - detailsLabelY - labelH - pad), TRUE);
    search::layout_page_feedback(st->feedback);
}

void finishQuery(ImmuneDuplicateState* st, ImmuneDuplicateQueryResult result) {
    if (!st) return;
    st->querying = false;
    EnableWindow(st->query, TRUE);
    search::hide_page_activity(st->feedback);
    if (!result.ok) {
        search::show_page_alert(st->feedback,
            L"查询失败：" + search::utf8_to_wide(result.error));
        return;
    }

    st->summary = result.summary;
    st->rows = std::move(result.rows);
    buildPatientGroups(st);
    populateSummary(st);
    populatePatients(st);

    wchar_t status[320]{};
    if (st->summary.base_barcode_count == 0) {
        swprintf(status, std::size(status), L"当前范围内未发现重复医嘱（没有匹配到基准医嘱）。");
    } else if (st->summary.duplicate_item_count == 0) {
        swprintf(status, std::size(status), L"当前范围内未发现重复医嘱。");
    } else {
        swprintf(status, std::size(status),
                 L"查询完成：%d 位患者存在重复医嘱，共 %d 项重复医嘱。",
                 st->summary.duplicate_patient_count,
                 st->summary.duplicate_item_count);
    }
    std::wstring statusText(status);
    if (st->summary.missing_base_barcode_count > 0) {
        statusText += L" 无条码基准申请 " +
                      std::to_wstring(st->summary.missing_base_barcode_count) + L" 条。";
    }
    if (st->summary.unmatched_inpatient_count > 0) {
        statusText += L" 未匹配住院信息 " +
                      std::to_wstring(st->summary.unmatched_inpatient_count) + L" 次。";
    }
    setStatus(st, statusText);
}

void runQuery(HWND hwnd, ImmuneDuplicateState* st) {
    if (!st || st->querying) return;
    if (!validateAndSaveCodes(hwnd, st, true)) return;
    const auto connection = search::build_connection_string_w(st->ctx.dbSettings);
    if (connection.empty()) {
        MessageBoxW(hwnd, L"请先在系统设置中配置数据库连接。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }

    search::ImmuneDuplicateStatQuery query;
    query.connection_string = search::wide_to_utf8(connection);
    query.start_time = dateTimeText(st->startDate);
    query.end_time = dateTimeText(st->endDate);
    query.base_item_codes = search::wide_to_utf8(windowText(st->baseCodes));
    query.duplicate_item_codes = search::wide_to_utf8(windowText(st->duplicateCodes));
    if (query.start_time > query.end_time) {
        MessageBoxW(hwnd, L"签收开始时间不能晚于结束时间。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }

    st->querying = true;
    EnableWindow(st->query, FALSE);
    setStatus(st, L"正在查询重复医嘱...");
    search::show_page_activity(st->feedback, L"正在查找重复医嘱，请稍候…");

    const bool queued = st->queryTask.start<ImmuneDuplicateQueryResult>(
        [query] {
            ImmuneDuplicateQueryResult result;
            result.ok = search::query_immune_duplicate_statistics(
                query, result.summary, result.rows, result.error);
            return result;
        },
        [hwnd](std::optional<ImmuneDuplicateQueryResult> result,
               std::exception_ptr error) {
            auto* state = reinterpret_cast<ImmuneDuplicateState*>(
                GetPropW(hwnd, PROP_STATE));
            if (!state) return;
            if (error || !result) {
                state->querying = false;
                EnableWindow(state->query, TRUE);
                search::hide_page_activity(state->feedback);
                search::show_page_alert(state->feedback,
                    L"免疫重复统计查询发生后台任务异常。");
                return;
            }
            finishQuery(state, std::move(*result));
        });
    if (!queued) {
        st->querying = false;
        EnableWindow(st->query, TRUE);
        search::hide_page_activity(st->feedback);
        search::show_page_alert(st->feedback, L"无法启动免疫重复统计后台查询。");
    }
}

void runCatalogQuery(HWND hwnd, ImmuneDuplicateState* st) {
    if (!st || st->cataloging) return;
    if (st->catalogWindow && IsWindow(st->catalogWindow)) {
        SetForegroundWindow(st->catalogWindow);
        return;
    }
    const auto connection = search::build_connection_string_w(st->ctx.dbSettings);
    if (connection.empty()) {
        MessageBoxW(hwnd, L"请先在系统设置中配置数据库连接。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }
    search::ImmuneDuplicateItemCatalogQuery query;
    query.connection_string = search::wide_to_utf8(connection);
    query.start_time = dateTimeText(st->startDate);
    query.end_time = dateTimeText(st->endDate);
    if (query.start_time > query.end_time) {
        MessageBoxW(hwnd, L"签收开始时间不能晚于结束时间。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }

    st->cataloging = true;
    EnableWindow(st->catalog, FALSE);
    setStatus(st, L"正在打开统计规则并读取当前时间段的项目记录...");
    const bool queued = st->catalogTask.start<ImmuneDuplicateCatalogResult>(
        [query] {
            ImmuneDuplicateCatalogResult result;
            result.ok = search::query_immune_duplicate_item_catalog(
                query, result.rows, result.error);
            return result;
        },
        [hwnd](std::optional<ImmuneDuplicateCatalogResult> result, std::exception_ptr error) {
            auto* state = reinterpret_cast<ImmuneDuplicateState*>(GetPropW(hwnd, PROP_STATE));
            if (!state) return;
            state->cataloging = false;
            EnableWindow(state->catalog, TRUE);
            if (error || !result) {
                search::show_page_alert(state->feedback, L"读取 YZXMID 项目记录时发生后台任务异常。");
                return;
            }
            if (!result->ok) {
                search::show_page_alert(state->feedback,
                    L"读取 YZXMID 项目记录失败：" + search::utf8_to_wide(result->error));
                showCatalogWindow(hwnd, state, {});
                return;
            }
            if (result->rows.empty()) {
                showCatalogWindow(hwnd, state, {});
                setStatus(state, L"当前时间段没有项目记录，仍可手工编辑并保存规则。");
            } else {
                const size_t count = result->rows.size();
                showCatalogWindow(hwnd, state, std::move(result->rows));
                setStatus(state, L"规则设置已打开，并读取 " + std::to_wstring(count) + L" 条项目记录。");
            }
        });
    if (!queued) {
        st->cataloging = false;
        EnableWindow(st->catalog, TRUE);
        search::show_page_alert(st->feedback, L"无法启动 YZXMID 项目记录查询。");
    }
}

void openRegularReportForRow(HWND owner, ImmuneDuplicateState* st, int index) {
    if (!st || index < 0 || index >= static_cast<int>(st->rows.size())) return;
    const auto& row = st->rows[static_cast<size_t>(index)];
    if (search::trim(row.report_no).empty() || search::trim(row.machine_code).empty() ||
        search::trim(row.inspect_date).empty()) {
        MessageBoxW(owner, L"当前明细缺少报告号、仪器或日期，无法跳转到常规报告。", WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }

    auto* target = new RegularReportOpenTarget;
    target->rep_no = search::trim(row.report_no);
    target->oper_no = search::trim(row.duplicate_sample_no);
    target->inspect_date = search::trim(row.inspect_date);
    target->mach_code = search::trim(row.machine_code);
    target->mach_name = search::trim(row.machine_name);
    target->room_code = search::trim(row.room_code);

    HWND regular = create_regular_report_module(st->ctx);
    if (!regular || !PostMessageW(regular, WM_REGULAR_OPEN_REPORT, 0, reinterpret_cast<LPARAM>(target))) {
        delete target;
        MessageBoxW(owner, L"常规报告页面打开失败。", WINDOW_TITLE, MB_ICONERROR);
    }
}

LRESULT drawResultList(ImmuneDuplicateState* st, NMLVCUSTOMDRAW* draw) {
    if (!st || !draw) return CDRF_DODEFAULT;
    const UINT_PTR id = draw->nmcd.hdr.idFrom;
    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
        if (id == IDC_SUMMARY) return CDRF_NOTIFYSUBITEMDRAW;
        if ((draw->nmcd.uItemState & CDIS_SELECTED) != 0) return CDRF_DODEFAULT;
        const size_t row = static_cast<size_t>(draw->nmcd.dwItemSpec);
        if (id == IDC_PATIENTS && row < st->visiblePatientIndices.size()) {
            const auto& group = st->patientGroups[st->visiblePatientIndices[row]];
            if (group.cross_count > 0 && group.same_count > 0) {
                draw->clrTextBk = RGB(255, 238, 226);
            } else if (group.cross_count > 0) {
                draw->clrTextBk = RGB(255, 244, 224);
            } else {
                draw->clrTextBk = RGB(235, 243, 253);
            }
        } else if (id == IDC_DETAILS && row < st->comparisonRowIndices.size()) {
            const int source = st->comparisonRowIndices[row];
            if (source < 0) {
                draw->clrTextBk = RGB(232, 247, 237);
                draw->clrText = RGB(28, 101, 63);
            } else if (source < static_cast<int>(st->rows.size()) &&
                       st->rows[static_cast<size_t>(source)].relation == "同条码") {
                draw->clrTextBk = RGB(235, 243, 253);
                draw->clrText = RGB(39, 82, 132);
            } else {
                draw->clrTextBk = RGB(255, 244, 224);
                draw->clrText = RGB(145, 80, 18);
            }
        }
        return CDRF_DODEFAULT;
    }
    if (id == IDC_SUMMARY &&
        draw->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
        switch (draw->iSubItem) {
            case 0:
                draw->clrTextBk = RGB(232, 241, 251);
                draw->clrText = RGB(32, 78, 126);
                break;
            case 1:
                draw->clrTextBk = RGB(253, 235, 232);
                draw->clrText = RGB(151, 54, 45);
                break;
            case 2:
                draw->clrTextBk = RGB(235, 243, 253);
                draw->clrText = RGB(39, 82, 132);
                break;
            case 3:
                draw->clrTextBk = RGB(255, 244, 224);
                draw->clrText = RGB(145, 80, 18);
                break;
        }
        return CDRF_DODEFAULT;
    }
    return CDRF_DODEFAULT;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<ImmuneDuplicateState*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            st = reinterpret_cast<ImmuneDuplicateState*>(mcs->lParam);
            SetPropW(hwnd, PROP_STATE, st);
            st->bgBrush = CreateSolidBrush(RGB(0xF0, 0xF0, 0xF0));

            st->startTimeLabel = label(hwnd, L"签收时间：", 0, 0, 0, 0);
            st->startDate = dateTimePicker(hwnd, IDC_START_TIME, S(hwnd, 92), S(hwnd, 10), S(hwnd, 172), S(hwnd, 24));
            st->endTimeLabel = label(hwnd, L"至", 0, 0, 0, 0, SS_CENTER);
            st->endDate = dateTimePicker(hwnd, IDC_END_TIME, S(hwnd, 300), S(hwnd, 10), S(hwnd, 172), S(hwnd, 24));
            setRelativeDay(st->startDate, -6, false);
            setRelativeDay(st->endDate, 0, true);
            st->baseCodes = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                search::load_module_str(CONFIG_SECTION, CONFIG_BASE_CODES, L"12731").c_str(),
                WS_CHILD | ES_AUTOHSCROLL,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_BASE_CODES), GetModuleHandleW(nullptr), nullptr);
            st->duplicateCodes = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                search::load_module_str(CONFIG_SECTION, CONFIG_DUPLICATE_CODES, L"8714;8724;7345").c_str(),
                WS_CHILD | ES_AUTOHSCROLL,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_DUPLICATE_CODES), GetModuleHandleW(nullptr), nullptr);
            st->query = search::create_button(hwnd, IDC_QUERY, L"查询重复医嘱", 0, 0, 0, 0);
            st->catalog = search::create_button(hwnd, IDC_CATALOG, L"规则设置", 0, 0, 0, 0);
            st->status = label(hwnd, L"请选择签收时间，然后查询重复医嘱。", 0, 0, 0, 0, SS_LEFT);

            st->relationLabel = label(hwnd, L"重复关系：", 0, 0, 0, 0);
            st->relationFilter = CreateWindowExW(0, WC_COMBOBOXW, L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_RELATION_FILTER), GetModuleHandleW(nullptr), nullptr);
            ComboBox_AddString(st->relationFilter, L"全部重复");
            ComboBox_AddString(st->relationFilter, L"同条码");
            ComboBox_AddString(st->relationFilter, L"跨条码");
            ComboBox_SetCurSel(st->relationFilter, 0);
            st->searchLabel = label(hwnd, L"患者：", 0, 0, 0, 0);
            st->patientSearch = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_PATIENT_SEARCH), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(st->patientSearch, EM_SETCUEBANNER, TRUE,
                         reinterpret_cast<LPARAM>(L"姓名 / 病人号 / 科室"));
            st->usageHint = label(hwnd,
                L"使用步骤：① 选择签收时间　② 点击“查询重复医嘱”　③ 选择患者，核对下方基准与重复医嘱",
                0, 0, 0, 0, SS_LEFT);

            st->summaryList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_SUMMARY), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->summaryList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            initList(st->summaryList, SUMMARY_COLUMNS, static_cast<int>(std::size(SUMMARY_COLUMNS)));

            st->patientsLabel = label(hwnd, L"患者结果｜查询后每位患者显示一行", 0, 0, 0, 0, SS_LEFT);
            st->patients = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_PATIENTS), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->patients,
                LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            initList(st->patients, PATIENT_COLUMNS, static_cast<int>(std::size(PATIENT_COLUMNS)));

            st->detailsLabel = label(hwnd,
                                     L"医嘱对照｜绿色=已有基准　蓝色=同条码重复　橙色=跨条码重复",
                                     0, 0, 0, 0, SS_LEFT);
            st->details = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_DETAILS), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->details, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            initList(st->details, DETAIL_COLUMNS, static_cast<int>(std::size(DETAIL_COLUMNS)));

            search::initialize_page_feedback(st->feedback, hwnd, st->status,
                                             st->patients, st->ctx.uiFont);
            search::add_page_tooltip(st->feedback, st->query,
                                     L"查询当前签收时间范围内的重复医嘱，并按患者归组展示。");
            search::add_page_tooltip(st->feedback, st->catalog,
                                     L"设置哪些项目作为基准、哪些项目视为重复；项目代码默认隐藏在此处。");
            search::add_page_tooltip(st->feedback, st->patients,
                                     L"每位涉及患者只显示一行；选中后在下方查看医嘱对照。");
            search::add_page_tooltip(st->feedback, st->details,
                                     L"先显示已有基准医嘱，再显示该患者的重复医嘱；右键可复制。");

            search::apply_font_to_children(hwnd, st->ctx.uiFont);
            populateSummary(st);
            resizeLayout(hwnd, st);
            return 0;
        }
        case WM_SIZE:
            resizeLayout(hwnd, st);
            return 0;
        case WM_COMMAND:
            if (st && search::handle_page_feedback_command(st->feedback, lp, WINDOW_TITLE)) return 0;
            if (LOWORD(wp) == IDC_QUERY) {
                runQuery(hwnd, st);
                return 0;
            }
            if (LOWORD(wp) == IDC_CATALOG) {
                runCatalogQuery(hwnd, st);
                return 0;
            }
            if (st && LOWORD(wp) == IDC_PATIENT_SEARCH && HIWORD(wp) == EN_CHANGE) {
                populatePatients(st);
                return 0;
            }
            if (st && LOWORD(wp) == IDC_RELATION_FILTER && HIWORD(wp) == CBN_SELCHANGE) {
                populatePatients(st);
                return 0;
            }
            break;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (st && nm->code == NM_CUSTOMDRAW &&
                (nm->idFrom == IDC_SUMMARY || nm->idFrom == IDC_PATIENTS ||
                 nm->idFrom == IDC_DETAILS)) {
                return drawResultList(st, reinterpret_cast<NMLVCUSTOMDRAW*>(lp));
            }
            if (st && nm->idFrom == IDC_PATIENTS && nm->code == LVN_ITEMCHANGED) {
                auto* item = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((item->uNewState & LVIS_SELECTED) && item->iItem >= 0 &&
                    item->iItem < static_cast<int>(st->visiblePatientIndices.size())) {
                    populateDetails(st, &st->patientGroups[
                        st->visiblePatientIndices[static_cast<size_t>(item->iItem)]]);
                }
                return 0;
            }
            if (st && nm->idFrom == IDC_DETAILS && nm->code == NM_RCLICK) {
                showCellContextMenu(hwnd, st, st->details, DETAIL_COLUMNS,
                                    static_cast<int>(std::size(DETAIL_COLUMNS)));
                return 0;
            }
            if (st && nm->idFrom == IDC_PATIENTS && nm->code == NM_RCLICK) {
                showCellContextMenu(hwnd, st, st->patients, PATIENT_COLUMNS,
                                    static_cast<int>(std::size(PATIENT_COLUMNS)));
                return 0;
            }
            if (st && nm->idFrom == IDC_DETAILS && nm->code == NM_DBLCLK) {
                auto* item = reinterpret_cast<NMITEMACTIVATE*>(lp);
                if (item->iItem >= 0 && item->iItem < static_cast<int>(st->comparisonRowIndices.size())) {
                    const int source = st->comparisonRowIndices[static_cast<size_t>(item->iItem)];
                    if (source >= 0) openRegularReportForRow(hwnd, st, source);
                }
                return 0;
            }
            break;
        }
        case app::WM_APP_SETTINGS_CHANGED:
        case app::WM_APP_FONT_CHANGED:
            if (st) {
                if (msg == app::WM_APP_FONT_CHANGED && lp) st->ctx.uiFont = reinterpret_cast<HFONT>(lp);
                search::apply_font_to_children(hwnd, st->ctx.uiFont);
                resizeLayout(hwnd, st);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        case WM_CTLCOLORSTATIC:
            if (st) {
                LRESULT result = 0;
                if (search::page_feedback_static_color(
                        st->feedback, reinterpret_cast<HDC>(wp),
                        reinterpret_cast<HWND>(lp), result)) return result;
                const HWND control = reinterpret_cast<HWND>(lp);
                if (control == st->usageHint) {
                    SetTextColor(reinterpret_cast<HDC>(wp), RGB(45, 91, 135));
                } else if (control == st->patientsLabel || control == st->detailsLabel) {
                    SetTextColor(reinterpret_cast<HDC>(wp), RGB(31, 91, 67));
                }
            }
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
                RemovePropW(hwnd, PROP_STATE);
                st->queryTask.cancel();
                st->catalogTask.cancel();
                if (st->catalogWindow && IsWindow(st->catalogWindow)) DestroyWindow(st->catalogWindow);
                search::destroy_page_feedback(st->feedback);
                if (st->bgBrush) DeleteObject(st->bgBrush);
                delete st;
            }
            return 0;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_immune_duplicate_statistics_module(const ModuleContext& ctx) {
    if (HWND existing = activate_existing_mdi_child_by_title(ctx.mdiClient, WINDOW_TITLE)) {
        return existing;
    }
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = ctx.instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(ctx.instance, MAKEINTRESOURCEW(IDI_APP));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = WND_CLASS;
    RegisterClassExW(&wc);

    auto* st = new ImmuneDuplicateState();
    st->ctx = ctx;

    MDICREATESTRUCTW mcs{};
    mcs.szTitle = WINDOW_TITLE;
    mcs.szClass = WND_CLASS;
    mcs.hOwner = ctx.instance;
    mcs.x = CW_USEDEFAULT;
    mcs.y = CW_USEDEFAULT;
    mcs.cx = CW_USEDEFAULT;
    mcs.cy = CW_USEDEFAULT;
    mcs.lParam = reinterpret_cast<LPARAM>(st);
    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&mcs)));
    if (!child) {
        delete st;
        MessageBoxW(ctx.mdiClient, L"免疫重复项目统计窗口创建失败。", WINDOW_TITLE, MB_ICONERROR);
        return nullptr;
    }
    SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    return child;
}

#endif
