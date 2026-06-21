#include "quality_control_module.h"

#ifdef _WIN32

#include "main_app.h"
#include "quality_control_import.h"
#include "quality_control_store.h"
#include "regular_report_module.h"
#include "resource.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"

#include <commctrl.h>
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"QualityControlModuleChild";
constexpr const wchar_t* WINDOW_TITLE = L"质控分析";
constexpr const wchar_t* PROP_STATE = L"QualityControlSt";
constexpr UINT WM_QC_QUERY_DONE = WM_APP + 0x681;
constexpr UINT WM_QC_IMPORT_DONE = WM_APP + 0x682;

enum ControlId {
    IDC_START_DATE = 6801,
    IDC_END_DATE = 6802,
    IDC_MACH_CODE = 6803,
    IDC_QUERY = 6804,
    IDC_IMPORT = 6805,
    IDC_GROUPS = 6806,
    IDC_CARDS = 6807,
    IDC_DETAILS = 6808,
    IDC_STATUS = 6809,
};

struct GroupRow {
    std::string key;
    std::string mach_code;
    std::string mach_name;
    std::string item_code;
    std::string item_name;
    std::string level;
    std::string qc_name;
    int count = 0;
    std::string latest_time;
    std::string latest_result;
};

struct State {
    ModuleContext ctx;
    HWND startLabel = nullptr;
    HWND startDate = nullptr;
    HWND endLabel = nullptr;
    HWND endDate = nullptr;
    HWND machLabel = nullptr;
    HWND machCode = nullptr;
    HWND queryButton = nullptr;
    HWND importButton = nullptr;
    HWND groups = nullptr;
    HWND cards = nullptr;
    HWND details = nullptr;
    HWND status = nullptr;
    HBRUSH bgBrush = nullptr;
    bool busy = false;
    std::vector<qc::Result> rows;
    std::vector<GroupRow> groupsRows;
    int selectedGroup = -1;
};

struct QueryDone {
    bool ok = false;
    std::string error;
    std::vector<qc::Result> rows;
    qc::ImportLog latest;
    bool hasLatest = false;
};

struct ImportDone {
    bool ok = false;
    std::string error;
    qc::ImportResult result;
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

void buildGroups(State* st) {
    st->groupsRows.clear();
    std::map<std::string, size_t> index;
    for (const auto& row : st->rows) {
        const std::string key = row.mach_code + "\x1f" + row.item_code + "\x1f" + row.level + "\x1f" + row.qc_name;
        auto found = index.find(key);
        if (found == index.end()) {
            index[key] = st->groupsRows.size();
            GroupRow group;
            group.key = key;
            group.mach_code = row.mach_code;
            group.mach_name = row.mach_name;
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
        setCell(st->groups, i, 2, std::to_string(row.count));
        setCell(st->groups, i, 3, row.latest_time);
    }
}

bool rowInSelectedGroup(const State* st, const qc::Result& row) {
    if (!st || st->selectedGroup < 0 || st->selectedGroup >= static_cast<int>(st->groupsRows.size())) return true;
    const auto& group = st->groupsRows[static_cast<size_t>(st->selectedGroup)];
    const std::string key = row.mach_code + "\x1f" + row.item_code + "\x1f" + row.level + "\x1f" + row.qc_name;
    return key == group.key;
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
        setCell(st->cards, out, 2, group.item_name);
        setCell(st->cards, out, 3, group.level);
        setCell(st->cards, out, 4, group.latest_result);
        setCell(st->cards, out, 5, group.latest_time);
        setCell(st->cards, out, 6, group.count > 0 ? "已导入" : "未导入");
        ++out;
    }
}

void populateDetails(State* st) {
    ListView_DeleteAllItems(st->details);
    int out = 0;
    for (const auto& row : st->rows) {
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
        setCell(st->details, out, 4, row.item_name);
        setCell(st->details, out, 5, row.level);
        setCell(st->details, out, 6, row.result_text);
        setCell(st->details, out, 7, row.unit);
        setCell(st->details, out, 8, row.imported_at);
        ++out;
    }
}

std::vector<const qc::Result*> visibleDetails(State* st) {
    std::vector<const qc::Result*> rows;
    if (!st) return rows;
    for (const auto& row : st->rows) {
        if (rowInSelectedGroup(st, row)) rows.push_back(&row);
    }
    return rows;
}

void resizeLayout(HWND hwnd, State* st) {
    if (!st) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int pad = S(hwnd, 10);
    const int topH = S(hwnd, 40);
    const int statusH = S(hwnd, 24);
    const int bottomH = std::max(S(hwnd, 170), static_cast<int>((rc.bottom - rc.top) / 3));
    const int leftW = std::max(S(hwnd, 260), static_cast<int>((rc.right - rc.left) / 4));
    int x = pad;
    MoveWindow(st->startLabel, x, pad, S(hwnd, 64), S(hwnd, 24), TRUE);
    MoveWindow(st->startDate, x + S(hwnd, 70), pad, S(hwnd, 120), S(hwnd, 24), TRUE);
    MoveWindow(st->endLabel, x + S(hwnd, 195), pad, S(hwnd, 20), S(hwnd, 24), TRUE);
    MoveWindow(st->endDate, x + S(hwnd, 220), pad, S(hwnd, 120), S(hwnd, 24), TRUE);
    MoveWindow(st->machLabel, x + S(hwnd, 345), pad, S(hwnd, 50), S(hwnd, 24), TRUE);
    MoveWindow(st->machCode, x + S(hwnd, 400), pad, S(hwnd, 120), S(hwnd, 24), TRUE);
    MoveWindow(st->queryButton, x + S(hwnd, 535), pad - S(hwnd, 2), S(hwnd, 80), S(hwnd, 28), TRUE);
    MoveWindow(st->importButton, x + S(hwnd, 625), pad - S(hwnd, 2), S(hwnd, 96), S(hwnd, 28), TRUE);
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
    query.mach_code = search::wide_to_utf8(editText(st->machCode));
    return query;
}

void runLocalQuery(HWND hwnd, State* st) {
    if (!st || st->busy) return;
    st->busy = true;
    EnableWindow(st->queryButton, FALSE);
    EnableWindow(st->importButton, FALSE);
    setStatus(st, L"正在查询本地质控数据...");
    const qc::Query query = buildQuery(st);
    std::thread([hwnd, query]() {
        auto* done = new QueryDone;
        done->ok = qc::query_results(query, done->rows, done->error) &&
                   qc::latest_import_log(done->latest, done->hasLatest, done->error);
        if (!PostMessageW(hwnd, WM_QC_QUERY_DONE, 0, reinterpret_cast<LPARAM>(done))) delete done;
    }).detach();
}

void runImport(HWND hwnd, State* st) {
    if (!st || st->busy) return;
    st->busy = true;
    EnableWindow(st->queryButton, FALSE);
    EnableWindow(st->importButton, FALSE);
    setStatus(st, L"正在从 LIS 同步质控结果...");
    qc::ImportRequest request;
    request.db_settings = st->ctx.dbSettings;
    request.start_date = dateText(st->startDate);
    request.end_date = dateText(st->endDate);
    request.mach_code = search::wide_to_utf8(editText(st->machCode));
    std::thread([hwnd, request]() {
        auto* done = new ImportDone;
        done->ok = qc::import_from_lis(request, done->result, done->error);
        if (!PostMessageW(hwnd, WM_QC_IMPORT_DONE, 0, reinterpret_cast<LPARAM>(done))) delete done;
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
            st->queryButton = button(hwnd, IDC_QUERY, L"查询");
            st->importButton = button(hwnd, IDC_IMPORT, L"同步/导入");
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
            setupList(st->groups, {{L"项目", 150}, {L"水平", 70}, {L"点数", 60}, {L"最近时间", 140}});
            setupList(st->cards, {{L"质控名称", 150}, {L"仪器", 160}, {L"项目", 180}, {L"水平", 60},
                                  {L"最近结果", 100}, {L"最近时间", 140}, {L"状态", 80}});
            setupList(st->details, {{L"时间", 140}, {L"样本号", 90}, {L"报告号", 90}, {L"仪器", 160},
                                    {L"项目", 180}, {L"水平", 60}, {L"结果", 90}, {L"单位", 70}, {L"导入时间", 140}});
            setTodayRange(st);
            search::apply_font_to_children(hwnd, st->ctx.uiFont);
            resizeLayout(hwnd, st);
            {
                std::string error;
                if (!qc::ensure_store(error)) {
                    setStatus(st, L"本地质控库初始化失败：" + search::utf8_to_wide(error));
                }
            }
            runLocalQuery(hwnd, st);
            return 0;
        }
        case WM_SIZE:
            resizeLayout(hwnd, st);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_QUERY) {
                runLocalQuery(hwnd, st);
                return 0;
            }
            if (LOWORD(wp) == IDC_IMPORT) {
                runImport(hwnd, st);
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
            EnableWindow(st->importButton, TRUE);
            if (!done->ok) {
                setStatus(st, L"查询失败：" + search::utf8_to_wide(done->error));
                MessageBoxW(hwnd, search::utf8_to_wide(done->error).c_str(), WINDOW_TITLE, MB_ICONERROR);
                return 0;
            }
            st->rows = std::move(done->rows);
            st->selectedGroup = -1;
            buildGroups(st);
            populateGroups(st);
            populateCards(st);
            populateDetails(st);
            std::wstring status = L"本地质控记录：" + std::to_wstring(st->rows.size()) + L" 条";
            if (done->hasLatest) {
                status += L"；最近同步：" + search::utf8_to_wide(done->latest.finished_at) +
                          L" 新增 " + std::to_wstring(done->latest.imported_count) +
                          L" 更新 " + std::to_wstring(done->latest.updated_count);
            }
            setStatus(st, status);
            return 0;
        }
        case WM_QC_IMPORT_DONE: {
            std::unique_ptr<ImportDone> done(reinterpret_cast<ImportDone*>(lp));
            st->busy = false;
            EnableWindow(st->queryButton, TRUE);
            EnableWindow(st->importButton, TRUE);
            if (!done->ok) {
                setStatus(st, L"同步失败：" + search::utf8_to_wide(done->error));
                MessageBoxW(hwnd, search::utf8_to_wide(done->error).c_str(), WINDOW_TITLE, MB_ICONERROR);
                return 0;
            }
            wchar_t buffer[160]{};
            swprintf(buffer, 160, L"同步完成：新增 %d，更新 %d，跳过 %d。",
                     done->result.imported_count, done->result.updated_count, done->result.skipped_count);
            setStatus(st, buffer);
            runLocalQuery(hwnd, st);
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
