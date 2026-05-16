#include "search_splitter.h"

#ifdef _WIN32

namespace search {

namespace {

constexpr const wchar_t* kSplitterClass = L"LISWorkbenchSplitter";

int parent_cursor_x(HWND hwnd) {
    POINT pt{};
    GetCursorPos(&pt);
    ScreenToClient(GetParent(hwnd), &pt);

    RECT rc{};
    GetWindowRect(hwnd, &rc);
    return pt.x - ((rc.right - rc.left) / 2);
}

LRESULT CALLBACK splitter_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return TRUE;
        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return 0;
        case WM_MOUSEMOVE:
            if (GetCapture() == hwnd && (wp & MK_LBUTTON)) {
                SendMessageW(GetParent(hwnd), WM_SPLITTER_DRAG,
                             static_cast<WPARAM>(parent_cursor_x(hwnd)),
                             reinterpret_cast<LPARAM>(hwnd));
            }
            return 0;
        case WM_LBUTTONUP:
            if (GetCapture() == hwnd) {
                ReleaseCapture();
                SendMessageW(GetParent(hwnd), WM_SPLITTER_RELEASED,
                             static_cast<WPARAM>(parent_cursor_x(hwnd)),
                             reinterpret_cast<LPARAM>(hwnd));
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, GetSysColorBrush(COLOR_3DSHADOW));
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

void register_splitter_class(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = splitter_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_SIZEWE);
    wc.hbrBackground = GetSysColorBrush(COLOR_3DSHADOW);
    wc.lpszClassName = kSplitterClass;
    RegisterClassExW(&wc);
    registered = true;
}

HWND create_splitter(HWND parent, int id, int x, int y, int w, int h, HINSTANCE instance) {
    register_splitter_class(instance);
    return CreateWindowExW(0, kSplitterClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                           x, y, w, h, parent, reinterpret_cast<HMENU>(id), instance, nullptr);
}

}  // namespace search

#endif
