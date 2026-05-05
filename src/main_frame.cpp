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

namespace {

constexpr int IDM_QUERY    = 1001;
constexpr int IDM_BLOOD    = 1002;
constexpr int IDM_SETTINGS = 2001;
constexpr int IDM_EXIT     = 2002;
constexpr int IDM_TOOL1    = 3001;
constexpr int IDM_TOOL2    = 3002;
constexpr int IDM_TOOL3    = 3003;
constexpr int IDM_TOOL4    = 3004;
constexpr int IDM_TOOL5    = 3005;
constexpr int ID_STATUS    = 4001;
constexpr int ID_TIMER     = 5001;

app::Context g_ctx;

HFONT createFont(int pointSize) {
    NONCLIENTMETRICSW nm{};
    nm.cbSize = sizeof(nm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(nm), &nm, 0);
    LOGFONTW lf = nm.lfMessageFont;
    HDC screen = GetDC(nullptr);
    lf.lfHeight = -MulDiv(pointSize, GetDeviceCaps(screen, LOGPIXELSY), 72);
    ReleaseDC(nullptr, screen);
    return CreateFontIndirectW(&lf);
}

void onQuery(HWND owner) {
    MessageBoxW(owner, L"检验结果查询 — 待接入", L"检验结果查询", MB_ICONINFORMATION);
}

void onBlood(HWND owner) {
    MessageBoxW(owner, L"输血结果查询 — 待开发", L"输血结果查询", MB_ICONINFORMATION);
}

void onSettings(HWND owner) {
    MessageBoxW(owner, L"参数设置 — 待接入", L"参数设置", MB_ICONINFORMATION);
}

void setupMenus(HWND hwnd) {
    HMENU bar = CreateMenu();

    HMENU labMenu = CreatePopupMenu();
    AppendMenuW(labMenu, MF_STRING, IDM_QUERY, L"检验结果查询(&Q)...");
    AppendMenuW(labMenu, MF_STRING, IDM_BLOOD, L"输血结果查询(&B)...");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)labMenu, L"检验管理(&L)");

    HMENU sysMenu = CreatePopupMenu();
    AppendMenuW(sysMenu, MF_STRING, IDM_SETTINGS, L"参数设置(&S)...");
    AppendMenuW(sysMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sysMenu, MF_STRING, IDM_EXIT, L"退出(&X)");
    HMENU toolMenu = CreatePopupMenu();
    AppendMenuW(toolMenu, MF_STRING, IDM_TOOL1, L"工具1(&1)");
    AppendMenuW(toolMenu, MF_STRING, IDM_TOOL2, L"工具2(&2)");
    AppendMenuW(toolMenu, MF_STRING, IDM_TOOL3, L"工具3(&3)");
    AppendMenuW(toolMenu, MF_STRING, IDM_TOOL4, L"工具4(&4)");
    AppendMenuW(toolMenu, MF_STRING, IDM_TOOL5, L"工具5(&5)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)toolMenu, L"工具(&T)");

    AppendMenuW(bar, MF_POPUP, (UINT_PTR)sysMenu, L"系统(&Y)");

    SetMenu(hwnd, bar);
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
            return utf8_to_wide(p->IpAddressList.IpAddress.String);
        }
        p = p->Next;
    }
    return L"0.0.0.0";
}

void setupStatusBar(HWND hwnd) {
    HWND sb = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE,
                              0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_STATUS)),
                              g_ctx.instance, nullptr);
    int parts[] = {300, -1, 200, 260};
    SendMessageW(sb, SB_SETPARTS, 4, (LPARAM)parts);
    SendMessageW(sb, SB_SETTEXT, 0, (LPARAM)L"就绪");
    SendMessageW(sb, SB_SETTEXT, MAKEWPARAM(1, SBT_NOBORDERS), (LPARAM)L"");
    std::wstring ip = L"本机：" + getLocalIp();
    SendMessageW(sb, SB_SETTEXT, MAKEWPARAM(2, SBT_NOBORDERS), (LPARAM)ip.c_str());
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            setupMenus(hwnd);
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
            HWND sb = GetDlgItem(hwnd, ID_STATUS);
            if (sb) SendMessageW(sb, WM_SIZE, 0, 0);
            return 0;
        }
        case WM_COMMAND: {
            switch (LOWORD(wp)) {
                case IDM_QUERY:    onQuery(hwnd);    break;
                case IDM_BLOOD:    onBlood(hwnd);    break;
                case IDM_SETTINGS: onSettings(hwnd); break;
                case IDM_EXIT:     DestroyWindow(hwnd); break;
                case IDM_TOOL1: case IDM_TOOL2: case IDM_TOOL3:
                case IDM_TOOL4: case IDM_TOOL5:
                    MessageBoxW(hwnd, L"工具功能 — 待开发", L"工具", MB_ICONINFORMATION);
                    break;
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
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    g_ctx.instance = instance;
    g_ctx.fontSize = 9;
    g_ctx.uiFont = createFont(g_ctx.fontSize);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
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
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_ctx.uiFont) DeleteObject(g_ctx.uiFont);
    return 0;
}
