#define _WINSOCKAPI_    // prevent windows.h from including winsock.h
#include <winsock2.h>   // must come before windows.h
#include "main_app.h"
#include "resource.h"
#include "search_text.h"

#include <vector>
#include <windows.h>
#include <iphlpapi.h>
#include <commctrl.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

#include "app_settings.h"
#include "app_settings_io.h"
#include "menu_toolbar.h"
#include "module_registry.h"
#include "query_module.h"
#include "settings_module.h"
namespace {

constexpr int IDM_QUERY        = 1001;
constexpr int IDM_BLOOD        = 1002;
constexpr int IDM_SETTINGS     = 2001;
constexpr int IDM_EXIT         = 2002;
constexpr int IDM_ABOUT        = 2003;
constexpr int IDM_CASCADE      = 2101;
constexpr int IDM_TILE_H       = 2102;
constexpr int IDM_TILE_V       = 2103;
constexpr int IDM_ARRANGE      = 2104;
constexpr int IDM_CLOSE_ACTIVE = 2105;
constexpr int ID_TOOLBAR       = 3100;
constexpr int ID_BTNCLOSE      = 3101;
constexpr int IDM_TOOL1        = 3011;
constexpr int IDM_TOOL2        = 3012;
constexpr int IDM_TOOL3        = 3013;
constexpr int IDM_TOOL4        = 3014;
constexpr int IDM_TOOL5        = 3015;
constexpr int ID_STATUS        = 4001;
constexpr int ID_TIMER         = 5001;

constexpr const wchar_t* MDI_CHILD_CLASS = L"MdiPlaceholderChild";

app::Context g_ctx;

LRESULT CALLBACK mdiChildProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            wchar_t title[256];
            GetWindowTextW(hwnd, title, 256);
            CreateWindowExW(0, L"STATIC", title,
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, 0, 0, 0, hwnd, nullptr, g_ctx.instance, nullptr);
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
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

HWND createMdiChild(const wchar_t* title) {
    MDICREATESTRUCTW mcs{};
    mcs.szClass = MDI_CHILD_CLASS;
    mcs.szTitle = title;
    mcs.hOwner = g_ctx.instance;
    mcs.x = CW_USEDEFAULT;
    mcs.y = CW_USEDEFAULT;
    mcs.cx = CW_USEDEFAULT;
    mcs.cy = CW_USEDEFAULT;
    HWND child = reinterpret_cast<HWND>(SendMessageW(g_ctx.mdiClient, WM_MDICREATE, 0,
        reinterpret_cast<LPARAM>(&mcs)));
    SendMessageW(g_ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    return child;
}

void closeActiveMdiChild() {
    HWND active = reinterpret_cast<HWND>(SendMessageW(g_ctx.mdiClient, WM_MDIGETACTIVE, 0, 0));
    if (active) SendMessageW(g_ctx.mdiClient, WM_MDIDESTROY, reinterpret_cast<WPARAM>(active), 0);
}

// ── placeholder factories (to be replaced with real modules) ────

HWND create_blood_placeholder(const ModuleContext&) { return createMdiChild(L"输血结果查询"); }
HWND create_tool1_placeholder(const ModuleContext&) { return createMdiChild(L"工具1"); }
HWND create_tool2_placeholder(const ModuleContext&) { return createMdiChild(L"工具2"); }
HWND create_tool3_placeholder(const ModuleContext&) { return createMdiChild(L"工具3"); }
HWND create_tool4_placeholder(const ModuleContext&) { return createMdiChild(L"工具4"); }
HWND create_tool5_placeholder(const ModuleContext&) { return createMdiChild(L"工具5"); }

// ── module registry ─────────────────────────────────────────────

const ModuleDef g_modules[] = {
    { L"Query",    L"检验管理", L"检验结果查询(&Q)...", IDM_QUERY,    create_query_module    },
    { L"Blood",    L"检验管理", L"输血结果查询(&B)...", IDM_BLOOD,    create_blood_placeholder },
    { L"Tool1",    L"工具",     L"工具1(&1)",           IDM_TOOL1,   create_tool1_placeholder },
    { L"Tool2",    L"工具",     L"工具2(&2)",           IDM_TOOL2,   create_tool2_placeholder },
    { L"Tool3",    L"工具",     L"工具3(&3)",           IDM_TOOL3,   create_tool3_placeholder },
    { L"Tool4",    L"工具",     L"工具4(&4)",           IDM_TOOL4,   create_tool4_placeholder },
    { L"Tool5",    L"工具",     L"工具5(&5)",           IDM_TOOL5,   create_tool5_placeholder },
    { L"Settings", L"系统",     L"系统设置(&S)...",     IDM_SETTINGS, create_settings_module  },
};
constexpr int g_moduleCount = sizeof(g_modules) / sizeof(g_modules[0]);

// ── helpers ─────────────────────────────────────────────────────

void updateStatusBarParts(HWND sb, int clientWidth) {
    int ipW  = (std::max)(220, clientWidth * 22 / 100);
    int timeW = (std::max)(300, clientWidth * 26 / 100);
    int parts[] = {clientWidth * 30 / 100, clientWidth - ipW - timeW, clientWidth - timeW, clientWidth};
    SendMessageW(sb, SB_SETPARTS, 4, (LPARAM)parts);
    SendMessageW(sb, WM_SIZE, 0, 0);
}

ModuleContext makeCtx() {
    return { g_ctx.mdiClient, g_ctx.instance, g_ctx.uiFont,
             g_ctx.dbSettings, g_ctx.fontSize, &g_ctx };
}

// ── menu setup ──────────────────────────────────────────────────

HMENU setupMenus(HWND hwnd) {
    HMENU bar = CreateMenu();
    HMENU subMenus[8]{};
    const wchar_t* subNames[8]{};
    int subCount = 0;

    // Auto-generate menus from g_modules[], grouped by menuParent
    for (int i = 0; i < g_moduleCount; i++) {
        const auto& m = g_modules[i];
        int idx = -1;
        for (int j = 0; j < subCount; j++) {
            if (wcscmp(subNames[j], m.menuParent) == 0) { idx = j; break; }
        }
        if (idx < 0) {
            subMenus[subCount] = CreatePopupMenu();
            subNames[subCount] = m.menuParent;
            AppendMenuW(bar, MF_POPUP, (UINT_PTR)subMenus[subCount], m.menuParent);
            idx = subCount++;
        }
        AppendMenuW(subMenus[idx], MF_STRING, m.menuId, m.menuLabel);
    }

    // Fixed: window menu
    HMENU windowMenu = CreatePopupMenu();
    AppendMenuW(windowMenu, MF_STRING, IDM_CASCADE, L"层叠(&C)");
    AppendMenuW(windowMenu, MF_STRING, IDM_TILE_H, L"水平平铺(&H)");
    AppendMenuW(windowMenu, MF_STRING, IDM_TILE_V, L"垂直平铺(&V)");
    AppendMenuW(windowMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(windowMenu, MF_STRING, IDM_ARRANGE, L"排列图标(&A)");
    AppendMenuW(windowMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(windowMenu, MF_STRING, IDM_CLOSE_ACTIVE, L"关闭当前(&L)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)windowMenu, L"窗口(&W)");

    // Fixed items appended to last menu (L"系统")
    for (int j = 0; j < subCount; j++) {
        if (wcscmp(subNames[j], L"系统") == 0) {
            AppendMenuW(subMenus[j], MF_SEPARATOR, 0, nullptr);
            AppendMenuW(subMenus[j], MF_STRING, IDM_ABOUT, L"关于(&A)...");
            AppendMenuW(subMenus[j], MF_STRING, IDM_EXIT, L"退出(&X)");
            break;
        }
    }

    SetMenu(hwnd, bar);
    return windowMenu;
}

void updateTimePane(HWND hwnd) {
    HWND sb = GetDlgItem(hwnd, ID_STATUS);
    if (!sb) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[64];
    swprintf(buf, 64, L"当前时间：%d年%d月%d日 %d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    SendMessageW(sb, SB_SETTEXT, MAKEWPARAM(3, SBT_NOBORDERS), (LPARAM)buf);
}

std::wstring getLocalIp() {
    ULONG bufLen = 0;
    GetAdaptersInfo(nullptr, &bufLen);
    if (bufLen == 0) return L"0.0.0.0";
    std::vector<BYTE> buf(bufLen);
    auto* p = reinterpret_cast<PIP_ADAPTER_INFO>(buf.data());
    if (GetAdaptersInfo(p, &bufLen) != ERROR_SUCCESS) return L"0.0.0.0";
    while (p) {
        if (p->IpAddressList.IpAddress.String[0] != '0' &&
            strcmp(p->IpAddressList.IpAddress.String, "127.0.0.1") != 0) {
            return search::utf8_to_wide(p->IpAddressList.IpAddress.String);
        }
        p = p->Next;
    }
    return L"0.0.0.0";
}

void setupStatusBar(HWND hwnd) {
    HWND sb = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE,
                              0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_STATUS)),
                              g_ctx.instance, nullptr);
    RECT rc;
    GetClientRect(hwnd, &rc);
    updateStatusBarParts(sb, rc.right - rc.left);
    SendMessageW(sb, SB_SETTEXT, 0, (LPARAM)L"就绪");
    std::wstring ip = L"本机：" + getLocalIp();
    SendMessageW(sb, SB_SETTEXT, MAKEWPARAM(2, SBT_NOBORDERS), (LPARAM)ip.c_str());
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HMENU windowMenu = setupMenus(hwnd);

            WNDCLASSEXW childWc{};
            childWc.cbSize = sizeof(childWc);
            childWc.lpfnWndProc = mdiChildProc;
            childWc.hInstance = g_ctx.instance;
            childWc.hIcon = LoadIconW(g_ctx.instance, MAKEINTRESOURCEW(IDI_APP));
            childWc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            childWc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            childWc.lpszClassName = MDI_CHILD_CLASS;
            RegisterClassExW(&childWc);

            CLIENTCREATESTRUCT ccs{};
            ccs.hWindowMenu = windowMenu;
            ccs.idFirstChild = 5000;
            g_ctx.mdiClient = CreateWindowExW(WS_EX_CLIENTEDGE, L"MDICLIENT", L"",
                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL | WS_HSCROLL,
                0, 0, 0, 0, hwnd, nullptr, g_ctx.instance, &ccs);

            HWND tb = mtCreate(hwnd, g_ctx.instance, g_ctx.menuFont, ID_TOOLBAR);
            mtAddStretch(tb);
            HICON closeIcon = (HICON)LoadImageW(g_ctx.instance, MAKEINTRESOURCEW(IDI_CLOSE), IMAGE_ICON, 16, 16, 0);
            mtAddButton(tb, L"关闭", ID_BTNCLOSE, closeIcon);

            setupStatusBar(hwnd);
            updateTimePane(hwnd);
            SetTimer(hwnd, ID_TIMER, 1000, nullptr);
            return 0;
        }
        case WM_TIMER: {
            if (wp == ID_TIMER) updateTimePane(hwnd);
            return 0;
        }
        case WM_SIZE: {
            HWND tb = GetDlgItem(hwnd, ID_TOOLBAR);
            int tbH = tb ? mtGetHeight(tb) : 28;
            if (tb) MoveWindow(tb, 0, 0, LOWORD(lp), tbH, TRUE);

            RECT sbRc{};
            HWND sb = GetDlgItem(hwnd, ID_STATUS);
            if (sb) GetWindowRect(sb, &sbRc);
            int sbH = sb ? (sbRc.bottom - sbRc.top) : 24;
            if (g_ctx.mdiClient)
                MoveWindow(g_ctx.mdiClient, 0, tbH, LOWORD(lp), HIWORD(lp) - tbH - sbH, TRUE);

            if (sb) updateStatusBarParts(sb, LOWORD(lp));
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);

            // Module registry dispatch
            for (int i = 0; i < g_moduleCount; i++) {
                if (g_modules[i].menuId == id) {
                    g_modules[i].create(makeCtx());
                    return 0;
                }
            }

            // Fixed items
            switch (id) {
                case IDM_ABOUT:
                    MessageBoxW(hwnd,
                        L"检验结果查询平台\n版本 v2026.05.07\n\n作者：Zhao Wang",
                        L"关于", MB_ICONINFORMATION);
                    return 0;
                case ID_BTNCLOSE:        closeActiveMdiChild(); return 0;
                case IDM_EXIT:           DestroyWindow(hwnd); return 0;
                case IDM_CASCADE:        SendMessageW(g_ctx.mdiClient, WM_MDICASCADE, 0, 0); return 0;
                case IDM_TILE_H:         SendMessageW(g_ctx.mdiClient, WM_MDITILE, MDITILE_HORIZONTAL, 0); return 0;
                case IDM_TILE_V:         SendMessageW(g_ctx.mdiClient, WM_MDITILE, MDITILE_VERTICAL, 0); return 0;
                case IDM_ARRANGE:        SendMessageW(g_ctx.mdiClient, WM_MDIICONARRANGE, 0, 0); return 0;
                case IDM_CLOSE_ACTIVE:   closeActiveMdiChild(); return 0;
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, ID_TIMER);
            PostQuitMessage(0);
            return 0;
    }
    return DefFrameProcW(hwnd, g_ctx.mdiClient, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    g_ctx.instance = instance;
    auto iniSettings = search::load_settings(search::default_ini_path());
    g_ctx.dbSettings = iniSettings.db;
    g_ctx.fontSize = iniSettings.ui.font_size;
    NONCLIENTMETRICSW nm{};
    nm.cbSize = sizeof(nm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(nm), &nm, 0);
    g_ctx.uiFont = CreateFontIndirectW(&nm.lfMessageFont);
    g_ctx.menuFont = CreateFontIndirectW(&nm.lfMenuFont);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
    wc.lpszClassName = L"ResultSearchMainWindow";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName,
        L"检验结果查询平台",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 800,
        nullptr, nullptr, instance, nullptr);

    g_ctx.mainWindow = hwnd;
    ShowWindow(hwnd, SW_MAXIMIZE);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!TranslateMDISysAccel(g_ctx.mdiClient, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (g_ctx.uiFont) DeleteObject(g_ctx.uiFont);
    if (g_ctx.menuFont) DeleteObject(g_ctx.menuFont);
    return 0;
}
