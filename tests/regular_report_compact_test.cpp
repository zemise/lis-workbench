#include "regular_report_module.h"
#include "regular_report_state.h"
#include "main_app.h"

#include <cstdlib>
#include <iostream>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
struct ControlSnapshot { HWND hwnd; RECT rect; LONG_PTR exStyle; };
std::vector<ControlSnapshot> normalLayout;
RECT relativeRect(HWND hwnd) {
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    MapWindowPoints(nullptr, GetParent(hwnd), reinterpret_cast<POINT*>(&rect), 2);
    return rect;
}
void checkRestoredLayout() {
    for (const auto& saved : normalLayout) {
        RECT actual = relativeRect(saved.hwnd);
        check(EqualRect(&actual, &saved.rect) != FALSE, "normal-page control position and size restored");
        check(GetWindowLongPtrW(saved.hwnd, GWL_EXSTYLE) == saved.exStyle, "original control styles restored");
    }
}
LRESULT CALLBACK frameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return DefFrameProcW(hwnd, GetDlgItem(hwnd, 1), msg, wp, lp);
}
void enter(RegularReportState* st) {
    SendMessageW(st->compactButton, BM_CLICK, 0, 0);
    check(st->compactWindow && st->compactAttached, "entered compact mode");
}
void restored(RegularReportState* st, HWND middle, HWND right, HWND results, HWND reports, int split) {
    check(!st->compactWindow && !st->compactAttached, "compact mode ended");
    check(st->middlePanel == middle && st->rightPanel == right &&
          st->resultList == results && st->reportList == reports, "original controls preserved");
    check(GetParent(middle) == st->hwnd && GetParent(right) == st->hwnd &&
          GetParent(st->splitter) == st->hwnd, "all panels returned to MDI child");
    check(st->splitterX == split, "full-page divider preserved");
    check(IsWindowVisible(GetAncestor(st->hwnd, GA_ROOT)), "main frame restored");
    checkRestoredLayout();
    check((GetWindowLongPtrW(st->status, GWL_STYLE) & WS_VISIBLE) != 0,
          "normal-page result status restored");
    for (int column = 0; column < REGULAR_REPORT_COLUMN_COUNT; ++column)
        check(ListView_GetColumnWidth(reports, column) > 0, "full-page columns restored");
    check(ListView_GetItemCount(results) == 1 && ListView_GetItemCount(reports) == 1 &&
          ListView_GetNextItem(reports, -1, LVNI_SELECTED) == 0, "data and selection preserved");
}
}

int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES | ICC_DATE_CLASSES};
    check(InitCommonControlsEx(&controls) != FALSE, "common controls initialized");
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = frameProc;
    wc.lpszClassName = L"RegularReportCompactTestFrame";
    check(RegisterClassW(&wc) != 0, "test frame registered");
    HWND frame = CreateWindowW(wc.lpszClassName, L"Report window tests", WS_OVERLAPPEDWINDOW,
                               0, 0, 1440, 900, nullptr, nullptr, instance, nullptr);
    CLIENTCREATESTRUCT client{};
    HWND mdi = CreateWindowW(L"MDICLIENT", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                             0, 0, 1420, 840, frame, reinterpret_cast<HMENU>(1), instance, &client);
    check(frame && mdi, "MDI fixture created");
    ShowWindow(frame, SW_SHOW);
    HWND other = CreateWindowW(L"STATIC", L"Other application window", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               10, 10, 240, 120, nullptr, nullptr, instance, nullptr);
    check(other != nullptr, "additional application window created");
    ModuleContext context;
    context.instance = instance;
    context.mdiClient = mdi;
    context.uiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HWND report = create_regular_report_module(context);
    auto* st = reinterpret_cast<RegularReportState*>(GetPropW(report, REGULAR_REPORT_PROP_STATE));
    check(st != nullptr, "report module created");
    // Keep the fixture offline: do not dispatch the initial machine timer or query on selection.
    KillTimer(report, IDT_REPORT_INITIAL_QUICK_MACHINE);
    st->initialQuickMachineTimerActive = false;
    st->suppressReportSelectionQuery = true;
    search::ReportRow row;
    row.id = "synthetic";
    row.rep_no = "synthetic";
    row.oper_no = "123";
    row.name = "Test Patient";
    st->reportRows.push_back(row);
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.pszText = const_cast<wchar_t*>(L"Synthetic row");
    check(ListView_InsertItem(st->reportList, &item) == 0, "report row inserted");
    check(ListView_InsertItem(st->resultList, &item) == 0, "result row inserted");
    ListView_SetItemState(st->reportList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    st->selectedReportIndex = 0;
    HWND middle = st->middlePanel, right = st->rightPanel;
    HWND results = st->resultList, reports = st->reportList;
    const int split = st->splitterX;
    const int queryGeneration = st->reportQueryGeneration;
    ListView_SetColumnWidth(reports, 0, 44);
    ListView_SetColumnWidth(reports, 5, 165);
    ListView_SetColumnWidth(reports, 6, 91);
    std::vector<int> fullWidths;
    for (int column = 0; column < REGULAR_REPORT_COLUMN_COUNT; ++column)
        fullWidths.push_back(ListView_GetColumnWidth(reports, column));
    for (HWND control : {middle, right, results, reports, st->rightTab, st->rightSearchEdit,
                         st->rightDateTodayButton, st->rightDatePrevButton, st->rightDateNextButton,
                         st->rightAutoRefreshEdit})
        normalLayout.push_back({control, relativeRect(control), GetWindowLongPtrW(control, GWL_EXSTYLE)});
    RECT entry{}, audit{};
    GetWindowRect(st->compactButton, &entry);
    GetWindowRect(GetDlgItem(st->bottomPanel, 5410), &audit);
    check(entry.left == audit.left && entry.top > audit.top, "entry below audit-print button");
    enter(st);
    HWND floating = st->compactWindow;
    check(!IsWindowVisible(frame) && !IsWindowVisible(other), "only compact window remains visible");
    check((GetWindowLongPtrW(floating, GWL_EXSTYLE) & WS_EX_APPWINDOW) != 0 &&
          GetWindow(floating, GW_OWNER) == nullptr, "compact host has its own taskbar window");
    for (int column = 0; column < REGULAR_REPORT_COLUMN_COUNT; ++column)
        check((ListView_GetColumnWidth(reports, column) > 0) == (column >= 1 && column < 6), "only sample/name/sex/age/order columns visible");
    ListView_SetColumnWidth(reports, 0, 80);
    check(ListView_GetColumnWidth(reports, 0) == 0, "label column stays hidden");
    check((GetWindowLongPtrW(st->status, GWL_STYLE) & WS_VISIBLE) == 0,
          "compact result status hidden");
    ListView_SetColumnWidth(reports, 6, 80);
    check(ListView_GetColumnWidth(reports, 6) == 0, "hidden columns cannot be resized open");
    check((GetWindowLongPtrW(st->rightSearchEdit, GWL_STYLE) & WS_VISIBLE) == 0,
          "right-side search toolbar hidden");
    RECT nextDay{}, refresh{}, reportRect{}, rightRect{};
    GetWindowRect(st->rightDateNextButton, &nextDay);
    GetWindowRect(st->compactRefreshButton, &refresh);
    check(refresh.top == nextDay.top && refresh.left > nextDay.right, "refresh follows next-day button");
    GetWindowRect(reports, &reportRect);
    GetWindowRect(right, &rightRect);
    check(reportRect.top - rightRect.top < regularS(right, 20), "report list fills removed statistics area");
    RECT today{};
    GetWindowRect(st->rightDateTodayButton, &today);
    check(reportRect.right - reportRect.left == refresh.right - today.left,
          "report list width equals the complete date/refresh button row");
    MINMAXINFO limits{};
    SendMessageW(floating, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
    check(limits.ptMinTrackSize.x < regularS(floating, 800), "compact minimum width reduced");
    SetWindowPos(floating, nullptr, 0, 0, limits.ptMinTrackSize.x, limits.ptMinTrackSize.y,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT narrowed{};
    GetWindowRect(reports, &narrowed);
    check(narrowed.right - narrowed.left == reportRect.right - reportRect.left,
          "right pane stays at button-row width when compact window shrinks");
    check((GetWindowLongPtrW(floating, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0, "floating window is topmost");
    check(GetParent(middle) == floating && GetParent(right) == floating &&
          GetParent(st->splitter) == floating, "panels detached into floating host");
    check((GetWindowLongPtrW(st->leftPanel, GWL_STYLE) & WS_VISIBLE) == 0 &&
          (GetWindowLongPtrW(st->bottomPanel, GWL_STYLE) & WS_VISIBLE) == 0, "full-page extras hidden");
    check(st->reportQueryGeneration == queryGeneration, "switching did not launch a report query");
    check(regularWindowText(st->compactSummary).find(L"Test Patient") != std::wstring::npos,
          "current patient appears in summary");
    check(create_regular_report_module(context) == report && st->compactWindow == floating,
          "reopening module reuses existing floating window");
    check(st->splitterX == split, "compact resize does not change the full-page divider");
    check((GetWindowLongPtrW(st->splitter, GWL_STYLE) & WS_VISIBLE) == 0,
          "fixed-width compact pane has no draggable divider");
    MSG key{};
    key.hwnd = reports;
    key.message = WM_KEYDOWN;
    SendMessageW(report, app::WM_APP_FONT_CHANGED, 0, reinterpret_cast<LPARAM>(context.uiFont));
    check(st->compactWindow == floating && st->resultList == results, "font broadcast preserves detached controls");
    SendMessageW(floating, WM_CLOSE, 0, 0);
    restored(st, middle, right, results, reports, split);
    check(!IsWindow(floating), "close destroyed floating host");
    for (int column = 0; column < REGULAR_REPORT_COLUMN_COUNT; ++column)
        check(ListView_GetColumnWidth(reports, column) == fullWidths[column], "all custom column widths restored exactly");
    check(IsWindowVisible(other), "other application window restored");
    for (int cycle = 0; cycle < 4; ++cycle) {
        enter(st);
        SendMessageW(st->compactWindow, WM_CLOSE, 0, 0);
        restored(st, middle, right, results, reports, split);
    }
    enter(st);
    key.hwnd = reports;
    key.wParam = VK_ESCAPE;
    check(regular_report_translate_compact_message(&key), "Escape handled");
    restored(st, middle, right, results, reports, split);
    enter(st);
    DestroyWindow(st->compactWindow);
    restored(st, middle, right, results, reports, split);
    // Preserve hidden tabs across a round trip too, without loading any database data.
    st->reportRows.clear();
    ListView_DeleteAllItems(reports);
    st->selectedReportIndex = -1;
    TabCtrl_SetCurSel(st->middleTab, 1);
    TabCtrl_SetCurSel(st->rightTab, 1);
    enter(st);
    check(TabCtrl_GetCurSel(st->middleTab) == 0 && TabCtrl_GetCurSel(st->rightTab) == 0,
          "compact mode forces the two requested list pages");
    SendMessageW(st->compactWindow, WM_CLOSE, 0, 0);
    check(TabCtrl_GetCurSel(st->middleTab) == 1 && TabCtrl_GetCurSel(st->rightTab) == 1,
          "original tabs restored");
    auto suspended = app::window_task_detail::suspendSessionTasks();
    SendMessageW(st->compactButton, BM_CLICK, 0, 0);
    check(!st->compactWindow, "suspended session cannot open compact window");
    app::window_task_detail::discardSuspendedTasks();
    enter(st);
    floating = st->compactWindow;
    SendMessageW(mdi, WM_MDIDESTROY, reinterpret_cast<WPARAM>(report), 0);
    check(!IsWindow(report) && !IsWindow(floating), "closing module also destroys floating host");
    check(IsWindowVisible(frame) && IsWindowVisible(other), "closing only the report restores other windows");
    report = create_regular_report_module(context);
    st = reinterpret_cast<RegularReportState*>(GetPropW(report, REGULAR_REPORT_PROP_STATE));
    check(st != nullptr, "second report module created");
    KillTimer(report, IDT_REPORT_INITIAL_QUICK_MACHINE);
    st->initialQuickMachineTimerActive = false;
    enter(st);
    floating = st->compactWindow;
    app::window_task_detail::invalidateSession();
    SendMessageW(mdi, WM_MDIDESTROY, reinterpret_cast<WPARAM>(report), 0);
    check(!IsWindow(floating) && !IsWindowVisible(frame) && !IsWindowVisible(other),
          "session teardown does not re-show old windows");
    DestroyWindow(other);
    DestroyWindow(frame);
    std::cout << "Regular report compact window lifecycle passed\n";
}
