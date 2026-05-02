#include "trend_window.h"

#ifdef _WIN32

#include "search_text.h"
#include "trend_chart_renderer.h"
#include "trend_core.h"

#include <commctrl.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace search {
namespace {

constexpr int IDC_TREND_ITEM = 5101;
constexpr int IDC_TREND_LIST = 5102;
constexpr int IDC_TREND_CHART = 5103;

struct TrendWindowContext {
    DbSettings settings;
    QueryInput input;
    HFONT font = nullptr;
    HWND hwnd = nullptr;
    HWND item_combo = nullptr;
    HWND chart = nullptr;
    HWND list = nullptr;
    std::vector<TrendPoint> points;
    std::vector<TrendItemOption> items;
    std::string selected_item_code;
};

void add_column(HWND list, int index, const wchar_t* title, int width) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.pszText = const_cast<wchar_t*>(title);
    col.cx = width;
    col.iSubItem = index;
    ListView_InsertColumn(list, index, &col);
}

void set_cell(HWND list, int row, int col, const std::string& text) {
    const auto wide = utf8_to_wide(text);
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(wide.c_str()));
}

std::vector<const TrendPoint*> selected_points(const TrendWindowContext& ctx) {
    std::vector<const TrendPoint*> out;
    for (const auto& point : ctx.points) {
        if (point.item_code == ctx.selected_item_code) {
            out.push_back(&point);
        }
    }
    return out;
}

std::vector<const TrendPoint*> numeric_points(const TrendWindowContext& ctx) {
    std::vector<const TrendPoint*> out;
    for (const auto* point : selected_points(ctx)) {
        if (point->has_numeric_value) {
            out.push_back(point);
        }
    }
    return out;
}

void fill_trend_list(TrendWindowContext& ctx) {
    ListView_DeleteAllItems(ctx.list);
    int row_index = 0;
    for (const auto* point : selected_points(ctx)) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = row_index;
        auto first = utf8_to_wide(point->report_time);
        item.pszText = first.data();
        ListView_InsertItem(ctx.list, &item);
        set_cell(ctx.list, row_index, 1, point->item_name);
        set_cell(ctx.list, row_index, 2, point->result_text);
        set_cell(ctx.list, row_index, 3, point->unit);
        set_cell(ctx.list, row_index, 4, point->lower_bound);
        set_cell(ctx.list, row_index, 5, point->upper_bound);
        set_cell(ctx.list, row_index, 6, point->rep_no);
        ++row_index;
    }
}

void refresh_selected_item(TrendWindowContext& ctx) {
    const int index = static_cast<int>(SendMessageW(ctx.item_combo, CB_GETCURSEL, 0, 0));
    if (index >= 0 && static_cast<size_t>(index) < ctx.items.size()) {
        ctx.selected_item_code = ctx.items[static_cast<size_t>(index)].item_code;
    }
    fill_trend_list(ctx);
    InvalidateRect(ctx.chart, nullptr, TRUE);
}

void fill_item_combo(TrendWindowContext& ctx) {
    SendMessageW(ctx.item_combo, CB_RESETCONTENT, 0, 0);
    for (const auto& item : ctx.items) {
        std::string label = item.item_code + " - " + item.item_name;
        if (!item.unit.empty()) {
            label += " (" + item.unit + ")";
        }
        SendMessageW(ctx.item_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(utf8_to_wide(label).c_str()));
    }
    if (!ctx.items.empty()) {
        SendMessageW(ctx.item_combo, CB_SETCURSEL, 0, 0);
        ctx.selected_item_code = ctx.items.front().item_code;
    }
}

void draw_chart(HWND hwnd, HDC dc) {
    auto* ctx = reinterpret_cast<TrendWindowContext*>(GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA));
    RECT rect{};
    GetClientRect(hwnd, &rect);
    if (!ctx) {
        FillRect(dc, &rect, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        DrawTextW(dc, L"趋势数据未加载", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    draw_trend_chart(hwnd, dc, numeric_points(*ctx));
}

LRESULT CALLBACK chart_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            draw_chart(hwnd, dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void load_trend_data(TrendWindowContext& ctx) {
    std::string error;
    if (!query_trend_points(ctx.settings, ctx.input, ctx.points, error)) {
        MessageBoxW(ctx.hwnd, utf8_to_wide(error).c_str(), L"趋势查询失败", MB_ICONERROR);
        return;
    }
    ctx.items = trend_item_options(ctx.points);
    fill_item_combo(ctx);
    fill_trend_list(ctx);
    InvalidateRect(ctx.chart, nullptr, TRUE);
}

void layout_trend_window(TrendWindowContext& ctx) {
    RECT rc{};
    GetClientRect(ctx.hwnd, &rc);
    const int client_w = static_cast<int>(rc.right - rc.left);
    const int client_h = static_cast<int>(rc.bottom - rc.top);
    const int margin = 10;
    const int combo_h = 26;
    const int content_w = std::max(280, client_w - margin * 2);
    const int chart_h = std::max(180, client_h / 2);
    MoveWindow(ctx.item_combo, margin, margin, content_w, combo_h, TRUE);
    MoveWindow(ctx.chart, margin, margin + combo_h + 8, content_w, chart_h, TRUE);
    MoveWindow(ctx.list, margin, margin + combo_h + chart_h + 16, content_w,
               std::max(120, client_h - (margin + combo_h + chart_h + 26)), TRUE);
}

LRESULT CALLBACK trend_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* ctx = reinterpret_cast<TrendWindowContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* createstruct = reinterpret_cast<CREATESTRUCTW*>(lparam);
            ctx = reinterpret_cast<TrendWindowContext*>(createstruct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
            ctx->hwnd = hwnd;
            ctx->item_combo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                             0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(IDC_TREND_ITEM), GetModuleHandleW(nullptr), nullptr);
            ctx->chart = CreateWindowExW(WS_EX_CLIENTEDGE, L"ResultTrendChart", L"", WS_CHILD | WS_VISIBLE,
                                        0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(IDC_TREND_CHART), GetModuleHandleW(nullptr), nullptr);
            ctx->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                       0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(IDC_TREND_LIST), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(ctx->list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            add_column(ctx->list, 0, L"时间", 140);
            add_column(ctx->list, 1, L"项目", 150);
            add_column(ctx->list, 2, L"结果", 80);
            add_column(ctx->list, 3, L"单位", 80);
            add_column(ctx->list, 4, L"下限", 80);
            add_column(ctx->list, 5, L"上限", 80);
            add_column(ctx->list, 6, L"报告号", 90);
            if (ctx->font) {
                SendMessageW(ctx->item_combo, WM_SETFONT, reinterpret_cast<WPARAM>(ctx->font), TRUE);
                SendMessageW(ctx->chart, WM_SETFONT, reinterpret_cast<WPARAM>(ctx->font), TRUE);
                SendMessageW(ctx->list, WM_SETFONT, reinterpret_cast<WPARAM>(ctx->font), TRUE);
            }
            layout_trend_window(*ctx);
            load_trend_data(*ctx);
            return 0;
        }
        case WM_SIZE:
            if (ctx) {
                layout_trend_window(*ctx);
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wparam) == IDC_TREND_ITEM && HIWORD(wparam) == CBN_SELCHANGE && ctx) {
                refresh_selected_item(*ctx);
                return 0;
            }
            break;
        case WM_DESTROY:
            delete ctx;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void register_trend_classes() {
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = trend_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    window_class.lpszClassName = L"ResultTrendWindow";
    RegisterClassW(&window_class);

    WNDCLASSW chart_class{};
    chart_class.lpfnWndProc = chart_proc;
    chart_class.hInstance = GetModuleHandleW(nullptr);
    chart_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    chart_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    chart_class.lpszClassName = L"ResultTrendChart";
    RegisterClassW(&chart_class);
    registered = true;
}

}  // namespace

void show_trend_window(HWND owner, HFONT font, const DbSettings& settings, const QueryInput& input) {
    register_trend_classes();
    auto* ctx = new TrendWindowContext;
    ctx->settings = settings;
    ctx->input = input;
    ctx->font = font;

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"ResultTrendWindow", L"检验结果趋势图",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 980, 680,
                                owner, nullptr, GetModuleHandleW(nullptr), ctx);
    if (!hwnd) {
        delete ctx;
        MessageBoxW(owner, L"趋势图窗口创建失败。", L"趋势图", MB_ICONERROR);
        return;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
}

}  // namespace search

#endif
