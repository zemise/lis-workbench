#include "quality_control_settings_dialog.h"

#ifdef _WIN32

#include "quality_control_store.h"
#include "resource.h"
#include "search_controller.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"

#include <commctrl.h>
#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"QualityControlSettingsDialog";
constexpr const wchar_t* PROP_STATE = L"QualityControlSettingsState";
constexpr COLORREF COLOR_PAGE_BG = RGB(0xF3, 0xF6, 0xFA);
constexpr COLORREF COLOR_CARD_BG = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF COLOR_CARD_BORDER = RGB(0xD8, 0xE0, 0xEA);
constexpr COLORREF COLOR_TEXT = RGB(0x1F, 0x29, 0x37);
constexpr COLORREF COLOR_MUTED_TEXT = RGB(0x6B, 0x72, 0x80);
constexpr COLORREF COLOR_ACCENT = RGB(0x25, 0x63, 0xEB);

enum ControlId {
    IDC_LIST = 6901,
    IDC_ENABLED = 6902,
    IDC_MACH_CODE = 6903,
    IDC_MACH_NAME = 6904,
    IDC_SAMPLE_NO = 6905,
    IDC_QC_NAME = 6906,
    IDC_LEVEL = 6907,
    IDC_ITEM_CODE = 6908,
    IDC_ITEM_NAME = 6909,
    IDC_TARGET_MEAN = 6910,
    IDC_TARGET_SD = 6911,
    IDC_NEW = 6912,
    IDC_SAVE = 6913,
    IDC_DELETE = 6914,
    IDC_CLOSE = 6915,
    IDC_PICK_MACHINE = 6916,
    IDC_ROOM_CODE = 6917,
    IDC_TITLE = 6918,
    IDC_SUBTITLE = 6919,
    IDC_LIST_TITLE = 6920,
    IDC_LIST_HINT = 6921,
    IDC_FORM_TITLE = 6922,
    IDC_FORM_HINT = 6923,
};

constexpr int PICKER_ROOM = 6951;
constexpr int PICKER_MACHINE = 6952;
constexpr const wchar_t* PICKER_CLASS = L"QualityControlMachinePicker";

struct State {
    HFONT font = nullptr;
    HFONT titleFont = nullptr;
    HFONT sectionFont = nullptr;
    HBRUSH pageBrush = nullptr;
    HBRUSH cardBrush = nullptr;
    HBRUSH editBrush = nullptr;
    search::DbSettings dbSettings;
    bool embedded = false;
    HWND title = nullptr;
    HWND subtitle = nullptr;
    HWND listTitle = nullptr;
    HWND listHint = nullptr;
    HWND formTitle = nullptr;
    HWND formHint = nullptr;
    HWND list = nullptr;
    HWND enabled = nullptr;
    HWND roomCodeLabel = nullptr;
    HWND roomCode = nullptr;
    HWND machCodeLabel = nullptr;
    HWND machCode = nullptr;
    HWND pickMachine = nullptr;
    HWND machNameLabel = nullptr;
    HWND machName = nullptr;
    HWND sampleNoLabel = nullptr;
    HWND sampleNo = nullptr;
    HWND qcNameLabel = nullptr;
    HWND qcName = nullptr;
    HWND levelLabel = nullptr;
    HWND level = nullptr;
    HWND itemCodeLabel = nullptr;
    HWND itemCode = nullptr;
    HWND itemNameLabel = nullptr;
    HWND itemName = nullptr;
    HWND targetMeanLabel = nullptr;
    HWND targetMean = nullptr;
    HWND targetSdLabel = nullptr;
    HWND targetSd = nullptr;
    std::vector<qc::Config> rows;
    int currentId = 0;
};

struct PickerState {
    State* ownerState = nullptr;
    HWND owner = nullptr;
    HWND roomCombo = nullptr;
    HWND machineList = nullptr;
    std::vector<search::RoomOption> rooms;
    std::vector<search::MachineOption> machines;
};

HFONT createDerivedFont(HFONT base, int pointDelta, LONG weight) {
    LOGFONTW lf{};
    if (!base || GetObjectW(base, sizeof(lf), &lf) != sizeof(lf)) {
        NONCLIENTMETRICSW nm{};
        nm.cbSize = sizeof(nm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(nm), &nm, 0);
        lf = nm.lfMessageFont;
    }
    HDC screen = GetDC(nullptr);
    const int dpi = screen ? GetDeviceCaps(screen, LOGPIXELSY) : 96;
    const int point = std::max(8, MulDiv(std::abs(lf.lfHeight), 72, dpi));
    lf.lfHeight = -MulDiv(std::max(8, point + pointDelta), dpi, 72);
    lf.lfWeight = weight;
    if (screen) ReleaseDC(nullptr, screen);
    return CreateFontIndirectW(&lf);
}

void drawRoundRect(HDC dc, const RECT& rc, int radius, COLORREF fill, COLORREF border) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

HWND text(HWND parent, int id, const wchar_t* value) {
    return CreateWindowExW(0, L"STATIC", value, WS_CHILD | WS_VISIBLE | SS_LEFT,
                           0, 0, 0, 0, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND label(HWND parent, const wchar_t* text) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
                           0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND edit(HWND parent, int id) {
    return search::create_edit(parent, id, 0, 0, 10, 24);
}

HWND button(HWND parent, int id, const wchar_t* text) {
    return search::create_button(parent, id, text, 0, 0, 76, 28);
}

std::string textUtf8(HWND hwnd) {
    wchar_t buffer[256]{};
    GetWindowTextW(hwnd, buffer, 256);
    return search::wide_to_utf8(buffer);
}

void setText(HWND hwnd, const std::string& value) {
    SetWindowTextW(hwnd, search::utf8_to_wide(value).c_str());
}

void setCell(HWND list, int row, int col, const std::string& text) {
    const auto wide = search::utf8_to_wide(text);
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(wide.c_str()));
}

void addColumn(HWND list, int col, const wchar_t* text, int width) {
    LVCOLUMNW lvc{};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    lvc.pszText = const_cast<wchar_t*>(text);
    lvc.cx = width;
    lvc.iSubItem = col;
    ListView_InsertColumn(list, col, &lvc);
}

void populateList(State* st) {
    if (!st || !st->list) return;
    ListView_DeleteAllItems(st->list);
    std::string error;
    if (!qc::load_configs(st->rows, error)) {
        MessageBoxW(GetParent(st->list), search::utf8_to_wide(error).c_str(), L"质控品设置", MB_ICONERROR);
        return;
    }
    for (int i = 0; i < static_cast<int>(st->rows.size()); ++i) {
        const auto& row = st->rows[static_cast<size_t>(i)];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        const auto enabled = row.enabled ? L"是" : L"否";
        item.pszText = const_cast<wchar_t*>(enabled);
        ListView_InsertItem(st->list, &item);
        setCell(st->list, i, 1, row.mach_code);
        setCell(st->list, i, 2, row.sample_no);
        setCell(st->list, i, 3, row.qc_name);
        setCell(st->list, i, 4, row.level);
        setCell(st->list, i, 5, row.item_code);
    }
}

std::string selectedPickerRoomCode(PickerState* ps) {
    if (!ps || !ps->roomCombo) return "";
    const int index = static_cast<int>(SendMessageW(ps->roomCombo, CB_GETCURSEL, 0, 0));
    if (index < 0 || index >= static_cast<int>(ps->rooms.size())) return "";
    return ps->rooms[static_cast<size_t>(index)].room_code;
}

void populatePickerMachines(PickerState* ps) {
    if (!ps || !ps->machineList) return;
    ListView_DeleteAllItems(ps->machineList);
    const std::string current = ps->ownerState ? search::trim(textUtf8(ps->ownerState->machCode)) : "";
    int selected = -1;
    for (int i = 0; i < static_cast<int>(ps->machines.size()); ++i) {
        const auto& machine = ps->machines[static_cast<size_t>(i)];
        const auto code = search::utf8_to_wide(machine.mach_code);
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<wchar_t*>(code.c_str());
        ListView_InsertItem(ps->machineList, &item);
        setCell(ps->machineList, i, 1, machine.mach_name);
        setCell(ps->machineList, i, 2, machine.group_code);
        setCell(ps->machineList, i, 3, machine.group_name);
        setCell(ps->machineList, i, 4, machine.sample_name);
        if (!current.empty() && machine.mach_code == current) selected = i;
    }
    if (selected < 0 && !ps->machines.empty()) selected = 0;
    if (selected >= 0) {
        ListView_SetItemState(ps->machineList, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

void reloadPickerMachines(PickerState* ps) {
    if (!ps || !ps->ownerState) return;
    ps->machines.clear();
    std::string error;
    if (!search::load_report_machine_picker_machine_options(ps->ownerState->dbSettings, selectedPickerRoomCode(ps),
                                                            ps->machines, error)) {
        MessageBoxW(ps->owner, search::utf8_to_wide(error).c_str(), L"选择检验仪器", MB_ICONERROR);
    }
    populatePickerMachines(ps);
}

void reloadPickerRooms(PickerState* ps) {
    if (!ps || !ps->ownerState || !ps->roomCombo) return;
    SendMessageW(ps->roomCombo, CB_RESETCONTENT, 0, 0);
    ps->rooms.clear();
    std::string error;
    if (!search::load_report_machine_picker_room_options(ps->ownerState->dbSettings, ps->rooms, error)) {
        MessageBoxW(ps->owner, search::utf8_to_wide(error).c_str(), L"选择检验仪器", MB_ICONERROR);
    }
    const std::string currentRoom = search::trim(textUtf8(ps->ownerState->roomCode));
    int selected = -1;
    for (int i = 0; i < static_cast<int>(ps->rooms.size()); ++i) {
        const auto& room = ps->rooms[static_cast<size_t>(i)];
        const auto text = search::utf8_to_wide(room.room_name.empty() ? room.room_code : room.room_name);
        SendMessageW(ps->roomCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        if (!currentRoom.empty() && room.room_code == currentRoom) selected = i;
    }
    if (selected < 0 && !ps->rooms.empty()) selected = 0;
    if (selected >= 0) SendMessageW(ps->roomCombo, CB_SETCURSEL, selected, 0);
    reloadPickerMachines(ps);
}

void acceptPicker(HWND hwnd, PickerState* ps) {
    if (!ps || !ps->ownerState || !ps->machineList) return;
    const int index = ListView_GetNextItem(ps->machineList, -1, LVNI_SELECTED);
    if (index < 0 || index >= static_cast<int>(ps->machines.size())) return;
    const auto& machine = ps->machines[static_cast<size_t>(index)];
    setText(ps->ownerState->roomCode, selectedPickerRoomCode(ps));
    setText(ps->ownerState->machCode, machine.mach_code);
    setText(ps->ownerState->machName, machine.mach_name);
    DestroyWindow(hwnd);
}

LRESULT CALLBACK pickerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ps = reinterpret_cast<PickerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            ps = reinterpret_cast<PickerState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ps));
            ps->roomCombo = search::create_combo(hwnd, PICKER_ROOM, 0, 0, 10, 180, false);
            ps->machineList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                              WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                              0, 0, 0, 0, hwnd, win32_control_id(PICKER_MACHINE),
                                              GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(ps->machineList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            addColumn(ps->machineList, 0, L"仪器", 90);
            addColumn(ps->machineList, 1, L"仪器名称", 160);
            addColumn(ps->machineList, 2, L"项目代码", 80);
            addColumn(ps->machineList, 3, L"项目名称", 150);
            addColumn(ps->machineList, 4, L"样本", 90);
            if (ps->ownerState && ps->ownerState->font) {
                SendMessageW(ps->roomCombo, WM_SETFONT, reinterpret_cast<WPARAM>(ps->ownerState->font), TRUE);
                SendMessageW(ps->machineList, WM_SETFONT, reinterpret_cast<WPARAM>(ps->ownerState->font), TRUE);
            }
            reloadPickerRooms(ps);
            return 0;
        }
        case WM_SIZE:
            if (ps && ps->roomCombo && ps->machineList) {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                const int pad = 10;
                MoveWindow(ps->roomCombo, pad, pad, std::max(120, static_cast<int>(rc.right - pad * 2)), 26, TRUE);
                MoveWindow(ps->machineList, pad, pad + 34, std::max(120, static_cast<int>(rc.right - pad * 2)),
                           std::max(80, static_cast<int>(rc.bottom - pad * 2 - 34)), TRUE);
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == PICKER_ROOM && HIWORD(wp) == CBN_SELCHANGE) {
                reloadPickerMachines(ps);
                return 0;
            }
            break;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (nm->idFrom == PICKER_MACHINE && nm->code == NM_DBLCLK) {
                acceptPicker(hwnd, ps);
                return 0;
            }
            if (nm->idFrom == PICKER_MACHINE && nm->code == LVN_KEYDOWN) {
                auto* key = reinterpret_cast<NMLVKEYDOWN*>(lp);
                if (key->wVKey == VK_RETURN) {
                    acceptPicker(hwnd, ps);
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
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void showMachinePicker(HWND owner, State* st) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = pickerProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = PICKER_CLASS;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        RegisterClassExW(&wc);
        registered = true;
    }
    auto* ps = new PickerState;
    ps->ownerState = st;
    ps->owner = owner;
    HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW, PICKER_CLASS, L"选择检验仪器",
                                 WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 720, 420,
                                 owner, nullptr, GetModuleHandleW(nullptr), ps);
    if (!popup) delete ps;
}

void clearForm(State* st) {
    st->currentId = 0;
    SendMessageW(st->enabled, BM_SETCHECK, BST_CHECKED, 0);
    SetWindowTextW(st->roomCode, L"");
    SetWindowTextW(st->machCode, L"");
    SetWindowTextW(st->machName, L"");
    SetWindowTextW(st->sampleNo, L"");
    SetWindowTextW(st->qcName, L"");
    SetWindowTextW(st->level, L"");
    SetWindowTextW(st->itemCode, L"");
    SetWindowTextW(st->itemName, L"");
    SetWindowTextW(st->targetMean, L"");
    SetWindowTextW(st->targetSd, L"");
}

void loadForm(State* st, int index) {
    if (!st || index < 0 || index >= static_cast<int>(st->rows.size())) return;
    const auto& row = st->rows[static_cast<size_t>(index)];
    st->currentId = row.id;
    SendMessageW(st->enabled, BM_SETCHECK, row.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    setText(st->roomCode, row.room_code);
    setText(st->machCode, row.mach_code);
    setText(st->machName, row.mach_name);
    setText(st->sampleNo, row.sample_no);
    setText(st->qcName, row.qc_name);
    setText(st->level, row.level);
    setText(st->itemCode, row.item_code);
    setText(st->itemName, row.item_name);
    setText(st->targetMean, row.target_mean);
    setText(st->targetSd, row.target_sd);
}

bool collectForm(State* st, qc::Config& row) {
    row.id = st->currentId;
    row.enabled = SendMessageW(st->enabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
    row.room_code = search::trim(textUtf8(st->roomCode));
    row.mach_code = search::trim(textUtf8(st->machCode));
    row.mach_name = search::trim(textUtf8(st->machName));
    row.sample_no = search::trim(textUtf8(st->sampleNo));
    row.qc_name = search::trim(textUtf8(st->qcName));
    row.level = search::trim(textUtf8(st->level));
    row.item_code = search::trim(textUtf8(st->itemCode));
    row.item_name = search::trim(textUtf8(st->itemName));
    row.target_mean = search::trim(textUtf8(st->targetMean));
    row.target_sd = search::trim(textUtf8(st->targetSd));
    if (row.mach_code.empty() || row.sample_no.empty()) {
        MessageBoxW(st->list, L"仪器代码和样本号不能为空。", L"质控品设置", MB_ICONWARNING);
        return false;
    }
    return true;
}

void layout(HWND hwnd, State* st) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const float scale = search::dpi_scale_factor(hwnd);
    auto S = [scale](int v) { return static_cast<int>(v * scale); };
    const int pad = S(24);
    const int cardPadX = S(22);
    const int top = S(86);
    const int clientW = std::max(0, static_cast<int>(rc.right - rc.left));
    const int gap = S(18);
    const int contentW = std::max(S(760), clientW - pad * 2);
    const int listW = std::max(S(400), std::min(S(580), (contentW - gap) * 3 / 5));
    const int formW = std::max(S(360), contentW - listW - gap);
    const RECT listCard{pad, top, pad + listW, std::max(top + S(360), static_cast<int>(rc.bottom) - S(24))};
    const RECT formCard{listCard.right + gap, top, listCard.right + gap + formW, listCard.bottom};
    const int labelW = S(78);
    const int rowH = S(34);
    const int editH = S(26);
    const int formX = formCard.left + cardPadX;
    const int formInnerW = formCard.right - formCard.left - cardPadX * 2;
    const int editW = std::max(S(170), formInnerW - labelW - S(10));
    if (st->embedded) {
        const int innerPad = S(0);
        const int embeddedGap = S(16);
        const int embeddedListW = std::max(S(360), std::min(S(560), (clientW - embeddedGap) / 2));
        const int embeddedFormX = embeddedListW + embeddedGap;
        const int embeddedFormW = std::max(S(320), clientW - embeddedFormX);
        const int embeddedLabelW = S(78);
        const int embeddedRowH = S(32);
        const int embeddedEditH = S(26);
        const int embeddedEditW = std::max(S(160), embeddedFormW - embeddedLabelW - S(10));
        ShowWindow(st->title, SW_HIDE);
        ShowWindow(st->subtitle, SW_HIDE);
        ShowWindow(st->listTitle, SW_HIDE);
        ShowWindow(st->listHint, SW_HIDE);
        ShowWindow(st->formTitle, SW_HIDE);
        ShowWindow(st->formHint, SW_HIDE);
        MoveWindow(st->list, innerPad, innerPad,
                   embeddedListW, std::max(S(160), static_cast<int>(rc.bottom - rc.top)), TRUE);
        const auto placeEmbedded = [&](int row, HWND labelHwnd, HWND ctrl) {
            const int y = innerPad + row * embeddedRowH;
            MoveWindow(labelHwnd, embeddedFormX, y, embeddedLabelW, embeddedEditH, TRUE);
            MoveWindow(ctrl, embeddedFormX + embeddedLabelW + S(10), y, embeddedEditW, embeddedEditH, TRUE);
        };
        placeEmbedded(0, st->roomCodeLabel, st->roomCode);
        {
            const int y = innerPad + embeddedRowH;
            const int pickW = S(76);
            const int editOnlyW = std::max(S(80), embeddedEditW - pickW - S(8));
            MoveWindow(st->machCodeLabel, embeddedFormX, y, embeddedLabelW, embeddedEditH, TRUE);
            MoveWindow(st->machCode, embeddedFormX + embeddedLabelW + S(10), y, editOnlyW, embeddedEditH, TRUE);
            MoveWindow(st->pickMachine, embeddedFormX + embeddedLabelW + S(10) + editOnlyW + S(8),
                       y - S(1), pickW, embeddedEditH + S(2), TRUE);
        }
        placeEmbedded(2, st->machNameLabel, st->machName);
        placeEmbedded(3, st->sampleNoLabel, st->sampleNo);
        placeEmbedded(4, st->qcNameLabel, st->qcName);
        placeEmbedded(5, st->levelLabel, st->level);
        placeEmbedded(6, st->itemCodeLabel, st->itemCode);
        placeEmbedded(7, st->itemNameLabel, st->itemName);
        placeEmbedded(8, st->targetMeanLabel, st->targetMean);
        placeEmbedded(9, st->targetSdLabel, st->targetSd);
        MoveWindow(st->enabled, embeddedFormX + embeddedLabelW + S(10), innerPad + 10 * embeddedRowH,
                   S(90), embeddedEditH, TRUE);
        const int btnY = innerPad + 11 * embeddedRowH;
        MoveWindow(GetDlgItem(hwnd, IDC_NEW), embeddedFormX, btnY, S(76), S(30), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_SAVE), embeddedFormX + S(86), btnY, S(76), S(30), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_DELETE), embeddedFormX + S(172), btnY, S(76), S(30), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_CLOSE), embeddedFormX + S(258), btnY, S(76), S(30), TRUE);
        ShowWindow(GetDlgItem(hwnd, IDC_CLOSE), SW_HIDE);
        return;
    }
    MoveWindow(st->title, pad, S(18), std::max(S(300), clientW - pad * 2), S(30), TRUE);
    MoveWindow(st->subtitle, pad, S(50), std::max(S(300), clientW - pad * 2), S(22), TRUE);
    MoveWindow(st->listTitle, listCard.left + cardPadX, listCard.top + S(16), listW - cardPadX * 2, S(22), TRUE);
    MoveWindow(st->listHint, listCard.left + cardPadX, listCard.top + S(42), listW - cardPadX * 2, S(22), TRUE);
    MoveWindow(st->list, listCard.left + cardPadX, listCard.top + S(74),
               listW - cardPadX * 2, listCard.bottom - listCard.top - S(92), TRUE);
    MoveWindow(st->formTitle, formCard.left + cardPadX, formCard.top + S(16), formW - cardPadX * 2, S(22), TRUE);
    MoveWindow(st->formHint, formCard.left + cardPadX, formCard.top + S(42), formW - cardPadX * 2, S(22), TRUE);
    const auto place = [&](int row, HWND labelHwnd, HWND ctrl) {
        const int y = formCard.top + S(74) + row * rowH;
        MoveWindow(labelHwnd, formX, y, labelW, editH, TRUE);
        MoveWindow(ctrl, formX + labelW + S(10), y, editW, editH, TRUE);
    };
    place(1, st->roomCodeLabel, st->roomCode);
    {
        const int y = formCard.top + S(74) + 2 * rowH;
        const int pickW = S(76);
        const int editOnlyW = std::max(S(80), editW - pickW - S(8));
        MoveWindow(st->machCodeLabel, formX, y, labelW, editH, TRUE);
        MoveWindow(st->machCode, formX + labelW + S(10), y, editOnlyW, editH, TRUE);
        MoveWindow(st->pickMachine, formX + labelW + S(10) + editOnlyW + S(8), y - S(1), pickW, editH + S(2), TRUE);
    }
    place(3, st->machNameLabel, st->machName);
    place(4, st->sampleNoLabel, st->sampleNo);
    place(5, st->qcNameLabel, st->qcName);
    place(6, st->levelLabel, st->level);
    place(7, st->itemCodeLabel, st->itemCode);
    place(8, st->itemNameLabel, st->itemName);
    place(9, st->targetMeanLabel, st->targetMean);
    place(10, st->targetSdLabel, st->targetSd);
    MoveWindow(st->enabled, formX + labelW + S(10), formCard.top + S(74), S(90), editH, TRUE);
    const int btnY = formCard.bottom - S(52);
    MoveWindow(GetDlgItem(hwnd, IDC_NEW), formX, btnY, S(76), S(30), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_SAVE), formX + S(86), btnY, S(76), S(30), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_DELETE), formX + S(172), btnY, S(76), S(30), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CLOSE), formCard.right - cardPadX - S(76), btnY, S(76), S(30), TRUE);
}

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<State*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            st = reinterpret_cast<State*>(cs->lpCreateParams);
            SetPropW(hwnd, PROP_STATE, reinterpret_cast<HANDLE>(st));
            st->titleFont = createDerivedFont(st->font, 5, FW_SEMIBOLD);
            st->sectionFont = createDerivedFont(st->font, 1, FW_SEMIBOLD);
            st->pageBrush = CreateSolidBrush(COLOR_PAGE_BG);
            st->cardBrush = CreateSolidBrush(COLOR_CARD_BG);
            st->editBrush = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
            st->title = text(hwnd, IDC_TITLE, L"质控品设置");
            st->subtitle = text(hwnd, IDC_SUBTITLE, L"维护仪器固定质控样本号、质控水平、项目靶值和 SD。");
            st->listTitle = text(hwnd, IDC_LIST_TITLE, L"质控配置");
            st->listHint = text(hwnd, IDC_LIST_HINT, L"左侧显示本地 SQLite 中已维护的质控规则。");
            st->formTitle = text(hwnd, IDC_FORM_TITLE, L"配置明细");
            st->formHint = text(hwnd, IDC_FORM_HINT, L"可手工录入，也可从 LIS 检验仪器列表选择。");
            st->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                       WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                       0, 0, 0, 0, hwnd, win32_control_id(IDC_LIST), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            addColumn(st->list, 0, L"启用", 54);
            addColumn(st->list, 1, L"仪器", 90);
            addColumn(st->list, 2, L"样本号", 90);
            addColumn(st->list, 3, L"质控名称", 140);
            addColumn(st->list, 4, L"水平", 70);
            addColumn(st->list, 5, L"项目", 90);
            st->enabled = CreateWindowExW(0, L"BUTTON", L"启用", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                          0, 0, 0, 0, hwnd, win32_control_id(IDC_ENABLED), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(st->enabled, BM_SETCHECK, BST_CHECKED, 0);
            st->roomCodeLabel = label(hwnd, L"科室代码");
            st->roomCode = edit(hwnd, IDC_ROOM_CODE);
            st->machCodeLabel = label(hwnd, L"仪器代码");
            st->machCode = edit(hwnd, IDC_MACH_CODE);
            st->pickMachine = button(hwnd, IDC_PICK_MACHINE, L"选择...");
            st->machNameLabel = label(hwnd, L"仪器名称");
            st->machName = edit(hwnd, IDC_MACH_NAME);
            st->sampleNoLabel = label(hwnd, L"样本号");
            st->sampleNo = edit(hwnd, IDC_SAMPLE_NO);
            st->qcNameLabel = label(hwnd, L"质控名称");
            st->qcName = edit(hwnd, IDC_QC_NAME);
            st->levelLabel = label(hwnd, L"水平");
            st->level = edit(hwnd, IDC_LEVEL);
            st->itemCodeLabel = label(hwnd, L"项目代码");
            st->itemCode = edit(hwnd, IDC_ITEM_CODE);
            st->itemNameLabel = label(hwnd, L"项目名称");
            st->itemName = edit(hwnd, IDC_ITEM_NAME);
            st->targetMeanLabel = label(hwnd, L"靶值");
            st->targetMean = edit(hwnd, IDC_TARGET_MEAN);
            st->targetSdLabel = label(hwnd, L"SD");
            st->targetSd = edit(hwnd, IDC_TARGET_SD);
            button(hwnd, IDC_NEW, L"新增");
            button(hwnd, IDC_SAVE, L"保存");
            button(hwnd, IDC_DELETE, L"删除");
            button(hwnd, IDC_CLOSE, L"关闭");
            search::apply_font_to_children(hwnd, st->font);
            SendMessageW(st->title, WM_SETFONT, reinterpret_cast<WPARAM>(st->titleFont), TRUE);
            SendMessageW(st->listTitle, WM_SETFONT, reinterpret_cast<WPARAM>(st->sectionFont), TRUE);
            SendMessageW(st->formTitle, WM_SETFONT, reinterpret_cast<WPARAM>(st->sectionFont), TRUE);
            populateList(st);
            layout(hwnd, st);
            return 0;
        }
        case WM_SIZE:
            layout(hwnd, st);
            return 0;
        case WM_ERASEBKGND: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc,
                     st && st->embedded
                         ? (st->cardBrush ? st->cardBrush : GetSysColorBrush(COLOR_WINDOW))
                         : (st && st->pageBrush ? st->pageBrush : GetSysColorBrush(COLOR_BTNFACE)));
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, st && st->embedded
                              ? (st->cardBrush ? st->cardBrush : GetSysColorBrush(COLOR_WINDOW))
                              : (st && st->pageBrush ? st->pageBrush : GetSysColorBrush(COLOR_BTNFACE)));
            if (st && st->embedded) {
                EndPaint(hwnd, &ps);
                return 0;
            }
            const float scale = search::dpi_scale_factor(hwnd);
            auto S = [scale](int v) { return static_cast<int>(v * scale); };
            const int pad = S(24);
            const int top = S(86);
            const int gap = S(18);
            const int clientW = std::max(0, static_cast<int>(rc.right - rc.left));
            const int contentW = std::max(S(760), clientW - pad * 2);
            const int listW = std::max(S(400), std::min(S(580), (contentW - gap) * 3 / 5));
            const int formW = std::max(S(360), contentW - listW - gap);
            const RECT listCard{pad, top, pad + listW, std::max(top + S(360), static_cast<int>(rc.bottom) - S(24))};
            const RECT formCard{listCard.right + gap, top, listCard.right + gap + formW, listCard.bottom};
            drawRoundRect(dc, listCard, S(8), COLOR_CARD_BG, COLOR_CARD_BORDER);
            drawRoundRect(dc, formCard, S(8), COLOR_CARD_BG, COLOR_CARD_BORDER);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
            SetBkMode(dc, TRANSPARENT);
            if (id == IDC_TITLE) {
                SetTextColor(dc, COLOR_TEXT);
            } else if (id == IDC_LIST_TITLE || id == IDC_FORM_TITLE) {
                SetTextColor(dc, COLOR_ACCENT);
            } else if (id == IDC_SUBTITLE || id == IDC_LIST_HINT || id == IDC_FORM_HINT) {
                SetTextColor(dc, COLOR_MUTED_TEXT);
            } else {
                SetTextColor(dc, COLOR_TEXT);
            }
            const bool pageText = id == IDC_TITLE || id == IDC_SUBTITLE;
            return reinterpret_cast<LRESULT>(pageText && st && st->pageBrush
                                                 ? st->pageBrush
                                                 : (st && st->cardBrush ? st->cardBrush : GetSysColorBrush(COLOR_WINDOW)));
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, RGB(0xFF, 0xFF, 0xFF));
            SetTextColor(dc, COLOR_TEXT);
            return reinterpret_cast<LRESULT>(st && st->editBrush ? st->editBrush : GetStockObject(WHITE_BRUSH));
        }
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (st && nm->idFrom == IDC_LIST && nm->code == LVN_ITEMCHANGED) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED)) {
                    loadForm(st, lv->iItem);
                }
            }
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_CLOSE) {
                if (st && st->embedded) return 0;
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wp) == IDC_NEW) {
                clearForm(st);
                return 0;
            }
            if (LOWORD(wp) == IDC_PICK_MACHINE) {
                showMachinePicker(hwnd, st);
                return 0;
            }
            if (LOWORD(wp) == IDC_SAVE) {
                qc::Config row;
                if (!collectForm(st, row)) return 0;
                std::string error;
                if (!qc::save_config(row, error)) {
                    MessageBoxW(hwnd, search::utf8_to_wide(error).c_str(), L"质控品设置", MB_ICONERROR);
                    return 0;
                }
                populateList(st);
                clearForm(st);
                return 0;
            }
            if (LOWORD(wp) == IDC_DELETE) {
                if (st->currentId <= 0) return 0;
                if (MessageBoxW(hwnd, L"确定删除当前质控品配置？", L"质控品设置", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
                std::string error;
                if (!qc::delete_config(st->currentId, error)) {
                    MessageBoxW(hwnd, search::utf8_to_wide(error).c_str(), L"质控品设置", MB_ICONERROR);
                    return 0;
                }
                populateList(st);
                clearForm(st);
                return 0;
            }
            break;
        case WM_DESTROY:
            if (st) {
                if (st->titleFont) DeleteObject(st->titleFont);
                if (st->sectionFont) DeleteObject(st->sectionFont);
                if (st->pageBrush) DeleteObject(st->pageBrush);
                if (st->cardBrush) DeleteObject(st->cardBrush);
                if (st->editBrush) DeleteObject(st->editBrush);
                RemovePropW(hwnd, PROP_STATE);
                delete st;
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void registerClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = WND_CLASS;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APP));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);
    registered = true;
}

}  // namespace

HWND create_quality_control_settings_panel(HWND parent, HFONT font, const search::DbSettings& db_settings) {
    registerClass();
    auto* st = new State;
    st->font = font;
    st->dbSettings = db_settings;
    st->embedded = true;
    HWND hwnd = CreateWindowExW(0, WND_CLASS, L"",
                                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                0, 0, 10, 10,
                                parent, nullptr, GetModuleHandleW(nullptr), st);
    if (!hwnd) delete st;
    return hwnd;
}

void update_quality_control_settings_panel_db(HWND panel, const search::DbSettings& db_settings) {
    auto* st = panel ? reinterpret_cast<State*>(GetPropW(panel, PROP_STATE)) : nullptr;
    if (st) st->dbSettings = db_settings;
}

#endif
