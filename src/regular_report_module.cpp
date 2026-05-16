#include "regular_report_module.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "main_app.h"
#include "resource.h"
#include "search_splitter.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"

#include <commctrl.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cwchar>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"RegularReportChild";
constexpr const wchar_t* WINDOW_TITLE = L"常规报告";
constexpr const wchar_t* PROP_STATE = L"RegularReportSt";

constexpr int IDC_RESULT_LIST = 5201;
constexpr int IDC_REPORT_LIST = 5202;
constexpr int IDC_SPLITTER = 5204;
constexpr int IDC_LEFT_SCROLL = 5205;
constexpr int IDC_RIGHT_TAB = 5206;
constexpr int IDC_MIDDLE_TAB = 5207;
constexpr UINT_PTR LEFT_PANEL_SUBCLASS = 6205;
constexpr UINT_PTR LEFT_CONTENT_SUBCLASS = 6206;
constexpr UINT_PTR RIGHT_PANEL_SUBCLASS = 6207;
constexpr UINT_PTR MIDDLE_PANEL_SUBCLASS = 6208;
constexpr int LEFT_CONTENT_HEIGHT = 875;
constexpr int LEFT_SCROLL_STEP = 36;
constexpr int PAD = 8;
constexpr int GAP = 6;
constexpr int SPLITTER_W = 8;
constexpr int TAB_H = 30;
constexpr int COMPACT_BUTTON_H = 28;
constexpr int BOTTOM_PANEL_H = 102;
constexpr int MIDDLE_TOOLBAR_Y = 32;
constexpr int MIDDLE_LIST_Y = 66;
constexpr int MIDDLE_LIST_BOTTOM_MARGIN = 88;
constexpr int MIDDLE_STATUS_BOTTOM = 22;
constexpr int MIDDLE_STATUS_H = 18;
constexpr int RIGHT_TAB_Y = 56;
constexpr int RIGHT_SEARCH_LABEL_Y = 94;
constexpr int RIGHT_SEARCH_CONTROL_Y = 90;
constexpr int RIGHT_LIST_Y = 128;
constexpr const wchar_t* RIGHT_SUMMARY_LINE1 = L"样本数(57):上机数(52):审核数(0):发送数(46)";
constexpr const wchar_t* RIGHT_SUMMARY_LINE2 = L"危急报告已阅:1；急诊报告已阅:2；危急报告未阅:3；急诊报告未阅:3；";

struct ColumnDef {
    int index;
    const wchar_t* title;
    int width;
};

struct ButtonDef {
    int id;
    const wchar_t* text;
};

// Drawn on leftContent instead of real GROUPBOX windows so sibling clipping can
// be enabled on the child controls without the group frame hiding them.
struct GroupFrame {
    RECT rect{};
    const wchar_t* title = L"";
};

struct RightHeaderLayout {
    RECT line1{};
    RECT line2{};
    int bottom = 0;
};

struct RegularReportState {
    ModuleContext ctx;
    HWND leftPanel = nullptr;
    HWND leftContent = nullptr;
    HWND leftScrollBar = nullptr;
    HWND middlePanel = nullptr;
    HWND rightPanel = nullptr;
    HWND bottomPanel = nullptr;
    HWND splitter = nullptr;
    HWND middleTab = nullptr;
    HWND rightTab = nullptr;
    HWND rightSearchLabel = nullptr;
    HWND rightSearchEdit = nullptr;
    HWND rightSearchIndexButton = nullptr;
    HWND rightSearchUpButton = nullptr;
    HWND rightSearchDownButton = nullptr;
    HWND rightSearchMenuButton = nullptr;
    HWND resultList = nullptr;
    HWND reportList = nullptr;
    HWND status = nullptr;
    int splitterX = 0;
    int pendingSplitterX = 0;
    int leftScrollY = 0;
    int leftContentHeight = LEFT_CONTENT_HEIGHT;
    bool splitterUserSet = false;
    std::vector<HWND> leftControls;
    std::vector<GroupFrame> leftGroupFrames;
    std::vector<HWND> middleResultControls;
    std::vector<HWND> rightInfoControls;
    HBRUSH bgBrush = nullptr;
    HBRUSH panelBrush = nullptr;
    HBRUSH blackBrush = nullptr;
    HFONT groupTitleFont = nullptr;
};

RegularReportState* g_pending = nullptr;

int S(HWND hwnd, int value) {
    return static_cast<int>(value * search::dpi_scale_factor(hwnd));
}

HWND addClipSiblings(HWND hwnd) {
    if (!hwnd) return hwnd;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, style | WS_CLIPSIBLINGS);
    return hwnd;
}

HWND makeStatic(HWND parent, const wchar_t* text, int x, int y, int w, int h, DWORD style = SS_LEFT) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND makeEdit(HWND parent, const wchar_t* text, int x, int y, int w, int h, DWORD extra = ES_AUTOHSCROLL) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | extra,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND makeButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
                           x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND makeCheckBox(HWND parent, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | BS_AUTOCHECKBOX,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND makeCombo(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    HWND combo = addClipSiblings(search::create_combo(parent, 0, x, y, w, h, false));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
    return combo;
}

SYSTEMTIME dateTime(int year, int month, int day, int hour = 0, int minute = 0, int second = 0) {
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(year);
    st.wMonth = static_cast<WORD>(month);
    st.wDay = static_cast<WORD>(day);
    st.wHour = static_cast<WORD>(hour);
    st.wMinute = static_cast<WORD>(minute);
    st.wSecond = static_cast<WORD>(second);
    return st;
}

HWND makeDatePicker(HWND parent, int x, int y, int w, int h, const wchar_t* format, const SYSTEMTIME* value) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | DTS_SHORTDATECENTURYFORMAT;
    if (!value) style |= DTS_SHOWNONE;
    HWND picker = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", style,
                                  x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (format) {
        SendMessageW(picker, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(format));
    }
    if (value) {
        SendMessageW(picker, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(value));
    } else {
        SendMessageW(picker, DTM_SETSYSTEMTIME, GDT_NONE, 0);
    }
    return picker;
}

void applyFont(HWND hwnd, HFONT font) {
    if (!font) return;
    EnumChildWindows(hwnd, [](HWND child, LPARAM p) -> BOOL {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(p), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font));
}

HFONT createBoldFont(HFONT base) {
    LOGFONTW lf{};
    if (base && GetObjectW(base, sizeof(lf), &lf) == sizeof(lf)) {
        lf.lfWeight = FW_BOLD;
        return CreateFontIndirectW(&lf);
    }
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0);
    ncm.lfMessageFont.lfWeight = FW_BOLD;
    return CreateFontIndirectW(&ncm.lfMessageFont);
}

void refreshLeftGroupTitleFont(RegularReportState* st) {
    if (!st) return;
    if (st->groupTitleFont) {
        DeleteObject(st->groupTitleFont);
        st->groupTitleFont = nullptr;
    }
    st->groupTitleFont = createBoldFont(st->ctx.uiFont);
    if (st->leftContent) {
        InvalidateRect(st->leftContent, nullptr, TRUE);
    }
}

int textLogicalWidth(HWND hwnd, HFONT font, const wchar_t* text) {
    HDC dc = GetDC(hwnd);
    if (!dc) return 0;
    HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    SIZE size{};
    GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &size);
    if (old) SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    const double scale = std::max(0.1f, search::dpi_scale_factor(hwnd));
    return static_cast<int>((size.cx + scale - 1) / scale);
}

int fontLogicalHeight(HWND hwnd, HFONT font) {
    HDC dc = GetDC(hwnd);
    if (!dc) return 16;
    HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    if (old) SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    const double scale = std::max(0.1f, search::dpi_scale_factor(hwnd));
    return static_cast<int>((tm.tmHeight + scale - 1) / scale);
}

int wrappedTextHeightPx(HWND hwnd, HFONT font, const wchar_t* text, int widthPx, int minHeightPx) {
    if (!text || widthPx <= 0) return minHeightPx;
    HDC dc = GetDC(hwnd);
    if (!dc) return minHeightPx;
    HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    RECT rc{0, 0, widthPx, 0};
    DrawTextW(dc, text, -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_CENTER);
    if (old) SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    return std::max(minHeightPx, static_cast<int>(rc.bottom - rc.top) + S(hwnd, 2));
}

RightHeaderLayout rightHeaderLayout(HWND hwnd, HFONT font, int panelWidthPx) {
    const int innerX = S(hwnd, PAD);
    const int innerW = std::max(S(hwnd, 80), panelWidthPx - S(hwnd, PAD * 2));
    const int top = S(hwnd, 4);
    const int gap = S(hwnd, 2);
    const int line1H = wrappedTextHeightPx(hwnd, font, RIGHT_SUMMARY_LINE1, innerW, S(hwnd, 20));
    const int line2Y = top + line1H + gap;
    const int line2H = wrappedTextHeightPx(hwnd, font, RIGHT_SUMMARY_LINE2, innerW, S(hwnd, 24));
    RightHeaderLayout layout{};
    layout.line1 = RECT{innerX, top, innerX + innerW, top + line1H};
    layout.line2 = RECT{innerX, line2Y, innerX + innerW, line2Y + line2H};
    layout.bottom = line2Y + line2H;
    return layout;
}

int leftLabelWidth(HWND hwnd, HFONT font) {
    const wchar_t* labels[] = {
        L"检验仪器", L"组合项目", L"检验单号", L"病人类型", L"临床科室",
        L"临床诊断", L"申请医生", L"申请日期", L"签收时间", L"上机时间",
        L"报告时间", L"检验日期", L"采集日期",
    };
    int width = 50;
    for (const wchar_t* label : labels) {
        width = std::max(width, textLogicalWidth(hwnd, font, label) + 8);
    }
    return width;
}

int rightLabelWidth(HWND hwnd, HFONT font) {
    const wchar_t* labels[] = {L"样本号", L"性别", L"床号", L"审核"};
    int width = 38;
    for (const wchar_t* label : labels) {
        width = std::max(width, textLogicalWidth(hwnd, font, label) + 8);
    }
    return width;
}

void clearLeftPanel(RegularReportState* st) {
    if (!st) return;
    for (HWND child : st->leftControls) {
        if (IsWindow(child)) DestroyWindow(child);
    }
    st->leftControls.clear();
    st->leftGroupFrames.clear();
}

void updateLeftScrollBar(RegularReportState* st) {
    if (!st || !st->leftPanel || !st->leftContent || !st->leftScrollBar) return;

    RECT rc{};
    GetClientRect(st->leftPanel, &rc);
    const int page = std::max(1, static_cast<int>(rc.bottom - rc.top));
    const int contentH = S(st->leftPanel, std::max(LEFT_CONTENT_HEIGHT, st->leftContentHeight));
    const int maxScroll = std::max(0, contentH - page);
    st->leftScrollY = std::clamp(st->leftScrollY, 0, maxScroll);
    const int scrollW = GetSystemMetrics(SM_CXVSCROLL);

    MoveWindow(st->leftContent, 0, -st->leftScrollY,
               std::max(0, static_cast<int>(rc.right - rc.left) - scrollW),
               contentH, TRUE);
    SetWindowPos(st->leftScrollBar, HWND_TOP,
                 std::max(0, static_cast<int>(rc.right - rc.left) - scrollW), 0,
                 scrollW, page, SWP_SHOWWINDOW);

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = std::max(0, contentH - 1);
    si.nPage = static_cast<UINT>(page);
    si.nPos = st->leftScrollY;
    SetScrollInfo(st->leftScrollBar, SB_CTL, &si, TRUE);
    ShowWindow(st->leftScrollBar, contentH > page ? SW_SHOW : SW_HIDE);
}

void scrollLeftPanelTo(RegularReportState* st, int targetY) {
    if (!st || !st->leftPanel || !st->leftContent) return;

    RECT rc{};
    GetClientRect(st->leftPanel, &rc);
    const int page = static_cast<int>(rc.bottom - rc.top);
    const int maxScroll = std::max(0, S(st->leftPanel, std::max(LEFT_CONTENT_HEIGHT, st->leftContentHeight)) - page);
    targetY = std::clamp(targetY, 0, maxScroll);
    st->leftScrollY = targetY;
    updateLeftScrollBar(st);
}

LRESULT CALLBACK leftPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                               UINT_PTR subclassId, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_VSCROLL: {
            if (!st) break;
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(st->leftScrollBar, SB_CTL, &si);

            int target = st->leftScrollY;
            switch (LOWORD(wp)) {
                case SB_LINEUP:
                    target -= S(hwnd, LEFT_SCROLL_STEP);
                    break;
                case SB_LINEDOWN:
                    target += S(hwnd, LEFT_SCROLL_STEP);
                    break;
                case SB_PAGEUP:
                    target -= static_cast<int>(si.nPage);
                    break;
                case SB_PAGEDOWN:
                    target += static_cast<int>(si.nPage);
                    break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION:
                    target = si.nTrackPos;
                    break;
                case SB_TOP:
                    target = 0;
                    break;
                case SB_BOTTOM:
                    target = si.nMax;
                    break;
                default:
                    return 0;
            }
            scrollLeftPanelTo(st, target);
            return 0;
        }
        case WM_MOUSEWHEEL:
            if (st) {
                const int delta = GET_WHEEL_DELTA_WPARAM(wp);
                scrollLeftPanelTo(st, st->leftScrollY - (delta / WHEEL_DELTA) * S(hwnd, LEFT_SCROLL_STEP * 3));
                return 0;
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, leftPanelProc, subclassId);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void drawLeftGroupFrames(HWND hwnd, RegularReportState* st, HDC dc) {
    RECT client{};
    GetClientRect(hwnd, &client);
    FillRect(dc, &client, st && st->panelBrush ? st->panelBrush : GetSysColorBrush(COLOR_BTNFACE));
    if (!st) return;

    HBRUSH frameBrush = GetSysColorBrush(COLOR_GRAYTEXT);
    HGDIOBJ oldFont = nullptr;
    HFONT titleFont = st->groupTitleFont ? st->groupTitleFont : st->ctx.uiFont;
    if (titleFont) oldFont = SelectObject(dc, titleFont);
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, RGB(0xEF, 0xEF, 0xEF));
    SetTextColor(dc, RGB(0, 0, 0));

    for (const auto& frame : st->leftGroupFrames) {
        RECT rc = frame.rect;
        FrameRect(dc, &rc, frameBrush);

        RECT titleRc = rc;
        titleRc.left += S(hwnd, 14);
        SIZE titleSize{};
        GetTextExtentPoint32W(dc, frame.title, static_cast<int>(wcslen(frame.title)), &titleSize);
        titleRc.top = rc.top - std::max(S(hwnd, 8), static_cast<int>(titleSize.cy) * 2 / 3);
        titleRc.right = std::min(rc.right - S(hwnd, 8), titleRc.left + titleSize.cx + S(hwnd, 24));
        titleRc.bottom = titleRc.top + std::max(S(hwnd, 20), static_cast<int>(titleSize.cy) + S(hwnd, 6));
        ExtTextOutW(dc, titleRc.left, titleRc.top, ETO_OPAQUE, &titleRc, L"", 0, nullptr);
        DrawTextW(dc, frame.title, -1, &titleRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }

    if (oldFont) SelectObject(dc, oldFont);
}

LRESULT CALLBACK leftContentProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR subclassId, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            drawLeftGroupFrames(hwnd, st, dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            HWND ctl = reinterpret_cast<HWND>(lp);
            if (GetPropW(ctl, L"RegularLeftLabel")) {
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(0x00, 0x00, 0xC4));
                return reinterpret_cast<LRESULT>(st ? st->panelBrush : nullptr);
            }
            break;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, leftContentProc, subclassId);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

COLORREF reportRowColor(int row) {
    if (row == 5) return RGB(0x0B, 0x7E, 0xD9);
    if (row == 6) return RGB(0x8F, 0xC0, 0x8A);
    if (row == 16) return RGB(0xD8, 0xD8, 0xD8);
    return RGB(0x8F, 0xC0, 0x8A);
}

void showRightInfoPage(RegularReportState* st) {
    if (!st || !st->rightTab) return;

    const bool showInfo = TabCtrl_GetCurSel(st->rightTab) == 0;
    for (HWND ctl : st->rightInfoControls) {
        if (IsWindow(ctl)) ShowWindow(ctl, showInfo ? SW_SHOW : SW_HIDE);
    }
    if (IsWindow(st->reportList)) ShowWindow(st->reportList, showInfo ? SW_SHOW : SW_HIDE);
    InvalidateRect(st->rightPanel, nullptr, TRUE);
}

void showMiddleResultPage(RegularReportState* st) {
    if (!st || !st->middleTab) return;

    const bool showResult = TabCtrl_GetCurSel(st->middleTab) == 0;
    for (HWND ctl : st->middleResultControls) {
        if (IsWindow(ctl)) ShowWindow(ctl, showResult ? SW_SHOW : SW_HIDE);
    }
    if (IsWindow(st->resultList)) ShowWindow(st->resultList, showResult ? SW_SHOW : SW_HIDE);
    if (IsWindow(st->status)) ShowWindow(st->status, showResult ? SW_SHOW : SW_HIDE);
    InvalidateRect(st->middlePanel, nullptr, TRUE);
}

LRESULT CALLBACK middlePanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR subclassId, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (st && nm->idFrom == IDC_MIDDLE_TAB && nm->code == TCN_SELCHANGE) {
                showMiddleResultPage(st);
                return 0;
            }
            if (nm->idFrom == IDC_RESULT_LIST && nm->code == NM_CUSTOMDRAW) {
                auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    cd->clrTextBk = RGB(0xC0, 0xC0, 0xC0);
                    cd->clrText = RGB(0, 0, 0);
                    return CDRF_NEWFONT;
                }
            }
            break;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, middlePanelProc, subclassId);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK rightPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR subclassId, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            FillRect(dc, &client, st && st->panelBrush ? st->panelBrush : GetSysColorBrush(COLOR_BTNFACE));
            if (st) {
                const RightHeaderLayout header = rightHeaderLayout(hwnd, st->ctx.uiFont, client.right - client.left);
                HGDIOBJ oldFont = st->ctx.uiFont ? SelectObject(dc, st->ctx.uiFont) : nullptr;
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(0, 0, 0xCC));
                RECT line1 = header.line1;
                DrawTextW(dc, RIGHT_SUMMARY_LINE1, -1, &line1, DT_WORDBREAK | DT_CENTER | DT_VCENTER);

                FillRect(dc, &header.line2,
                         st->blackBrush ? st->blackBrush : reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                SetTextColor(dc, RGB(0xFF, 0xFF, 0));
                RECT line2 = header.line2;
                DrawTextW(dc, RIGHT_SUMMARY_LINE2, -1, &line2, DT_WORDBREAK | DT_CENTER | DT_VCENTER);
                if (oldFont) SelectObject(dc, oldFont);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (st && nm->idFrom == IDC_RIGHT_TAB && nm->code == TCN_SELCHANGE) {
                showRightInfoPage(st);
                return 0;
            }
            if (nm->idFrom == IDC_REPORT_LIST && nm->code == NM_CUSTOMDRAW) {
                auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const int row = static_cast<int>(cd->nmcd.dwItemSpec);
                    cd->clrTextBk = reportRowColor(row);
                    cd->clrText = row == 5 ? RGB(0xFF, 0xFF, 0xFF) : RGB(0, 0, 0);
                    if (row == 6) cd->clrText = RGB(0xCC, 0, 0);
                    return CDRF_NEWFONT;
                }
            }
            break;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, rightPanelProc, subclassId);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void addColumns(HWND list, const ColumnDef* columns, int count, HWND scaleHost) {
    for (int i = 0; i < count; ++i) {
        search::add_list_column(list, columns[i].index, columns[i].title, S(scaleHost, columns[i].width));
    }
}

void insertTabs(HWND tab, const wchar_t* const* labels, int count) {
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    for (int i = 0; i < count; ++i) {
        item.pszText = const_cast<wchar_t*>(labels[i]);
        TabCtrl_InsertItem(tab, i, &item);
    }
    TabCtrl_SetCurSel(tab, 0);
}

void setCell(HWND list, int row, int col, const wchar_t* text) {
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(text));
}

void insertResultRow(HWND list, int row, const wchar_t* group, const wchar_t* en,
                     const wchar_t* item, const wchar_t* result, const wchar_t* ref, const wchar_t* unit) {
    LVITEMW itemDef{};
    itemDef.mask = LVIF_TEXT;
    itemDef.iItem = row;
    itemDef.pszText = const_cast<wchar_t*>(L"");
    ListView_InsertItem(list, &itemDef);
    wchar_t no[16]{};
    wsprintfW(no, L"%d", row + 1);
    setCell(list, row, 1, no);
    setCell(list, row, 2, group);
    setCell(list, row, 3, en);
    setCell(list, row, 4, item);
    setCell(list, row, 5, result);
    setCell(list, row, 6, L"");
    setCell(list, row, 7, ref);
    setCell(list, row, 8, unit);
    setCell(list, row, 9, L"");
}

void insertReportRow(HWND list, int row, const wchar_t* sample, const wchar_t* name,
                     const wchar_t* sex, const wchar_t* age, const wchar_t* dept,
                     const wchar_t* bed, const wchar_t* type, const wchar_t* group) {
    LVITEMW itemDef{};
    itemDef.mask = LVIF_TEXT;
    itemDef.iItem = row;
    wchar_t no[16]{};
    wsprintfW(no, L"%d", row + 1);
    itemDef.pszText = no;
    ListView_InsertItem(list, &itemDef);
    setCell(list, row, 1, sample);
    setCell(list, row, 2, name);
    setCell(list, row, 3, sex);
    setCell(list, row, 4, age);
    setCell(list, row, 5, dept);
    setCell(list, row, 6, bed);
    setCell(list, row, 7, L"未打");
    setCell(list, row, 8, type);
    setCell(list, row, 9, L"刘安娜");
    setCell(list, row, 10, group);
}

void seedLists(RegularReportState* st) {
    const ColumnDef resultColumns[] = {
        {0, L"", 32}, {1, L"", 44}, {2, L"组合项目", 82}, {3, L"英文", 72},
        {4, L"项目名称", 132}, {5, L"@结  果", 96}, {6, L"偏", 36},
        {7, L"参考区间", 112}, {8, L"单位", 76}, {9, L"说明", 96},
    };
    addColumns(st->resultList, resultColumns, static_cast<int>(sizeof(resultColumns) / sizeof(resultColumns[0])), st->resultList);
    insertResultRow(st->resultList, 0, L"凝血常规", L"PT", L"*凝血酶原时间", L"14.30", L"11-15", L"s");
    insertResultRow(st->resultList, 1, L"", L"PT-%", L"凝血酶原活动", L"86.00", L"80-120", L"%");
    insertResultRow(st->resultList, 2, L"", L"INR", L"国际标准化比", L"1.15", L"0.8-1.2", L"");
    insertResultRow(st->resultList, 3, L"", L"FIB", L"*纤维蛋白原", L"2.56", L"2-4", L"g/l");
    insertResultRow(st->resultList, 4, L"", L"APTT", L"*活化部分凝血", L"37.70", L"27-45", L"s");
    insertResultRow(st->resultList, 5, L"", L"TT", L"*凝血酶时间", L"15.60", L"11-20", L"s");
    insertResultRow(st->resultList, 6, L"D二聚体", L"D-Dim", L"D-二聚体", L"0.48", L"0-0.5", L"ug/ml");

    const ColumnDef reportColumns[] = {
        {0, L"", 54}, {1, L"样本号", 58}, {2, L"姓名", 70}, {3, L"性别", 36},
        {4, L"年龄", 66}, {5, L"医嘱内容", 110}, {6, L"科室代码", 78},
        {7, L"床号", 46}, {8, L"打印", 64}, {9, L"病人类型", 84},
        {10, L"检验者", 92}, {11, L"项目名称", 120},
    };
    addColumns(st->reportList, reportColumns, static_cast<int>(sizeof(reportColumns) / sizeof(reportColumns[0])), st->reportList);
    const wchar_t* names[] = {L"徐...", L"唐...", L"邱...", L"陈...", L"朱...", L"沈...", L"闫...", L"曾...", L"陶...", L"V...", L"刘...", L"王...", L"郑岚", L"周...", L"熊...", L"王...", L"金...", L"周...", L"刘江"};
    const wchar_t* sexes[] = {L"男", L"男", L"女", L"女", L"男", L"女", L"男", L"男", L"女", L"男", L"男", L"男", L"女", L"男", L"女", L"女", L"男", L"男", L"女"};
    const wchar_t* ages[] = {L"61 岁", L"79 岁", L"64 岁", L"77 岁", L"38 岁", L"39 岁", L"65 岁", L"56 岁", L"72 岁", L"22 岁", L"74 岁", L"80 岁", L"46 岁", L"60 岁", L"20 岁", L"21 岁", L"71 岁", L"78 岁", L"80 岁"};
    const wchar_t* depts[] = {L"呼吸...", L"心血...", L"心血...", L"中医...", L"骨科...", L"骨科...", L"急诊内科", L"急诊ICU", L"急诊ICU", L"骨科...", L"心血...", L"神经...", L"妇产科", L"呼吸...", L"全科...", L"妇科门诊", L"神经...", L"神经...", L"妇产科"};
    for (int i = 0; i < 19; ++i) {
        wchar_t sample[16]{};
        wsprintfW(sample, L"%d", i + 1);
        insertReportRow(st->reportList, i, sample, names[i], sexes[i], ages[i], depts[i],
                        i == 5 ? L"28" : L"58", (i == 6 || i == 14 || i == 15) ? L"门诊" : L"2住院",
                        (i == 12 || i == 15) ? L"血浆D..." : L"凝血报告");
    }
    ListView_SetItemState(st->reportList, 5, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void createLeftPanel(HWND parent, RegularReportState* st) {
    HWND p = st->leftContent;
    auto add = [&](HWND h) { st->leftControls.push_back(h); return h; };
    auto x = [&](int value) { return S(parent, value); };
    auto y = [&](int value) { return S(parent, value); };
    RECT panelRc{};
    if (st->leftPanel) GetClientRect(st->leftPanel, &panelRc);
    const int scrollW = GetSystemMetrics(SM_CXVSCROLL);
    const int panelLogicalW = panelRc.right > panelRc.left
                                  ? static_cast<int>((panelRc.right - panelRc.left - scrollW) /
                                                     std::max(0.1f, search::dpi_scale_factor(parent)))
                                  : 300;
    const int groupX = 8;
    const int innerPad = 6;
    const int labelW = leftLabelWidth(parent, st->ctx.uiFont);
    const int groupW = std::max(240, panelLogicalW - groupX - 8);
    const int groupRight = groupX + groupW - innerPad;
    const int inputX = std::max(groupX + innerPad + labelW + 4, groupX + innerPad + 54);
    const int labelX = std::max(groupX + innerPad, inputX - labelW - 4);
    const int fullInputW = std::max(120, groupRight - inputX);
    const int rightEditW = 72;
    const int rightEditX = groupRight - rightEditW;
    const int narrowRightEditW = 48;
    const int narrowRightEditX = groupRight - narrowRightEditW;
    const int rightLabelW = rightLabelWidth(parent, st->ctx.uiFont);
    const int rightLabelX = rightEditX - rightLabelW - 4;
    const int narrowRightLabelX = narrowRightEditX - rightLabelW - 4;
    const int leftShortW = std::max(42, rightLabelX - inputX - 8);
    const int narrowLeftShortW = std::max(42, narrowRightLabelX - inputX - 8);
    const int fontH = fontLogicalHeight(parent, st->ctx.uiFont);
    const int labelH = std::max(24, fontH + 4);
    const int editH = std::max(22, fontH + 8);
    const int buttonH = std::max(26, fontH + 10);
    const int comboItemH = std::max(18, fontH + 6);
    const int checkSize = std::max(20, fontH + 6);
    const int rowStep = std::max(34, editH + 10);
    const int groupTopPad = std::max(14, fontH);
    const int groupBottomPad = 4;
    const int groupGap = std::max(6, fontH / 3 + 2);
    auto fitW = [&](int px, int requested, int minW = 20) {
        const int maxW = groupRight - px;
        return std::max(minW, std::min(requested, maxW));
    };
    auto group = [&](const wchar_t* text, int px, int py, int w, int h) {
        RECT rc{x(px), y(py), x(px + w), y(py + h)};
        st->leftGroupFrames.push_back({rc, text});
    };
    auto label = [&](const wchar_t* text, int px, int py, int w, int h = 24,
                     DWORD style = SS_RIGHT | SS_CENTERIMAGE | SS_ENDELLIPSIS) {
        HWND hwnd = makeStatic(p, text, x(px), y(py), x(fitW(px, w, 16)), x(std::max(h, labelH)), style | SS_CENTERIMAGE);
        SetPropW(hwnd, L"RegularLeftLabel", reinterpret_cast<HANDLE>(1));
        add(hwnd);
        return hwnd;
    };
    auto edit = [&](const wchar_t* text, int px, int py, int w, int h = 24, DWORD extra = ES_AUTOHSCROLL) {
        const bool multiline = (extra & ES_MULTILINE) != 0;
        add(makeEdit(p, text, x(px), y(py), x(fitW(px, w, 24)), x(multiline ? std::max(h, editH) : editH), extra | ES_CENTER));
    };
    auto button = [&](const wchar_t* text, int px, int py, int w, int h) {
        add(makeButton(p, 0, text, x(px), y(py), x(fitW(px, w, 22)), x(std::max(h, buttonH))));
    };
    auto combo = [&](const wchar_t* text, int px, int py, int w, int h) {
        HWND combo = makeCombo(p, text, x(px), y(py), x(fitW(px, w, 42)), x(std::max(h, comboItemH * 5)));
        SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), x(comboItemH));
        SendMessageW(combo, CB_SETITEMHEIGHT, 0, x(comboItemH));
        add(combo);
    };
    auto datePicker = [&](int px, int py, int w, int h, const wchar_t* format, const SYSTEMTIME* value) {
        add(makeDatePicker(p, x(px), y(py), x(fitW(px, w, 80)), x(std::max(h, editH)), format, value));
    };
    auto check = [&](int px, int py, int w, int h) {
        const int size = std::max({w, h, checkSize});
        add(makeCheckBox(p, x(px), y(py), x(fitW(px, size, 16)), x(size)));
    };

    const int firstGroupTopPad = std::max(10, fontH * 2 / 3 + 2);
    int groupY = firstGroupTopPad;
    auto rowY = [&](int index) { return groupY + groupTopPad + index * rowStep; };
    auto groupHeight = [&](int rows) {
        return groupTopPad + std::max(0, rows - 1) * rowStep + editH + groupBottomPad;
    };

    const int sampleRows = 7;
    const int sampleH = groupHeight(sampleRows);
    group(L"标本信息", groupX, groupY, groupW, sampleH);
    label(L"检验仪器", labelX, rowY(0), labelW);
    const int pickerButtonW = 34;
    const int pickerButtonX = groupRight - pickerButtonW;
    edit(L"凝血 (SF-8200)", inputX, rowY(0) - 2, std::max(80, pickerButtonX - inputX - 6), editH, ES_CENTER | ES_READONLY);
    button(L"...", pickerButtonX, rowY(0) - 2, pickerButtonW, buttonH);
    label(L"组合项目", labelX, rowY(1), labelW); edit(L"凝血报告", inputX, rowY(1) - 2, fullInputW, editH, ES_CENTER | ES_READONLY);
    label(L"标本", labelX, rowY(2), labelW); edit(L"血浆", inputX, rowY(2) - 2, fullInputW, editH, ES_CENTER | ES_READONLY);
    label(L"检验单号", labelX, rowY(3), labelW); edit(L"14027546", inputX, rowY(3) - 2, narrowLeftShortW, editH, ES_CENTER | ES_READONLY);
    label(L"样本号", narrowRightLabelX, rowY(3), rightLabelW); edit(L"6", narrowRightEditX, rowY(3) - 2, narrowRightEditW, editH, ES_CENTER);
    label(L"病人类型", labelX, rowY(4), labelW); combo(L"2-住院", inputX, rowY(4) - 2, narrowLeftShortW, comboItemH * 5);
    label(L"急诊", narrowRightLabelX, rowY(4), rightLabelW); edit(L"", narrowRightEditX, rowY(4) - 2, narrowRightEditW, editH);
    label(L"条形码", labelX, rowY(5), labelW); edit(L"008068540", inputX, rowY(5) - 2, fullInputW, editH, ES_CENTER);
    label(L"住院号:", labelX, rowY(6), labelW); edit(L"202633017", inputX, rowY(6) - 2, fullInputW, editH);

    groupY += sampleH + groupGap;
    const int patientRows = 6;
    const int patientH = groupHeight(patientRows);
    group(L"病人信息", groupX, groupY, groupW, patientH);
    label(L"姓名", labelX, rowY(0), labelW); edit(L"沈画（意）", inputX, rowY(0) - 2, narrowLeftShortW + 6, editH);
    label(L"性别", narrowRightLabelX, rowY(0), rightLabelW); edit(L"女", narrowRightEditX, rowY(0) - 2, narrowRightEditW, editH);
    label(L"年龄", labelX, rowY(1), labelW); edit(L"39", inputX, rowY(1) - 2, 42, editH); combo(L"岁", inputX + 46, rowY(1) - 2, std::max(42, narrowRightLabelX - inputX - 54), comboItemH * 5);
    label(L"床号", narrowRightLabelX, rowY(1), rightLabelW); edit(L"28", narrowRightEditX, rowY(1) - 2, narrowRightEditW, editH);
    label(L"电话", labelX, rowY(2), labelW); edit(L"15211148633", inputX, rowY(2) - 2, fullInputW, editH, ES_CENTER);
    label(L"临床科室", labelX, rowY(3), labelW); edit(L"骨科二区(脊柱外科)", inputX, rowY(3) - 2, fullInputW, editH);
    label(L"临床诊断", labelX, rowY(4), labelW); edit(L"腰椎压缩性骨折", inputX, rowY(4) - 2, fullInputW, editH, ES_CENTER);
    label(L"申请医生", labelX, rowY(5), labelW); edit(L"王攀", inputX, rowY(5) - 2, leftShortW + 6, editH, ES_CENTER);
    label(L"费用", rightLabelX, rowY(5), rightLabelW); edit(L"135.00", rightEditX, rowY(5) - 2, rightEditW, editH, ES_CENTER);

    groupY += patientH + groupGap;
    const int orderRows = 8;
    const int orderH = groupHeight(orderRows);
    group(L"验单信息", groupX, groupY, groupW, orderH);
    label(L"检验者", labelX, rowY(0), labelW); edit(L"刘安娜", inputX, rowY(0) - 2, leftShortW + 6, editH);
    label(L"审核", rightLabelX, rowY(0), rightLabelW); edit(L"涂希晨", rightEditX, rowY(0) - 2, rightEditW, editH);
    label(L"备注", labelX, rowY(1), labelW); edit(L"", inputX, rowY(1) - 2, 42, editH); edit(L"", inputX + 48, rowY(1) - 2, std::max(80, groupRight - inputX - 48), editH);
    SYSTEMTIME applyDate = dateTime(2026, 5, 11, 10, 0);
    SYSTEMTIME receiveDate = dateTime(2026, 5, 12, 4, 47, 37);
    SYSTEMTIME machineDate = dateTime(2026, 5, 12, 4, 50);
    SYSTEMTIME reportDate = dateTime(2026, 5, 12, 5, 39);
    SYSTEMTIME inspectDate = dateTime(2026, 5, 12);
    label(L"申请日期", labelX, rowY(2), labelW); datePicker(inputX, rowY(2) - 2, fullInputW, editH, L"yyyy-MM-dd HH:mm", &applyDate);
    label(L"签收时间", labelX, rowY(3), labelW); datePicker(inputX, rowY(3) - 2, fullInputW, editH, L"yyyy-MM-dd HH:mm:ss", &receiveDate);
    label(L"上机时间", labelX, rowY(4), labelW); datePicker(inputX, rowY(4) - 2, fullInputW, editH, L"yyyy-MM-dd HH:mm", &machineDate);
    label(L"报告时间", labelX, rowY(5), labelW); datePicker(inputX, rowY(5) - 2, fullInputW, editH, L"yyyy-MM-dd HH:mm", &reportDate);
    label(L"检验日期", labelX, rowY(6), labelW); datePicker(inputX, rowY(6) - 2, fullInputW, editH, L"yyyy-MM-dd", &inspectDate);
    label(L"采集日期", labelX, rowY(7), labelW); edit(L"", inputX, rowY(7) - 2, fullInputW, editH);

    st->leftContentHeight = std::max(LEFT_CONTENT_HEIGHT, groupY + orderH + groupGap);
}

void createMiddlePanel(HWND parent, RegularReportState* st) {
    HWND p = st->middlePanel;
    auto addResult = [&](HWND h) { st->middleResultControls.push_back(h); return h; };
    st->middleTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
                                    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                    S(parent, PAD), S(parent, 0), S(parent, 304), S(parent, TAB_H),
                                    p, win32_control_id(IDC_MIDDLE_TAB),
                                    GetModuleHandleW(nullptr), nullptr);
    const wchar_t* tabs[] = {L"检验结果", L"图象", L"手工复查[无]"};
    insertTabs(st->middleTab, tabs, static_cast<int>(sizeof(tabs) / sizeof(tabs[0])));
    const ButtonDef toolbar[] = {
        {5301, L"»  增 加"}, {5302, L"删除项目"}, {5303, L"保存(F2)"}, {5304, L"项目设置"}, {5305, L"发送(F9)"},
    };
    int x = S(parent, 10);
    for (const auto& b : toolbar) {
        addResult(makeButton(p, b.id, b.text, x, S(parent, MIDDLE_TOOLBAR_Y), S(parent, 86), S(parent, COMPACT_BUTTON_H)));
        x += S(parent, 96);
    }
    st->resultList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                     WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                     S(parent, PAD), S(parent, MIDDLE_LIST_Y), S(parent, 520), S(parent, 350),
                                     p, win32_control_id(IDC_RESULT_LIST), GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(st->resultList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    st->status = makeStatic(p, L"结果列表右键功能：项目复制；参数设置。[项目总数：7]", S(parent, PAD), S(parent, 424), S(parent, 520), S(parent, MIDDLE_STATUS_H));
    addResult(st->resultList);
    addResult(st->status);
    showMiddleResultPage(st);
}

void createRightPanel(HWND parent, RegularReportState* st) {
    HWND p = st->rightPanel;
    auto addInfo = [&](HWND h) { st->rightInfoControls.push_back(h); return h; };
    st->rightTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
                                   WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                   S(parent, PAD), S(parent, RIGHT_TAB_Y), S(parent, 576), S(parent, TAB_H),
                                   p, win32_control_id(IDC_RIGHT_TAB),
                                   GetModuleHandleW(nullptr), nullptr);
    const wchar_t* tabs[] = {L"信息列表", L"结果比较"};
    insertTabs(st->rightTab, tabs, static_cast<int>(sizeof(tabs) / sizeof(tabs[0])));
    st->rightSearchLabel = addInfo(makeStatic(p, L"按姓名查", S(parent, PAD), S(parent, RIGHT_SEARCH_LABEL_Y), S(parent, 70), S(parent, 24)));
    st->rightSearchEdit = addInfo(makeEdit(p, L"", S(parent, 82), S(parent, RIGHT_SEARCH_CONTROL_Y), S(parent, 84), S(parent, 26)));
    st->rightSearchIndexButton = addInfo(makeButton(p, 0, L"1", S(parent, 174), S(parent, RIGHT_SEARCH_CONTROL_Y), S(parent, 38), S(parent, COMPACT_BUTTON_H)));
    st->rightSearchUpButton = addInfo(makeButton(p, 0, L"⇧", S(parent, 220), S(parent, RIGHT_SEARCH_CONTROL_Y), S(parent, 38), S(parent, COMPACT_BUTTON_H)));
    st->rightSearchDownButton = addInfo(makeButton(p, 0, L"⇩", S(parent, 266), S(parent, RIGHT_SEARCH_CONTROL_Y), S(parent, 38), S(parent, COMPACT_BUTTON_H)));
    st->rightSearchMenuButton = addInfo(makeButton(p, 0, L"▼", S(parent, 548), S(parent, RIGHT_SEARCH_CONTROL_Y), S(parent, 36), S(parent, COMPACT_BUTTON_H)));
    st->reportList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                     WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                     S(parent, PAD), S(parent, RIGHT_LIST_Y), S(parent, 576), S(parent, 588),
                                     p, win32_control_id(IDC_REPORT_LIST), GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(st->reportList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    showRightInfoPage(st);
}

void createBottomPanel(HWND parent, RegularReportState* st) {
    HWND p = st->bottomPanel;
    const ButtonDef row1[] = {
        {5401, L"1"}, {5402, L"⟳ 刷新(F5)"}, {5403, L"▣ 保存(F1)"}, {5404, L"✓ 审核(F3)"},
        {5405, L"预览(V)"}, {5406, L"打印(F4)"}, {5407, L"✕ 删除(D)"},
        {5408, L"⇧ 上一个"}, {5409, L"⇩ 下一个"}, {5410, L"审核打印"},
    };
    const ButtonDef row2[] = {
        {5411, L"2"}, {5412, L"批审核"}, {5413, L"批取消"}, {5414, L"批录入"},
        {5415, L"批调整"}, {5416, L"批打印"}, {5417, L"批删除"},
        {5418, L"医嘱"}, {5419, L"汇总(F6)"},
    };
    const ButtonDef row3[] = {
        {5420, L"3"}, {5421, L"追踪(Z)"}, {5422, L"计算(F8)"}, {5423, L"合并(U)"},
        {5424, L"图形(T)"}, {5425, L"项目分析"}, {5426, L"日统计"},
        {5427, L"设置"}, {5428, L"审核规则"}, {5429, L"批修改"},
    };
    auto createRow = [&](const ButtonDef* row, int count, int y) {
        int x = S(parent, PAD);
        for (int i = 0; i < count; ++i) {
            int w = (i == 0) ? S(parent, 42) : S(parent, 98);
            makeButton(p, row[i].id, row[i].text, x, S(parent, y), w, S(parent, COMPACT_BUTTON_H));
            x += w + S(parent, PAD);
        }
    };
    createRow(row1, static_cast<int>(sizeof(row1) / sizeof(row1[0])), 4);
    createRow(row2, static_cast<int>(sizeof(row2) / sizeof(row2[0])), 36);
    createRow(row3, static_cast<int>(sizeof(row3) / sizeof(row3[0])), 68);
}

void createControls(HWND hwnd, RegularReportState* st) {
    st->leftPanel = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
                                    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                    0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(st->leftPanel, leftPanelProc, LEFT_PANEL_SUBCLASS, reinterpret_cast<DWORD_PTR>(st));
    st->leftContent = CreateWindowExW(0, L"STATIC", L"",
                                      WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                      0, 0, 0, 0, st->leftPanel, nullptr,
                                      GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(st->leftContent, leftContentProc, LEFT_CONTENT_SUBCLASS, reinterpret_cast<DWORD_PTR>(st));
    st->leftScrollBar = CreateWindowExW(0, L"SCROLLBAR", L"",
                                        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SBS_VERT,
                                        0, 0, 0, 0, st->leftPanel,
                                        win32_control_id(IDC_LEFT_SCROLL),
                                        GetModuleHandleW(nullptr), nullptr);
    st->middlePanel = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
                                      WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                      0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(st->middlePanel, middlePanelProc, MIDDLE_PANEL_SUBCLASS, reinterpret_cast<DWORD_PTR>(st));
    st->rightPanel = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
                                     WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                     0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(st->rightPanel, rightPanelProc, RIGHT_PANEL_SUBCLASS, reinterpret_cast<DWORD_PTR>(st));
    st->bottomPanel = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                      0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    st->splitter = search::create_splitter(hwnd, IDC_SPLITTER, 0, 0, 0, 0, st->ctx.instance);
    createLeftPanel(hwnd, st);
    createMiddlePanel(hwnd, st);
    createRightPanel(hwnd, st);
    createBottomPanel(hwnd, st);
    seedLists(st);
    applyFont(hwnd, st->ctx.uiFont);
    refreshLeftGroupTitleFont(st);
    updateLeftScrollBar(st);
}

void layout(HWND hwnd, RegularReportState* st) {
    if (!st) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const int bottomH = S(hwnd, BOTTOM_PANEL_H);
    const int gap = S(hwnd, GAP);
    const int splitterW = S(hwnd, SPLITTER_W);
    const int leftW = std::max(S(hwnd, 300), w * 21 / 100);
    const int topH = std::max(S(hwnd, 420), h - bottomH - gap);
    const int centerX = leftW + gap;
    const int minCenterW = S(hwnd, 460);
    const int minRightW = S(hwnd, 360);
    const int availableW = std::max(S(hwnd, 760), w - centerX - gap);
    const int defaultCenterW = std::max(S(hwnd, 500), availableW * 46 / 100);
    const int minSplitterX = centerX + minCenterW;
    int maxSplitterX = w - gap - minRightW - splitterW - gap;
    if (maxSplitterX < minSplitterX) maxSplitterX = minSplitterX;
    if (!st->splitterUserSet) st->splitterX = centerX + defaultCenterW;
    st->splitterX = std::clamp(st->splitterX, minSplitterX, maxSplitterX);

    const int centerW = st->splitterX - centerX;
    const int rightX = st->splitterX + splitterW + gap;
    const int rightW = std::max(S(hwnd, 200), w - rightX - gap);

    MoveWindow(st->leftPanel, 0, 0, leftW, h, TRUE);
    scrollLeftPanelTo(st, st->leftScrollY);
    MoveWindow(st->middlePanel, centerX, 0, centerW, topH, TRUE);
    MoveWindow(st->splitter, st->splitterX, 0, splitterW, topH, TRUE);
    MoveWindow(st->rightPanel, rightX, 0, rightW, topH, TRUE);
    MoveWindow(st->bottomPanel, centerX, topH + gap, w - centerX - gap, bottomH, TRUE);

    MoveWindow(st->middleTab, S(hwnd, PAD), S(hwnd, 0), centerW - S(hwnd, PAD * 2), S(hwnd, TAB_H), TRUE);
    MoveWindow(st->resultList, S(hwnd, PAD), S(hwnd, MIDDLE_LIST_Y),
               centerW - S(hwnd, PAD * 2), topH - S(hwnd, MIDDLE_LIST_BOTTOM_MARGIN), TRUE);
    MoveWindow(st->status, S(hwnd, PAD), topH - S(hwnd, MIDDLE_STATUS_BOTTOM),
               centerW - S(hwnd, PAD * 2), S(hwnd, MIDDLE_STATUS_H), TRUE);
    const int rightInnerX = S(hwnd, PAD);
    const int rightInnerW = std::max(S(hwnd, 80), rightW - S(hwnd, PAD * 2));
    const RightHeaderLayout rightHeader = rightHeaderLayout(hwnd, st->ctx.uiFont, rightW);
    const int rightTabY = rightHeader.bottom + S(hwnd, 6);
    const int rightSearchLabelY = rightTabY + S(hwnd, RIGHT_SEARCH_LABEL_Y - RIGHT_TAB_Y);
    const int rightSearchControlY = rightTabY + S(hwnd, RIGHT_SEARCH_CONTROL_Y - RIGHT_TAB_Y);
    const int rightListY = rightTabY + S(hwnd, RIGHT_LIST_Y - RIGHT_TAB_Y);
    MoveWindow(st->rightTab, rightInnerX, rightTabY, rightInnerW, S(hwnd, TAB_H), TRUE);
    MoveWindow(st->rightSearchLabel, S(hwnd, PAD), rightSearchLabelY, S(hwnd, 70), S(hwnd, 24), TRUE);
    MoveWindow(st->rightSearchEdit, S(hwnd, 82), rightSearchControlY, S(hwnd, 84), S(hwnd, 26), TRUE);
    MoveWindow(st->rightSearchIndexButton, S(hwnd, 174), rightSearchControlY, S(hwnd, 38), S(hwnd, COMPACT_BUTTON_H), TRUE);
    MoveWindow(st->rightSearchUpButton, S(hwnd, 220), rightSearchControlY, S(hwnd, 38), S(hwnd, COMPACT_BUTTON_H), TRUE);
    MoveWindow(st->rightSearchDownButton, S(hwnd, 266), rightSearchControlY, S(hwnd, 38), S(hwnd, COMPACT_BUTTON_H), TRUE);
    MoveWindow(st->rightSearchMenuButton, rightW - S(hwnd, PAD + 36), rightSearchControlY, S(hwnd, 36), S(hwnd, COMPACT_BUTTON_H), TRUE);
    MoveWindow(st->reportList, rightInnerX, rightListY,
               rightInnerW, std::max(S(hwnd, 80), topH - rightListY - S(hwnd, PAD)), TRUE);
    InvalidateRect(st->rightPanel, nullptr, TRUE);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<RegularReportState*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE:
            st = g_pending;
            g_pending = nullptr;
            SetPropW(hwnd, PROP_STATE, reinterpret_cast<HANDLE>(st));
            st->bgBrush = CreateSolidBrush(RGB(0xB8, 0xB8, 0xB8));
            st->panelBrush = CreateSolidBrush(RGB(0xEF, 0xEF, 0xEF));
            st->blackBrush = CreateSolidBrush(RGB(0, 0, 0));
            st->pendingSplitterX = search::load_module_int(L"RegularReport", L"SplitterX", 0);
            createControls(hwnd, st);
            layout(hwnd, st);
            return 0;
        case WM_SIZE:
            if (st && st->pendingSplitterX > 0 && IsZoomed(hwnd)) {
                st->splitterX = st->pendingSplitterX;
                st->splitterUserSet = true;
                st->pendingSplitterX = 0;
            }
            layout(hwnd, st);
            return 0;
        case search::WM_SPLITTER_DRAG:
            if (st && reinterpret_cast<HWND>(lp) == st->splitter) {
                st->splitterX = static_cast<int>(wp);
                st->splitterUserSet = true;
                layout(hwnd, st);
            }
            return 0;
        case search::WM_SPLITTER_RELEASED:
            if (st && reinterpret_cast<HWND>(lp) == st->splitter) {
                st->splitterX = static_cast<int>(wp);
                st->splitterUserSet = true;
                layout(hwnd, st);
                search::save_module_int(L"RegularReport", L"SplitterX", st->splitterX);
            }
            return 0;
        case app::WM_APP_FONT_CHANGED:
            if (st && lp) {
                st->ctx.uiFont = reinterpret_cast<HFONT>(lp);
                clearLeftPanel(st);
                createLeftPanel(hwnd, st);
                applyFont(hwnd, st->ctx.uiFont);
                refreshLeftGroupTitleFont(st);
                layout(hwnd, st);
            }
            return 0;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            HWND ctl = reinterpret_cast<HWND>(lp);
            if (GetPropW(ctl, L"RegularLeftLabel")) {
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(0x00, 0x00, 0xC4));
                return reinterpret_cast<LRESULT>(st ? st->panelBrush : nullptr);
            }
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(0, 0, 0xCC));
            return reinterpret_cast<LRESULT>(st ? st->panelBrush : nullptr);
        }
        case WM_CTLCOLORDLG:
            return reinterpret_cast<LRESULT>(st ? st->bgBrush : nullptr);
        case WM_DESTROY:
            RemovePropW(hwnd, PROP_STATE);
            if (st) {
                if (st->bgBrush) DeleteObject(st->bgBrush);
                if (st->panelBrush) DeleteObject(st->panelBrush);
                if (st->blackBrush) DeleteObject(st->blackBrush);
                if (st->groupTitleFont) DeleteObject(st->groupTitleFont);
                delete st;
            }
            break;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_regular_report_module(const ModuleContext& ctx) {
    if (HWND existing = activate_existing_mdi_child_by_title(ctx.mdiClient, WINDOW_TITLE)) {
        return existing;
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = wndProc;
        wc.hInstance = ctx.instance;
        wc.hIcon = LoadIconW(ctx.instance, MAKEINTRESOURCEW(IDI_APP));
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = WND_CLASS;
        RegisterClassExW(&wc);
        registered = true;
    }

    auto* st = new RegularReportState;
    st->ctx = ctx;

    MDICREATESTRUCTW mcs{};
    mcs.szClass = WND_CLASS;
    mcs.szTitle = WINDOW_TITLE;
    mcs.hOwner = ctx.instance;
    mcs.x = mcs.y = mcs.cx = mcs.cy = CW_USEDEFAULT;

    g_pending = st;
    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&mcs)));
    if (child) {
        SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    } else {
        g_pending = nullptr;
        delete st;
    }
    return child;
}

#endif
