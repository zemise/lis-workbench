#include "menu_toolbar.h"

#ifdef _WIN32

#include <windowsx.h>

namespace {

struct MTButton { const wchar_t* text; int id; RECT rect; };

struct MTState {
    HFONT font = nullptr;
    MTButton btns[MT_MAXBTN];
    int count = 0;
    int hover = -1;
};

LRESULT CALLBACK mtProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* s = reinterpret_cast<MTState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            s = new MTState;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
            return TRUE;
        }
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            s->font = reinterpret_cast<HFONT>(cs->lpCreateParams);
            return 0;
        }
        case WM_NCDESTROY:
            delete s;
            return 0;

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            int hit = -1;
            for (int i = 0; i < s->count; ++i)
                if (PtInRect(&s->btns[i].rect, {x, y})) { hit = i; break; }
            if (hit != s->hover) {
                s->hover = hit;
                if (hit != -1) SetCapture(hwnd); else ReleaseCapture();
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
            if (s->hover >= 0 && s->hover < s->count)
                PostMessageW(GetParent(hwnd), WM_COMMAND, s->btns[s->hover].id, 0);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);

            // Menu-colored background
            HBRUSH menuBr = GetSysColorBrush(COLOR_MENU);
            FillRect(dc, &rc, menuBr);

            HFONT oldFont = s->font ? (HFONT)SelectObject(dc, s->font) : nullptr;
            SetBkMode(dc, TRANSPARENT);

            int x = 4;
            for (int i = 0; i < s->count; ++i) {
                const auto& b = s->btns[i];
                SIZE sz; GetTextExtentPoint32W(dc, b.text, (int)wcslen(b.text), &sz);
                RECT br = {x, 0, x + sz.cx + 16, rc.bottom};
                if (i == s->hover) {
                    FillRect(dc, &br, GetSysColorBrush(COLOR_MENUHILIGHT));
                }
                SetTextColor(dc, GetSysColor(COLOR_MENUTEXT));
                DrawTextW(dc, b.text, -1, &br, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                s->btns[i].rect = br;
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
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 28,
        parent, reinterpret_cast<HMENU>(static_cast<intptr_t>(ctrlId)), inst, font);
}

void mtAddButton(HWND hwnd, const wchar_t* text, int cmdId) {
    auto* s = reinterpret_cast<MTState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (s && s->count < MT_MAXBTN) {
        s->btns[s->count++] = {text, cmdId};
        InvalidateRect(hwnd, nullptr, TRUE);
    }
}

#endif
