#include "regular_report_tracking_window.h"

#ifdef _WIN32

#include "regular_report_tracking.h"
#include "regular_report_state.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>

namespace {

constexpr wchar_t kClass[] = L"RegularReportTrackingWindow";
enum { Machine = 7101, Patient, Days, Spin, Reviewed, Refresh, Reports, Results, Exit };
constexpr UINT_PTR kDaysTimer = 1;
constexpr UINT kDaysQueryDelayMs = 350;
constexpr int kDefaultWidth = 1000;
constexpr int kMinWidth = 980;
constexpr int kDefaultHeight = 800;
constexpr int kMinHeight = 580;
constexpr int kLeftListWidth = 310;
constexpr int kItemNameWidth = 260;
enum PatientField { NameField, AgeField, SexField, DepartmentField = 11, FieldCount = 18 };

struct State {
    HWND hwnd = nullptr, machine = nullptr, patient = nullptr, days = nullptr, spin = nullptr;
    HWND reviewed = nullptr, refresh = nullptr, reports = nullptr, results = nullptr;
    HWND status = nullptr, exit = nullptr, daysLabel = nullptr;
    std::array<HWND, FieldCount> labels{}, fields{};
    HFONT font = nullptr;
    regular_tracking::Options options;
    std::string preferred;
    std::vector<search::ReportRow> rows, allRows;
    std::vector<search::ResultRow> items;
    std::map<std::string, std::vector<search::ResultRow>> cache;
    std::string queryKey, range, itemsError, displayedReport;
    bool filling = false, snapshotReady = false;
    app::WindowTask listTask;
    ~State() { if (font) DeleteObject(font); }
};
struct ReportsResult {
    std::vector<search::ReportRow> rows;
    std::map<std::string, std::vector<search::ResultRow>> cache;
    std::string error, itemsError;
    bool ok = false;
};
State* state(HWND h) { return reinterpret_cast<State*>(GetWindowLongPtrW(h, GWLP_USERDATA)); }
int scaled(State* s, int n) { return regularS(s->hwnd, n); }
void text(HWND control, const std::string& value) { SetWindowTextW(control, search::utf8_to_wide(value).c_str()); }
HWND control(State* s, const wchar_t* cls, const wchar_t* title, int id, DWORD style, DWORD ex = 0) {
    HWND h = CreateWindowExW(ex, cls, title, WS_CHILD | WS_VISIBLE | style,
        0, 0, 0, 0, s->hwnd, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(s->font), TRUE);
    return h;
}
void cell(HWND list, int row, int col, const std::string& value) {
    auto w = search::utf8_to_wide(value);
    ListView_SetItemText(list, row, col, w.data());
}
void move(State* s, HWND h, int x, int y, int w, int height) {
    MoveWindow(h, scaled(s, x), scaled(s, y), scaled(s, std::max(w, 1)), scaled(s, std::max(height, 1)), TRUE);
}
void layout(State* s) {
    RECT rc{}; GetClientRect(s->hwnd, &rc);
    const float scale = search::dpi_scale_factor(s->hwnd);
    const int w = static_cast<int>(rc.right / scale), h = static_cast<int>(rc.bottom / scale);
    move(s, s->machine, 10, 10, 106, 28);
    move(s, s->patient, 125, 10, 110, 28);
    move(s, s->daysLabel, 244, 15, 74, 24);
    move(s, s->days, 320, 10, 56, 28);
    move(s, s->spin, 376, 10, 20, 28);
    move(s, s->reviewed, 412, 10, 94, 28);
    move(s, s->refresh, 522, 10, 76, 28);
    const int left = kLeftListWidth, rightX = left + 20;
    move(s, s->reports, 10, 50, left, h - 96);
    const int col = (w - rightX - 10) / 3;
    for (int i = 0; i < FieldCount; ++i) {
        const int x = rightX + (i % 3) * col, y = 50 + (i / 3) * 34;
        move(s, s->labels[i], x, y + 5, 68, 25);
        move(s, s->fields[i], x + 68, y, col - 76, 28);
    }
    move(s, s->results, rightX, 264, w - rightX - 10, h - 310);
    move(s, s->status, 10, h - 33, w - 122, 25);
    move(s, s->exit, w - 100, h - 36, 90, 28);
    ListView_SetColumnWidth(s->reports, 0, scaled(s, 90));
    ListView_SetColumnWidth(s->reports, 1, scaled(s, 82));
    ListView_SetColumnWidth(s->reports, 2, scaled(s, 112));
    ListView_SetColumnWidth(s->results, 1, scaled(s, kItemNameWidth));
}
void clearDetails(State* s) {
    s->items.clear(); s->displayedReport.clear();
    ListView_DeleteAllItems(s->results);
    for (auto field : s->fields) SetWindowTextW(field, L"");
}
void updateStatus(State* s) {
    text(s->status, "病人号：" + s->options.patient + "；" + s->range + "；报告 " + std::to_string(s->rows.size()) +
         " 份；当前报告项目 " + std::to_string(s->items.size()) + " 项");
}
void displayItems(State* s, const std::vector<search::ResultRow>& rows) {
    s->items = rows;
    SendMessageW(s->results, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(s->results);
    for (size_t i = 0; i < rows.size(); ++i) {
        auto number = std::to_wstring(i + 1);
        LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(i); item.pszText = number.data();
        const int index = ListView_InsertItem(s->results, &item);
        const auto& row = rows[i];
        cell(s->results, index, 1, row.item_name);
        std::string value = row.result;
        switch (search::result_row_tone(row)) {
            case search::ResultRowTone::High: value += " ↑"; break;
            case search::ResultRowTone::Low: value += " ↓"; break;
            default: break;
        }
        cell(s->results, index, 2, value);
        cell(s->results, index, 3, row.downbound);
        cell(s->results, index, 4, row.upbound);
        cell(s->results, index, 5, row.unit);
    }
    SendMessageW(s->results, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(s->results, nullptr, TRUE);
    updateStatus(s);
}
void selectReport(State* s) {
    const int index = ListView_GetNextItem(s->reports, -1, LVNI_SELECTED);
    if (index < 0 || index >= static_cast<int>(s->rows.size())) { clearDetails(s); return; }
    const auto& r = s->rows[index];
    if (r.rep_no == s->displayedReport) {
        if (s->cache.find(r.rep_no) != s->cache.end()) updateStatus(s);
        return; // Keep the current fields, result selection and scroll position intact.
    }
    clearDetails(s);
    s->preferred = r.rep_no; s->displayedReport = r.rep_no;
    const std::array<std::string, FieldCount> values = {r.name, r.age, r.sex, r.sample_name, r.bed_code, r.note,
        r.oper_no, r.requester, r.group_name, r.mach_name, r.rep_no, r.dept_name,
        r.reviewer, regularSlashDateTimeMinute(r.chk_date), r.req_doctor,
        regularSlashDateTimeMinute(r.create_time), r.diag_name, regularSlashDateTimeMinute(r.rep_time)};
    for (size_t i = 0; i < values.size(); ++i) text(s->fields[i], values[i]);
    auto found = s->cache.find(r.rep_no);
    if (found != s->cache.end()) { displayItems(s, found->second); return; }
    text(s->status, "项目结果加载失败：" + s->itemsError + "；点击刷新重试。");
}
// Rebuild only from the date-range snapshot; checkbox changes never start a task.
void applyLocalFilters(State* s) {
    auto options = s->options;
    options.current_machine = Button_GetCheck(s->machine) == BST_CHECKED;
    options.reviewed = Button_GetCheck(s->reviewed) == BST_CHECKED;
    if (s->listTask.active() || !s->snapshotReady) return; // Completion applies the latest checkbox state.
    s->rows = regular_tracking::filter_reports(s->allRows, options);
    s->filling = true;
    SendMessageW(s->reports, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(s->reports);
    int selected = 0;
    for (size_t i = 0; i < s->rows.size(); ++i) {
        const auto& r = s->rows[i];
        auto name = search::utf8_to_wide(r.mach_name.empty() ? r.mach_code : r.mach_name);
        LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(i); item.pszText = name.data();
        const int index = ListView_InsertItem(s->reports, &item);
        cell(s->reports, index, 1, r.rep_no);
        cell(s->reports, index, 2, regularSlashDateTimeMinute(r.rep_time));
        if (r.rep_no == s->preferred) selected = static_cast<int>(i);
    }
    if (!s->rows.empty()) {
        ListView_SetItemState(s->reports, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(s->reports, selected, FALSE);
    }
    SendMessageW(s->reports, WM_SETREDRAW, TRUE, 0); InvalidateRect(s->reports, nullptr, TRUE);
    s->filling = false;
    if (!s->rows.empty()) selectReport(s);
    else {
        clearDetails(s);
        text(s->status, "病人号：" + s->options.patient + "；" + s->range + "；没有符合条件的报告。");
    }
}
ReportsResult loadSnapshot(const search::QueryFilters& filters, app::WindowTaskContext task) {
    ReportsResult result;
    if (task.cancelled()) return result;
    result.ok = search::query_reports(filters, result.rows, result.error);
    if (!result.ok || result.rows.empty() || task.cancelled()) return result;
    std::vector<std::string> reports;
    std::set<std::string> seen;
    for (const auto& row : result.rows)
        if (seen.insert(row.rep_no).second) reports.push_back(row.rep_no);
    if (task.cancelled()) return result;
    std::vector<std::vector<search::ResultRow>> details;
    if (search::query_results_batch(filters.connection_string, reports, details, result.itemsError)) {
        for (size_t i = 0; i < reports.size(); ++i)
            result.cache.emplace(reports[i], std::move(details[i]));
    }
    return result;
}

int readDays(State* s) {
    wchar_t value[16]{};
    GetWindowTextW(s->days, value, 16);
    wchar_t* end = nullptr;
    const long days = wcstol(value, &end, 10);
    if (end == value || *end || days < regular_tracking::kMinDays || days > regular_tracking::kMaxDays)
        return 0;
    return static_cast<int>(days);
}

void query(State* s, bool refresh = false) {
    KillTimer(s->hwnd, kDaysTimer);
    const int days = readDays(s);
    if (!days) {
        text(s->status, "追踪天数请输入1至9999。"); return;
    }
    auto o = s->options; o.days = days;
    const SYSTEMTIME today = regularTodayDate();
    char date[16]{}; std::snprintf(date, sizeof(date), "%04u-%02u-%02u", today.wYear, today.wMonth, today.wDay);
    o.today = date;
    search::QueryFilters filters; std::string error;
    if (!regular_tracking::build_filters(o, filters, error)) { text(s->status, error); return; }
    const auto key = filters.start_date + "/" + filters.end_date;
    if (!refresh && key == s->queryKey) return;
    s->queryKey = key;
    s->range = filters.start_date + " 至 " + filters.end_date;
    s->listTask.cancel(); clearDetails(s);
    s->snapshotReady = false;
    s->cache.clear(); s->allRows.clear(); s->itemsError.clear();
    s->filling = true; ListView_DeleteAllItems(s->reports); s->filling = false; s->rows.clear();
    text(s->status, "正在查询病人号 " + o.patient + "；检验日期 " + filters.start_date + " 至 " + filters.end_date + "…");
    const HWND hwnd = s->hwnd;
    if (!s->listTask.start<ReportsResult>([filters](app::WindowTaskContext task) {
        return loadSnapshot(filters, task);
    }, [hwnd](std::optional<ReportsResult> result, std::exception_ptr exception) {
        auto* st = state(hwnd); if (!st) return;
        if (exception || !result || !result->ok) {
            st->queryKey.clear();
            text(st->status, "追踪查询失败：" + (result ? result->error : "后台任务异常") + "；点击刷新重试。"); return;
        }
        st->allRows = std::move(result->rows);
        st->cache = std::move(result->cache);
        st->itemsError = std::move(result->itemsError);
        st->snapshotReady = true;
        applyLocalFilters(st);
    })) { s->queryKey.clear(); text(s->status, "无法启动追踪查询，点击刷新重试。"); }
}
void createControls(State* s) {
    s->machine = control(s, L"BUTTON", L"当前仪器", Machine, BS_AUTOCHECKBOX | WS_TABSTOP);
    s->patient = control(s, L"BUTTON", L"病人号", Patient, BS_RADIOBUTTON);
    Button_SetCheck(s->patient, BST_CHECKED);
    EnableWindow(s->machine, !search::trim(s->options.origin_machine).empty());
    s->daysLabel = control(s, L"STATIC", L"追踪天数", 0, SS_LEFT);
    s->days = control(s, L"EDIT", L"30", Days, ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE);
    SendMessageW(s->days, EM_SETLIMITTEXT, 4, 0);
    s->spin = control(s, UPDOWN_CLASSW, L"", Spin, UDS_SETBUDDYINT | UDS_ARROWKEYS | UDS_NOTHOUSANDS);
    SendMessageW(s->spin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(s->days), 0);
    SendMessageW(s->spin, UDM_SETRANGE32, regular_tracking::kMinDays, regular_tracking::kMaxDays);
    SendMessageW(s->spin, UDM_SETPOS32, 0, regular_tracking::kDefaultDays);
    s->reviewed = control(s, L"BUTTON", L"已审核", Reviewed, BS_AUTOCHECKBOX | WS_TABSTOP);
    s->refresh = control(s, L"BUTTON", L"刷新(&R)", Refresh, BS_PUSHBUTTON | WS_TABSTOP);
    s->reports = control(s, WC_LISTVIEWW, L"", Reports, LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, WS_EX_CLIENTEDGE);
    s->results = control(s, WC_LISTVIEWW, L"", Results, LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, WS_EX_CLIENTEDGE);
    for (HWND list : {s->reports, s->results})
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    const wchar_t* reportColumns[] = {L"仪器", L"检验单号", L"确认时间"};
    for (int i = 0; i < 3; ++i) search::add_list_column(s->reports, i, reportColumns[i], scaled(s, 120));
    const wchar_t* resultColumns[] = {L"序号", L"项目名称", L"结果", L"下限", L"上限", L"单位"};
    const int widths[] = {44, kItemNameWidth, 110, 70, 70, 50};
    for (int i = 0; i < 6; ++i) search::add_list_column(s->results, i, resultColumns[i], scaled(s, widths[i]));
    const wchar_t* labels[] = {L"姓名", L"年龄", L"性别", L"标本", L"床号", L"备注", L"样本号", L"检验者", L"组合项目",
        L"检验仪器", L"检验单号", L"临床科室", L"审核者", L"申请日期", L"开单医生", L"上机时间", L"临床诊断", L"报告日期"};
    for (int i = 0; i < FieldCount; ++i) {
        s->labels[i] = control(s, L"STATIC", labels[i], 0, SS_LEFT);
        s->fields[i] = control(s, L"EDIT", L"", 0, ES_READONLY | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
    }
    s->status = control(s, L"STATIC", L"", 0, SS_LEFT | SS_ENDELLIPSIS);
    s->exit = control(s, L"BUTTON", L"退出(&Q)", Exit, WS_TABSTOP | BS_PUSHBUTTON);
}
LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* s = state(hwnd);
    switch (msg) {
        case WM_NCCREATE: {
            s = static_cast<std::unique_ptr<State>*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams)->release();
            s->hwnd = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s)); return TRUE;
        }
        case WM_CREATE: createControls(s); layout(s); query(s); return 0;
        case WM_SIZE: if (s && s->reports) layout(s); return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize = {regularS(hwnd, kMinWidth), regularS(hwnd, kMinHeight)}; return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == Exit || LOWORD(wp) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
            if (LOWORD(wp) == Refresh || LOWORD(wp) == IDOK) { query(s, true); return 0; }
            if ((LOWORD(wp) == Machine || LOWORD(wp) == Reviewed) && HIWORD(wp) == BN_CLICKED) { applyLocalFilters(s); return 0; }
            if (LOWORD(wp) == Days && HIWORD(wp) == EN_CHANGE && s->reports) { SetTimer(hwnd, kDaysTimer, kDaysQueryDelayMs, nullptr); return 0; }
            break;
        case WM_TIMER: if (wp == kDaysTimer) { query(s); return 0; } break;
        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lp);
            if (hdr->hwndFrom == s->spin && hdr->code == UDN_DELTAPOS) {
                const auto* delta = reinterpret_cast<NMUPDOWN*>(lp);
                const int current = readDays(s);
                const int base = current ? current : regular_tracking::kDefaultDays;
                const int next = std::clamp(base + delta->iDelta,
                    regular_tracking::kMinDays, regular_tracking::kMaxDays);
                SendMessageW(s->spin, UDM_SETPOS32, 0, next);
                SetWindowTextW(s->days, std::to_wstring(next).c_str());
                SetTimer(hwnd, kDaysTimer, kDaysQueryDelayMs, nullptr);
                return TRUE; // We synchronized buddy text and position; suppress a second increment.
            }
            if (hdr->hwndFrom == s->reports && hdr->code == LVN_ITEMCHANGED && !s->filling) {
                auto* item = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((item->uChanged & LVIF_STATE) && ((item->uOldState ^ item->uNewState) & LVIS_SELECTED)) selectReport(s);
            }
            if (hdr->hwndFrom == s->results && hdr->code == NM_CUSTOMDRAW) {
                auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const size_t i = static_cast<size_t>(cd->nmcd.dwItemSpec);
                    if (i < s->items.size()) {
                        auto color = regularResultTextColor(s->items[i]);
                        if (color != CLR_INVALID) cd->clrText = color;
                    }
                    return CDRF_NEWFONT;
                }
            }
            break;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
            if (s) {
                const HWND field = reinterpret_cast<HWND>(lp);
                if (field == s->fields[NameField] || field == s->fields[AgeField] || field == s->fields[SexField] || field == s->fields[DepartmentField]) {
                    const HDC dc = reinterpret_cast<HDC>(wp);
                    SetTextColor(dc, field == s->fields[DepartmentField] ? RGB(255, 0, 0) : RGB(0, 0, 255));
                    SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
                    return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
                }
            }
            break;
        case WM_DESTROY: KillTimer(hwnd, kDaysTimer); s->listTask.cancel(); return 0;
        case WM_NCDESTROY: SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); delete s; return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}  // namespace

void regularShowTrackingWindow(HWND owner, HFONT font, const std::string& connection,
                               const search::ReportRow& selected,
                               const std::string& originRoom, const std::string& originMachine) {
    if (search::trim(selected.reg_no).empty() || connection.empty()) {
        MessageBoxW(owner, L"当前报告缺少病人号或数据库连接，请先选择有效报告。", L"追踪", MB_ICONINFORMATION); return;
    }
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr); wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    auto owned = std::make_unique<State>();
    auto* s = owned.get();
    s->options.connection = connection; s->options.patient = search::trim(selected.reg_no);
    s->options.origin_room = originRoom; s->options.origin_machine = originMachine; s->preferred = selected.rep_no;
    LOGFONTW lf{};
    if (font && GetObjectW(font, sizeof(lf), &lf)) s->font = CreateFontIndirectW(&lf);
    if (!s->font) s->font = search::create_ui_font(9);
    HWND root = GetAncestor(owner, GA_ROOT);
    const auto title = search::utf8_to_wide("追踪 — " + selected.name + "（病人号：" + s->options.patient + "）");
    RECT work{}; MONITORINFO mi{}; mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi)) work = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = std::min(regularS(owner, kDefaultWidth), static_cast<int>(work.right - work.left));
    const int height = std::min(regularS(owner, kDefaultHeight), static_cast<int>(work.bottom - work.top));
    HWND hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, kClass, title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        work.left + (work.right - work.left - width) / 2, work.top + (work.bottom - work.top - height) / 2,
        width, height, root, nullptr, wc.hInstance, &owned);
    if (!hwnd) { MessageBoxW(owner, L"追踪窗口创建失败。", L"追踪", MB_ICONERROR); return; }
    const bool enabled = root && IsWindowEnabled(root);
    if (enabled) EnableWindow(root, FALSE);
    ShowWindow(hwnd, SW_SHOW); SetFocus(s->reports);
    MSG message{};
    while (IsWindow(hwnd)) {
        const int result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            if (IsWindow(hwnd)) DestroyWindow(hwnd);
            if (!result) PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (!IsDialogMessageW(hwnd, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (enabled && IsWindow(root)) { EnableWindow(root, TRUE); SetActiveWindow(root); SetFocus(owner); }
}
#endif
