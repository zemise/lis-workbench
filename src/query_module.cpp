#include "query_module.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "resource.h"
#include "search_app.h"
#include "search_controller.h"
#include "search_input_view_model.h"
#include "search_text.h"
#include "search_ui_context.h"
#include "search_ui_events.h"
#include "search_ui_layout.h"
#include "search_ui_presenter.h"
#include "search_view_state.h"
#include "trend_window.h"
#include <windows.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

namespace {

constexpr int IDC_PATIENT_ID = 1002;
constexpr int IDC_BARCODE = 1003;
constexpr int IDC_NAME = 1004;
constexpr int IDC_PATIENT_NO = 1005;
constexpr int IDC_OPER = 1006;
constexpr int IDC_START = 1007;
constexpr int IDC_END = 1008;
constexpr int IDC_ROOM = 1009;
constexpr int IDC_MACH = 1010;
constexpr int IDC_GROUP = 1011;
constexpr int IDC_ITEM = 1012;
constexpr int IDC_PATIENT_TYPE = 1013;
constexpr int IDC_REPORT_STATUS = 1014;
constexpr int IDC_REPORTS = 2001;
constexpr int IDC_RESULTS = 2002;
constexpr int IDC_SPLITTER = 2003;
constexpr int IDC_QUERY = 3001;
constexpr int IDC_EXIT = 3002;
constexpr int IDC_EXPORT = 3003;
constexpr int IDC_PREVIEW = 3004;
constexpr int IDC_PRINT = 3005;
constexpr int IDC_SETTINGS = 3006;
constexpr int IDC_TREND = 3007;
constexpr int IDC_STATUS = 4001;

constexpr const wchar_t* WND_CLASS   = L"QueryModuleChild";
constexpr const wchar_t* SPLITTER_CLASS = L"ResultSearchSplitter";
constexpr const wchar_t* PROP_STATE  = L"QuerySt";

int clampFontSize(int value) { return value < 8 ? 8 : (value > 24 ? 24 : value); }

HFONT createUiFont(int pointSize) {
    NONCLIENTMETRICSW nm{};
    nm.cbSize = sizeof(nm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(nm), &nm, 0);
    LOGFONTW lf = nm.lfMessageFont;
    HDC screen = GetDC(nullptr);
    lf.lfHeight = -MulDiv(pointSize, GetDeviceCaps(screen, LOGPIXELSY), 72);
    ReleaseDC(nullptr, screen);
    return CreateFontIndirectW(&lf);
}

struct QueryState {
    ModuleContext ctx;
    search::MainUiIds ids{IDC_PATIENT_ID, IDC_BARCODE, IDC_NAME, IDC_PATIENT_NO, IDC_OPER,
        IDC_START, IDC_END, IDC_ROOM, IDC_MACH, IDC_GROUP, IDC_ITEM, IDC_PATIENT_TYPE,
        IDC_REPORT_STATUS, IDC_REPORTS, IDC_RESULTS, IDC_SPLITTER,
        IDC_SETTINGS, IDC_QUERY, IDC_TREND, IDC_EXPORT, IDC_PREVIEW, IDC_PRINT, IDC_EXIT, IDC_STATUS};
    search::MainUiHandles ui;
    HFONT uiFont = nullptr;
    search::ViewState viewState;
    search::QueryInput lastQueryInput;
    bool hasLastQueryInput = false;
    bool draggingSplitter = false;
    int pendingSplitterX = 0;
};

QueryState* g_pending = nullptr;

search::DbSettings& db(QueryState* q)  { return q->viewState.settings.db; }
int& fontSize(QueryState* q)          { return q->viewState.settings.ui.font_size; }
int& splitterX(QueryState* q)         { return q->viewState.settings.ui.splitter_x; }
auto& reportRows(QueryState* q)       { return q->viewState.report_rows; }
auto& resultRows(QueryState* q)       { return q->viewState.result_rows; }
auto& roomOpts(QueryState* q)         { return q->viewState.room_options; }
auto& patientTypeOpts(QueryState* q)  { return q->viewState.patient_type_options; }
auto& machineOpts(QueryState* q)      { return q->viewState.machine_options; }
auto& connStr(QueryState* q)          { return q->viewState.connection_string; }

void setStatus(QueryState* q, const std::wstring& text) {
    search::set_status_text(q->ui, text);
}

COLORREF reportRowBg(const search::ReportRow& row) {
    switch (search::report_row_tone(row)) {
        case search::ReportRowTone::Printed:  return RGB(0xFF, 0xFF, 0xFF);
        case search::ReportRowTone::Reviewed: return RGB(0x75, 0xFB, 0xFD);
        default:                              return RGB(0xED, 0x6D, 0x52);
    }
}

COLORREF resultRowColor(const search::ResultRow& row) {
    switch (search::result_row_tone(row)) {
        case search::ResultRowTone::High: return RGB(220, 0, 0);
        case search::ResultRowTone::Low:  return RGB(0, 0, 220);
        default:                          return CLR_INVALID;
    }
}

void reloadRoomOptions(QueryState* q) {
    if (!q->ui.room) return;
    roomOpts(q).clear();
    std::string error;
    if (!search::load_room_options(db(q), roomOpts(q), error)) {
        setStatus(q, L"检验科室加载失败。");
        return;
    }
    search::populate_room_combo(q->ui.room, roomOpts(q));
}

void reloadPatientTypeOptions(QueryState* q) {
    if (!q->ui.patient_type) return;
    patientTypeOpts(q).clear();
    std::string error;
    if (!search::load_patient_type_options(db(q), patientTypeOpts(q), error)) {
        setStatus(q, L"病人类型加载失败。");
        return;
    }
    search::populate_patient_type_combo(q->ui.patient_type, patientTypeOpts(q));
}

void reloadMachineOptions(QueryState* q) {
    if (!q->ui.mach) return;
    machineOpts(q).clear();
    std::string error;
    if (!search::load_machine_options(db(q), search::selected_room_code(q->ui.room, roomOpts(q)), machineOpts(q), error)) {
        setStatus(q, L"检验仪器加载失败。");
        return;
    }
    search::populate_machine_combo(q->ui.mach, machineOpts(q));
}

void runQuery(QueryState* q) {
    auto input = search::build_query_input(q->ui, q->viewState);
    if (search::build_connection_string_w(db(q)).empty()) {
        MessageBoxW(nullptr, L"请先在“设置”中填写数据库连接信息。", L"缺少数据库设置", MB_ICONWARNING);
        return;
    }
    search::clear_result_lists(q->ui);
    reportRows(q).clear();
    resultRows(q).clear();
    setStatus(q, L"正在查询...");

    std::string error;
    if (!search::run_report_query(db(q), input, reportRows(q), connStr(q), error)) {
        setStatus(q, L"查询失败");
        MessageBoxW(nullptr, search::utf8_to_wide(error).c_str(), L"查询失败", MB_ICONERROR);
        return;
    }
    q->lastQueryInput = input;
    q->hasLastQueryInput = true;
    search::present_report_rows(q->ui, reportRows(q));
    setStatus(q, search::utf8_to_wide(search::make_query_count_status(reportRows(q).size())));
}

void querySelectedResults(QueryState* q, int selected) {
    resultRows(q).clear();
    if (selected < 0 || selected >= static_cast<int>(reportRows(q).size())) {
        ListView_DeleteAllItems(q->ui.results);
        return;
    }
    std::string error;
    if (!search::load_result_rows(connStr(q), reportRows(q)[static_cast<size_t>(selected)].rep_no, resultRows(q), error)) {
        MessageBoxW(nullptr, search::utf8_to_wide(error).c_str(), L"查询项目明细失败", MB_ICONERROR);
        return;
    }
    search::present_result_rows(q->ui, resultRows(q));
}

void showTrend(HWND owner, QueryState* q) {
    if (search::build_connection_string_w(db(q)).empty()) {
        MessageBoxW(owner, L"请先在“设置”中填写数据库连接信息。", L"缺少数据库设置", MB_ICONWARNING);
        return;
    }
    if (!q->hasLastQueryInput || reportRows(q).empty()) {
        MessageBoxW(owner, L"请先查询出同一病人的结果后再打开趋势图。", L"趋势图提示", MB_ICONWARNING);
        return;
    }
    auto input = q->lastQueryInput;
    if (search::trim(input.patient_name).empty() && search::trim(input.patient_no).empty()) {
        MessageBoxW(owner, L"上次查询未使用病人姓名或病人号，请先按病人姓名或病人号查询后再打开趋势图。", L"趋势图提示", MB_ICONWARNING);
        return;
    }
    search::show_trend_window(owner, q->uiFont ? q->uiFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                              db(q), input);
}

LRESULT CALLBACK splitterProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* q = reinterpret_cast<QueryState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_SETCURSOR:
            SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
            return TRUE;
        case WM_LBUTTONDOWN:
            if (q) { q->draggingSplitter = true; SetCapture(hwnd); }
            return 0;
        case WM_MOUSEMOVE:
            if (q && q->draggingSplitter) {
                POINT pt{}; GetCursorPos(&pt);
                ScreenToClient(GetParent(hwnd), &pt);
                splitterX(q) = pt.x - 4;
                search::layout_main_window(GetParent(hwnd), q->ui, splitterX(q));
            }
            return 0;
        case WM_LBUTTONUP:
            if (q && q->draggingSplitter) {
                q->draggingSplitter = false; ReleaseCapture();
                search::save_module_int(L"Query", L"SplitterX", splitterX(q));
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* q = reinterpret_cast<QueryState*>(GetPropW(hwnd, PROP_STATE));

    switch (msg) {
        case WM_CREATE: {
            q = g_pending;
            g_pending = nullptr;
            SetPropW(hwnd, PROP_STATE, reinterpret_cast<HANDLE>(q));

            q->uiFont = createUiFont(fontSize(q));
            HFONT font = q->uiFont ? q->uiFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            search::create_main_controls(hwnd, font, q->ids, q->ui);
            DestroyWindow(q->ui.settings_button);
            q->ui.settings_button = nullptr;
            SetWindowLongPtrW(q->ui.splitter, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(q));
            search::set_date_picker_today(q->ui.start);
            search::set_date_picker_today(q->ui.end);
            search::initialize_report_status_combo(q->ui.report_status);
            reloadRoomOptions(q);
            reloadPatientTypeOptions(q);
            reloadMachineOptions(q);
            q->pendingSplitterX = search::load_module_int(L"Query", L"SplitterX", 0);
            int initX = 0;
            search::layout_main_window(hwnd, q->ui, initX);
            return 0;
        }
        case WM_SIZE: {
            if (!q) return 0;
            if (q->pendingSplitterX > 0 && IsZoomed(hwnd)) {
                splitterX(q) = q->pendingSplitterX;
                q->pendingSplitterX = 0;
            }
            int x = splitterX(q);
            search::layout_main_window(hwnd, q->ui, x);
            splitterX(q) = x;
            return 0;
        }
        case WM_DPICHANGED: {
            auto* rect = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, rect->left, rect->top,
                         rect->right - rect->left, rect->bottom - rect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_COMMAND: {
            if (!q) break;
            search::CommandEventHandlers handlers;
            handlers.on_room_changed = [q] { reloadMachineOptions(q); };
            handlers.on_query = [q] { runQuery(q); };
            handlers.on_show_trend = [q](HWND owner) { showTrend(owner, q); };
            handlers.on_unimplemented_action = [](HWND owner) {
                MessageBoxW(owner, L"该功能暂未实现。", L"提示", MB_ICONINFORMATION);
            };
            handlers.on_exit = [](HWND owner) { DestroyWindow(owner); };
            if (search::handle_command(hwnd, wp, q->ids, handlers)) return 0;
            break;
        }
        case WM_NOTIFY: {
            if (!q) break;
            search::NotifyEventHandlers handlers;
            handlers.on_report_selected = [q](int i) { querySelectedResults(q, i); };
            handlers.report_row_background = [](const search::ReportRow& r) { return reportRowBg(r); };
            handlers.result_row_color = [](const search::ResultRow& r) { return resultRowColor(r); };
            handlers.report_rows = &reportRows(q);
            handlers.result_rows = &resultRows(q);
            LRESULT result = 0;
            if (search::handle_notify(lp, q->ids, handlers, result)) return result;
            break;
        }
        case WM_DESTROY: {
            delete q;
            RemovePropW(hwnd, PROP_STATE);
            break;
        }
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_query_module(const ModuleContext& ctx) {
    static bool classesRegistered = false;
    if (!classesRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = wndProc;
        wc.hInstance = ctx.instance;
        wc.hIcon = LoadIconW(ctx.instance, MAKEINTRESOURCEW(IDI_APP));
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = WND_CLASS;
        RegisterClassExW(&wc);

        WNDCLASSEXW swc{};
        swc.cbSize = sizeof(swc);
        swc.lpfnWndProc = splitterProc;
        swc.hInstance = ctx.instance;
        swc.hCursor = LoadCursor(nullptr, IDC_SIZEWE);
        swc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_3DSHADOW + 1);
        swc.lpszClassName = SPLITTER_CLASS;
        RegisterClassExW(&swc);
        classesRegistered = true;
    }

    auto* q = new QueryState;
    q->ctx = ctx;
    q->viewState.settings.db = ctx.dbSettings;
    fontSize(q) = clampFontSize(ctx.fontSize);

    MDICREATESTRUCTW mcs{};
    mcs.szClass = WND_CLASS;
    mcs.szTitle = L"检验结果查询";
    mcs.hOwner = ctx.instance;
    mcs.x = mcs.y = mcs.cx = mcs.cy = CW_USEDEFAULT;

    g_pending = q;
    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0,
        reinterpret_cast<LPARAM>(&mcs)));
    SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    return child;
}

#endif
