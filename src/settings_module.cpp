#include "settings_module.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "main_app.h"
#include "resource.h"
#include "search_controller.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "update_source.h"
#include "version.h"
#include <commctrl.h>
#include <shellapi.h>
#include <windows.h>
#include <winspool.h>

#include <algorithm>
#include <array>
#include <memory>
#include <thread>
#include <vector>

namespace {

constexpr int IDC_SET_SERVER   = 5101;
constexpr int IDC_SET_USER     = 5103;
constexpr int IDC_SET_PASSWORD = 5104;
constexpr int IDC_SET_TEST     = 5106;
constexpr int IDC_SET_SAVE     = 5107;
constexpr int IDC_SET_CANCEL   = 5108;
constexpr int IDC_SET_INITIAL_DATABASE = 5109;
constexpr int IDC_SET_FONT_SIZE = 5110;
constexpr int IDC_SET_LIS_ABO_CODES = 5111;
constexpr int IDC_SET_LIS_RHD_CODES = 5112;
constexpr int IDC_SET_LIS_HGB_CODES = 5113;
constexpr int IDC_SET_LIS_PLT_CODES = 5114;
constexpr int IDC_SET_BARCODE_PRINTER = 5115;
constexpr int IDC_SET_QUICK_MACHINE_1 = 5116;
constexpr int IDC_SET_QUICK_MACHINE_2 = 5117;
constexpr int IDC_SET_QUICK_MACHINE_3 = 5118;
constexpr int IDC_SET_QUICK_MACHINE_PICK_1 = 5119;
constexpr int IDC_SET_QUICK_MACHINE_PICK_2 = 5120;
constexpr int IDC_SET_QUICK_MACHINE_PICK_3 = 5121;
constexpr int IDC_PICKER_ROOM = 5122;
constexpr int IDC_PICKER_MACHINE = 5123;
constexpr int IDC_SET_UPDATE_SOURCE = 5124;
constexpr int IDC_SET_UPDATE_MANIFEST_URL = 5125;
constexpr int IDC_SET_UPDATE_FOLDER = 5126;
constexpr int IDC_SET_CHECK_UPDATE = 5127;
constexpr UINT WM_SETTINGS_UPDATE_CHECK_DONE = WM_APP + 73;

constexpr const wchar_t* WND_CLASS  = L"SettingsModuleChild";
constexpr const wchar_t* PICKER_CLASS = L"SettingsMachinePicker";
constexpr const wchar_t* PROP_STATE = L"SettingsSt";
constexpr const wchar_t* WINDOW_TITLE = L"系统设置";
constexpr const wchar_t* DEFAULT_BARCODE_PRINTER_NAME = L"Xprinter XP-360B #2";
constexpr const wchar_t* UPDATE_SECTION = L"Update";
constexpr const wchar_t* UPDATE_SOURCE_FOLDER = L"Folder";
constexpr const wchar_t* UPDATE_SOURCE_HTTP = L"Http";
constexpr const wchar_t* UPDATE_SOURCE_FOLDER_LABEL = L"共享文件夹";
constexpr const wchar_t* UPDATE_SOURCE_HTTP_LABEL = L"HTTP";
constexpr const wchar_t* UPDATE_CACHE_DIR_NAME = L"LISWorkbench\\UpdateCache";
constexpr const wchar_t* UPDATE_APP_EXE_NAME = L"lis_workbench.exe";
constexpr const wchar_t* UPDATE_UPDATER_EXE_NAME = L"Updater.exe";
constexpr int PICKER_W = 440;
constexpr int PICKER_H = 360;
constexpr int PICKER_PAD = 12;
constexpr int PICKER_COMBO_H = 180;
constexpr int PICKER_LIST_Y = 48;
constexpr int PICKER_CODE_W = 92;
constexpr int PICKER_NAME_W = 300;
constexpr int QUICK_MACHINE_COUNT = 3;
constexpr std::array<const wchar_t*, QUICK_MACHINE_COUNT> QUICK_MACHINE_CODE_KEYS = {
    L"QuickMachine1Code", L"QuickMachine2Code", L"QuickMachine3Code"};
constexpr std::array<const wchar_t*, QUICK_MACHINE_COUNT> QUICK_MACHINE_NAME_KEYS = {
    L"QuickMachine1Name", L"QuickMachine2Name", L"QuickMachine3Name"};
constexpr std::array<const wchar_t*, QUICK_MACHINE_COUNT> QUICK_MACHINE_ROOM_KEYS = {
    L"QuickMachine1RoomCode", L"QuickMachine2RoomCode", L"QuickMachine3RoomCode"};

struct SettingsState {
    ModuleContext ctx;
    search::AppSettings app;
    std::array<std::string, QUICK_MACHINE_COUNT> quickMachineCodes;
    std::array<std::string, QUICK_MACHINE_COUNT> quickMachineRoomCodes;
    std::array<std::wstring, QUICK_MACHINE_COUNT> quickMachineNames;
};

struct SettingsMachinePickerState {
    SettingsState* settings = nullptr;
    HWND owner = nullptr;
    HWND roomCombo = nullptr;
    HWND machineList = nullptr;
    int slot = 0;
    std::vector<search::RoomOption> rooms;
    std::vector<search::MachineOption> machines;
};

struct SettingsUpdateCheckDone {
    bool ok = false;
    lis_update::UpdateCheckResult result;
    std::string error;
};

struct SettingsUpdateConfig {
    std::wstring sourceType;
    std::wstring manifestUrl;
    std::wstring folderPath;
};

SettingsState* g_pending = nullptr;

int clampFontSize(int v) { return v < 8 ? 8 : (v > 24 ? 24 : v); }

const wchar_t* quickMachineCodeKey(int slot) {
    return QUICK_MACHINE_CODE_KEYS[static_cast<size_t>(std::clamp(slot, 0, QUICK_MACHINE_COUNT - 1))];
}

const wchar_t* quickMachineNameKey(int slot) {
    return QUICK_MACHINE_NAME_KEYS[static_cast<size_t>(std::clamp(slot, 0, QUICK_MACHINE_COUNT - 1))];
}

const wchar_t* quickMachineRoomKey(int slot) {
    return QUICK_MACHINE_ROOM_KEYS[static_cast<size_t>(std::clamp(slot, 0, QUICK_MACHINE_COUNT - 1))];
}

int quickMachineEditId(int slot) {
    return slot == 0 ? IDC_SET_QUICK_MACHINE_1 : (slot == 1 ? IDC_SET_QUICK_MACHINE_2 : IDC_SET_QUICK_MACHINE_3);
}

std::wstring readEdit(HWND hwnd, int id) {
    HWND ctrl = GetDlgItem(hwnd, id);
    int len = GetWindowTextLengthW(ctrl);
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(ctrl, text.data(), len + 1);
    text.resize(static_cast<size_t>(len));
    return text;
}

std::wstring readCombo(HWND hwnd, int id) {
    HWND combo = GetDlgItem(hwnd, id);
    const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index >= 0) {
        const int len = static_cast<int>(SendMessageW(combo, CB_GETLBTEXTLEN, index, 0));
        if (len >= 0) {
            std::wstring text(static_cast<size_t>(len) + 1, L'\0');
            SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text.data()));
            text.resize(static_cast<size_t>(len));
            return text;
        }
    }
    return readEdit(hwnd, id);
}

void selectComboText(HWND combo, const wchar_t* text, int fallback_index = 0) {
    const int found = static_cast<int>(SendMessageW(combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                                    reinterpret_cast<LPARAM>(text)));
    SendMessageW(combo, CB_SETCURSEL, found >= 0 ? found : fallback_index, 0);
}

void populateUpdateSourceCombo(HWND hwnd) {
    HWND combo = GetDlgItem(hwnd, IDC_SET_UPDATE_SOURCE);
    if (!combo) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(UPDATE_SOURCE_FOLDER_LABEL));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(UPDATE_SOURCE_HTTP_LABEL));

    const std::wstring saved = search::load_module_str(UPDATE_SECTION, L"SourceType", UPDATE_SOURCE_FOLDER);
    selectComboText(combo, saved == UPDATE_SOURCE_HTTP ? UPDATE_SOURCE_HTTP_LABEL : UPDATE_SOURCE_FOLDER_LABEL, 0);
}

std::wstring selectedUpdateSourceType(HWND hwnd) {
    HWND combo = GetDlgItem(hwnd, IDC_SET_UPDATE_SOURCE);
    const int index = combo ? static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0)) : 0;
    return index == 1 ? UPDATE_SOURCE_HTTP : UPDATE_SOURCE_FOLDER;
}

SettingsUpdateConfig collectUpdateConfig(HWND hwnd) {
    SettingsUpdateConfig cfg;
    cfg.sourceType = selectedUpdateSourceType(hwnd);
    cfg.manifestUrl = readEdit(hwnd, IDC_SET_UPDATE_MANIFEST_URL);
    cfg.folderPath = readEdit(hwnd, IDC_SET_UPDATE_FOLDER);
    return cfg;
}

void saveUpdateConfig(const SettingsUpdateConfig& cfg) {
    search::save_module_str(UPDATE_SECTION, L"SourceType", cfg.sourceType);
    search::save_module_str(UPDATE_SECTION, L"ManifestUrl", cfg.manifestUrl);
    search::save_module_str(UPDATE_SECTION, L"FolderPath", cfg.folderPath);
}

void setUpdateCheckButtonState(HWND hwnd, bool checking) {
    HWND button = GetDlgItem(hwnd, IDC_SET_CHECK_UPDATE);
    if (!button) return;
    EnableWindow(button, checking ? FALSE : TRUE);
    SetWindowTextW(button, checking ? L"检查中..." : L"检查更新");
}

bool localFileExists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring quoteProcessArg(const std::wstring& value) {
    std::wstring out = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'"') out += L'\\';
        out += ch;
    }
    out += L"\"";
    return out;
}

bool launchUpdaterForPackage(HWND hwnd, SettingsState* st, const std::wstring& package_path) {
    const std::wstring app_dir = search::module_dir();
    const std::wstring updater_path = lis_update::join_update_path(app_dir, UPDATE_UPDATER_EXE_NAME);
    if (!localFileExists(updater_path)) {
        MessageBoxW(hwnd, L"安装目录中未找到 Updater.exe。", L"安装更新", MB_ICONERROR);
        return false;
    }

    std::wstring args;
    args += L"--app-dir " + quoteProcessArg(app_dir);
    args += L" --app-exe " + quoteProcessArg(UPDATE_APP_EXE_NAME);
    args += L" --package-file " + quoteProcessArg(package_path);
    args += L" --pid " + std::to_wstring(GetCurrentProcessId());

    HINSTANCE started = ShellExecuteW(nullptr, L"open", updater_path.c_str(), args.c_str(),
                                      app_dir.c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(started) <= 32) {
        MessageBoxW(hwnd, L"启动 Updater.exe 失败。", L"安装更新", MB_ICONERROR);
        return false;
    }

    if (st && st->ctx.appContext) {
        auto* gctx = static_cast<app::Context*>(st->ctx.appContext);
        if (gctx->mainWindow) {
            PostMessageW(gctx->mainWindow, WM_CLOSE, 0, 0);
            return true;
        }
    }
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (root) PostMessageW(root, WM_CLOSE, 0, 0);
    return true;
}

std::wstring programDataUpdateCacheDir() {
    DWORD needed = GetEnvironmentVariableW(L"ProgramData", nullptr, 0);
    std::wstring base;
    if (needed > 0) {
        std::wstring buffer(static_cast<size_t>(needed), L'\0');
        DWORD written = GetEnvironmentVariableW(L"ProgramData", buffer.data(), needed);
        if (written > 0 && written < needed) {
            buffer.resize(static_cast<size_t>(written));
            base = buffer;
        }
    }
    if (base.empty()) {
        base = search::module_dir();
    }
    return lis_update::join_update_path(base, UPDATE_CACHE_DIR_NAME);
}

void showUpdateCheckResult(HWND hwnd, SettingsState* st, const SettingsUpdateCheckDone& done) {
    setUpdateCheckButtonState(hwnd, false);

    if (!done.ok) {
        MessageBoxW(hwnd, search::utf8_to_wide(done.error).c_str(), L"检查更新失败", MB_ICONERROR);
        return;
    }

    if (!done.result.update_available) {
        const std::wstring message = L"当前已是最新版本。\r\n当前版本：" + search::utf8_to_wide(search::kVersion);
        MessageBoxW(hwnd, message.c_str(), L"检查更新", MB_ICONINFORMATION);
        return;
    }

    std::wstring message = L"发现新版本：" + search::utf8_to_wide(done.result.manifest.version) +
                           L"\r\n更新包已缓存到：\r\n" + done.result.package_path +
                           L"\r\n\r\n是否立即安装并重启程序？";
    const int answer = MessageBoxW(hwnd, message.c_str(), L"检查更新",
                                   MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2);
    if (answer == IDYES) {
        launchUpdaterForPackage(hwnd, st, done.result.package_path);
    }
}

void startConfiguredUpdateCheck(HWND hwnd) {
    const SettingsUpdateConfig cfg = collectUpdateConfig(hwnd);
    if (cfg.sourceType == UPDATE_SOURCE_HTTP) {
        if (cfg.manifestUrl.empty()) {
            MessageBoxW(hwnd, L"请先填写 HTTP manifest URL。", L"检查更新", MB_ICONWARNING);
            return;
        }
    } else {
        if (cfg.folderPath.empty()) {
            MessageBoxW(hwnd, L"请先填写共享文件夹路径。", L"检查更新", MB_ICONWARNING);
            return;
        }
    }

    setUpdateCheckButtonState(hwnd, true);

    const std::wstring cache_dir = programDataUpdateCacheDir();
    std::thread([hwnd, cfg, cache_dir]() {
        auto* done = new SettingsUpdateCheckDone;
        std::unique_ptr<lis_update::IUpdateSource> source;
        if (cfg.sourceType == UPDATE_SOURCE_HTTP) {
            source = std::make_unique<lis_update::HttpUpdateSource>(cfg.manifestUrl);
        } else {
            source = std::make_unique<lis_update::FolderUpdateSource>(cfg.folderPath);
        }
        done->ok = lis_update::check_and_fetch_update(*source, search::kVersion,
                                                      cache_dir, done->result, done->error);
        if (!PostMessageW(hwnd, WM_SETTINGS_UPDATE_CHECK_DONE, 0, reinterpret_cast<LPARAM>(done))) {
            delete done;
        }
    }).detach();
}

std::vector<std::wstring> enumPrinterNames() {
    DWORD needed = 0;
    DWORD returned = 0;
    constexpr DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
    EnumPrintersW(flags, nullptr, 4, nullptr, 0, &needed, &returned);
    if (needed == 0) return {};

    std::vector<BYTE> buffer(needed);
    if (!EnumPrintersW(flags, nullptr, 4, buffer.data(), needed, &needed, &returned)) {
        return {};
    }

    auto* info = reinterpret_cast<PRINTER_INFO_4W*>(buffer.data());
    std::vector<std::wstring> names;
    names.reserve(returned);
    for (DWORD i = 0; i < returned; ++i) {
        if (info[i].pPrinterName && info[i].pPrinterName[0]) {
            names.emplace_back(info[i].pPrinterName);
        }
    }
    return names;
}

void populatePrinterCombo(HWND hwnd) {
    HWND combo = GetDlgItem(hwnd, IDC_SET_BARCODE_PRINTER);
    if (!combo) return;

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    const std::wstring configured = search::load_module_str(
        L"RegularReport", L"BarcodePrinterName", DEFAULT_BARCODE_PRINTER_NAME);
    int selected = -1;
    const auto printers = enumPrinterNames();
    for (const auto& printer : printers) {
        const int index = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0,
                                                        reinterpret_cast<LPARAM>(printer.c_str())));
        if (printer == configured) selected = index;
    }

    if (!configured.empty() && selected < 0) {
        selected = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0,
                                                 reinterpret_cast<LPARAM>(configured.c_str())));
    }
    if (selected >= 0) {
        SendMessageW(combo, CB_SETCURSEL, selected, 0);
    }
}

search::DbSettings collectForm(HWND hwnd) {
    search::DbSettings s;
    s.server = readEdit(hwnd, IDC_SET_SERVER);
    s.initial_database = readEdit(hwnd, IDC_SET_INITIAL_DATABASE);
    s.user = readEdit(hwnd, IDC_SET_USER);
    s.password = readEdit(hwnd, IDC_SET_PASSWORD);
    return s;
}

std::string selectedPickerRoomCode(SettingsMachinePickerState* ps) {
    if (!ps || !ps->roomCombo) return "";
    const int index = static_cast<int>(SendMessageW(ps->roomCombo, CB_GETCURSEL, 0, 0));
    if (index < 0 || index >= static_cast<int>(ps->rooms.size())) return "";
    return ps->rooms[static_cast<size_t>(index)].room_code;
}

void populatePickerMachines(SettingsMachinePickerState* ps) {
    if (!ps || !ps->machineList) return;
    ListView_DeleteAllItems(ps->machineList);
    int selected = -1;
    const std::string current = ps->settings ? ps->settings->quickMachineCodes[static_cast<size_t>(ps->slot)] : "";
    for (int i = 0; i < static_cast<int>(ps->machines.size()); ++i) {
        const auto& machine = ps->machines[static_cast<size_t>(i)];
        const auto code = search::utf8_to_wide(machine.mach_code);
        const auto name = search::utf8_to_wide(machine.mach_name);
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<wchar_t*>(code.c_str());
        ListView_InsertItem(ps->machineList, &item);
        ListView_SetItemText(ps->machineList, i, 1, const_cast<wchar_t*>(name.c_str()));
        if (!current.empty() && machine.mach_code == current) selected = i;
    }
    if (selected < 0 && !ps->machines.empty()) selected = 0;
    if (selected >= 0) {
        ListView_SetItemState(ps->machineList, selected, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    }
}

void reloadPickerMachines(SettingsMachinePickerState* ps) {
    if (!ps || !ps->settings) return;
    ps->machines.clear();
    std::string error;
    if (!search::load_machine_options(collectForm(ps->owner), selectedPickerRoomCode(ps), ps->machines, error)) {
        MessageBoxW(ps->owner, L"检验仪器加载失败。", L"系统设置", MB_ICONERROR);
    }
    populatePickerMachines(ps);
}

void reloadPickerRooms(SettingsMachinePickerState* ps) {
    if (!ps || !ps->settings || !ps->roomCombo) return;
    SendMessageW(ps->roomCombo, CB_RESETCONTENT, 0, 0);
    ps->rooms.clear();
    std::string error;
    if (!search::load_room_options(collectForm(ps->owner), ps->rooms, error)) {
        MessageBoxW(ps->owner, L"检验科室加载失败。", L"系统设置", MB_ICONERROR);
    }
    const std::string currentRoom = ps->settings->quickMachineRoomCodes[static_cast<size_t>(ps->slot)];
    int selected = -1;
    for (int i = 0; i < static_cast<int>(ps->rooms.size()); ++i) {
        const auto text = search::utf8_to_wide(ps->rooms[static_cast<size_t>(i)].room_name);
        SendMessageW(ps->roomCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        if (!currentRoom.empty() && ps->rooms[static_cast<size_t>(i)].room_code == currentRoom) selected = i;
    }
    if (selected < 0 && !ps->rooms.empty()) selected = 0;
    if (selected >= 0) SendMessageW(ps->roomCombo, CB_SETCURSEL, selected, 0);
    reloadPickerMachines(ps);
}

void acceptSettingsMachinePicker(HWND hwnd, SettingsMachinePickerState* ps) {
    if (!ps || !ps->settings || !ps->machineList) return;
    const int index = ListView_GetNextItem(ps->machineList, -1, LVNI_SELECTED);
    if (index < 0 || index >= static_cast<int>(ps->machines.size())) return;
    const auto& machine = ps->machines[static_cast<size_t>(index)];
    const auto slot = static_cast<size_t>(ps->slot);
    ps->settings->quickMachineCodes[slot] = machine.mach_code;
    ps->settings->quickMachineRoomCodes[slot] = selectedPickerRoomCode(ps);
    ps->settings->quickMachineNames[slot] = search::utf8_to_wide(machine.mach_name);
    SetWindowTextW(GetDlgItem(ps->owner, quickMachineEditId(ps->slot)),
                   ps->settings->quickMachineNames[slot].c_str());
    DestroyWindow(hwnd);
}

LRESULT CALLBACK pickerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ps = reinterpret_cast<SettingsMachinePickerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            ps = reinterpret_cast<SettingsMachinePickerState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ps));
            if (!ps) return -1;
            const float scale = search::dpi_scale_factor(hwnd);
            auto S = [scale](int v) { return static_cast<int>(v * scale); };
            const int innerW = S(PICKER_W - PICKER_PAD * 2);
            ps->roomCombo = search::create_combo(hwnd, IDC_PICKER_ROOM, S(PICKER_PAD), S(10),
                                                 innerW, S(PICKER_COMBO_H), false);
            ps->machineList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                              WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                              S(PICKER_PAD), S(PICKER_LIST_Y),
                                              innerW, S(PICKER_H - PICKER_LIST_Y - PICKER_PAD),
                                              hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PICKER_MACHINE)),
                                              GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(ps->machineList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            search::add_list_column(ps->machineList, 0, L"仪器", S(PICKER_CODE_W));
            search::add_list_column(ps->machineList, 1, L"仪器名称", S(PICKER_NAME_W));
            if (ps->settings && ps->settings->ctx.uiFont) {
                EnumChildWindows(hwnd, [](HWND child, LPARAM font) -> BOOL {
                    SendMessageW(child, WM_SETFONT, font, TRUE);
                    return TRUE;
                }, reinterpret_cast<LPARAM>(ps->settings->ctx.uiFont));
            }
            reloadPickerRooms(ps);
            return 0;
        }
        case WM_SIZE:
            if (ps && ps->roomCombo && ps->machineList) {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                const float scale = search::dpi_scale_factor(hwnd);
                const int pad = static_cast<int>(PICKER_PAD * scale);
                const int listY = static_cast<int>(PICKER_LIST_Y * scale);
                const int innerW = std::max(120, static_cast<int>(rc.right - rc.left) - pad * 2);
                MoveWindow(ps->roomCombo, pad, static_cast<int>(10 * scale),
                           innerW, static_cast<int>(PICKER_COMBO_H * scale), TRUE);
                MoveWindow(ps->machineList, pad, listY, innerW,
                           std::max(80, static_cast<int>(rc.bottom - rc.top) - listY - pad), TRUE);
                ListView_SetColumnWidth(ps->machineList, 0, static_cast<int>(PICKER_CODE_W * scale));
                ListView_SetColumnWidth(ps->machineList, 1,
                                        std::max(160, innerW - static_cast<int>((PICKER_CODE_W + 16) * scale)));
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_PICKER_ROOM && HIWORD(wp) == CBN_SELCHANGE) {
                reloadPickerMachines(ps);
                return 0;
            }
            break;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (nm->idFrom == IDC_PICKER_MACHINE && nm->code == NM_DBLCLK) {
                acceptSettingsMachinePicker(hwnd, ps);
                return 0;
            }
            if (nm->idFrom == IDC_PICKER_MACHINE && nm->code == LVN_KEYDOWN) {
                auto* key = reinterpret_cast<NMLVKEYDOWN*>(lp);
                if (key->wVKey == VK_RETURN) {
                    acceptSettingsMachinePicker(hwnd, ps);
                    return 0;
                }
            }
            break;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_DESTROY:
            delete ps;
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void showSettingsMachinePicker(HWND owner, SettingsState* st, int slot, HWND anchor) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = pickerProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = PICKER_CLASS;
        RegisterClassExW(&wc);
        registered = true;
    }
    RECT rc{};
    GetWindowRect(anchor ? anchor : owner, &rc);
    auto* ps = new SettingsMachinePickerState;
    ps->settings = st;
    ps->owner = owner;
    ps->slot = slot;
    const float scale = search::dpi_scale_factor(owner);
    RECT popupRc{0, 0, static_cast<LONG>(PICKER_W * scale), static_cast<LONG>(PICKER_H * scale)};
    AdjustWindowRectEx(&popupRc, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_TOOLWINDOW);
    HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW, PICKER_CLASS, L"选择检验仪器",
                                 WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                 rc.left, rc.bottom + 2,
                                 popupRc.right - popupRc.left, popupRc.bottom - popupRc.top,
                                 owner, nullptr, GetModuleHandleW(nullptr), ps);
    ShowWindow(popup, SW_SHOW);
    SetForegroundWindow(popup);
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

            search::create_label(hwnd, L"LIS ABO代码", S(20), S(200), S(86), S(22));
            search::create_edit(hwnd, IDC_SET_LIS_ABO_CODES, S(116), S(200), S(520), S(24));

            search::create_label(hwnd, L"LIS RhD代码", S(20), S(234), S(86), S(22));
            search::create_edit(hwnd, IDC_SET_LIS_RHD_CODES, S(116), S(234), S(520), S(24));

            search::create_label(hwnd, L"LIS Hb代码", S(20), S(268), S(86), S(22));
            search::create_edit(hwnd, IDC_SET_LIS_HGB_CODES, S(116), S(268), S(520), S(24));

            search::create_label(hwnd, L"LIS PLT代码", S(20), S(302), S(86), S(22));
            search::create_edit(hwnd, IDC_SET_LIS_PLT_CODES, S(116), S(302), S(520), S(24));

            CreateWindowExW(0, L"STATIC", L"多个项目代码用分号分隔；",
                            WS_CHILD | WS_VISIBLE | SS_LEFT,
                            S(116), S(336), S(620), S(22), hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

            search::create_label(hwnd, L"条码打印机", S(20), S(370), S(86), S(22));
            search::create_combo(hwnd, IDC_SET_BARCODE_PRINTER, S(116), S(370), S(520), S(220), false);

            search::create_label(hwnd, L"快捷仪器1", S(20), S(414), S(86), S(22));
            search::create_edit(hwnd, IDC_SET_QUICK_MACHINE_1, S(116), S(414), S(470), S(24));
            search::create_button(hwnd, IDC_SET_QUICK_MACHINE_PICK_1, L"...", S(596), S(412), S(40), S(28));

            search::create_label(hwnd, L"快捷仪器2", S(20), S(448), S(86), S(22));
            search::create_edit(hwnd, IDC_SET_QUICK_MACHINE_2, S(116), S(448), S(470), S(24));
            search::create_button(hwnd, IDC_SET_QUICK_MACHINE_PICK_2, L"...", S(596), S(446), S(40), S(28));

            search::create_label(hwnd, L"快捷仪器3", S(20), S(482), S(86), S(22));
            search::create_edit(hwnd, IDC_SET_QUICK_MACHINE_3, S(116), S(482), S(470), S(24));
            search::create_button(hwnd, IDC_SET_QUICK_MACHINE_PICK_3, L"...", S(596), S(480), S(40), S(28));

            search::create_label(hwnd, L"更新源", S(20), S(526), S(86), S(22));
            search::create_combo(hwnd, IDC_SET_UPDATE_SOURCE, S(116), S(526), S(140), S(180), false);
            search::create_button(hwnd, IDC_SET_CHECK_UPDATE, L"检查更新", S(276), S(524), S(100), S(28));

            search::create_label(hwnd, L"HTTP地址", S(20), S(560), S(86), S(22));
            search::create_edit(hwnd, IDC_SET_UPDATE_MANIFEST_URL, S(116), S(560), S(520), S(24));

            search::create_label(hwnd, L"共享目录", S(20), S(594), S(86), S(22));
            search::create_edit(hwnd, IDC_SET_UPDATE_FOLDER, S(116), S(594), S(520), S(24));

            search::create_button(hwnd, IDC_SET_TEST, L"测试连接", S(116), S(646), S(92), S(30));
            search::create_button(hwnd, IDC_SET_SAVE, L"保存", S(254), S(646), S(84), S(30));
            search::create_button(hwnd, IDC_SET_CANCEL, L"取消", S(362), S(646), S(84), S(30));

            auto& app = st->app;
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_SERVER), app.db.server.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_INITIAL_DATABASE), app.db.initial_database.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_USER), app.db.user.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_PASSWORD), app.db.password.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_FONT_SIZE), std::to_wstring(app.ui.font_size).c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_LIS_ABO_CODES), app.lis.abo_codes.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_LIS_RHD_CODES), app.lis.rhd_codes.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_LIS_HGB_CODES), app.lis.hgb_codes.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_LIS_PLT_CODES), app.lis.plt_codes.c_str());
            for (int i = 0; i < QUICK_MACHINE_COUNT; ++i) {
                st->quickMachineCodes[static_cast<size_t>(i)] =
                    search::wide_to_utf8(search::load_module_str(L"RegularReport", quickMachineCodeKey(i), L""));
                st->quickMachineRoomCodes[static_cast<size_t>(i)] =
                    search::wide_to_utf8(search::load_module_str(L"RegularReport", quickMachineRoomKey(i), L""));
                st->quickMachineNames[static_cast<size_t>(i)] =
                    search::load_module_str(L"RegularReport", quickMachineNameKey(i), L"");
                SetWindowTextW(GetDlgItem(hwnd, quickMachineEditId(i)),
                               st->quickMachineNames[static_cast<size_t>(i)].c_str());
                SendMessageW(GetDlgItem(hwnd, quickMachineEditId(i)), EM_SETREADONLY, TRUE, 0);
            }
            populatePrinterCombo(hwnd);
            populateUpdateSourceCombo(hwnd);
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_UPDATE_MANIFEST_URL),
                           search::load_module_str(UPDATE_SECTION, L"ManifestUrl", L"").c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_UPDATE_FOLDER),
                           search::load_module_str(UPDATE_SECTION, L"FolderPath", L"").c_str());

            EnumChildWindows(hwnd, [](HWND child, LPARAM param) -> BOOL {
                SendMessageW(child, WM_SETFONT, param, TRUE);
                return TRUE;
            }, reinterpret_cast<LPARAM>(st->ctx.uiFont));
            return 0;
        }
        case WM_SIZE:
            return 0;
        case WM_SETTINGS_UPDATE_CHECK_DONE: {
            std::unique_ptr<SettingsUpdateCheckDone> done(
                reinterpret_cast<SettingsUpdateCheckDone*>(lp));
            if (done) {
                showUpdateCheckResult(hwnd, st, *done);
            }
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == IDC_SET_CANCEL) { DestroyWindow(hwnd); return 0; }
            if (id == IDC_SET_QUICK_MACHINE_PICK_1 || id == IDC_SET_QUICK_MACHINE_PICK_2 ||
                id == IDC_SET_QUICK_MACHINE_PICK_3) {
                const int slot = id == IDC_SET_QUICK_MACHINE_PICK_1 ? 0 :
                                 (id == IDC_SET_QUICK_MACHINE_PICK_2 ? 1 : 2);
                showSettingsMachinePicker(hwnd, st, slot, reinterpret_cast<HWND>(lp));
                return 0;
            }
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
            if (id == IDC_SET_CHECK_UPDATE) {
                startConfiguredUpdateCheck(hwnd);
                return 0;
            }
            if (id == IDC_SET_SAVE) {
                st->app.db = collectForm(hwnd);
                st->app.ui.font_size = clampFontSize(_wtoi(readEdit(hwnd, IDC_SET_FONT_SIZE).c_str()));
                st->app.lis.abo_codes = readEdit(hwnd, IDC_SET_LIS_ABO_CODES);
                st->app.lis.rhd_codes = readEdit(hwnd, IDC_SET_LIS_RHD_CODES);
                st->app.lis.hgb_codes = readEdit(hwnd, IDC_SET_LIS_HGB_CODES);
                st->app.lis.plt_codes = readEdit(hwnd, IDC_SET_LIS_PLT_CODES);
                search::save_settings(search::default_ini_path(), st->app);
                search::save_module_str(L"RegularReport", L"BarcodePrinterName",
                                        readCombo(hwnd, IDC_SET_BARCODE_PRINTER));
                for (int i = 0; i < QUICK_MACHINE_COUNT; ++i) {
                    search::save_module_str(L"RegularReport", quickMachineCodeKey(i),
                                            search::utf8_to_wide(st->quickMachineCodes[static_cast<size_t>(i)]));
                    search::save_module_str(L"RegularReport", quickMachineRoomKey(i),
                                            search::utf8_to_wide(st->quickMachineRoomCodes[static_cast<size_t>(i)]));
                    search::save_module_str(L"RegularReport", quickMachineNameKey(i),
                                            st->quickMachineNames[static_cast<size_t>(i)]);
                }
                saveUpdateConfig(collectUpdateConfig(hwnd));
                if (st->ctx.appContext) {
                    auto* gctx = static_cast<app::Context*>(st->ctx.appContext);
                    gctx->dbSettings = st->app.db;
                    gctx->fontSize = st->app.ui.font_size;
                    if (gctx->mainWindow) {
                        SendMessageW(gctx->mainWindow, app::WM_APP_SETTINGS_CHANGED, 0, 0);
                    }
                }
                MessageBoxW(hwnd, L"系统设置已保存。", L"保存", MB_ICONINFORMATION);
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
    if (HWND existing = activate_existing_mdi_child_by_title(ctx.mdiClient, WINDOW_TITLE)) {
        return existing;
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
    mcs.szTitle = WINDOW_TITLE;
    mcs.hOwner = ctx.instance;
    mcs.x = mcs.y = mcs.cx = mcs.cy = CW_USEDEFAULT;

    g_pending = st;
    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0,
        reinterpret_cast<LPARAM>(&mcs)));
    SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    return child;
}

#endif
