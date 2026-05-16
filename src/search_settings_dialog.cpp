#include "search_settings_dialog.h"

#ifdef _WIN32

#include "resource.h"
#include "search_ui_layout.h"

#include <windows.h>

namespace search {
namespace {

constexpr int IDC_SET_SERVER = 5001;
constexpr int IDC_SET_USER = 5003;
constexpr int IDC_SET_PASSWORD = 5004;
constexpr int IDC_SET_TEST = 5006;
constexpr int IDC_SET_SAVE = 5007;
constexpr int IDC_SET_CANCEL = 5008;
constexpr int IDC_SET_INITIAL_DATABASE = 5009;
constexpr int IDC_SET_FONT_SIZE = 5010;

int clamp_font_size(int value) {
    return value < 8 ? 8 : (value > 24 ? 24 : value);
}

std::wstring window_text_w(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(len), L'\0');
    GetWindowTextW(hwnd, text.data(), len + 1);
    return text;
}

void set_text(HWND hwnd, const std::wstring& text) {
    SetWindowTextW(hwnd, text.c_str());
}

struct SettingsDialogState {
    HFONT font = nullptr;
    DbSettings settings;
    int font_size = 9;
    SettingsDialogCallbacks callbacks;
};

LRESULT CALLBACK settings_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<SettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            auto* initial_state = reinterpret_cast<SettingsDialogState*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(initial_state));
            return TRUE;
        }
        case WM_CREATE: {
            const float s = search::dpi_scale_factor(hwnd);
            auto S = [s](int v) { return static_cast<int>(v * s); };
            search::create_label(hwnd, L"服务器", S(20), S(24), S(86), S(22));
            HWND server = search::create_edit(hwnd, IDC_SET_SERVER, S(116), S(22), S(330), S(24));
            search::create_label(hwnd, L"初始数据库", S(20), S(58), S(86), S(22));
            HWND initial_database = search::create_edit(hwnd, IDC_SET_INITIAL_DATABASE, S(116), S(56), S(330), S(24));
            search::create_label(hwnd, L"用户名", S(20), S(92), S(86), S(22));
            HWND user = search::create_edit(hwnd, IDC_SET_USER, S(116), S(90), S(330), S(24));
            search::create_label(hwnd, L"密码", S(20), S(126), S(86), S(22));
            HWND password = search::create_password_edit(hwnd, IDC_SET_PASSWORD, S(116), S(124), S(330), S(24));
            search::create_label(hwnd, L"字号", S(20), S(160), S(86), S(22));
            HWND font_size = search::create_edit(hwnd, IDC_SET_FONT_SIZE, S(116), S(158), S(80), S(24));
            search::create_button(hwnd, IDC_SET_TEST, L"测试连接", S(116), S(200), S(92), S(30));
            search::create_button(hwnd, IDC_SET_SAVE, L"保存", S(254), S(200), S(84), S(30));
            search::create_button(hwnd, IDC_SET_CANCEL, L"取消", S(362), S(200), S(84), S(30));

            set_text(server, state->settings.server);
            set_text(initial_database, state->settings.initial_database);
            set_text(user, state->settings.user);
            set_text(password, state->settings.password);
            set_text(font_size, std::to_wstring(state->font_size));

            EnumChildWindows(hwnd, [](HWND child, LPARAM param) -> BOOL {
                SendMessageW(child, WM_SETFONT, param, TRUE);
                return TRUE;
            }, reinterpret_cast<LPARAM>(state->font));
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            if (id == IDC_SET_CANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            if (id == IDC_SET_TEST || id == IDC_SET_SAVE) {
                DbSettings settings;
                settings.server = window_text_w(GetDlgItem(hwnd, IDC_SET_SERVER));
                settings.initial_database = window_text_w(GetDlgItem(hwnd, IDC_SET_INITIAL_DATABASE));
                settings.user = window_text_w(GetDlgItem(hwnd, IDC_SET_USER));
                settings.password = window_text_w(GetDlgItem(hwnd, IDC_SET_PASSWORD));
                if (id == IDC_SET_TEST) {
                    if (state->callbacks.on_test) {
                        state->callbacks.on_test(hwnd, settings);
                    }
                    return 0;
                }
                const int font_size = clamp_font_size(_wtoi(window_text_w(GetDlgItem(hwnd, IDC_SET_FONT_SIZE)).c_str()));
                if (state->callbacks.on_save) {
                    state->callbacks.on_save(settings, font_size);
                }
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_NCDESTROY:
            delete state;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

void show_settings_dialog(HWND owner,
                          HFONT font,
                          const DbSettings& settings,
                          int font_size,
                          const SettingsDialogCallbacks& callbacks) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = settings_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APP));
        wc.hIconSm = static_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                                     GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"LISWorkbenchSettingsWindow";
        RegisterClassExW(&wc);
        registered = true;
    }

    auto* state = new SettingsDialogState{font, settings, font_size, callbacks};
    const float s = search::dpi_scale_factor(owner);
    HWND win = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LISWorkbenchSettingsWindow", L"数据库设置",
                               WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                               CW_USEDEFAULT, CW_USEDEFAULT, static_cast<int>(490 * s), static_cast<int>(286 * s),
                               owner, nullptr, GetModuleHandleW(nullptr), state);
    EnableWindow(owner, FALSE);
    MSG msg{};
    while (IsWindow(win) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(win, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

}  // namespace search

#endif
