#include "blood_module.h"

#ifdef _WIN32

#include "resource.h"
#include <windows.h>

namespace {

constexpr const wchar_t* WND_CLASS  = L"BloodModuleChild";
constexpr const wchar_t* PROP_STATE = L"BloodSt";

struct BloodState {
    ModuleContext ctx;
};

BloodState* g_pending = nullptr;

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<BloodState*>(GetPropW(hwnd, PROP_STATE));

    switch (msg) {
        case WM_CREATE: {
            st = g_pending;
            g_pending = nullptr;
            SetPropW(hwnd, PROP_STATE, reinterpret_cast<HANDLE>(st));

            CreateWindowExW(0, L"STATIC", L"输血结果查询 — 待开发",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, 0, 0, 0, hwnd, nullptr, st->ctx.instance, nullptr);
            return 0;
        }
        case WM_SIZE: {
            HWND label = GetWindow(hwnd, GW_CHILD);
            if (label) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                MoveWindow(label, 0, 0, rc.right, rc.bottom, TRUE);
            }
            return 0;
        }
        case WM_DESTROY:
            RemovePropW(hwnd, PROP_STATE);
            delete st;
            break;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_blood_module(const ModuleContext& ctx) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = wndProc;
        wc.hInstance = ctx.instance;
        wc.hIcon = LoadIconW(ctx.instance, MAKEINTRESOURCEW(IDI_APP));
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = WND_CLASS;
        RegisterClassExW(&wc);
        registered = true;
    }

    auto* st = new BloodState;
    st->ctx = ctx;

    MDICREATESTRUCTW mcs{};
    mcs.szClass = WND_CLASS;
    mcs.szTitle = L"输血结果查询";
    mcs.hOwner = ctx.instance;
    mcs.x = mcs.y = mcs.cx = mcs.cy = CW_USEDEFAULT;

    g_pending = st;
    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0,
        reinterpret_cast<LPARAM>(&mcs)));
    SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    return child;
}

#endif
