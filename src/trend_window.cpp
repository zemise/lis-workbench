#include "trend_window.h"

#ifdef _WIN32

#include "search_text.h"
#include "trend_chart_renderer.h"
#include "trend_core.h"

#include <commdlg.h>
#include <commctrl.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <set>
#include <sstream>
#include <vector>

namespace search {
namespace {

constexpr int IDC_TREND_ITEM = 5101;
constexpr int IDC_TREND_LIST = 5102;
constexpr int IDC_TREND_CHART = 5103;
constexpr int IDC_TREND_EXPORT = 5104;

struct TrendWindowContext {
    DbSettings settings;
    QueryInput input;
    HFONT font = nullptr;
    HWND hwnd = nullptr;
    HWND item_list = nullptr;
    HWND export_button = nullptr;
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

void refresh_selected_item(TrendWindowContext& ctx, int index) {
    if (index >= 0 && static_cast<size_t>(index) < ctx.items.size()) {
        ctx.selected_item_code = ctx.items[static_cast<size_t>(index)].item_code;
    }
    fill_trend_list(ctx);
    InvalidateRect(ctx.chart, nullptr, TRUE);
}

void fill_item_list(TrendWindowContext& ctx) {
    ListView_DeleteAllItems(ctx.item_list);
    for (size_t i = 0; i < ctx.items.size(); ++i) {
        const auto& item = ctx.items[i];
        std::string label = item.item_code + " - " + item.item_name;
        if (!item.unit.empty()) {
            label += " (" + item.unit + ")";
        }
        LVITEMW row{};
        row.mask = LVIF_TEXT;
        row.iItem = static_cast<int>(i);
        auto wide = utf8_to_wide(label);
        row.pszText = wide.data();
        ListView_InsertItem(ctx.item_list, &row);
        ListView_SetCheckState(ctx.item_list, static_cast<int>(i), i == 0);
    }
    if (!ctx.items.empty()) {
        ctx.selected_item_code = ctx.items.front().item_code;
        ListView_SetItemState(ctx.item_list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

std::set<std::string> checked_item_codes(const TrendWindowContext& ctx) {
    std::set<std::string> codes;
    for (size_t i = 0; i < ctx.items.size(); ++i) {
        if (ListView_GetCheckState(ctx.item_list, static_cast<int>(i))) {
            codes.insert(ctx.items[i].item_code);
        }
    }
    return codes;
}

std::string csv_escape(const std::string& text) {
    bool quote = text.find_first_of(",\"\r\n") != std::string::npos;
    std::string out;
    out.reserve(text.size() + 2);
    if (quote) {
        out.push_back('"');
    }
    for (const char ch : text) {
        if (ch == '"') {
            out += "\"\"";
        } else {
            out.push_back(ch);
        }
    }
    if (quote) {
        out.push_back('"');
    }
    return out;
}

std::string sanitize_filename_part(std::string text) {
    text = trim(text);
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

std::string query_date_label(const QueryInput& input) {
    const auto start = sanitize_filename_part(input.start_date);
    const auto end = sanitize_filename_part(input.end_date);
    if (start.empty()) {
        return end;
    }
    if (end.empty() || start == end) {
        return start;
    }
    return start + "至" + end;
}

std::wstring default_export_filename(const QueryInput& input) {
    std::vector<std::string> parts;
    const auto name = sanitize_filename_part(input.patient_name);
    const auto patient_no = sanitize_filename_part(input.patient_no);
    const auto date = query_date_label(input);
    if (!name.empty()) {
        parts.push_back(name);
    }
    if (!patient_no.empty()) {
        parts.push_back(patient_no);
    }
    if (!date.empty()) {
        parts.push_back(date);
    }
    if (parts.empty()) {
        parts.push_back("trend_export");
    }

    std::string filename;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            filename += "-";
        }
        filename += parts[i];
    }
    filename += ".csv";
    return utf8_to_wide(filename);
}

bool choose_export_path(HWND owner, const QueryInput& input, std::wstring& path) {
    wchar_t buffer[MAX_PATH] = {};
    const auto default_name = default_export_filename(input);
    lstrcpynW(buffer, default_name.c_str(), MAX_PATH);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"CSV 文件 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) {
        return false;
    }
    path = buffer;
    return true;
}

void export_checked_items(TrendWindowContext& ctx) {
    const auto codes = checked_item_codes(ctx);
    if (codes.empty()) {
        MessageBoxW(ctx.hwnd, L"请先勾选需要导出的项目。", L"趋势图提示", MB_ICONWARNING);
        return;
    }

    std::wstring path;
    if (!choose_export_path(ctx.hwnd, ctx.input, path)) {
        return;
    }

    FILE* file = nullptr;
#ifdef _MSC_VER
    _wfopen_s(&file, path.c_str(), L"wb");
#else
    file = _wfopen(path.c_str(), L"wb");
#endif
    if (!file) {
        MessageBoxW(ctx.hwnd, L"导出文件创建失败。", L"趋势图", MB_ICONERROR);
        return;
    }

    std::ostringstream csv;
    csv << "\xEF\xBB\xBF";
    csv << "报告时间,项目代码,项目名称,英文名,结果,单位,下限,上限,NORMAL,报告号,条码号,样本号,患者姓名\n";
    for (const auto& point : ctx.points) {
        if (codes.find(point.item_code) == codes.end()) {
            continue;
        }
        csv << csv_escape(point.report_time) << ','
            << csv_escape(point.item_code) << ','
            << csv_escape(point.item_name) << ','
            << csv_escape(point.item_eng) << ','
            << csv_escape(point.result_text) << ','
            << csv_escape(point.unit) << ','
            << csv_escape(point.lower_bound) << ','
            << csv_escape(point.upper_bound) << ','
            << csv_escape(point.normal) << ','
            << csv_escape(point.rep_no) << ','
            << csv_escape(point.txm_no) << ','
            << csv_escape(point.oper_no) << ','
            << csv_escape(point.patient_name) << '\n';
    }
    const auto text = csv.str();
    fwrite(text.data(), 1, text.size(), file);
    fclose(file);
    MessageBoxW(ctx.hwnd, L"导出完成。", L"趋势图", MB_ICONINFORMATION);
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
    fill_item_list(ctx);
    fill_trend_list(ctx);
    InvalidateRect(ctx.chart, nullptr, TRUE);
}

void layout_trend_window(TrendWindowContext& ctx) {
    RECT rc{};
    GetClientRect(ctx.hwnd, &rc);
    const int client_w = static_cast<int>(rc.right - rc.left);
    const int client_h = static_cast<int>(rc.bottom - rc.top);
    const int margin = 10;
    const int side_w = std::min(320, std::max(240, client_w / 4));
    const int export_h = 32;
    const int gap = 10;
    const int left_w = std::max(360, client_w - margin * 3 - side_w);
    const int chart_h = std::max(220, client_h / 2 + 20);
    MoveWindow(ctx.chart, margin, margin, left_w, chart_h, TRUE);
    MoveWindow(ctx.list, margin, margin + chart_h + gap, left_w,
               std::max(120, client_h - (margin + chart_h + gap + margin)), TRUE);
    MoveWindow(ctx.item_list, margin * 2 + left_w, margin, side_w,
               std::max(120, client_h - margin * 2 - export_h - gap), TRUE);
    MoveWindow(ctx.export_button, margin * 2 + left_w, client_h - margin - export_h, side_w, export_h, TRUE);
}

LRESULT CALLBACK trend_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* ctx = reinterpret_cast<TrendWindowContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* createstruct = reinterpret_cast<CREATESTRUCTW*>(lparam);
            ctx = reinterpret_cast<TrendWindowContext*>(createstruct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
            ctx->hwnd = hwnd;
            ctx->item_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                             0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(IDC_TREND_ITEM), GetModuleHandleW(nullptr), nullptr);
            ctx->export_button = CreateWindowExW(0, L"BUTTON", L"导出勾选项目", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                                 0, 0, 100, 32, hwnd, reinterpret_cast<HMENU>(IDC_TREND_EXPORT), GetModuleHandleW(nullptr), nullptr);
            ctx->chart = CreateWindowExW(WS_EX_CLIENTEDGE, L"ResultTrendChart", L"", WS_CHILD | WS_VISIBLE,
                                        0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(IDC_TREND_CHART), GetModuleHandleW(nullptr), nullptr);
            ctx->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                       0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(IDC_TREND_LIST), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(ctx->item_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES);
            add_column(ctx->item_list, 0, L"项目", 260);
            ListView_SetExtendedListViewStyle(ctx->list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            add_column(ctx->list, 0, L"时间", 140);
            add_column(ctx->list, 1, L"项目", 150);
            add_column(ctx->list, 2, L"结果", 80);
            add_column(ctx->list, 3, L"单位", 80);
            add_column(ctx->list, 4, L"下限", 80);
            add_column(ctx->list, 5, L"上限", 80);
            add_column(ctx->list, 6, L"报告号", 90);
            if (ctx->font) {
                SendMessageW(ctx->item_list, WM_SETFONT, reinterpret_cast<WPARAM>(ctx->font), TRUE);
                SendMessageW(ctx->export_button, WM_SETFONT, reinterpret_cast<WPARAM>(ctx->font), TRUE);
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
            if (LOWORD(wparam) == IDC_TREND_EXPORT && ctx) {
                export_checked_items(*ctx);
                return 0;
            }
            break;
        case WM_NOTIFY:
            if (ctx) {
                auto* hdr = reinterpret_cast<NMHDR*>(lparam);
                if (hdr->idFrom == IDC_TREND_ITEM && hdr->code == LVN_ITEMCHANGED) {
                    auto* item = reinterpret_cast<NMLISTVIEW*>(lparam);
                    if ((item->uNewState & LVIS_SELECTED) != 0) {
                        refresh_selected_item(*ctx, item->iItem);
                    }
                    return 0;
                }
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
