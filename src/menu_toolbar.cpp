#include "menu_toolbar.h"

#ifdef _WIN32

#include "win32_control_id.h"

#include <windowsx.h>

#include <algorithm>

namespace {

struct MTButton {
    const wchar_t* text = nullptr;
    int id = 0;
    int flags = 0;
    HICON icon = nullptr;
    RECT rect{};
};

struct MTState {
    HFONT font = nullptr;
    MTButton btns[MT_MAXBTN];
    int count = 0;
    int hover = -1;
    int pressed = -1;
    int activeId = 0;
    int focus = -1;
    bool keyboardFocusVisible = false;
    bool mouseFocusRequest = false;
    int btnH = 42;
};

constexpr COLORREF kToolbarBg = RGB(245, 248, 251);
constexpr COLORREF kToolbarLine = RGB(214, 224, 234);
constexpr COLORREF kButtonHover = RGB(232, 242, 255);
constexpr COLORREF kButtonPressed = RGB(214, 233, 255);
constexpr COLORREF kButtonActive = RGB(221, 238, 255);
constexpr COLORREF kButtonFocus = RGB(91, 155, 213);
constexpr COLORREF kText = RGB(31, 41, 51);
constexpr COLORREF kTextDisabled = RGB(154, 165, 177);
constexpr COLORREF kSeparator = RGB(208, 218, 229);
constexpr COLORREF kCloseBorder = RGB(205, 214, 224);
constexpr COLORREF kCloseDisabledBorder = RGB(225, 231, 238);
constexpr COLORREF kCloseText = RGB(75, 85, 99);
constexpr COLORREF kCloseHoverBg = RGB(255, 241, 242);
constexpr COLORREF kClosePressedBg = RGB(255, 228, 230);
constexpr COLORREF kCloseHoverBorder = RGB(253, 164, 175);
constexpr COLORREF kCloseHoverText = RGB(185, 28, 28);

void mtUpdateMetrics(HWND hwnd, MTState* s) {
    if (!s) return;
    s->btnH = 42;
    if (!s->font) return;
    HDC dc = GetDC(hwnd);
    HFONT old = (HFONT)SelectObject(dc, s->font);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    s->btnH = std::max(40, static_cast<int>(tm.tmHeight) + 22);
}

void fillRoundRect(HDC dc, const RECT& rc, int radius, COLORREF color) {
    HBRUSH br = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldBr = SelectObject(dc, br);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBr);
    DeleteObject(pen);
    DeleteObject(br);
}

void strokeRoundRect(HDC dc, const RECT& rc, int radius, COLORREF color) {
    HBRUSH oldBr = (HBRUSH)SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBr);
    DeleteObject(pen);
}

int firstEnabledButton(const MTState* s) {
    if (!s) return -1;
    for (int i = 0; i < s->count; ++i) {
        if (!(s->btns[i].flags & (MTBS_SEPARATOR | MTBS_STRETCH | MTBS_DISABLED))) {
            return i;
        }
    }
    return -1;
}

void drawToolbar(MTState* s, HWND hwnd, HDC dc) {
    RECT rc;
    GetClientRect(hwnd, &rc);

    HBRUSH bg = CreateSolidBrush(kToolbarBg);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    RECT line = {rc.left, rc.bottom - 1, rc.right, rc.bottom};
    HBRUSH lineBr = CreateSolidBrush(kToolbarLine);
    FillRect(dc, &line, lineBr);
    DeleteObject(lineBr);

    HFONT oldFont = s->font ? (HFONT)SelectObject(dc, s->font) : nullptr;
    SetBkMode(dc, TRANSPARENT);

    // Pass 1: measure button widths.
    int totalW = 20;
    for (int i = 0; i < s->count; ++i) {
        auto& b = s->btns[i];
        if (b.flags & MTBS_SEPARATOR) {
            totalW += 12;
            continue;
        }
        if (b.flags & MTBS_STRETCH) continue;
        SIZE sz{};
        GetTextExtentPoint32W(dc, b.text, (int)wcslen(b.text), &sz);
        int iconSz = b.icon ? std::max(16, static_cast<int>(sz.cy)) : 0;
        totalW += sz.cx + 28 + iconSz + (b.icon ? 8 : 0) + 4;
    }
    int stretchW = rc.right - totalW;
    if (stretchW < 0) stretchW = 0;

    // Pass 2: draw.
    int x = 10;
    int buttonTop = 5;
    int buttonBottom = std::max(buttonTop + 28, static_cast<int>(rc.bottom) - 6);
    for (int i = 0; i < s->count; ++i) {
        auto& b = s->btns[i];

        if (b.flags & MTBS_STRETCH) {
            x += stretchW;
            continue;
        }

        if (b.flags & MTBS_SEPARATOR) {
            int sx = x + 5;
            RECT sr = {sx, 10, sx + 1, rc.bottom - 10};
            HBRUSH sepBr = CreateSolidBrush(kSeparator);
            FillRect(dc, &sr, sepBr);
            DeleteObject(sepBr);
            b.rect = {sx, 0, sx + 1, rc.bottom};
            x = sx + 12;
            continue;
        }

        SIZE sz{};
        GetTextExtentPoint32W(dc, b.text, (int)wcslen(b.text), &sz);
        int iconSz = b.icon ? std::max(16, static_cast<int>(sz.cy)) : 0;
        int iconW = b.icon ? iconSz + 8 : 0;
        RECT br = {x, buttonTop, x + sz.cx + 28 + iconW, buttonBottom};
        bool isHover = (i == s->hover);
        bool isPressed = (i == s->pressed);
        bool isActive = (b.id != 0 && b.id == s->activeId);
        bool isFocus = (i == s->focus);
        bool isDisabled = (b.flags & MTBS_DISABLED);
        bool isCloseButton = (b.flags & MTBS_CLOSE) != 0;

        if (isCloseButton) {
            const COLORREF border = isDisabled ? kCloseDisabledBorder : (isHover || isPressed ? kCloseHoverBorder : kCloseBorder);
            if (!isDisabled && (isHover || isPressed)) {
                fillRoundRect(dc, br, 8, isPressed ? kClosePressedBg : kCloseHoverBg);
            }
            RECT cr = br;
            InflateRect(&cr, -1, -1);
            strokeRoundRect(dc, cr, 8, border);
        } else if (!isDisabled && (isActive || isPressed || isHover)) {
            fillRoundRect(dc, br, 8, isPressed ? kButtonPressed : (isHover ? kButtonHover : kButtonActive));
        }

        if (!isCloseButton && isActive && !isDisabled) {
            RECT ar = br;
            InflateRect(&ar, -1, -1);
            strokeRoundRect(dc, ar, 8, kButtonFocus);
        }

        if (s->keyboardFocusVisible && isFocus && !isDisabled) {
            RECT fr = br;
            InflateRect(&fr, -1, -1);
            strokeRoundRect(dc, fr, 8, kButtonFocus);
        }

        COLORREF textColor = isDisabled ? kTextDisabled : kText;
        if (isCloseButton && !isDisabled) {
            textColor = (isHover || isPressed) ? kCloseHoverText : kCloseText;
        }
        SetTextColor(dc, textColor);

        int contentX = x + 14;
        int pressedOffset = isPressed ? 1 : 0;
        if (b.icon) {
            int iy = (br.top + br.bottom - iconSz) / 2 + pressedOffset;
            DrawIconEx(dc, contentX, iy, b.icon, iconSz, iconSz, 0, nullptr, DI_NORMAL);
            contentX += iconW;
        }

        RECT textRect = br;
        textRect.left = contentX;
        textRect.right -= 12;
        OffsetRect(&textRect, 0, pressedOffset);
        DrawTextW(dc, b.text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        b.rect = br;
        x = br.right + 4;
    }

    if (oldFont) SelectObject(dc, oldFont);
}

LRESULT CALLBACK mtProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* s = reinterpret_cast<MTState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE:
            s = new MTState;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
            return TRUE;
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            s->font = reinterpret_cast<HFONT>(cs->lpCreateParams);
            mtUpdateMetrics(hwnd, s);
            return 0;
        }
        case WM_NCDESTROY:
            delete s;
            return 0;

        case WM_SETFOCUS:
            if (s->mouseFocusRequest) {
                s->mouseFocusRequest = false;
                s->keyboardFocusVisible = false;
            } else {
                s->keyboardFocusVisible = true;
                if (s->focus < 0 || s->focus >= s->count ||
                        (s->btns[s->focus].flags & (MTBS_SEPARATOR | MTBS_STRETCH | MTBS_DISABLED))) {
                    s->focus = firstEnabledButton(s);
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_KILLFOCUS:
            s->focus = -1;
            s->pressed = -1;
            s->keyboardFocusVisible = false;
            s->mouseFocusRequest = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;

        case WM_KEYDOWN: {
            s->keyboardFocusVisible = true;
            if (s->focus < 0 || s->focus >= s->count) {
                s->focus = firstEnabledButton(s);
            }
            int next = s->focus;
            switch (wp) {
                case VK_RIGHT: case VK_TAB:
                    next++; break;
                case VK_LEFT:
                    next--; break;
                case VK_RETURN: case VK_SPACE:
                    if (s->focus >= 0 && s->focus < s->count && !(s->btns[s->focus].flags & MTBS_DISABLED))
                        PostMessageW(GetParent(hwnd), WM_COMMAND, s->btns[s->focus].id, 0);
                    return 0;
                default: return 0;
            }
            // Find next non-separator, non-disabled button
            int step = (wp == VK_LEFT) ? -1 : 1;
            for (int i = next; i >= 0 && i < s->count; i += step) {
                if (!(s->btns[i].flags & (MTBS_SEPARATOR | MTBS_DISABLED))) {
                    s->focus = i;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            int hit = -1;
            for (int i = 0; i < s->count; ++i) {
                if (s->btns[i].flags & MTBS_SEPARATOR) continue;
                if (PtInRect(&s->btns[i].rect, {x, y})) { hit = i; break; }
            }
            if (hit != s->hover) {
                s->hover = hit;
                if (hit != -1) { SetCapture(hwnd); }
                else ReleaseCapture();
                InvalidateRect(hwnd, nullptr, FALSE);
                SetTimer(hwnd, 1, hit != -1 ? 50 : 0, nullptr);
            }
            return 0;
        }
        case WM_TIMER: {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
            RECT rc; GetClientRect(hwnd, &rc);
            if (!PtInRect(&rc, pt)) {
                s->hover = -1;
                ReleaseCapture();
                KillTimer(hwnd, 1);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
            if (s->hover >= 0 && s->hover < s->count && !(s->btns[s->hover].flags & MTBS_DISABLED)) {
                s->pressed = s->hover;
                s->focus = s->hover;
                s->keyboardFocusVisible = false;
                s->mouseFocusRequest = true;
                SetCapture(hwnd);
                SetFocus(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (s->pressed >= 0 && s->pressed == s->hover &&
                    s->hover < s->count && !(s->btns[s->hover].flags & MTBS_DISABLED)) {
                PostMessageW(GetParent(hwnd), WM_COMMAND, s->btns[s->hover].id, 0);
            }
            s->pressed = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_CANCELMODE:
            s->pressed = -1;
            s->hover = -1;
            s->keyboardFocusVisible = false;
            s->mouseFocusRequest = false;
            ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HDC memDc = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right - rc.left, rc.bottom - rc.top);
            HGDIOBJ oldBmp = SelectObject(memDc, bmp);
            drawToolbar(s, hwnd, memDc);
            BitBlt(dc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, memDc, 0, 0, SRCCOPY);
            SelectObject(memDc, oldBmp);
            DeleteObject(bmp);
            DeleteDC(memDc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SIZE:
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND mtCreate(HWND parent, HINSTANCE inst, HFONT font, int ctrlId) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = mtProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"MenuToolbar";
        RegisterClassW(&wc);
        registered = true;
    }
    return CreateWindowExW(0, L"MenuToolbar", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 42, parent,
        win32_control_id(ctrlId), inst, font);
}

void mtSetFont(HWND hwnd, HFONT font) {
    auto* s = reinterpret_cast<MTState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!s) return;
    s->font = font;
    mtUpdateMetrics(hwnd, s);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void mtAddButton(HWND hwnd, const wchar_t* text, int cmdId, bool enabled) {
    mtAddButton(hwnd, text, cmdId, nullptr, enabled);
}

void mtAddButton(HWND hwnd, const wchar_t* text, int cmdId, HICON icon, bool enabled) {
    auto* s = reinterpret_cast<MTState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (s && s->count < MT_MAXBTN) {
        int flags = enabled ? 0 : MTBS_DISABLED;
        s->btns[s->count++] = {text, cmdId, flags, icon};
        InvalidateRect(hwnd, nullptr, TRUE);
    }
}

void mtAddCloseButton(HWND hwnd, const wchar_t* text, int cmdId, bool enabled) {
    auto* s = reinterpret_cast<MTState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (s && s->count < MT_MAXBTN) {
        int flags = MTBS_CLOSE | (enabled ? 0 : MTBS_DISABLED);
        s->btns[s->count++] = {text, cmdId, flags, nullptr};
        InvalidateRect(hwnd, nullptr, TRUE);
    }
}

void mtAddStretch(HWND hwnd) {
    auto* s = reinterpret_cast<MTState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (s && s->count < MT_MAXBTN) {
        s->btns[s->count++] = {nullptr, 0, MTBS_STRETCH};
        InvalidateRect(hwnd, nullptr, TRUE);
    }
}

void mtAddSeparator(HWND hwnd) {
    auto* s = reinterpret_cast<MTState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (s && s->count < MT_MAXBTN) {
        s->btns[s->count++] = {nullptr, 0, MTBS_SEPARATOR};
        InvalidateRect(hwnd, nullptr, TRUE);
    }
}

void mtEnableButton(HWND hwnd, int cmdId, bool enable) {
    auto* s = reinterpret_cast<MTState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!s) return;
    for (int i = 0; i < s->count; ++i) {
        if (s->btns[i].id == cmdId) {
            if (enable) s->btns[i].flags &= ~MTBS_DISABLED;
            else        s->btns[i].flags |= MTBS_DISABLED;
            InvalidateRect(hwnd, nullptr, TRUE);
            return;
        }
    }
}

void mtSetActiveButton(HWND hwnd, int cmdId) {
    auto* s = reinterpret_cast<MTState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!s || s->activeId == cmdId) return;
    s->activeId = cmdId;
    InvalidateRect(hwnd, nullptr, TRUE);
}

int mtGetHeight(HWND hwnd) {
    auto* s = reinterpret_cast<MTState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return s ? s->btnH : 42;
}

#endif
