#include "trend_window.h"

#ifdef _WIN32

#include "resource.h"
#include "search_ui_layout.h"
#include "search_text.h"
#include "trend_chart_renderer.h"
#include "trend_core.h"

#include <commdlg.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

namespace search {
namespace {

constexpr int IDC_TREND_ITEM = 5101;
constexpr int IDC_TREND_LIST = 5102;
constexpr int IDC_TREND_CHART = 5103;
constexpr int IDC_TREND_EXPORT = 5104;
constexpr int IDC_TREND_EXPORT_IMAGE = 5105;
constexpr UINT WM_TREND_LOADED = WM_APP + 51;

struct TrendWindowContext {
    DbSettings settings;
    QueryInput input;
    HFONT font = nullptr;
    HWND hwnd = nullptr;
    HWND item_list = nullptr;
    HWND export_button = nullptr;
    HWND export_image_button = nullptr;
    HWND chart = nullptr;
    HWND list = nullptr;
    std::vector<TrendPoint> points;
    std::vector<TrendItemOption> items;
    std::string selected_item_code;
    bool loading = false;
};

struct TrendLoadResult {
    bool ok = false;
    std::string error;
    std::vector<TrendPoint> points;
    std::vector<TrendItemOption> items;
};

class ScopedComInit {
public:
    ScopedComInit() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ScopedComInit() {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }

    ScopedComInit(const ScopedComInit&) = delete;
    ScopedComInit& operator=(const ScopedComInit&) = delete;

private:
    HRESULT result_ = E_FAIL;
};

class ScopedGdiplus {
public:
    ScopedGdiplus() {
        Gdiplus::GdiplusStartupInput startup_input;
        ok_ = Gdiplus::GdiplusStartup(&token_, &startup_input, nullptr) == Gdiplus::Ok;
    }
    ~ScopedGdiplus() {
        if (ok_) {
            Gdiplus::GdiplusShutdown(token_);
        }
    }

    ScopedGdiplus(const ScopedGdiplus&) = delete;
    ScopedGdiplus& operator=(const ScopedGdiplus&) = delete;

    bool ok() const {
        return ok_;
    }

private:
    ULONG_PTR token_ = 0;
    bool ok_ = false;
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

std::wstring default_chart_filename(const QueryInput& input, const TrendItemOption* item) {
    auto filename = default_export_filename(input);
    if (filename.size() >= 4 && filename.substr(filename.size() - 4) == L".csv") {
        filename.resize(filename.size() - 4);
    }
    if (item) {
        filename += L"-";
        filename += utf8_to_wide(sanitize_filename_part(item->item_code));
        if (!trim(item->item_name).empty()) {
            filename += L"-";
            filename += utf8_to_wide(sanitize_filename_part(item->item_name));
        }
    }
    filename += L".png";
    return filename;
}

bool choose_save_path(HWND owner, const std::wstring& default_name, const wchar_t* filter, const wchar_t* default_ext, std::wstring& path) {
    wchar_t buffer[MAX_PATH] = {};
    lstrcpynW(buffer, default_name.c_str(), MAX_PATH);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = default_ext;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) {
        return false;
    }
    path = buffer;
    return true;
}

bool choose_export_path(HWND owner, const QueryInput& input, std::wstring& path) {
    return choose_save_path(owner,
                            default_export_filename(input),
                            L"CSV 文件 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0",
                            L"csv",
                            path);
}

bool choose_export_folder(HWND owner, std::wstring& folder) {
    ScopedComInit com;
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"请选择图片导出文件夹";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) {
        return false;
    }
    wchar_t buffer[MAX_PATH] = {};
    const bool ok = SHGetPathFromIDListW(pidl, buffer) == TRUE;
    CoTaskMemFree(pidl);
    if (!ok) {
        return false;
    }
    folder = buffer;
    return true;
}

void export_checked_items(TrendWindowContext& ctx) {
    if (ctx.loading) {
        MessageBoxW(ctx.hwnd, L"趋势数据仍在加载，请稍后再导出。", L"趋势图提示", MB_ICONWARNING);
        return;
    }
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

const TrendItemOption* item_option_by_code(const TrendWindowContext& ctx, const std::string& item_code) {
    for (const auto& item : ctx.items) {
        if (item.item_code == item_code) {
            return &item;
        }
    }
    return nullptr;
}

std::vector<const TrendPoint*> numeric_points_for_item(const TrendWindowContext& ctx, const std::string& item_code) {
    std::vector<const TrendPoint*> out;
    for (const auto& point : ctx.points) {
        if (point.item_code == item_code && point.has_numeric_value) {
            out.push_back(&point);
        }
    }
    return out;
}

int get_encoder_clsid(const wchar_t* mime_type, CLSID& clsid) {
    UINT count = 0;
    UINT size = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &size) != Gdiplus::Ok || size == 0) {
        return -1;
    }
    std::vector<unsigned char> buffer(size);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, size, encoders) != Gdiplus::Ok) {
        return -1;
    }
    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(encoders[i].MimeType, mime_type) == 0) {
            clsid = encoders[i].Clsid;
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool save_chart_png(HWND owner,
                    const std::vector<const TrendPoint*>& points,
                    const std::wstring& path,
                    const CLSID& png_clsid,
                    bool show_point_warning) {
    if (points.size() < 2) {
        if (show_point_warning) {
            MessageBoxW(owner, L"当前项目不足两个有效数值点，无法导出图片。", L"趋势图提示", MB_ICONWARNING);
        }
        return false;
    }

    constexpr int width = 1600;
    constexpr int height = 1000;
    HDC screen_dc = GetDC(nullptr);
    HDC memory_dc = CreateCompatibleDC(screen_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(screen_dc, width, height);
    HGDIOBJ old_bitmap = SelectObject(memory_dc, bitmap);
    RECT rect{0, 0, width, height};
    draw_trend_chart_to_rect(memory_dc, rect, points);
    SelectObject(memory_dc, old_bitmap);

    bool ok = false;
    {
        Gdiplus::Bitmap image(bitmap, nullptr);
        ok = image.Save(path.c_str(), &png_clsid, nullptr) == Gdiplus::Ok;
    }

    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(nullptr, screen_dc);
    if (!ok) {
        MessageBoxW(owner, L"PNG 图片保存失败。", L"趋势图", MB_ICONERROR);
    }
    return ok;
}

std::wstring join_path(const std::wstring& folder, const std::wstring& filename) {
    if (folder.empty()) {
        return filename;
    }
    if (folder.back() == L'\\' || folder.back() == L'/') {
        return folder + filename;
    }
    return folder + L"\\" + filename;
}

void export_checked_chart_images(TrendWindowContext& ctx) {
    if (ctx.loading) {
        MessageBoxW(ctx.hwnd, L"趋势数据仍在加载，请稍后再导出图片。", L"趋势图提示", MB_ICONWARNING);
        return;
    }
    const auto codes = checked_item_codes(ctx);
    if (codes.empty()) {
        MessageBoxW(ctx.hwnd, L"请先勾选需要导出的项目。", L"趋势图提示", MB_ICONWARNING);
        return;
    }

    std::wstring folder;
    if (!choose_export_folder(ctx.hwnd, folder)) {
        return;
    }

    ScopedGdiplus gdiplus;
    if (!gdiplus.ok()) {
        MessageBoxW(ctx.hwnd, L"GDI+ 初始化失败，无法导出图片。", L"趋势图", MB_ICONERROR);
        return;
    }
    CLSID png_clsid{};
    if (get_encoder_clsid(L"image/png", png_clsid) < 0) {
        MessageBoxW(ctx.hwnd, L"未找到 PNG 编码器，无法导出图片。", L"趋势图", MB_ICONERROR);
        return;
    }

    int exported = 0;
    int skipped = 0;
    for (const auto& item : ctx.items) {
        if (codes.find(item.item_code) == codes.end()) {
            continue;
        }
        const auto points = numeric_points_for_item(ctx, item.item_code);
        if (points.size() < 2) {
            ++skipped;
            continue;
        }
        if (save_chart_png(ctx.hwnd, points, join_path(folder, default_chart_filename(ctx.input, &item)), png_clsid, false)) {
            ++exported;
        } else {
            ++skipped;
        }
    }

    std::wstringstream message;
    message << L"图片导出完成：" << exported << L" 张";
    if (skipped > 0) {
        message << L"，跳过：" << skipped << L" 项";
    }
    MessageBoxW(ctx.hwnd, message.str().c_str(), L"趋势图", MB_ICONINFORMATION);
}

void draw_centered_text(HDC dc, const RECT& rect, const wchar_t* text) {
    DrawTextW(dc, text, -1, const_cast<RECT*>(&rect), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void draw_chart(HWND hwnd, HDC dc) {
    auto* ctx = reinterpret_cast<TrendWindowContext*>(GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA));
    RECT rect{};
    GetClientRect(hwnd, &rect);
    if (!ctx) {
        FillRect(dc, &rect, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        draw_centered_text(dc, rect, L"趋势数据未加载");
        return;
    }
    if (ctx->loading) {
        FillRect(dc, &rect, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        draw_centered_text(dc, rect, L"趋势数据加载中...");
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

void begin_load_trend_data(TrendWindowContext& ctx) {
    ctx.loading = true;
    EnableWindow(ctx.export_button, FALSE);
    EnableWindow(ctx.export_image_button, FALSE);
    InvalidateRect(ctx.chart, nullptr, TRUE);
    const HWND hwnd = ctx.hwnd;
    const DbSettings settings = ctx.settings;
    const QueryInput input = ctx.input;
    std::thread([hwnd, settings, input]() {
        auto* result = new TrendLoadResult;
        result->ok = query_trend_points(settings, input, result->points, result->error);
        if (result->ok) {
            result->items = trend_item_options(result->points);
        }
        if (!PostMessageW(hwnd, WM_TREND_LOADED, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

void finish_load_trend_data(TrendWindowContext& ctx, std::unique_ptr<TrendLoadResult> result) {
    ctx.loading = false;
    EnableWindow(ctx.export_button, TRUE);
    EnableWindow(ctx.export_image_button, TRUE);
    if (!result->ok) {
        MessageBoxW(ctx.hwnd, utf8_to_wide(result->error).c_str(), L"趋势查询失败", MB_ICONERROR);
        InvalidateRect(ctx.chart, nullptr, TRUE);
        return;
    }
    ctx.points = std::move(result->points);
    ctx.items = std::move(result->items);
    fill_item_list(ctx);
    fill_trend_list(ctx);
    InvalidateRect(ctx.chart, nullptr, TRUE);
}

void layout_trend_window(TrendWindowContext& ctx) {
    RECT rc{};
    GetClientRect(ctx.hwnd, &rc);
    const int client_w = static_cast<int>(rc.right - rc.left);
    const int client_h = static_cast<int>(rc.bottom - rc.top);
    const float s = search::dpi_scale_factor(ctx.hwnd);
    auto S = [s](int v) { return static_cast<int>(v * s); };
    const int margin = S(10);
    const int side_w = std::min(S(320), std::max(S(240), client_w / 4));
    const int export_h = S(32);
    const int button_gap = S(8);
    const int gap = S(10);
    const int left_w = std::max(S(360), client_w - margin * 3 - side_w);
    const int chart_h = std::max(S(220), client_h / 2 + S(20));
    MoveWindow(ctx.chart, margin, margin, left_w, chart_h, TRUE);
    MoveWindow(ctx.list, margin, margin + chart_h + gap, left_w,
               std::max(S(120), client_h - (margin + chart_h + gap + margin)), TRUE);
    MoveWindow(ctx.item_list, margin * 2 + left_w, margin, side_w,
               std::max(S(120), client_h - margin * 2 - export_h * 2 - button_gap - gap), TRUE);
    MoveWindow(ctx.export_image_button, margin * 2 + left_w, client_h - margin - export_h * 2 - button_gap, side_w, export_h, TRUE);
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
            ctx->export_image_button = CreateWindowExW(0, L"BUTTON", L"导出勾选图片", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                                       0, 0, 100, 32, hwnd, reinterpret_cast<HMENU>(IDC_TREND_EXPORT_IMAGE), GetModuleHandleW(nullptr), nullptr);
            ctx->chart = CreateWindowExW(WS_EX_CLIENTEDGE, L"ResultTrendChart", L"", WS_CHILD | WS_VISIBLE,
                                        0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(IDC_TREND_CHART), GetModuleHandleW(nullptr), nullptr);
            ctx->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                       0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(IDC_TREND_LIST), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(ctx->item_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES);
            const float ds = search::dpi_scale_factor(hwnd);
            auto dS = [ds](int v) { return static_cast<int>(v * ds); };
            add_column(ctx->item_list, 0, L"项目", dS(260));
            ListView_SetExtendedListViewStyle(ctx->list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            add_column(ctx->list, 0, L"时间", dS(140));
            add_column(ctx->list, 1, L"项目", dS(150));
            add_column(ctx->list, 2, L"结果", dS(80));
            add_column(ctx->list, 3, L"单位", dS(80));
            add_column(ctx->list, 4, L"下限", dS(80));
            add_column(ctx->list, 5, L"上限", dS(80));
            add_column(ctx->list, 6, L"报告号", dS(90));
            if (ctx->font) {
                SendMessageW(ctx->item_list, WM_SETFONT, reinterpret_cast<WPARAM>(ctx->font), TRUE);
                SendMessageW(ctx->export_button, WM_SETFONT, reinterpret_cast<WPARAM>(ctx->font), TRUE);
                SendMessageW(ctx->export_image_button, WM_SETFONT, reinterpret_cast<WPARAM>(ctx->font), TRUE);
                SendMessageW(ctx->chart, WM_SETFONT, reinterpret_cast<WPARAM>(ctx->font), TRUE);
                SendMessageW(ctx->list, WM_SETFONT, reinterpret_cast<WPARAM>(ctx->font), TRUE);
            }
            layout_trend_window(*ctx);
            begin_load_trend_data(*ctx);
            return 0;
        }
        case WM_TREND_LOADED:
            if (ctx) {
                std::unique_ptr<TrendLoadResult> result(reinterpret_cast<TrendLoadResult*>(lparam));
                finish_load_trend_data(*ctx, std::move(result));
            } else {
                delete reinterpret_cast<TrendLoadResult*>(lparam);
            }
            return 0;
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
            if (LOWORD(wparam) == IDC_TREND_EXPORT_IMAGE && ctx) {
                export_checked_chart_images(*ctx);
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
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = trend_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hIcon = LoadIconW(window_class.hInstance, MAKEINTRESOURCEW(IDI_APP));
    window_class.hIconSm = static_cast<HICON>(LoadImageW(window_class.hInstance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                                           GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    window_class.lpszClassName = L"ResultTrendWindow";
    RegisterClassExW(&window_class);

    WNDCLASSEXW chart_class{};
    chart_class.cbSize = sizeof(chart_class);
    chart_class.lpfnWndProc = chart_proc;
    chart_class.hInstance = GetModuleHandleW(nullptr);
    chart_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    chart_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    chart_class.lpszClassName = L"ResultTrendChart";
    RegisterClassExW(&chart_class);
    registered = true;
}

}  // namespace

void show_trend_window(HWND owner, HFONT font, const DbSettings& settings, const QueryInput& input) {
    register_trend_classes();
    auto* ctx = new TrendWindowContext;
    ctx->settings = settings;
    ctx->input = input;
    ctx->font = font;

    const float s = search::dpi_scale_factor(owner);
    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"ResultTrendWindow", L"检验结果趋势图",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, static_cast<int>(980 * s), static_cast<int>(680 * s),
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
