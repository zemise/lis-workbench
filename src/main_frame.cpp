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
#include "search_child.h"
#include "search_controller.h"
#include "search_ui_layout.h"
namespace {

constexpr int IDC_SET_SERVER   = 5101;
constexpr int IDC_SET_USER     = 5103;
constexpr int IDC_SET_PASSWORD = 5104;
constexpr int IDC_SET_TEST     = 5106;
constexpr int IDC_SET_SAVE     = 5107;
constexpr int IDC_SET_CANCEL   = 5108;
constexpr int IDC_SET_INITIAL_DATABASE = 5109;
constexpr int IDC_SET_FONT_SIZE = 5110;
constexpr const wchar_t* PROP_SETTINGS = L"SettingsSt";

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

struct SettingsState {
    HFONT font = nullptr;
    search::AppSettings app;
};
SettingsState* g_pendingSettings = nullptr;

int clampFontSize(int v) { return v < 8 ? 8 : (v > 24 ? 24 : v); }

std::wstring readEdit(HWND hwnd, int id) {
    HWND ctrl = GetDlgItem(hwnd, id);
    int len = GetWindowTextLengthW(ctrl);
    std::wstring text(static_cast<size_t>(len), L'\0');
    GetWindowTextW(ctrl, text.data(), len + 1);
    return text;
}

search::DbSettings collectForm(HWND hwnd) {
    search::DbSettings s;
    s.server = readEdit(hwnd, IDC_SET_SERVER);
    s.initial_database = readEdit(hwnd, IDC_SET_INITIAL_DATABASE);
    s.user = readEdit(hwnd, IDC_SET_USER);
    s.password = readEdit(hwnd, IDC_SET_PASSWORD);
    return s;
}

LRESULT CALLBACK mdiChildProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            if (g_pendingSettings) {
                auto* st = g_pendingSettings;
                g_pendingSettings = nullptr;
                SetPropW(hwnd, PROP_SETTINGS, reinterpret_cast<HANDLE>(st));

                const float s = search::dpi_scale_factor(hwnd);
                auto S = [s](int v) { return static_cast<int>(v * s); };

                search::create_label(hwnd, L"服务器", S(20), S(22), S(86), S(22));
                search::create_edit(hwnd, IDC_SET_SERVER, S(116), S(22), S(330), S(24));

                search::create_label(hwnd, L"初始数据库", S(20), S(56), S(86), S(22));
                search::create_edit(hwnd, IDC_SET_INITIAL_DATABASE, S(116), S(56), S(330), S(24));

                search::create_label(hwnd, L"用户名", S(20), S(90), S(86), S(22));
                search::create_edit(hwnd, IDC_SET_USER, S(116), S(90), S(330), S(24));

                search::create_label(hwnd, L"密码", S(20), S(124), S(86), S(22));
                search::create_password_edit(hwnd, IDC_SET_PASSWORD, S(116), S(124), S(330), S(24));

                search::create_label(hwnd, L"字号", S(20), S(158), S(86), S(22));
                search::create_edit(hwnd, IDC_SET_FONT_SIZE, S(116), S(158), S(80), S(24));

                search::create_button(hwnd, IDC_SET_TEST, L"测试连接", S(116), S(200), S(92), S(30));
                search::create_button(hwnd, IDC_SET_SAVE, L"保存", S(254), S(200), S(84), S(30));
                search::create_button(hwnd, IDC_SET_CANCEL, L"取消", S(362), S(200), S(84), S(30));

                auto& app = st->app;
                SetWindowTextW(GetDlgItem(hwnd, IDC_SET_SERVER), app.db.server.c_str());
                SetWindowTextW(GetDlgItem(hwnd, IDC_SET_INITIAL_DATABASE), app.db.initial_database.c_str());
                SetWindowTextW(GetDlgItem(hwnd, IDC_SET_USER), app.db.user.c_str());
                SetWindowTextW(GetDlgItem(hwnd, IDC_SET_PASSWORD), app.db.password.c_str());
                SetWindowTextW(GetDlgItem(hwnd, IDC_SET_FONT_SIZE), std::to_wstring(app.ui.font_size).c_str());

                EnumChildWindows(hwnd, [](HWND child, LPARAM param) -> BOOL {
                    SendMessageW(child, WM_SETFONT, param, TRUE);
                    return TRUE;
                }, reinterpret_cast<LPARAM>(st->font));
                return 0;
            }
            wchar_t title[256];
            GetWindowTextW(hwnd, title, 256);
            CreateWindowExW(0, L"STATIC", title,
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, 0, 0, 0, hwnd, nullptr, g_ctx.instance, nullptr);
            return 0;
        }
        case WM_SIZE:
            // Settings form uses fixed layout — skip fill, and do NOT fall
            // through to DefMDIChildProcW (causes menu bar artifacts on maximize).
            if (GetPropW(hwnd, PROP_SETTINGS)) return 0;
            {
                HWND label = GetWindow(hwnd, GW_CHILD);
                if (label) {
                    RECT rc;
                    GetClientRect(hwnd, &rc);
                    MoveWindow(label, 0, 0, rc.right, rc.bottom, TRUE);
                }
            }
            return 0;
        case WM_COMMAND: {
            auto* st = reinterpret_cast<SettingsState*>(GetPropW(hwnd, PROP_SETTINGS));
            if (!st) break;
            int id = LOWORD(wp);
            if (id == IDC_SET_CANCEL) { DestroyWindow(hwnd); return 0; }
            if (id == IDC_SET_TEST) {
                auto db = collectForm(hwnd);
                if (search::build_connection_string_w(db).empty()) {
                    MessageBoxW(hwnd, L"请先填写服务器、初始数据库和用户名。", L"测试连接", MB_ICONWARNING);
                } else {
                    std::string error;
                    if (search::test_database_connection(db, error))
                        MessageBoxW(hwnd, L"数据库连接成功。", L"测试连接", MB_ICONINFORMATION);
                    else
                        MessageBoxW(hwnd, search::utf8_to_wide(error).c_str(), L"数据库连接失败", MB_ICONERROR);
                }
                return 0;
            }
            if (id == IDC_SET_SAVE) {
                st->app.db = collectForm(hwnd);
                st->app.ui.font_size = clampFontSize(_wtoi(readEdit(hwnd, IDC_SET_FONT_SIZE).c_str()));
                search::save_settings(search::default_ini_path(), st->app);
                g_ctx.dbSettings = st->app.db;
                g_ctx.fontSize = st->app.ui.font_size;
                MessageBoxW(hwnd, L"数据库设置已保存。", L"保存", MB_ICONINFORMATION);
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_DESTROY: {
            auto* st = reinterpret_cast<SettingsState*>(RemovePropW(hwnd, PROP_SETTINGS));
            delete st;
            break;
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

void openSettingsChild() {
    // Single-instance: if already open, just activate it
    HWND existing = GetWindow(g_ctx.mdiClient, GW_CHILD);
    while (existing) {
        wchar_t title[256];
        if (GetWindowTextW(existing, title, 256) && wcscmp(title, L"系统设置") == 0) {
            SendMessageW(g_ctx.mdiClient, WM_MDIACTIVATE, reinterpret_cast<WPARAM>(existing), 0);
            return;
        }
        existing = GetWindow(existing, GW_HWNDNEXT);
    }
    auto* st = new SettingsState;
    st->font = g_ctx.uiFont;
    st->app = search::load_settings(search::default_ini_path());
    g_pendingSettings = st;
    createMdiChild(L"系统设置");
    g_pendingSettings = nullptr;
}

void closeActiveMdiChild() {
    HWND active = reinterpret_cast<HWND>(SendMessageW(g_ctx.mdiClient, WM_MDIGETACTIVE, 0, 0));
    if (active) SendMessageW(g_ctx.mdiClient, WM_MDIDESTROY, reinterpret_cast<WPARAM>(active), 0);
}

void updateStatusBarParts(HWND sb, int clientWidth) {
    int ipW  = (std::max)(220, clientWidth * 22 / 100);
    int timeW = (std::max)(300, clientWidth * 26 / 100);
    int parts[] = {clientWidth * 30 / 100, clientWidth - ipW - timeW, clientWidth - timeW, clientWidth};
    SendMessageW(sb, SB_SETPARTS, 4, (LPARAM)parts);
    SendMessageW(sb, WM_SIZE, 0, 0);
}

HMENU setupMenus(HWND hwnd) {
    HMENU bar = CreateMenu();

    HMENU labMenu = CreatePopupMenu();
    AppendMenuW(labMenu, MF_STRING, IDM_QUERY, L"检验结果查询(&Q)...");
    AppendMenuW(labMenu, MF_STRING, IDM_BLOOD, L"输血结果查询(&B)...");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)labMenu, L"检验管理(&L)");

    HMENU toolMenu = CreatePopupMenu();
    AppendMenuW(toolMenu, MF_STRING, IDM_TOOL1, L"工具1(&1)");
    AppendMenuW(toolMenu, MF_STRING, IDM_TOOL2, L"工具2(&2)");
    AppendMenuW(toolMenu, MF_STRING, IDM_TOOL3, L"工具3(&3)");
    AppendMenuW(toolMenu, MF_STRING, IDM_TOOL4, L"工具4(&4)");
    AppendMenuW(toolMenu, MF_STRING, IDM_TOOL5, L"工具5(&5)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)toolMenu, L"工具(&T)");

    HMENU windowMenu = CreatePopupMenu();
    AppendMenuW(windowMenu, MF_STRING, IDM_CASCADE, L"层叠(&C)");
    AppendMenuW(windowMenu, MF_STRING, IDM_TILE_H, L"水平平铺(&H)");
    AppendMenuW(windowMenu, MF_STRING, IDM_TILE_V, L"垂直平铺(&V)");
    AppendMenuW(windowMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(windowMenu, MF_STRING, IDM_ARRANGE, L"排列图标(&A)");
    AppendMenuW(windowMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(windowMenu, MF_STRING, IDM_CLOSE_ACTIVE, L"关闭当前(&L)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)windowMenu, L"窗口(&W)");

    HMENU sysMenu = CreatePopupMenu();
    AppendMenuW(sysMenu, MF_STRING, IDM_SETTINGS, L"系统设置(&S)...");
    AppendMenuW(sysMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sysMenu, MF_STRING, IDM_ABOUT, L"关于(&A)...");
    AppendMenuW(sysMenu, MF_STRING, IDM_EXIT, L"退出(&X)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)sysMenu, L"系统(&Y)");

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
            switch (LOWORD(wp)) {
                case IDM_ABOUT:
                    MessageBoxW(hwnd,
                        L"检验结果查询平台\n版本 v2026.05.06\n\n作者：Zhao Wang",
                        L"关于", MB_ICONINFORMATION);
                    break;
                case ID_BTNCLOSE:
                    closeActiveMdiChild();
                    break;
                case IDM_QUERY:    create_search_child(g_ctx.mdiClient, g_ctx.instance, g_ctx.uiFont, g_ctx.dbSettings, g_ctx.fontSize); break;
                case IDM_BLOOD:    createMdiChild(L"输血结果查询"); break;
                case IDM_SETTINGS: openSettingsChild();              break;
                case IDM_EXIT:     DestroyWindow(hwnd);             break;
                case IDM_TOOL1: case IDM_TOOL2: case IDM_TOOL3:
                case IDM_TOOL4: case IDM_TOOL5: {
                    static const wchar_t* names[] = {L"工具1", L"工具2", L"工具3", L"工具4", L"工具5"};
                    createMdiChild(names[LOWORD(wp) - IDM_TOOL1]);
                    break;
                }
                case IDM_CASCADE:      SendMessageW(g_ctx.mdiClient, WM_MDICASCADE, 0, 0); break;
                case IDM_TILE_H:       SendMessageW(g_ctx.mdiClient, WM_MDITILE, MDITILE_HORIZONTAL, 0); break;
                case IDM_TILE_V:       SendMessageW(g_ctx.mdiClient, WM_MDITILE, MDITILE_VERTICAL, 0); break;
                case IDM_ARRANGE:      SendMessageW(g_ctx.mdiClient, WM_MDIICONARRANGE, 0, 0); break;
                case IDM_CLOSE_ACTIVE: closeActiveMdiChild(); break;
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
