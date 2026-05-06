#include "menu_toolbar.h"

#ifdef _WIN32

#include <windowsx.h>

namespace {

struct MTButton {
    const wchar_t* text = nullptr;
    int id = 0;
    int flags = 0;
    RECT rect{};
};

struct MTState {
    HFONT font = nullptr;
    MTButton btns[MT_MAXBTN];
    int count = 0;
    int hover = -1;
    int focus = -1;
    int btnH = 24;
};

COLORREF mtBlend(COLORREF c1, COLORREF c2, double t) {
    return RGB(
        (BYTE)(GetRValue(c1) * (1 - t) + GetRValue(c2) * t),
        (BYTE)(GetGValue(c1) * (1 - t) + GetGValue(c2) * t),
        (BYTE)(GetBValue(c1) * (1 - t) + GetBValue(c2) * t));
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
            if (s->font) {
                HDC dc = GetDC(hwnd);
                HFONT old = (HFONT)SelectObject(dc, s->font);
                TEXTMETRICW tm; GetTextMetricsW(dc, &tm);
                SelectObject(dc, old);
                ReleaseDC(hwnd, dc);
                s->btnH = tm.tmHeight + 8;
            }
            return 0;
        }
        case WM_NCDESTROY:
            delete s;
            return 0;

        case WM_SETFOCUS:
            s->focus = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_KILLFOCUS:
            s->focus = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;

        case WM_KEYDOWN: {
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
                if (hit != -1) { SetCapture(hwnd); SetFocus(hwnd); }
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
        case WM_LBUTTONUP:
            if (s->hover >= 0 && s->hover < s->count && !(s->btns[s->hover].flags & MTBS_DISABLED)) {
                PostMessageW(GetParent(hwnd), WM_COMMAND, s->btns[s->hover].id, 0);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);

            // Menu-colored background
            COLORREF menuBg = GetSysColor(COLOR_MENU);
            HBRUSH menuBr = GetSysColorBrush(COLOR_MENU);
            FillRect(dc, &rc, menuBr);

            HFONT oldFont = s->font ? (HFONT)SelectObject(dc, s->font) : nullptr;
            SetBkMode(dc, TRANSPARENT);

            int x = 6;
            for (int i = 0; i < s->count; ++i) {
                auto& b = s->btns[i];

                if (b.flags & MTBS_SEPARATOR) {
                    // Vertical separator line
                    int sx = x + 3;
                    RECT sr = {sx, 6, sx + 1, rc.bottom - 6};
                    COLORREF sepColor = mtBlend(menuBg, GetSysColor(COLOR_MENUTEXT), 0.3);
                    HBRUSH sepBr = CreateSolidBrush(sepColor);
                    FillRect(dc, &sr, sepBr);
                    DeleteObject(sepBr);
                    b.rect = {sx, 0, sx + 1, rc.bottom};
                    x = sx + 8;
                    continue;
                }

                SIZE sz; GetTextExtentPoint32W(dc, b.text, (int)wcslen(b.text), &sz);
                RECT br = {x, 0, x + sz.cx + 20, rc.bottom};
                bool isHover = (i == s->hover);
                bool isFocus = (i == s->focus);
                bool isDisabled = (b.flags & MTBS_DISABLED);

                if (isHover && !isDisabled) {
                    COLORREF hilight = GetSysColor(COLOR_MENUHILIGHT);
                    HBRUSH hibr = CreateSolidBrush(hilight);
                    FillRect(dc, &br, hibr);
                    DeleteObject(hibr);
                }

                COLORREF textColor = isDisabled
                    ? mtBlend(menuBg, GetSysColor(COLOR_MENUTEXT), 0.5)
                    : GetSysColor(COLOR_MENUTEXT);
                SetTextColor(dc, textColor);

                DrawTextW(dc, b.text, -1, &br, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                // Focus rectangle
                if (isFocus && !isDisabled) {
                    RECT fr = br;
                    InflateRect(&fr, -2, -3);
                    DrawFocusRect(dc, &fr);
                }

                b.rect = br;
                x = br.right + 2;
            }

            if (oldFont) SelectObject(dc, oldFont);
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
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_MENU + 1);
        wc.lpszClassName = L"MenuToolbar";
        RegisterClassW(&wc);
        registered = true;
    }
    return CreateWindowExW(0, L"MenuToolbar", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 28, parent,
        reinterpret_cast<HMENU>(static_cast<intptr_t>(ctrlId)), inst, font);
}

void mtAddButton(HWND hwnd, const wchar_t* text, int cmdId, bool enabled) {
    auto* s = reinterpret_cast<MTState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (s && s->count < MT_MAXBTN) {
        int flags = enabled ? 0 : MTBS_DISABLED;
        s->btns[s->count++] = {text, cmdId, flags};
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

int mtGetHeight(HWND hwnd) {
    auto* s = reinterpret_cast<MTState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return s ? s->btnH : 24;
}

#endif
