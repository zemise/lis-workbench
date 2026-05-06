#include "settings_module.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "main_app.h"
#include "resource.h"
#include "search_controller.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include <windows.h>

namespace {

constexpr int IDC_SET_SERVER   = 5101;
constexpr int IDC_SET_USER     = 5103;
constexpr int IDC_SET_PASSWORD = 5104;
constexpr int IDC_SET_TEST     = 5106;
constexpr int IDC_SET_SAVE     = 5107;
constexpr int IDC_SET_CANCEL   = 5108;
constexpr int IDC_SET_INITIAL_DATABASE = 5109;
constexpr int IDC_SET_FONT_SIZE = 5110;

constexpr const wchar_t* WND_CLASS  = L"SettingsModuleChild";
constexpr const wchar_t* PROP_STATE = L"SettingsSt";

struct SettingsState {
    ModuleContext ctx;
    search::AppSettings app;
};

SettingsState* g_pending = nullptr;

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

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<SettingsState*>(GetPropW(hwnd, PROP_STATE));

    switch (msg) {
        case WM_CREATE: {
            st = g_pending;
            g_pending = nullptr;
            SetPropW(hwnd, PROP_STATE, reinterpret_cast<HANDLE>(st));

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
            }, reinterpret_cast<LPARAM>(st->ctx.uiFont));
            return 0;
        }
        case WM_SIZE:
            return 0;
        case WM_COMMAND: {
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
                if (st->ctx.appContext) {
                    auto* gctx = static_cast<app::Context*>(st->ctx.appContext);
                    gctx->dbSettings = st->app.db;
                    gctx->fontSize = st->app.ui.font_size;
                }
                MessageBoxW(hwnd, L"数据库设置已保存。", L"保存", MB_ICONINFORMATION);
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_DESTROY:
            RemovePropW(hwnd, PROP_STATE);
            delete st;
            break;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_settings_module(const ModuleContext& ctx) {
    // Single-instance: if already open, just activate
    HWND existing = GetWindow(ctx.mdiClient, GW_CHILD);
    while (existing) {
        wchar_t title[256];
        if (GetWindowTextW(existing, title, 256) && wcscmp(title, L"系统设置") == 0) {
            SendMessageW(ctx.mdiClient, WM_MDIACTIVATE, reinterpret_cast<WPARAM>(existing), 0);
            return existing;
        }
        existing = GetWindow(existing, GW_HWNDNEXT);
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

    auto* st = new SettingsState;
    st->ctx = ctx;
    st->app = search::load_settings(search::default_ini_path());

    MDICREATESTRUCTW mcs{};
    mcs.szClass = WND_CLASS;
    mcs.szTitle = L"系统设置";
    mcs.hOwner = ctx.instance;
    mcs.x = mcs.y = mcs.cx = mcs.cy = CW_USEDEFAULT;

    g_pending = st;
    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0,
        reinterpret_cast<LPARAM>(&mcs)));
    SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    return child;
}

#endif
