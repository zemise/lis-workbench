#include "specimen_sign_module.h"

#ifdef _WIN32

#include "main_app.h"
#include "resource.h"
#include "search_controller.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"

#include <commctrl.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"SpecimenSignModuleChild";
constexpr const wchar_t* WINDOW_TITLE = L"标本签收中心";
constexpr const wchar_t* PROP_STATE = L"SpecimenSignSt";

constexpr COLORREF COLOR_BG = RGB(0xF0, 0xF0, 0xF0);
constexpr COLORREF COLOR_SCAN = RGB(0xF3, 0xE6, 0xD2);
constexpr COLORREF COLOR_ALERT = RGB(0xF5, 0x0D, 0x19);
constexpr UINT_PTR IDT_ROLLOVER_DATES = 6201;
constexpr UINT WM_SPECIMEN_RUN_QUERY = WM_APP + 0x531;
constexpr UINT WM_SPECIMEN_QUERY_DONE = WM_APP + 0x532;

enum ControlId {
    IDC_SCAN_INPUT = 6101,
    IDC_MANUAL_SIGN = 6102,
    IDC_REJECT = 6103,
    IDC_MANAGE = 6104,
    IDC_RESET = 6105,
    IDC_CLOSE = 6106,
    IDC_PATIENT_TYPE = 6120,
    IDC_ORDER_LIST = 6140,
    IDC_SIGN_DATE_ENABLED = 6160,
    IDC_APPLY_DATE_ENABLED = 6161,
    IDC_SIGN_START = 6162,
    IDC_SIGN_END = 6163,
    IDC_APPLY_START = 6164,
    IDC_APPLY_END = 6165,
    IDC_BARCODE = 6166,
    IDC_ROOM = 6167,
    IDC_NAME = 6168,
    IDC_SHOW_PERSONAL = 6169,
    IDC_SIGNED = 6170,
    IDC_QUERY = 6171,
    IDC_GROUP_STAT = 6172,
    IDC_STAT = 6173,
    IDC_PRINT_SIGN = 6174,
    IDC_PATCH_BARCODE = 6175,
    IDC_PRINT_SELECTED = 6176,
    IDC_MOVE_SIGNED = 6177,
    IDC_NOT_DELIVERED = 6178,
    IDC_EXPORT_EXCEL = 6179,
    IDC_SIGNED_LIST = 6180,
};

struct SpecimenSignState {
    ModuleContext ctx;
    HWND scanInput = nullptr;
    HWND barcodeStatus = nullptr;
    HWND generalStatus = nullptr;
    HWND patientName = nullptr;
    HWND patientType = nullptr;
    HWND patientSex = nullptr;
    HWND patientAge = nullptr;
    HWND professionalGroup = nullptr;
    HWND patientNo = nullptr;
    HWND doctor = nullptr;
    HWND dept = nullptr;
    HWND bedNo = nullptr;
    HWND fee = nullptr;
    HWND orderList = nullptr;
    HWND orderRemoveButton = nullptr;
    HWND orderAddButton = nullptr;
    HWND signStart = nullptr;
    HWND signEnd = nullptr;
    HWND applyStart = nullptr;
    HWND applyEnd = nullptr;
    HWND barcode = nullptr;
    HWND room = nullptr;
    HWND name = nullptr;
    HWND showPersonal = nullptr;
    HWND signedOnly = nullptr;
    HWND signedList = nullptr;
    HBRUSH bgBrush = nullptr;
    HBRUSH scanBrush = nullptr;
    std::vector<search::PatientTypeOption> patientTypes;
    int autoDateStamp = 0;
    bool querying = false;
};

struct QueryPayload {
    bool ok = false;
    bool listMode = false;
    std::string error;
    search::SpecimenBarcodeResult result;
    std::vector<search::SpecimenSignedListRow> signedRows;
};

SpecimenSignState* g_pending = nullptr;

int S(HWND hwnd, int value) {
    return static_cast<int>(value * search::dpi_scale_factor(hwnd));
}

void applyFont(HWND hwnd, HFONT font) {
    if (!font) return;
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    EnumChildWindows(hwnd, [](HWND child, LPARAM p) -> BOOL {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(p), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font));
}

HWND label(HWND parent, const wchar_t* text, int x, int y, int w, int h, DWORD align = SS_RIGHT) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | align,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND edit(HWND parent, int id, int x, int y, int w, int h, DWORD style = ES_AUTOHSCROLL) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
                           x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND combo(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                           x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND dateTimePicker(HWND parent, int id, int x, int y, int w, int h) {
    HWND hwnd = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATECENTURYFORMAT,
                                x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
    DateTime_SetFormat(hwnd, L"yyyy-MM-dd HH:mm");
    return hwnd;
}

HWND button(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    return search::create_button(parent, id, text, x, y, w, h);
}

HWND checkbox(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h, bool checked) {
    HWND hwnd = CreateWindowExW(0, L"BUTTON", text,
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
    Button_SetCheck(hwnd, checked ? BST_CHECKED : BST_UNCHECKED);
    return hwnd;
}

std::string windowTextUtf8(HWND hwnd) {
    if (!hwnd) return {};
    const int len = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), len + 1);
    text.resize(static_cast<size_t>(len));
    return search::trim(search::wide_to_utf8(text));
}

void setWindowTextUtf8(HWND hwnd, const std::string& text) {
    if (!hwnd) return;
    const auto wide = search::utf8_to_wide(text);
    SetWindowTextW(hwnd, wide.c_str());
}

void setBarcodeStatus(SpecimenSignState* st, const std::wstring& text) {
    if (st && st->barcodeStatus) {
        SetWindowTextW(st->barcodeStatus, text.c_str());
    }
}

void setGeneralStatus(SpecimenSignState* st, const std::wstring& text) {
    if (st && st->generalStatus) {
        SetWindowTextW(st->generalStatus, text.c_str());
    }
}

void setCell(HWND list, int row, int col, const std::string& text) {
    const auto wide = search::utf8_to_wide(text);
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(wide.c_str()));
}

bool hasBarcodeResult(const search::SpecimenBarcodeResult& result) {
    return result.has_barcode_rows || result.has_report_rows ||
           result.has_outpatient_rows || result.has_inpatient_rows;
}

bool isChecked(HWND hwnd) {
    return hwnd && Button_GetCheck(hwnd) == BST_CHECKED;
}

std::string dateTimeValue(HWND hwnd) {
    if (!hwnd) return {};
    SYSTEMTIME value{};
    if (DateTime_GetSystemtime(hwnd, &value) != GDT_VALID) {
        return {};
    }
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u",
                  static_cast<unsigned>(value.wYear),
                  static_cast<unsigned>(value.wMonth),
                  static_cast<unsigned>(value.wDay),
                  static_cast<unsigned>(value.wHour),
                  static_cast<unsigned>(value.wMinute),
                  static_cast<unsigned>(value.wSecond));
    return buf;
}

std::wstring barcodeExistsStatusText(const search::SpecimenBarcodeResult& result) {
    std::wstringstream ss;
    if (!search::trim(result.signed_time).empty()) {
        auto appendPart = [&ss](const wchar_t* label, const std::string& value) {
            const auto trimmed = search::trim(value);
            if (!trimmed.empty()) {
                ss << L"【" << label << search::utf8_to_wide(trimmed) << L"】";
            }
        };

        ss << L"此条码已存在";
        ss << L"，";
        appendPart(L"签收时间：", result.signed_time);
        appendPart(L"上机时间：", result.create_time);
        appendPart(L"仪器：", result.mach_code);
        if (search::trim(result.oper_state) == "0") {
            ss << L"【未上机检验!】";
        }
        appendPart(L"样本号：", result.oper_no);
        return ss.str();
    }
    return L"";
}

std::wstring barcodeQueryStatusText(const search::SpecimenBarcodeResult& result) {
    return hasBarcodeResult(result) ? L"已查询到条码信息。" : L"未查询到该条码。";
}

void selectPatientType(SpecimenSignState* st, const search::SpecimenBarcodeResult& result) {
    if (!st || !st->patientType) return;
    const auto typeCode = search::trim(result.type_code);
    const auto typeName = search::trim(result.type_name);
    for (size_t i = 0; i < st->patientTypes.size(); ++i) {
        const auto& row = st->patientTypes[i];
        if ((!typeCode.empty() && search::trim(row.type_code) == typeCode) ||
            (!typeName.empty() && search::trim(row.type_name) == typeName)) {
            SendMessageW(st->patientType, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
            return;
        }
    }
}

void clearBarcodeResult(SpecimenSignState* st) {
    if (!st) return;
    setWindowTextUtf8(st->patientName, "");
    setWindowTextUtf8(st->patientSex, "");
    setWindowTextUtf8(st->patientAge, "");
    setWindowTextUtf8(st->professionalGroup, "");
    setWindowTextUtf8(st->patientNo, "");
    setWindowTextUtf8(st->doctor, "");
    setWindowTextUtf8(st->dept, "");
    setWindowTextUtf8(st->bedNo, "");
    setWindowTextUtf8(st->fee, "");
    if (st->patientType) SendMessageW(st->patientType, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    if (st->orderList) ListView_DeleteAllItems(st->orderList);
    if (st->signedList) ListView_DeleteAllItems(st->signedList);
}

void insertOrderRow(HWND list, int row, const search::SpecimenOrderRow& data) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = row;
    std::wstring first = search::utf8_to_wide(data.room_code);
    item.pszText = first.data();
    ListView_InsertItem(list, &item);
    setCell(list, row, 1, data.order_text);
    setCell(list, row, 2, data.sample_name);
    setCell(list, row, 3, data.fee);
    setCell(list, row, 4, data.request_time);
    setCell(list, row, 5, data.note);
}

void insertSignedRow(HWND list, int row, int displayNo, const search::SpecimenBarcodeResult& result,
                     const search::SpecimenOrderRow* order) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = row;
    std::wstring first = std::to_wstring(displayNo);
    item.pszText = first.data();
    ListView_InsertItem(list, &item);
    const auto barcode = order && !search::trim(order->barcode).empty() ? order->barcode : result.barcode;
    setCell(list, row, 1, barcode);
    setCell(list, row, 2, result.reg_no);
    setCell(list, row, 3, result.type_name);
    setCell(list, row, 4, result.name);
    setCell(list, row, 5, result.sex);
    setCell(list, row, 6, result.dept_name);
    setCell(list, row, 7, order ? order->order_text : "");
    setCell(list, row, 8, order && !search::trim(order->fee).empty() ? order->fee : result.fee);
    setCell(list, row, 9, order ? order->request_time : "");
    setCell(list, row, 10, result.collection_time);
    setCell(list, row, 11, result.signed_time);
    setCell(list, row, 12, result.submit_time);
    setCell(list, row, 13, result.age);
    setCell(list, row, 14, result.receiver);
    setCell(list, row, 15, order ? order->sample_name : "");
    setCell(list, row, 16, order && !search::trim(order->room_code).empty() ? order->room_code : result.room_code);
}

void insertSignedListRow(HWND list, int row, int displayNo, const search::SpecimenSignedListRow& data) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = row;
    std::wstring first = std::to_wstring(displayNo);
    item.pszText = first.data();
    ListView_InsertItem(list, &item);
    setCell(list, row, 1, data.barcode);
    setCell(list, row, 2, data.reg_no);
    setCell(list, row, 3, data.type_name);
    setCell(list, row, 4, data.name);
    setCell(list, row, 5, data.sex);
    setCell(list, row, 6, data.dept_name);
    setCell(list, row, 7, data.order_text);
    setCell(list, row, 8, data.fee);
    setCell(list, row, 9, data.request_time);
    setCell(list, row, 10, data.collection_time);
    setCell(list, row, 11, data.signed_time);
    setCell(list, row, 12, data.submit_time);
    setCell(list, row, 13, data.age);
    setCell(list, row, 14, data.receiver);
    setCell(list, row, 15, data.sample_name);
    setCell(list, row, 16, data.room_code);
}

void presentBarcodeResult(SpecimenSignState* st, const search::SpecimenBarcodeResult& result) {
    if (!st) return;
    clearBarcodeResult(st);
    setWindowTextUtf8(st->patientName, result.name);
    setWindowTextUtf8(st->patientSex, result.sex);
    setWindowTextUtf8(st->patientAge, result.age);
    setWindowTextUtf8(st->professionalGroup, result.room_code);
    setWindowTextUtf8(st->patientNo, result.reg_no);
    setWindowTextUtf8(st->doctor, result.requester);
    setWindowTextUtf8(st->dept, result.dept_name);
    setWindowTextUtf8(st->bedNo, result.bed_no);
    setWindowTextUtf8(st->fee, result.fee);
    selectPatientType(st, result);

    for (size_t i = 0; i < result.orders.size(); ++i) {
        insertOrderRow(st->orderList, static_cast<int>(i), result.orders[i]);
    }

    if (hasBarcodeResult(result)) {
        if (result.orders.empty()) {
            insertSignedRow(st->signedList, 0, 1, result, nullptr);
        } else {
            std::string previousBarcode;
            int displayNo = 0;
            for (size_t i = 0; i < result.orders.size(); ++i) {
                const auto currentBarcode = search::trim(result.orders[i].barcode.empty() ? result.barcode : result.orders[i].barcode);
                if (i == 0 || currentBarcode != previousBarcode) {
                    ++displayNo;
                    previousBarcode = currentBarcode;
                }
                insertSignedRow(st->signedList, static_cast<int>(i), displayNo, result, &result.orders[i]);
            }
        }
        ListView_SetItemState(st->signedList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    setBarcodeStatus(st, barcodeExistsStatusText(result));
    setGeneralStatus(st, barcodeQueryStatusText(result));
}

void presentSignedList(SpecimenSignState* st, const std::vector<search::SpecimenSignedListRow>& rows) {
    if (!st) return;
    clearBarcodeResult(st);

    std::string previousBarcode;
    int displayNo = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto currentBarcode = search::trim(rows[i].barcode);
        if (i == 0 || currentBarcode != previousBarcode) {
            ++displayNo;
            previousBarcode = currentBarcode;
        }
        insertSignedListRow(st->signedList, static_cast<int>(i), displayNo, rows[i]);
    }

    if (!rows.empty()) {
        ListView_SetItemState(st->signedList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    std::wstringstream ss;
    ss << L"已查询到 " << rows.size() << L" 条已签收记录。";
    setBarcodeStatus(st, L"");
    setGeneralStatus(st, ss.str());
}

search::SpecimenSignedListQuery signedListQueryFromUi(HWND hwnd, SpecimenSignState* st) {
    search::SpecimenSignedListQuery query;
    query.use_sign_time = isChecked(GetDlgItem(hwnd, IDC_SIGN_DATE_ENABLED));
    query.use_apply_time = isChecked(GetDlgItem(hwnd, IDC_APPLY_DATE_ENABLED));
    query.sign_start = dateTimeValue(st ? st->signStart : nullptr);
    query.sign_end = dateTimeValue(st ? st->signEnd : nullptr);
    query.apply_start = dateTimeValue(st ? st->applyStart : nullptr);
    query.apply_end = dateTimeValue(st ? st->applyEnd : nullptr);
    query.room_code = windowTextUtf8(st ? st->room : nullptr);
    query.patient_name = windowTextUtf8(st ? st->name : nullptr);
    return query;
}

void startSignedListQuery(HWND hwnd, SpecimenSignState* st) {
    if (!st) return;
    auto query = signedListQueryFromUi(hwnd, st);
    if (!query.use_sign_time && !query.use_apply_time) {
        setBarcodeStatus(st, L"");
        setGeneralStatus(st, L"请输入条码，或勾选签收日期/申请日期后查询。");
        return;
    }
    if (st->querying) {
        setGeneralStatus(st, L"正在查询，请稍候。");
        return;
    }
    st->querying = true;
    setBarcodeStatus(st, L"");
    setGeneralStatus(st, L"正在按时间段查询已签收条码...");

    auto settings = st->ctx.dbSettings;
    std::thread([hwnd, settings, query]() {
        auto payload = std::make_unique<QueryPayload>();
        payload->listMode = true;
        payload->ok = search::load_specimen_signed_list(settings, query, payload->signedRows, payload->error);
        if (!PostMessageW(hwnd, WM_SPECIMEN_QUERY_DONE, 0, reinterpret_cast<LPARAM>(payload.get()))) {
            return;
        }
        payload.release();
    }).detach();
}

void startBarcodeQuery(HWND hwnd, SpecimenSignState* st, const std::string& barcode) {
    if (!st) return;
    const auto value = search::trim(barcode);
    if (value.empty()) {
        setBarcodeStatus(st, L"");
        setGeneralStatus(st, L"请输入条码。");
        return;
    }
    if (st->querying) {
        setGeneralStatus(st, L"正在查询，请稍候。");
        return;
    }
    st->querying = true;
    setBarcodeStatus(st, L"");
    setGeneralStatus(st, L"正在查询条码...");

    auto settings = st->ctx.dbSettings;
    std::thread([hwnd, settings, value]() {
        auto payload = std::make_unique<QueryPayload>();
        payload->ok = search::load_specimen_barcode(settings, value, payload->result, payload->error);
        if (!PostMessageW(hwnd, WM_SPECIMEN_QUERY_DONE, 0, reinterpret_cast<LPARAM>(payload.get()))) {
            return;
        }
        payload.release();
    }).detach();
}

void runQueryFromInput(HWND hwnd, SpecimenSignState* st, HWND source) {
    if (!st) return;
    std::string barcode = windowTextUtf8(source);
    if (!barcode.empty()) {
        setWindowTextUtf8(st->barcode, barcode);
        setWindowTextUtf8(st->scanInput, barcode);
        startBarcodeQuery(hwnd, st, barcode);
        return;
    }
    startSignedListQuery(hwnd, st);
}

LRESULT CALLBACK barcodeEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        PostMessageW(GetParent(hwnd), WM_SPECIMEN_RUN_QUERY,
                     static_cast<WPARAM>(GetDlgCtrlID(hwnd)), 0);
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

int todayStamp() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    return static_cast<int>(now.wYear) * 10000 + static_cast<int>(now.wMonth) * 100 +
           static_cast<int>(now.wDay);
}

void setTodayDateTime(HWND hwnd, bool endOfDay) {
    if (!hwnd) return;
    SYSTEMTIME value{};
    GetLocalTime(&value);
    value.wHour = endOfDay ? 23 : 0;
    value.wMinute = endOfDay ? 59 : 0;
    value.wSecond = 0;
    value.wMilliseconds = 0;
    DateTime_SetSystemtime(hwnd, GDT_VALID, &value);
}

void resetDateRangeToToday(SpecimenSignState* st) {
    if (!st) return;
    setTodayDateTime(st->signStart, false);
    setTodayDateTime(st->signEnd, true);
    setTodayDateTime(st->applyStart, false);
    setTodayDateTime(st->applyEnd, true);
    st->autoDateStamp = todayStamp();
}

void updateDateRangeAfterMidnight(SpecimenSignState* st) {
    const int current = todayStamp();
    if (st && st->autoDateStamp != current) {
        resetDateRangeToToday(st);
    }
}

void addColumn(HWND list, int index, const wchar_t* title, int width) {
    search::add_list_column(list, index, title, width);
}

void loadPatientTypes(SpecimenSignState* st) {
    if (!st || !st->patientType) return;
    SendMessageW(st->patientType, CB_RESETCONTENT, 0, 0);
    st->patientTypes.clear();

    std::string error;
    if (!search::load_patient_type_options(st->ctx.dbSettings, st->patientTypes, error)) {
        return;
    }
    for (const auto& row : st->patientTypes) {
        const auto text = search::utf8_to_wide(row.type_name);
        SendMessageW(st->patientType, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
    if (!st->patientTypes.empty()) {
        SendMessageW(st->patientType, CB_SETCURSEL, 0, 0);
    }
}

int topActionButtonsStartX(HWND hwnd, int clientWidth) {
    const int margin = S(hwnd, 8);
    const int buttonW = S(hwnd, 104);
    const int buttonGap = S(hwnd, 12);
    constexpr int buttonCount = 5;
    const int totalW = buttonCount * buttonW + (buttonCount - 1) * buttonGap;
    return std::max(S(hwnd, 240), clientWidth - margin - totalW);
}

void moveTopActionButtons(HWND hwnd, int clientWidth) {
    const int buttonW = S(hwnd, 104);
    const int buttonH = S(hwnd, 40);
    const int buttonGap = S(hwnd, 12);
    const std::array<int, 5> buttonIds{
        IDC_MANUAL_SIGN, IDC_REJECT, IDC_MANAGE, IDC_RESET, IDC_CLOSE,
    };
    int x = topActionButtonsStartX(hwnd, clientWidth);
    for (int id : buttonIds) {
        if (HWND buttonHwnd = GetDlgItem(hwnd, id)) {
            MoveWindow(buttonHwnd, x, S(hwnd, 30), buttonW, buttonH, TRUE);
            x += buttonW + buttonGap;
        }
    }
}

void moveBarcodeStatusLabel(HWND hwnd, SpecimenSignState* st, int clientWidth) {
    if (!st || !st->barcodeStatus) return;
    const int x = S(hwnd, 10);
    const int buttonX = topActionButtonsStartX(hwnd, clientWidth);
    const int w = std::max(S(hwnd, 210), buttonX - x - S(hwnd, 12));
    MoveWindow(st->barcodeStatus, x, S(hwnd, 64), w, S(hwnd, 24), TRUE);
}

void layoutOrderArea(HWND hwnd, SpecimenSignState* st, int clientWidth) {
    const int margin = S(hwnd, 8);
    const int orderTop = S(hwnd, 216);
    const int orderH = S(hwnd, 126);
    const int buttonW = S(hwnd, 36);
    const int buttonH = S(hwnd, 30);
    const int buttonGap = S(hwnd, 8);
    const int buttonX = std::max(margin + S(hwnd, 500), clientWidth - margin - buttonW);

    MoveWindow(st->orderList, margin, orderTop,
               std::max(S(hwnd, 500), buttonX - margin - buttonGap), orderH, TRUE);
    MoveWindow(st->orderRemoveButton, buttonX, orderTop + S(hwnd, 22),
               buttonW, buttonH, TRUE);
    MoveWindow(st->orderAddButton, buttonX, orderTop + S(hwnd, 64),
               buttonW, buttonH, TRUE);
}

void layoutSignedList(HWND hwnd, SpecimenSignState* st, int clientWidth, int clientHeight) {
    const int margin = S(hwnd, 8);
    const int bottomTop = S(hwnd, 370);
    const int leftW = S(hwnd, 424);
    MoveWindow(st->signedList, leftW + margin, bottomTop,
               std::max(S(hwnd, 500), clientWidth - leftW - S(hwnd, 12)),
               std::max(S(hwnd, 260), clientHeight - bottomTop - S(hwnd, 10)), TRUE);
}

void createControls(HWND hwnd, SpecimenSignState* st) {
    const int margin = S(hwnd, 8);
    const int editH = S(hwnd, 26);
    const int btnH = S(hwnd, 40);

    label(hwnd, L"标本签收工作台", S(hwnd, 10), S(hwnd, 4), S(hwnd, 210), S(hwnd, 20), SS_LEFT);
    st->scanInput = edit(hwnd, IDC_SCAN_INPUT, S(hwnd, 10), S(hwnd, 26), S(hwnd, 210), S(hwnd, 34), ES_AUTOHSCROLL);
    SetPropW(st->scanInput, L"SpecimenScanInput", reinterpret_cast<HANDLE>(1));
    SetWindowSubclass(st->scanInput, barcodeEditProc, 1, 0);
    st->barcodeStatus = label(hwnd, L"", S(hwnd, 10), S(hwnd, 64), S(hwnd, 1000), S(hwnd, 24), SS_LEFT);

    const int topBtnW = S(hwnd, 104);
    const int topBtnGap = S(hwnd, 12);
    int bx = S(hwnd, 1470);
    button(hwnd, IDC_MANUAL_SIGN, L"手工登记", bx, S(hwnd, 30), topBtnW, btnH); bx += topBtnW + topBtnGap;
    button(hwnd, IDC_REJECT, L"拒 签", bx, S(hwnd, 30), topBtnW, btnH); bx += topBtnW + topBtnGap;
    button(hwnd, IDC_MANAGE, L"管 理", bx, S(hwnd, 30), topBtnW, btnH); bx += topBtnW + topBtnGap;
    button(hwnd, IDC_RESET, L"重 置", bx, S(hwnd, 30), topBtnW, btnH); bx += topBtnW + topBtnGap;
    button(hwnd, IDC_CLOSE, L"关 闭", bx, S(hwnd, 30), topBtnW, btnH);

    label(hwnd, L"病人信息", S(hwnd, 10), S(hwnd, 96), S(hwnd, 120), S(hwnd, 22), SS_LEFT);
    label(hwnd, L"姓    名", S(hwnd, 20), S(hwnd, 120), S(hwnd, 76), S(hwnd, 24));
    st->patientName = edit(hwnd, 0, S(hwnd, 115), S(hwnd, 116), S(hwnd, 165), editH);
    label(hwnd, L"病人类型", S(hwnd, 282), S(hwnd, 120), S(hwnd, 84), S(hwnd, 24));
    st->patientType = combo(hwnd, IDC_PATIENT_TYPE, S(hwnd, 376), S(hwnd, 116), S(hwnd, 118), S(hwnd, 160));
    loadPatientTypes(st);
    label(hwnd, L"性    别", S(hwnd, 506), S(hwnd, 120), S(hwnd, 76), S(hwnd, 24));
    st->patientSex = edit(hwnd, 0, S(hwnd, 592), S(hwnd, 116), S(hwnd, 114), editH);
    label(hwnd, L"年    龄", S(hwnd, 710), S(hwnd, 120), S(hwnd, 76), S(hwnd, 24));
    st->patientAge = edit(hwnd, 0, S(hwnd, 788), S(hwnd, 116), S(hwnd, 114), editH);
    label(hwnd, L"专业组", S(hwnd, 914), S(hwnd, 120), S(hwnd, 64), S(hwnd, 24));
    st->professionalGroup = edit(hwnd, 0, S(hwnd, 988), S(hwnd, 116), S(hwnd, 214), editH);

    label(hwnd, L"门 诊 号", S(hwnd, 20), S(hwnd, 158), S(hwnd, 76), S(hwnd, 24));
    st->patientNo = edit(hwnd, 0, S(hwnd, 115), S(hwnd, 154), S(hwnd, 165), editH);
    label(hwnd, L"医    生", S(hwnd, 282), S(hwnd, 158), S(hwnd, 84), S(hwnd, 24));
    st->doctor = edit(hwnd, 0, S(hwnd, 376), S(hwnd, 154), S(hwnd, 114), editH);
    label(hwnd, L"科    室", S(hwnd, 506), S(hwnd, 158), S(hwnd, 76), S(hwnd, 24));
    st->dept = edit(hwnd, 0, S(hwnd, 592), S(hwnd, 154), S(hwnd, 114), editH);
    label(hwnd, L"床    号", S(hwnd, 710), S(hwnd, 158), S(hwnd, 76), S(hwnd, 24));
    st->bedNo = edit(hwnd, 0, S(hwnd, 788), S(hwnd, 154), S(hwnd, 114), editH);
    label(hwnd, L"费    用", S(hwnd, 914), S(hwnd, 158), S(hwnd, 64), S(hwnd, 24));
    st->fee = edit(hwnd, 0, S(hwnd, 988), S(hwnd, 154), S(hwnd, 214), editH, ES_AUTOHSCROLL | ES_RIGHT);

    label(hwnd, L"医嘱明细", S(hwnd, 10), S(hwnd, 192), S(hwnd, 120), S(hwnd, 22), SS_LEFT);
    st->orderList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL,
                                    margin, S(hwnd, 216), S(hwnd, 1980), S(hwnd, 126),
                                    hwnd, win32_control_id(IDC_ORDER_LIST), GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(st->orderList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    const std::array<std::pair<const wchar_t*, int>, 6> orderColumns{{
        {L"检验室", 150}, {L"医嘱(处方)内容", 500}, {L"标本类型", 120},
        {L"费用", 110}, {L"申请时间", 130}, {L"备注", 240},
    }};
    for (int i = 0; i < static_cast<int>(orderColumns.size()); ++i) {
        addColumn(st->orderList, i, orderColumns[static_cast<size_t>(i)].first,
                  S(hwnd, orderColumns[static_cast<size_t>(i)].second));
    }
    st->orderRemoveButton = button(hwnd, 0, L"-", S(hwnd, 1996), S(hwnd, 238), S(hwnd, 36), S(hwnd, 30));
    st->orderAddButton = button(hwnd, 0, L"+", S(hwnd, 1996), S(hwnd, 280), S(hwnd, 36), S(hwnd, 30));

    label(hwnd, L"已签收条码", S(hwnd, 10), S(hwnd, 346), S(hwnd, 120), S(hwnd, 22), SS_LEFT);

    checkbox(hwnd, IDC_SIGN_DATE_ENABLED, L"签收日期", S(hwnd, 10), S(hwnd, 390), S(hwnd, 112), S(hwnd, 26), true);
    st->signStart = dateTimePicker(hwnd, IDC_SIGN_START, S(hwnd, 132), S(hwnd, 390), S(hwnd, 290), editH);
    label(hwnd, L"至", S(hwnd, 92), S(hwnd, 430), S(hwnd, 28), S(hwnd, 22));
    st->signEnd = dateTimePicker(hwnd, IDC_SIGN_END, S(hwnd, 132), S(hwnd, 428), S(hwnd, 290), editH);

    checkbox(hwnd, IDC_APPLY_DATE_ENABLED, L"申请日期", S(hwnd, 10), S(hwnd, 470), S(hwnd, 112), S(hwnd, 26), false);
    st->applyStart = dateTimePicker(hwnd, IDC_APPLY_START, S(hwnd, 132), S(hwnd, 470), S(hwnd, 290), editH);
    label(hwnd, L"至", S(hwnd, 92), S(hwnd, 510), S(hwnd, 28), S(hwnd, 22));
    st->applyEnd = dateTimePicker(hwnd, IDC_APPLY_END, S(hwnd, 132), S(hwnd, 508), S(hwnd, 290), editH);
    resetDateRangeToToday(st);

    label(hwnd, L"条    码", S(hwnd, 20), S(hwnd, 548), S(hwnd, 86), S(hwnd, 24));
    st->barcode = edit(hwnd, IDC_BARCODE, S(hwnd, 132), S(hwnd, 544), S(hwnd, 290), editH);
    SetWindowSubclass(st->barcode, barcodeEditProc, 2, 0);
    label(hwnd, L"专 业 组", S(hwnd, 20), S(hwnd, 586), S(hwnd, 86), S(hwnd, 24));
    st->room = edit(hwnd, IDC_ROOM, S(hwnd, 132), S(hwnd, 582), S(hwnd, 126), editH);
    label(hwnd, L"姓名", S(hwnd, 258), S(hwnd, 586), S(hwnd, 48), S(hwnd, 24));
    st->name = edit(hwnd, IDC_NAME, S(hwnd, 310), S(hwnd, 582), S(hwnd, 112), editH);

    st->showPersonal = checkbox(hwnd, IDC_SHOW_PERSONAL, L"显示本人签收单", S(hwnd, 108), S(hwnd, 620), S(hwnd, 158), S(hwnd, 24), true);
    st->signedOnly = checkbox(hwnd, IDC_SIGNED, L"已生成签收单", S(hwnd, 270), S(hwnd, 620), S(hwnd, 148), S(hwnd, 24), false);

    button(hwnd, IDC_QUERY, L"查    询", S(hwnd, 14), S(hwnd, 652), S(hwnd, 116), S(hwnd, 34));
    button(hwnd, IDC_GROUP_STAT, L"分组统计", S(hwnd, 160), S(hwnd, 652), S(hwnd, 116), S(hwnd, 34));
    button(hwnd, IDC_STAT, L"统    计", S(hwnd, 306), S(hwnd, 652), S(hwnd, 116), S(hwnd, 34));
    button(hwnd, IDC_PRINT_SIGN, L"打印签收单", S(hwnd, 14), S(hwnd, 692), S(hwnd, 116), S(hwnd, 34));
    button(hwnd, IDC_PATCH_BARCODE, L"补打条码", S(hwnd, 160), S(hwnd, 692), S(hwnd, 116), S(hwnd, 34));
    button(hwnd, IDC_PRINT_SELECTED, L"打印选中", S(hwnd, 306), S(hwnd, 692), S(hwnd, 116), S(hwnd, 34));
    button(hwnd, IDC_MOVE_SIGNED, L"移至已签单", S(hwnd, 14), S(hwnd, 732), S(hwnd, 116), S(hwnd, 34));
    button(hwnd, IDC_NOT_DELIVERED, L"未送达标本", S(hwnd, 160), S(hwnd, 732), S(hwnd, 116), S(hwnd, 34));
    button(hwnd, IDC_EXPORT_EXCEL, L"导出到Excel", S(hwnd, 306), S(hwnd, 732), S(hwnd, 116), S(hwnd, 34));
    st->generalStatus = label(hwnd, L"", S(hwnd, 14), S(hwnd, 770), S(hwnd, 408), S(hwnd, 44), SS_LEFT);

    st->signedList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL,
                                     S(hwnd, 432), S(hwnd, 370), S(hwnd, 1600), S(hwnd, 690),
                                     hwnd, win32_control_id(IDC_SIGNED_LIST), GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(st->signedList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    const std::array<std::pair<const wchar_t*, int>, 17> signedColumns{{
        {L"序号", 58}, {L"条形码", 130}, {L"病人号", 150}, {L"类型", 88},
        {L"姓名", 130}, {L"性别", 82}, {L"申请科室", 140}, {L"医嘱内容", 220},
        {L"费用", 110}, {L"申请时间", 220}, {L"采集时间", 220}, {L"签收时间", 160},
        {L"送检时间", 160}, {L"年龄", 80}, {L"签收人", 100}, {L"标本类型", 120},
        {L"检验室", 90},
    }};
    for (int i = 0; i < static_cast<int>(signedColumns.size()); ++i) {
        addColumn(st->signedList, i, signedColumns[static_cast<size_t>(i)].first,
                  S(hwnd, signedColumns[static_cast<size_t>(i)].second));
    }

    applyFont(hwnd, st->ctx.uiFont);
}

void layout(HWND hwnd, SpecimenSignState* st) {
    if (!st) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    moveTopActionButtons(hwnd, w);
    moveBarcodeStatusLabel(hwnd, st, w);
    layoutOrderArea(hwnd, st, w);
    layoutSignedList(hwnd, st, w, h);
}

void paintSectionLine(HDC dc, const RECT& rc, int y) {
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0xD8, 0xD8, 0xD8));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, rc.left, y, nullptr);
    LineTo(dc, rc.right, y);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void paintVerticalLine(HDC dc, int x, int top, int bottom) {
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0xC8, 0xC8, 0xC8));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, x, top, nullptr);
    LineTo(dc, x, bottom);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<SpecimenSignState*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE:
            st = g_pending;
            g_pending = nullptr;
            SetPropW(hwnd, PROP_STATE, reinterpret_cast<HANDLE>(st));
            st->bgBrush = CreateSolidBrush(COLOR_BG);
            st->scanBrush = CreateSolidBrush(COLOR_SCAN);
            createControls(hwnd, st);
            layout(hwnd, st);
            SetTimer(hwnd, IDT_ROLLOVER_DATES, 60 * 1000, nullptr);
            return 0;
        case WM_SIZE:
            layout(hwnd, st);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, st ? st->bgBrush : reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
            paintSectionLine(dc, rc, S(hwnd, 92));
            paintSectionLine(dc, rc, S(hwnd, 188));
            paintSectionLine(dc, rc, S(hwnd, 344));
            paintVerticalLine(dc, S(hwnd, 424), S(hwnd, 344), rc.bottom);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case app::WM_APP_FONT_CHANGED:
            if (st && lp) {
                st->ctx.uiFont = reinterpret_cast<HFONT>(lp);
                applyFont(hwnd, st->ctx.uiFont);
                layout(hwnd, st);
            }
            return 0;
        case WM_TIMER:
            if (wp == IDT_ROLLOVER_DATES) {
                updateDateRangeAfterMidnight(st);
                return 0;
            }
            break;
        case WM_SPECIMEN_RUN_QUERY:
            if (st) {
                HWND source = wp ? GetDlgItem(hwnd, static_cast<int>(wp)) : st->barcode;
                runQueryFromInput(hwnd, st, source ? source : st->barcode);
            }
            return 0;
        case WM_SPECIMEN_QUERY_DONE: {
            std::unique_ptr<QueryPayload> payload(reinterpret_cast<QueryPayload*>(lp));
            if (!st || !payload) return 0;
            st->querying = false;
            if (!payload->ok) {
                clearBarcodeResult(st);
                std::wstring message = payload->listMode ? L"已签收条码查询失败：" : L"条码查询失败：";
                message += search::utf8_to_wide(payload->error);
                setBarcodeStatus(st, L"");
                setGeneralStatus(st, message);
                return 0;
            }
            if (payload->listMode) {
                presentSignedList(st, payload->signedRows);
                return 0;
            }
            presentBarcodeResult(st, payload->result);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_CLOSE) {
                SendMessageW(GetParent(hwnd), WM_MDIDESTROY, reinterpret_cast<WPARAM>(hwnd), 0);
                return 0;
            }
            if (LOWORD(wp) == IDC_QUERY) {
                runQueryFromInput(hwnd, st, st ? st->barcode : nullptr);
                return 0;
            }
            if (LOWORD(wp) == IDC_RESET) {
                if (st) {
                    SetWindowTextW(st->scanInput, L"");
                    SetWindowTextW(st->barcode, L"");
                    clearBarcodeResult(st);
                    setBarcodeStatus(st, L"");
                    setGeneralStatus(st, L"");
                }
                return 0;
            }
            break;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            if (st && reinterpret_cast<HWND>(lp) == st->barcodeStatus) {
                SetTextColor(dc, COLOR_ALERT);
            } else {
                SetTextColor(dc, RGB(0, 0, 0));
            }
            return reinterpret_cast<LRESULT>(st ? st->bgBrush : nullptr);
        }
        case WM_CTLCOLOREDIT: {
            HWND ctl = reinterpret_cast<HWND>(lp);
            if (st && GetPropW(ctl, L"SpecimenScanInput")) {
                SetBkColor(reinterpret_cast<HDC>(wp), COLOR_SCAN);
                return reinterpret_cast<LRESULT>(st->scanBrush);
            }
            break;
        }
        case WM_CTLCOLORDLG:
            return reinterpret_cast<LRESULT>(st ? st->bgBrush : nullptr);
        case WM_DESTROY:
            KillTimer(hwnd, IDT_ROLLOVER_DATES);
            RemovePropW(hwnd, PROP_STATE);
            if (st) {
                if (st->scanInput) {
                    RemoveWindowSubclass(st->scanInput, barcodeEditProc, 1);
                    RemovePropW(st->scanInput, L"SpecimenScanInput");
                }
                if (st->barcode) {
                    RemoveWindowSubclass(st->barcode, barcodeEditProc, 2);
                }
                if (st->bgBrush) DeleteObject(st->bgBrush);
                if (st->scanBrush) DeleteObject(st->scanBrush);
                delete st;
            }
            return 0;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_specimen_sign_module(const ModuleContext& ctx) {
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

    auto* st = new SpecimenSignState;
    st->ctx = ctx;

    MDICREATESTRUCTW mcs{};
    mcs.szClass = WND_CLASS;
    mcs.szTitle = WINDOW_TITLE;
    mcs.hOwner = ctx.instance;
    mcs.x = mcs.y = mcs.cx = mcs.cy = CW_USEDEFAULT;

    g_pending = st;
    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&mcs)));
    if (child) {
        SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    } else {
        g_pending = nullptr;
        delete st;
    }
    return child;
}

#endif
