#include "regular_report_module.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "main_app.h"
#include "resource.h"
#include "search_controller.h"
#include "search_splitter.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"

#if defined(LIS_HAS_LABELPRINT)
#include "labelprint/labelprint.h"
#endif

#include <commctrl.h>
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <exception>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"RegularReportChild";
constexpr const wchar_t* PICTURE_POPUP_CLASS = L"RegularReportPicturePopup";
constexpr const wchar_t* WINDOW_TITLE = L"常规报告";
constexpr const wchar_t* PROP_STATE = L"RegularReportSt";
constexpr const wchar_t* PROP_DATE_FORMAT = L"RegularReportDateFormat";
constexpr const wchar_t* BLANK_DATE_FORMAT = L" ";
constexpr const wchar_t* DEFAULT_BARCODE_PRINTER_NAME = L"Xprinter XP-360B #2";
constexpr UINT WM_REGULAR_REPORTS_LOADED = WM_APP + 171;
constexpr UINT WM_REGULAR_RESULTS_LOADED = WM_APP + 172;
constexpr UINT WM_REGULAR_PICTURE_LOADED = WM_APP + 173;
constexpr UINT WM_POPUP_PICTURE_LOADED = WM_APP + 174;
constexpr UINT_PTR IDT_REPORT_AUTO_REFRESH = 6310;

constexpr int IDC_RESULT_LIST = 5201;
constexpr int IDC_REPORT_LIST = 5202;
constexpr int IDC_SPLITTER = 5204;
constexpr int IDC_LEFT_SCROLL = 5205;
constexpr int IDC_RIGHT_TAB = 5206;
constexpr int IDC_MIDDLE_TAB = 5207;
constexpr int IDC_MACHINE_PICKER_BUTTON = 5208;
constexpr int IDC_MACHINE_PICKER_ROOM = 5209;
constexpr int IDC_MACHINE_PICKER_MACH = 5210;
constexpr int IDC_INSPECT_DATE = 5211;
constexpr int IDC_REPORT_FIRST_BUTTON = 5212;
constexpr int IDC_REPORT_LAST_BUTTON = 5213;
constexpr int IDC_REPORT_DATE_TODAY_BUTTON = 5214;
constexpr int IDC_REPORT_DATE_PREV_BUTTON = 5215;
constexpr int IDC_REPORT_DATE_NEXT_BUTTON = 5216;
constexpr int IDC_REPORT_AUTO_REFRESH_CHECK = 5217;
constexpr int IDC_REPORT_AUTO_REFRESH_SECONDS = 5218;
constexpr int IDC_BOTTOM_MACHINE_1 = 5401;
constexpr int IDC_BOTTOM_REFRESH = 5402;
constexpr int IDC_BOTTOM_MACHINE_2 = 5411;
constexpr int IDC_BOTTOM_MACHINE_3 = 5420;
constexpr int IDC_BOTTOM_GRAPH = 5424;
constexpr int IDC_BOTTOM_PREV_REPORT = 5408;
constexpr int IDC_BOTTOM_NEXT_REPORT = 5409;
constexpr int IDM_REPORT_PRINT_BARCODE = 5220;
constexpr int IDM_REPORT_PRINT_CHECKED_BARCODES = 5221;
constexpr int IDM_REPORT_TODO = 5222;
constexpr int REPORT_COLUMN_COUNT = 30;
constexpr int RESULT_VALUE_COL = 5;
constexpr int RIGHT_REPORT_PRINT_COL = 8;
constexpr UINT_PTR LEFT_PANEL_SUBCLASS = 6205;
constexpr UINT_PTR LEFT_CONTENT_SUBCLASS = 6206;
constexpr UINT_PTR RIGHT_PANEL_SUBCLASS = 6207;
constexpr UINT_PTR MIDDLE_PANEL_SUBCLASS = 6208;
constexpr UINT_PTR BOTTOM_PANEL_SUBCLASS = 6209;
constexpr UINT_PTR SAMPLE_INPUT_SUBCLASS = 6210;
constexpr UINT_PTR PICTURE_VIEW_SUBCLASS = 6211;
constexpr UINT_PTR PICTURE_VIEWPORT_SUBCLASS = 6212;
constexpr UINT_PTR RESULT_EDIT_SUBCLASS = 6213;
constexpr UINT_PTR RESULT_LIST_SUBCLASS = 6214;
constexpr UINT_PTR LEFT_TAB_SUBCLASS = 6215;
constexpr int LEFT_CONTENT_HEIGHT = 875;
constexpr int LEFT_SCROLL_STEP = 36;
constexpr int LEFT_PANEL_MIN_W = 360;
constexpr int LEFT_PANEL_MAX_W = 360;
constexpr int PAD = 8;
constexpr int GAP = 6;
constexpr int SPLITTER_W = 8;
constexpr int TAB_H = 30;
constexpr int COMPACT_BUTTON_H = 28;
constexpr int BOTTOM_PANEL_H = 102;
constexpr int MIDDLE_TOOLBAR_Y = 32;
constexpr int MIDDLE_LIST_Y = 66;
constexpr int MIDDLE_LIST_BOTTOM_MARGIN = 88;
constexpr int MIDDLE_STATUS_BOTTOM = 22;
constexpr int MIDDLE_STATUS_H = 18;
constexpr int PICTURE_FIXED_W = 1560;
constexpr int PICTURE_FIXED_H = 1050;
constexpr int PICTURE_POPUP_DEFAULT_W = 980;
constexpr int PICTURE_POPUP_DEFAULT_H = 700;
constexpr int PICTURE_POPUP_MIN_W = 360;
constexpr int PICTURE_POPUP_MIN_H = 260;
constexpr int QUICK_MACHINE_COUNT = 3;
constexpr std::array<int, QUICK_MACHINE_COUNT> QUICK_MACHINE_BUTTON_IDS = {
    IDC_BOTTOM_MACHINE_1, IDC_BOTTOM_MACHINE_2, IDC_BOTTOM_MACHINE_3};
constexpr std::array<const wchar_t*, QUICK_MACHINE_COUNT> QUICK_MACHINE_CODE_KEYS = {
    L"QuickMachine1Code", L"QuickMachine2Code", L"QuickMachine3Code"};
constexpr std::array<const wchar_t*, QUICK_MACHINE_COUNT> QUICK_MACHINE_NAME_KEYS = {
    L"QuickMachine1Name", L"QuickMachine2Name", L"QuickMachine3Name"};
constexpr std::array<const wchar_t*, QUICK_MACHINE_COUNT> QUICK_MACHINE_ROOM_KEYS = {
    L"QuickMachine1RoomCode", L"QuickMachine2RoomCode", L"QuickMachine3RoomCode"};
constexpr int RIGHT_TAB_Y = 56;
constexpr int RIGHT_SEARCH_LABEL_Y = 94;
constexpr int RIGHT_SEARCH_CONTROL_Y = 90;
constexpr int RIGHT_LIST_Y = 128;
constexpr int RIGHT_DATE_BUTTON_H = 28;
constexpr int RIGHT_DATE_BUTTON_W = 72;
constexpr int AUTO_REFRESH_DEFAULT_SECONDS = 10;
constexpr int AUTO_REFRESH_MIN_SECONDS = 5;
constexpr int AUTO_REFRESH_MAX_SECONDS = 3600;
constexpr const wchar_t* RIGHT_SUMMARY_LINE2 = L"危急报告已阅:1；急诊报告已阅:2；危急报告未阅:3；急诊报告未阅:3；";
constexpr const wchar_t* MACHINE_PICKER_CLASS = L"RegularReportMachinePicker";
constexpr int MACHINE_PICKER_CLIENT_W = 256;
constexpr int MACHINE_PICKER_INITIAL_H = 182;
constexpr int MACHINE_PICKER_INPUT_X = 10;
constexpr int MACHINE_PICKER_ROOM_Y = 12;
constexpr int MACHINE_PICKER_LIST_Y = 44;
constexpr int MACHINE_PICKER_LIST_W = 230;
constexpr int MACHINE_PICKER_CODE_COL_W = 76;
constexpr int MACHINE_PICKER_COL_GAP = 8;
constexpr int MACHINE_PICKER_NAME_COL_W = MACHINE_PICKER_LIST_W - MACHINE_PICKER_CODE_COL_W - MACHINE_PICKER_COL_GAP;
constexpr int MACHINE_PICKER_COMBO_DROP_H = 180;
constexpr int MACHINE_PICKER_INITIAL_LIST_H = 92;
constexpr int MACHINE_PICKER_MIN_ROWS = 3;
constexpr int MACHINE_PICKER_MAX_ROWS = 8;
constexpr int MACHINE_PICKER_HEADER_H = 28;
constexpr int MACHINE_PICKER_BOTTOM_PAD = 10;
constexpr int MACHINE_PICKER_LIST_EXTRA_H = 6;

struct ColumnDef {
    int index;
    const wchar_t* title;
    int width;
};

struct ButtonDef {
    int id;
    const wchar_t* text;
};

// Drawn on leftContent instead of real GROUPBOX windows so sibling clipping can
// be enabled on the child controls without the group frame hiding them.
struct GroupFrame {
    RECT rect{};
    const wchar_t* title = L"";
};

struct RightHeaderLayout {
    RECT line1{};
    RECT line2{};
    int bottom = 0;
};

struct RegularReportState {
    ModuleContext ctx;
    HWND hwnd = nullptr;
    HWND leftPanel = nullptr;
    HWND leftContent = nullptr;
    HWND leftScrollBar = nullptr;
    HWND middlePanel = nullptr;
    HWND rightPanel = nullptr;
    HWND bottomPanel = nullptr;
    HWND splitter = nullptr;
    HWND middleTab = nullptr;
    HWND rightTab = nullptr;
    HWND rightSearchLabel = nullptr;
    HWND rightSearchEdit = nullptr;
    HWND rightSearchIndexButton = nullptr;
    HWND rightSearchUpButton = nullptr;
    HWND rightSearchDownButton = nullptr;
    HWND rightSearchMenuButton = nullptr;
    HWND rightDateTodayButton = nullptr;
    HWND rightDatePrevButton = nullptr;
    HWND rightDateNextButton = nullptr;
    HWND rightAutoRefreshCheck = nullptr;
    HWND rightAutoRefreshLabel = nullptr;
    HWND rightAutoRefreshEdit = nullptr;
    HWND rightAutoRefreshUnitLabel = nullptr;
    HWND machineEdit = nullptr;
    HWND machinePickerButton = nullptr;
    HWND machinePickerPopup = nullptr;
    HWND groupEdit = nullptr;
    HWND sampleEdit = nullptr;
    HWND reportNoEdit = nullptr;
    HWND operNoEdit = nullptr;
    HWND patientTypeCombo = nullptr;
    HWND urgentEdit = nullptr;
    HWND barcodeEdit = nullptr;
    HWND regNoEdit = nullptr;
    HWND patientNameEdit = nullptr;
    HWND sexEdit = nullptr;
    HWND ageEdit = nullptr;
    HWND ageUnitCombo = nullptr;
    HWND bedEdit = nullptr;
    HWND phoneEdit = nullptr;
    HWND deptEdit = nullptr;
    HWND diagEdit = nullptr;
    HWND reqDoctorEdit = nullptr;
    HWND feeEdit = nullptr;
    HWND testerEdit = nullptr;
    HWND auditEdit = nullptr;
    HWND noteCodeEdit = nullptr;
    HWND noteEdit = nullptr;
    HWND applyDatePicker = nullptr;
    HWND receiveDatePicker = nullptr;
    HWND machineDatePicker = nullptr;
    HWND reportDatePicker = nullptr;
    HWND inspectDatePicker = nullptr;
    HWND collectDateEdit = nullptr;
    HWND resultList = nullptr;
    HWND resultEdit = nullptr;
    HWND pictureViewport = nullptr;
    HWND pictureView = nullptr;
    HWND pictureHScroll = nullptr;
    HWND pictureVScroll = nullptr;
    HWND picturePopup = nullptr;
    HWND reportList = nullptr;
    HWND status = nullptr;
    int splitterX = 0;
    int pendingSplitterX = 0;
    int leftScrollY = 0;
    int pictureScrollX = 0;
    int pictureScrollY = 0;
    int resultEditRow = -1;
    int leftContentHeight = LEFT_CONTENT_HEIGHT;
    bool splitterUserSet = false;
    int reportQueryGeneration = 0;
    int resultQueryGeneration = 0;
    int pictureQueryGeneration = 0;
    int selectedReportIndex = -1;
    bool reportQueryLoading = false;
    bool resultQueryLoading = false;
    bool pictureQueryLoading = false;
    bool autoRefreshTimerActive = false;
    bool suppressInspectDateQuery = false;
    bool suppressReportSelectionQuery = false;
    int reportSortColumn = -1;
    bool reportSortAscending = true;
    std::vector<HWND> leftControls;
    std::vector<HWND> leftTabControls;
    std::vector<GroupFrame> leftGroupFrames;
    std::vector<HWND> middleResultControls;
    std::vector<HWND> middlePictureControls;
    std::vector<HWND> rightInfoControls;
    HBRUSH bgBrush = nullptr;
    HBRUSH panelBrush = nullptr;
    HBRUSH blackBrush = nullptr;
    HFONT groupTitleFont = nullptr;
    std::string selectedMachineCode;
    std::string selectedRoomCode;
    std::string reportConnectionString;
    std::string reportQueryDate;
    std::vector<search::ReportRow> reportRows;
    std::vector<search::ResultRow> resultRows;
    std::wstring pictureStatus;
    std::string pictureRepNo;
    IStream* pictureStream = nullptr;
    Gdiplus::Image* pictureImage = nullptr;
    ULONG_PTR gdiplusToken = 0;
    bool gdiplusReady = false;
    int contextReportIndex = -1;
};

struct MachinePickerState {
    RegularReportState* report = nullptr;
    HWND roomCombo = nullptr;
    HWND machineList = nullptr;
    std::vector<search::RoomOption> rooms;
    std::vector<search::MachineOption> machines;
    bool closePosted = false;
};

struct ReportLoadResult {
    int generation = 0;
    bool ok = false;
    bool preserveState = false;
    std::vector<search::ReportRow> rows;
    std::string connectionString;
    std::string queryDate;
    std::string error;
};

struct ResultLoadResult {
    int generation = 0;
    bool ok = false;
    std::vector<search::ResultRow> rows;
    std::string error;
};

struct PictureLoadResult {
    int generation = 0;
    bool ok = false;
    std::vector<unsigned char> picture;
    std::string error;
};

struct PicturePopupLoadResult {
    int generation = 0;
    bool ok = false;
    std::vector<unsigned char> picture;
    std::string error;
};

struct PicturePopupState {
    RegularReportState* owner = nullptr;
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    IStream* stream = nullptr;
    Gdiplus::Image* image = nullptr;
    ULONG_PTR gdiplusToken = 0;
    bool gdiplusReady = false;
    bool loading = false;
    int generation = 0;
    std::string repNo;
    std::wstring status;
};

void clearPictureView(RegularReportState* st, const std::wstring& status);

RegularReportState* g_pending = nullptr;

std::wstring rightSummaryLine1(const RegularReportState* st) {
    int sampleCount = 0;
    int machineCount = 0;
    int reviewedCount = 0;
    int sentCount = 0;
    if (st) {
        for (const auto& row : st->reportRows) {
            if (!search::trim(row.rep_no).empty()) ++sampleCount;
            if (!search::trim(row.name).empty()) ++machineCount;
            if (search::trim(row.chk_flag) == "T") ++reviewedCount;
            if (search::trim(row.conf) == "S") ++sentCount;
        }
    }
    return L"样本数(" + std::to_wstring(sampleCount) + L"):上机数(" +
           std::to_wstring(machineCount) + L"):审核数(" + std::to_wstring(reviewedCount) +
           L"):发送数(" + std::to_wstring(sentCount) + L")";
}

const wchar_t* quickMachineCodeKey(int slot) {
    return QUICK_MACHINE_CODE_KEYS[static_cast<size_t>(std::clamp(slot, 0, QUICK_MACHINE_COUNT - 1))];
}

const wchar_t* quickMachineNameKey(int slot) {
    return QUICK_MACHINE_NAME_KEYS[static_cast<size_t>(std::clamp(slot, 0, QUICK_MACHINE_COUNT - 1))];
}

const wchar_t* quickMachineRoomKey(int slot) {
    return QUICK_MACHINE_ROOM_KEYS[static_cast<size_t>(std::clamp(slot, 0, QUICK_MACHINE_COUNT - 1))];
}

bool quickMachineMatchesCurrent(const RegularReportState* st, int slot) {
    if (!st || slot < 0 || slot >= QUICK_MACHINE_COUNT) return false;
    const std::string code = search::wide_to_utf8(
        search::load_module_str(L"RegularReport", quickMachineCodeKey(slot), L""));
    const std::string room = search::wide_to_utf8(
        search::load_module_str(L"RegularReport", quickMachineRoomKey(slot), L""));
    const std::string currentCode = search::trim(st->selectedMachineCode);
    const std::string currentRoom = search::trim(st->selectedRoomCode);
    return !currentCode.empty() && currentCode == search::trim(code) &&
           currentRoom == search::trim(room);
}

void updateQuickMachineButtonLabels(RegularReportState* st) {
    if (!st || !st->bottomPanel) return;
    for (int i = 0; i < QUICK_MACHINE_COUNT; ++i) {
        const bool active = quickMachineMatchesCurrent(st, i);
        const std::wstring text = active ? L"[" + std::to_wstring(i + 1) + L"]"
                                         : std::to_wstring(i + 1);
        HWND button = GetDlgItem(st->bottomPanel, QUICK_MACHINE_BUTTON_IDS[static_cast<size_t>(i)]);
        if (button) {
            SetWindowTextW(button, text.c_str());
        }
    }
}

int S(HWND hwnd, int value) {
    return static_cast<int>(value * search::dpi_scale_factor(hwnd));
}

HWND addClipSiblings(HWND hwnd) {
    if (!hwnd) return hwnd;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, style | WS_CLIPSIBLINGS);
    return hwnd;
}

HWND makeStatic(HWND parent, const wchar_t* text, int x, int y, int w, int h, DWORD style = SS_LEFT) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND makeEdit(HWND parent, const wchar_t* text, int x, int y, int w, int h, DWORD extra = ES_AUTOHSCROLL) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | extra,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND makeButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
                           x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND makeCombo(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    HWND combo = addClipSiblings(search::create_combo(parent, 0, x, y, w, h, false));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
    return combo;
}

SYSTEMTIME todayDate() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    st.wHour = 0;
    st.wMinute = 0;
    st.wSecond = 0;
    st.wMilliseconds = 0;
    return st;
}

SYSTEMTIME normalizeDate(SYSTEMTIME st) {
    st.wHour = 0;
    st.wMinute = 0;
    st.wSecond = 0;
    st.wMilliseconds = 0;
    return st;
}

SYSTEMTIME addDays(SYSTEMTIME st, int days) {
    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft)) {
        return todayDate();
    }
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    constexpr ULONGLONG TICKS_PER_DAY = 24ULL * 60ULL * 60ULL * 10000000ULL;
    if (days >= 0) {
        value.QuadPart += static_cast<ULONGLONG>(days) * TICKS_PER_DAY;
    } else {
        value.QuadPart -= static_cast<ULONGLONG>(-days) * TICKS_PER_DAY;
    }
    ft.dwLowDateTime = value.LowPart;
    ft.dwHighDateTime = value.HighPart;
    SYSTEMTIME result{};
    if (!FileTimeToSystemTime(&ft, &result)) {
        return todayDate();
    }
    return normalizeDate(result);
}

HWND makeDatePicker(HWND parent, int x, int y, int w, int h, const wchar_t* format, const SYSTEMTIME* value, int id = 0) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | DTS_SHORTDATECENTURYFORMAT;
    HWND picker = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", style,
                                  x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
    if (format) {
        SetPropW(picker, PROP_DATE_FORMAT, reinterpret_cast<HANDLE>(const_cast<wchar_t*>(format)));
    }
    if (value) {
        SendMessageW(picker, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(format));
        SendMessageW(picker, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(value));
    } else {
        SYSTEMTIME blankValue = todayDate();
        SendMessageW(picker, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(BLANK_DATE_FORMAT));
        SendMessageW(picker, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&blankValue));
    }
    return picker;
}

std::string datePickerValue(HWND hwnd) {
    SYSTEMTIME st{};
    if (!hwnd || DateTime_GetSystemtime(hwnd, &st) != GDT_VALID) {
        return "";
    }
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
    return buffer;
}

SYSTEMTIME datePickerSystemTime(HWND hwnd) {
    SYSTEMTIME st{};
    if (!hwnd || DateTime_GetSystemtime(hwnd, &st) != GDT_VALID) {
        return todayDate();
    }
    return normalizeDate(st);
}

std::string slashDate(const std::string& value) {
    int year = 0;
    int month = 0;
    int day = 0;
    if ((std::sscanf(value.c_str(), "%d-%d-%d", &year, &month, &day) == 3 ||
         std::sscanf(value.c_str(), "%d/%d/%d", &year, &month, &day) == 3) &&
        year > 0 && month > 0 && day > 0) {
        char buffer[16]{};
        std::snprintf(buffer, sizeof(buffer), "%d/%d/%d", year, month, day);
        return buffer;
    }
    return value;
}

std::string slashDateTimeMinute(const std::string& value) {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    if ((std::sscanf(value.c_str(), "%d-%d-%d %d:%d", &year, &month, &day, &hour, &minute) == 5 ||
         std::sscanf(value.c_str(), "%d/%d/%d %d:%d", &year, &month, &day, &hour, &minute) == 5) &&
        year > 0 && month > 0 && day > 0) {
        char buffer[24]{};
        std::snprintf(buffer, sizeof(buffer), "%d/%d/%d %d:%02d", year, month, day, hour, minute);
        return buffer;
    }
    return value;
}

bool parseDateTimeText(const std::string& value, SYSTEMTIME& out) {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    const int matched = std::sscanf(value.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second);
    if (matched < 3) {
        const int slashMatched = std::sscanf(value.c_str(), "%d/%d/%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second);
        if (slashMatched < 3) return false;
        if (slashMatched < 5) {
            hour = 0;
            minute = 0;
            second = 0;
        } else if (slashMatched < 6) {
            second = 0;
        }
    } else if (matched < 5) {
        hour = 0;
        minute = 0;
        second = 0;
    } else if (matched < 6) {
        second = 0;
    }
    if (year <= 0 || month <= 0 || month > 12 || day <= 0 || day > 31) return false;
    out = {};
    out.wYear = static_cast<WORD>(year);
    out.wMonth = static_cast<WORD>(month);
    out.wDay = static_cast<WORD>(day);
    out.wHour = static_cast<WORD>(std::clamp(hour, 0, 23));
    out.wMinute = static_cast<WORD>(std::clamp(minute, 0, 59));
    out.wSecond = static_cast<WORD>(std::clamp(second, 0, 59));
    return true;
}

void setControlText(HWND hwnd, const std::string& text) {
    if (!hwnd) return;
    const auto wide = search::utf8_to_wide(text);
    SetWindowTextW(hwnd, wide.c_str());
}

std::wstring windowText(HWND hwnd) {
    if (!hwnd) return L"";
    const int length = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0) {
        GetWindowTextW(hwnd, text.data(), static_cast<int>(text.size()));
    }
    text.resize(static_cast<size_t>(length));
    return text;
}

void setComboSingleText(HWND hwnd, const std::string& text) {
    if (!hwnd) return;
    SendMessageW(hwnd, CB_RESETCONTENT, 0, 0);
    const auto wide = search::utf8_to_wide(text);
    SendMessageW(hwnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
    SendMessageW(hwnd, CB_SETCURSEL, 0, 0);
}

struct AgeDisplayParts {
    std::string value;
    std::string unit = "岁";
};

AgeDisplayParts splitAgeDisplayText(const std::string& text) {
    AgeDisplayParts parts;
    std::string trimmed = search::trim(text);
    if (trimmed.empty()) {
        parts.value.clear();
        return parts;
    }

    const char* units[] = {"小时", "岁", "月", "天", "分"};
    for (const char* unit : units) {
        const size_t unitLen = std::strlen(unit);
        if (trimmed.size() >= unitLen && trimmed.compare(trimmed.size() - unitLen, unitLen, unit) == 0) {
            parts.value = search::trim(trimmed.substr(0, trimmed.size() - unitLen));
            parts.unit = unit;
            return parts;
        }
    }
    parts.value = trimmed;
    return parts;
}

void fillAgeUnitCombo(HWND combo, const std::string& selectedUnit = "岁") {
    if (!combo) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    const char* units[] = {"岁", "月", "天", "小时", "分"};
    int selected = 0;
    for (int i = 0; i < static_cast<int>(sizeof(units) / sizeof(units[0])); ++i) {
        const auto wide = search::utf8_to_wide(units[i]);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
        if (selectedUnit == units[i]) selected = i;
    }
    SendMessageW(combo, CB_SETCURSEL, selected, 0);
}

void setDatePickerValue(HWND hwnd, const std::string& text) {
    if (!hwnd) return;
    SYSTEMTIME st{};
    if (parseDateTimeText(text, st)) {
        auto* format = reinterpret_cast<const wchar_t*>(GetPropW(hwnd, PROP_DATE_FORMAT));
        SendMessageW(hwnd, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(format ? format : L"yyyy-MM-dd"));
        DateTime_SetSystemtime(hwnd, GDT_VALID, &st);
    } else {
        SYSTEMTIME blankValue = todayDate();
        SendMessageW(hwnd, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(BLANK_DATE_FORMAT));
        DateTime_SetSystemtime(hwnd, GDT_VALID, &blankValue);
    }
}

void clearDatePickerValue(HWND hwnd) {
    if (!hwnd) return;
    SYSTEMTIME blankValue = todayDate();
    SendMessageW(hwnd, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(BLANK_DATE_FORMAT));
    DateTime_SetSystemtime(hwnd, GDT_VALID, &blankValue);
}

void setControlsEnabled(bool enabled, std::initializer_list<HWND> controls) {
    for (HWND hwnd : controls) {
        if (hwnd) EnableWindow(hwnd, enabled ? TRUE : FALSE);
    }
}

void applyFont(HWND hwnd, HFONT font) {
    if (!font) return;
    EnumChildWindows(hwnd, [](HWND child, LPARAM p) -> BOOL {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(p), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font));
}

HFONT createBoldFont(HFONT base) {
    LOGFONTW lf{};
    if (base && GetObjectW(base, sizeof(lf), &lf) == sizeof(lf)) {
        lf.lfWeight = FW_BOLD;
        return CreateFontIndirectW(&lf);
    }
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0);
    ncm.lfMessageFont.lfWeight = FW_BOLD;
    return CreateFontIndirectW(&ncm.lfMessageFont);
}

void refreshLeftGroupTitleFont(RegularReportState* st) {
    if (!st) return;
    if (st->groupTitleFont) {
        DeleteObject(st->groupTitleFont);
        st->groupTitleFont = nullptr;
    }
    st->groupTitleFont = createBoldFont(st->ctx.uiFont);
    if (st->leftContent) {
        InvalidateRect(st->leftContent, nullptr, TRUE);
    }
}

int textLogicalWidth(HWND hwnd, HFONT font, const wchar_t* text) {
    HDC dc = GetDC(hwnd);
    if (!dc) return 0;
    HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    SIZE size{};
    GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &size);
    if (old) SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    const double scale = std::max(0.1f, search::dpi_scale_factor(hwnd));
    return static_cast<int>((size.cx + scale - 1) / scale);
}

int fontLogicalHeight(HWND hwnd, HFONT font) {
    HDC dc = GetDC(hwnd);
    if (!dc) return 16;
    HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    if (old) SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    const double scale = std::max(0.1f, search::dpi_scale_factor(hwnd));
    return static_cast<int>((tm.tmHeight + scale - 1) / scale);
}

int wrappedTextHeightPx(HWND hwnd, HFONT font, const wchar_t* text, int widthPx, int minHeightPx) {
    if (!text || widthPx <= 0) return minHeightPx;
    HDC dc = GetDC(hwnd);
    if (!dc) return minHeightPx;
    HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    RECT rc{0, 0, widthPx, 0};
    DrawTextW(dc, text, -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_CENTER);
    if (old) SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    return std::max(minHeightPx, static_cast<int>(rc.bottom - rc.top) + S(hwnd, 2));
}

RightHeaderLayout rightHeaderLayout(HWND hwnd, HFONT font, int panelWidthPx, const std::wstring& summaryLine1) {
    const int innerX = S(hwnd, PAD);
    const int innerW = std::max(S(hwnd, 80), panelWidthPx - S(hwnd, PAD * 2));
    const int top = S(hwnd, 4);
    const int gap = S(hwnd, 2);
    const int line1H = wrappedTextHeightPx(hwnd, font, summaryLine1.c_str(), innerW, S(hwnd, 20));
    const int line2Y = top + line1H + gap;
    const int line2H = wrappedTextHeightPx(hwnd, font, RIGHT_SUMMARY_LINE2, innerW, S(hwnd, 24));
    RightHeaderLayout layout{};
    layout.line1 = RECT{innerX, top, innerX + innerW, top + line1H};
    layout.line2 = RECT{innerX, line2Y, innerX + innerW, line2Y + line2H};
    layout.bottom = line2Y + line2H;
    return layout;
}

void comboReset(HWND combo) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
}

void comboAdd(HWND combo, const std::wstring& text) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
}

void comboSelectFirst(HWND combo) {
    if (SendMessageW(combo, CB_GETCOUNT, 0, 0) > 0) {
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }
}

void populateMachinePickerRooms(MachinePickerState* ps) {
    if (!ps || !ps->roomCombo) return;
    comboReset(ps->roomCombo);
    const std::string currentRoom = ps->report ? search::trim(ps->report->selectedRoomCode) : "";
    int selected = -1;
    for (const auto& row : ps->rooms) {
        comboAdd(ps->roomCombo, search::utf8_to_wide(row.room_name));
        if (selected < 0 && !currentRoom.empty() && search::trim(row.room_code) == currentRoom) {
            selected = static_cast<int>(SendMessageW(ps->roomCombo, CB_GETCOUNT, 0, 0)) - 1;
        }
    }
    if (selected >= 0) {
        SendMessageW(ps->roomCombo, CB_SETCURSEL, selected, 0);
    } else {
        comboSelectFirst(ps->roomCombo);
    }
}

std::string selectedMachinePickerRoomCode(MachinePickerState* ps) {
    if (!ps || !ps->roomCombo) return "";
    const auto index = static_cast<int>(SendMessageW(ps->roomCombo, CB_GETCURSEL, 0, 0));
    if (index < 0 || index >= static_cast<int>(ps->rooms.size())) return "";
    return ps->rooms[static_cast<size_t>(index)].room_code;
}

int machinePickerListHeight(HWND hwnd, MachinePickerState* ps) {
    const int visibleRows = std::clamp(static_cast<int>(ps ? ps->machines.size() : 0),
                                       MACHINE_PICKER_MIN_ROWS, MACHINE_PICKER_MAX_ROWS);
    const int fontH = ps && ps->report ? fontLogicalHeight(hwnd, ps->report->ctx.uiFont) : 16;
    const int rowH = S(hwnd, std::max(22, fontH + 6));
    return S(hwnd, MACHINE_PICKER_HEADER_H) + visibleRows * rowH + S(hwnd, MACHINE_PICKER_LIST_EXTRA_H);
}

void layoutMachinePicker(HWND hwnd, MachinePickerState* ps) {
    if (!hwnd || !ps || !ps->machineList) return;
    const int inputX = S(hwnd, MACHINE_PICKER_INPUT_X);
    const int listY = S(hwnd, MACHINE_PICKER_LIST_Y);
    const int listW = S(hwnd, MACHINE_PICKER_LIST_W);
    const int listH = machinePickerListHeight(hwnd, ps);
    MoveWindow(ps->machineList, inputX, listY, listW, listH, TRUE);
    ListView_SetColumnWidth(ps->machineList, 0, S(hwnd, MACHINE_PICKER_CODE_COL_W));
    ListView_SetColumnWidth(ps->machineList, 1, S(hwnd, MACHINE_PICKER_NAME_COL_W));

    RECT clientRc{0, 0, S(hwnd, MACHINE_PICKER_CLIENT_W), listY + listH + S(hwnd, MACHINE_PICKER_BOTTOM_PAD)};
    AdjustWindowRectEx(&clientRc, GetWindowLongW(hwnd, GWL_STYLE), FALSE, GetWindowLongW(hwnd, GWL_EXSTYLE));
    SetWindowPos(hwnd, nullptr, 0, 0, clientRc.right - clientRc.left, clientRc.bottom - clientRc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void populateMachinePickerMachines(MachinePickerState* ps) {
    if (!ps || !ps->machineList) return;
    ListView_DeleteAllItems(ps->machineList);
    const std::string currentCode = ps->report ? search::trim(ps->report->selectedMachineCode) : "";
    wchar_t currentName[256]{};
    if (ps->report && ps->report->machineEdit) {
        GetWindowTextW(ps->report->machineEdit, currentName, 256);
    }

    int selected = -1;
    for (int i = 0; i < static_cast<int>(ps->machines.size()); ++i) {
        const auto code = search::utf8_to_wide(ps->machines[static_cast<size_t>(i)].mach_code);
        const auto name = search::utf8_to_wide(ps->machines[static_cast<size_t>(i)].mach_name);
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.iSubItem = 0;
        item.pszText = const_cast<wchar_t*>(code.c_str());
        ListView_InsertItem(ps->machineList, &item);
        ListView_SetItemText(ps->machineList, i, 1, const_cast<wchar_t*>(name.c_str()));
        if (selected < 0 && !currentCode.empty() &&
            search::trim(ps->machines[static_cast<size_t>(i)].mach_code) == currentCode) {
            selected = i;
        } else if (selected < 0 && currentName[0] && lstrcmpW(currentName, name.c_str()) == 0) {
            selected = i;
        }
    }
    if (selected < 0 && !ps->machines.empty()) selected = 0;
    if (selected >= 0) {
        ListView_SetItemState(ps->machineList, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(ps->machineList, selected, FALSE);
    }
    layoutMachinePicker(ps->report ? ps->report->machinePickerPopup : nullptr, ps);
}

void reloadMachinePickerMachines(MachinePickerState* ps) {
    if (!ps || !ps->report || !ps->machineList) return;
    ps->machines.clear();
    std::string error;
    const std::string roomCode = selectedMachinePickerRoomCode(ps);
    if (!search::load_machine_options(ps->report->ctx.dbSettings, roomCode, ps->machines, error)) {
        populateMachinePickerMachines(ps);
        MessageBoxW(ps->report->machinePickerPopup ? ps->report->machinePickerPopup : ps->report->leftContent,
                    L"检验仪器加载失败。", L"常规报告", MB_ICONERROR);
        return;
    }
    populateMachinePickerMachines(ps);
}

void reloadMachinePickerRooms(MachinePickerState* ps) {
    if (!ps || !ps->report || !ps->roomCombo) return;
    ps->rooms.clear();
    std::string error;
    if (!search::load_room_options(ps->report->ctx.dbSettings, ps->rooms, error)) {
        populateMachinePickerRooms(ps);
        MessageBoxW(ps->report->machinePickerPopup ? ps->report->machinePickerPopup : ps->report->leftContent,
                    L"检验科室加载失败。", L"常规报告", MB_ICONERROR);
        return;
    }
    populateMachinePickerRooms(ps);
    reloadMachinePickerMachines(ps);
}

bool isAutoRefreshChecked(const RegularReportState* st) {
    return st && st->rightAutoRefreshCheck &&
           SendMessageW(st->rightAutoRefreshCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

int autoRefreshSeconds(const RegularReportState* st) {
    if (!st || !st->rightAutoRefreshEdit) return AUTO_REFRESH_DEFAULT_SECONDS;
    wchar_t text[32]{};
    GetWindowTextW(st->rightAutoRefreshEdit, text, static_cast<int>(sizeof(text) / sizeof(text[0])));
    wchar_t* end = nullptr;
    const long value = std::wcstol(text, &end, 10);
    if (end == text) return AUTO_REFRESH_DEFAULT_SECONDS;
    return std::clamp(static_cast<int>(value), AUTO_REFRESH_MIN_SECONDS, AUTO_REFRESH_MAX_SECONDS);
}

void stopAutoRefreshTimer(RegularReportState* st) {
    if (!st || !st->hwnd || !st->autoRefreshTimerActive) return;
    KillTimer(st->hwnd, IDT_REPORT_AUTO_REFRESH);
    st->autoRefreshTimerActive = false;
}

void updateAutoRefreshTimer(RegularReportState* st) {
    if (!st || !st->hwnd) return;
    stopAutoRefreshTimer(st);
    if (!isAutoRefreshChecked(st)) return;
    const int seconds = autoRefreshSeconds(st);
    if (SetTimer(st->hwnd, IDT_REPORT_AUTO_REFRESH, static_cast<UINT>(seconds * 1000), nullptr)) {
        st->autoRefreshTimerActive = true;
    }
}

search::QueryInput buildReportQueryInput(RegularReportState* st) {
    search::QueryInput input;
    if (!st) return input;
    const std::string inspectDate = datePickerValue(st->inspectDatePicker);
    input.start_date = inspectDate;
    input.end_date = inspectDate;
    input.room_code = st->selectedRoomCode;
    input.mach_code = st->selectedMachineCode;
    input.limit = 0;
    return input;
}

void runReportQuery(RegularReportState* st, bool preserveState = false) {
    if (!st || search::trim(st->selectedMachineCode).empty()) return;
    if (search::build_connection_string_w(st->ctx.dbSettings).empty()) {
        MessageBoxW(st->hwnd, L"请先在“设置”中填写数据库连接信息。", L"缺少数据库设置", MB_ICONWARNING);
        return;
    }

    if (!preserveState) {
        ListView_DeleteAllItems(st->reportList);
        ListView_DeleteAllItems(st->resultList);
        st->reportRows.clear();
        st->resultRows.clear();
        InvalidateRect(st->rightPanel, nullptr, TRUE);
        st->pictureQueryLoading = false;
        st->pictureRepNo.clear();
        ++st->pictureQueryGeneration;
        clearPictureView(st, L"");
        st->selectedReportIndex = -1;
        st->contextReportIndex = -1;
    }
    SetWindowTextW(st->status, preserveState ? L"正在刷新样本列表..." : L"正在查询样本列表...");
    st->reportQueryLoading = true;
    const int generation = ++st->reportQueryGeneration;
    const search::DbSettings settings = st->ctx.dbSettings;
    const search::QueryInput input = buildReportQueryInput(st);
    const std::string queryDate = input.start_date;
    const HWND hwnd = st->hwnd;

    std::thread([hwnd, settings, input, generation, preserveState, queryDate]() {
        auto* result = new ReportLoadResult;
        result->generation = generation;
        result->preserveState = preserveState;
        result->queryDate = queryDate;
        result->ok = search::run_report_query(settings, input, result->rows, result->connectionString, result->error);
        if (!PostMessageW(hwnd, WM_REGULAR_REPORTS_LOADED, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

void runAutoRefreshQuery(RegularReportState* st) {
    if (!st || st->reportQueryLoading) return;
    if (search::trim(st->selectedMachineCode).empty()) return;
    if (search::build_connection_string_w(st->ctx.dbSettings).empty()) return;
    runReportQuery(st, true);
}

bool hasSelectedReportRow(const RegularReportState* st) {
    return st && st->reportList && ListView_GetNextItem(st->reportList, -1, LVNI_SELECTED) >= 0;
}

bool inspectDateMatchesCurrentQuery(const RegularReportState* st) {
    if (!st) return false;
    const std::string inspectDate = datePickerValue(st->inspectDatePicker);
    return !inspectDate.empty() && inspectDate == st->reportQueryDate;
}

void setInspectDateAndQuery(RegularReportState* st, SYSTEMTIME date, bool preserveWhenPossible = false) {
    if (!st || !st->inspectDatePicker) return;
    date = normalizeDate(date);
    st->suppressInspectDateQuery = true;
    DateTime_SetSystemtime(st->inspectDatePicker, GDT_VALID, &date);
    st->suppressInspectDateQuery = false;
    if (!search::trim(st->selectedMachineCode).empty()) {
        runReportQuery(st, preserveWhenPossible && hasSelectedReportRow(st));
    }
}

void applyQuickMachine(RegularReportState* st, int slot) {
    if (!st || slot < 0 || slot >= QUICK_MACHINE_COUNT) return;
    const std::wstring name = search::load_module_str(L"RegularReport", quickMachineNameKey(slot), L"");
    const std::wstring code = search::load_module_str(L"RegularReport", quickMachineCodeKey(slot), L"");
    const std::wstring room = search::load_module_str(L"RegularReport", quickMachineRoomKey(slot), L"");
    if (search::trim(search::wide_to_utf8(code)).empty()) {
        MessageBoxW(st->hwnd, L"请先在系统设置中配置该快捷检验仪器。", L"常规报告", MB_ICONINFORMATION);
        return;
    }
    const std::string nextCode = search::wide_to_utf8(code);
    const std::string nextRoom = search::wide_to_utf8(room);
    const bool sameMachine = search::trim(st->selectedMachineCode) == search::trim(nextCode) &&
                             search::trim(st->selectedRoomCode) == search::trim(nextRoom);
    SetWindowTextW(st->machineEdit, name.empty() ? code.c_str() : name.c_str());
    st->selectedMachineCode = nextCode;
    st->selectedRoomCode = nextRoom;
    updateQuickMachineButtonLabels(st);
    runReportQuery(st, sameMachine && hasSelectedReportRow(st));
}

void acceptMachinePicker(MachinePickerState* ps) {
    if (!ps || !ps->report || !ps->report->machineEdit || !ps->machineList) return;
    const int sel = ListView_GetNextItem(ps->machineList, -1, LVNI_SELECTED);
    if (sel >= 0 && sel < static_cast<int>(ps->machines.size())) {
        const auto& machine = ps->machines[static_cast<size_t>(sel)];
        SetWindowTextW(ps->report->machineEdit, search::utf8_to_wide(machine.mach_name).c_str());
        ps->report->selectedMachineCode = machine.mach_code;
        ps->report->selectedRoomCode = selectedMachinePickerRoomCode(ps);
        updateQuickMachineButtonLabels(ps->report);
    }
}

void acceptAndCloseMachinePicker(HWND hwnd, MachinePickerState* ps) {
    RegularReportState* report = ps ? ps->report : nullptr;
    acceptMachinePicker(ps);
    DestroyWindow(hwnd);
    runReportQuery(report);
}

void postCloseMachinePicker(HWND hwnd, MachinePickerState* ps) {
    if (!ps || ps->closePosted) return;
    ps->closePosted = true;
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

LRESULT CALLBACK machinePickerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ps = reinterpret_cast<MachinePickerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            ps = reinterpret_cast<MachinePickerState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ps));
            if (!ps || !ps->report) return -1;
            ps->report->machinePickerPopup = hwnd;

            const int inputX = S(hwnd, MACHINE_PICKER_INPUT_X);
            const int roomY = S(hwnd, MACHINE_PICKER_ROOM_Y);
            const int listY = S(hwnd, MACHINE_PICKER_LIST_Y);
            ps->roomCombo = search::create_combo(hwnd, IDC_MACHINE_PICKER_ROOM, inputX, roomY,
                                                 S(hwnd, MACHINE_PICKER_LIST_W), S(hwnd, MACHINE_PICKER_COMBO_DROP_H), false);
            ps->machineList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                              WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                              inputX, listY, S(hwnd, MACHINE_PICKER_LIST_W), S(hwnd, MACHINE_PICKER_INITIAL_LIST_H),
                                              hwnd, win32_control_id(IDC_MACHINE_PICKER_MACH), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(ps->machineList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            search::add_list_column(ps->machineList, 0, L"仪器", S(hwnd, MACHINE_PICKER_CODE_COL_W));
            search::add_list_column(ps->machineList, 1, L"仪器名称", S(hwnd, MACHINE_PICKER_NAME_COL_W));
            applyFont(hwnd, ps->report->ctx.uiFont);
            reloadMachinePickerRooms(ps);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            const int code = HIWORD(wp);
            if (id == IDC_MACHINE_PICKER_ROOM && code == CBN_SELCHANGE) {
                reloadMachinePickerMachines(ps);
                return 0;
            }
            break;
        }
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (nm && nm->idFrom == IDC_MACHINE_PICKER_MACH &&
                (nm->code == NM_RETURN || nm->code == NM_DBLCLK)) {
                acceptAndCloseMachinePicker(hwnd, ps);
                return 0;
            }
            break;
        }
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) {
                postCloseMachinePicker(hwnd, ps);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_NCDESTROY:
            if (ps) {
                if (ps->report && ps->report->machinePickerPopup == hwnd) {
                    ps->report->machinePickerPopup = nullptr;
                }
                delete ps;
            }
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void registerMachinePickerClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = machinePickerProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = MACHINE_PICKER_CLASS;
    RegisterClassExW(&wc);
    registered = true;
}

void showMachinePicker(RegularReportState* st, HWND anchor) {
    if (!st || !anchor) return;
    if (IsWindow(st->machinePickerPopup)) {
        SetForegroundWindow(st->machinePickerPopup);
        return;
    }

    registerMachinePickerClass(st->ctx.instance);
    RECT anchorRc{};
    GetWindowRect(anchor, &anchorRc);
    const int width = S(anchor, MACHINE_PICKER_CLIENT_W);
    const int height = S(anchor, MACHINE_PICKER_INITIAL_H);
    int x = anchorRc.left;
    int y = anchorRc.bottom + S(anchor, 2);

    HMONITOR monitor = MonitorFromRect(&anchorRc, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(monitor, &mi)) {
        x = std::min(x, static_cast<int>(mi.rcWork.right) - width);
        y = std::min(y, static_cast<int>(mi.rcWork.bottom) - height);
        x = std::max(x, static_cast<int>(mi.rcWork.left));
        y = std::max(y, static_cast<int>(mi.rcWork.top));
    }

    auto* ps = new MachinePickerState;
    ps->report = st;
    HWND owner = GetAncestor(st->leftContent, GA_ROOT);
    HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW, MACHINE_PICKER_CLASS, L"选择检验仪器",
                                 WS_POPUP | WS_CAPTION,
                                 x, y, width, height, owner, nullptr, st->ctx.instance, ps);
    if (!popup) {
        delete ps;
        return;
    }
    ShowWindow(popup, SW_SHOWNORMAL);
    UpdateWindow(popup);
}

int leftLabelWidth(HWND hwnd, HFONT font) {
    const wchar_t* labels[] = {
        L"检验仪器", L"组合项目", L"检验单号", L"病人类型", L"临床科室",
        L"临床诊断", L"申请医生", L"申请日期", L"签收时间", L"上机时间",
        L"报告时间", L"检验日期", L"采集日期",
    };
    int width = 50;
    for (const wchar_t* label : labels) {
        width = std::max(width, textLogicalWidth(hwnd, font, label) + 8);
    }
    return width;
}

int rightLabelWidth(HWND hwnd, HFONT font) {
    const wchar_t* labels[] = {L"样本号", L"性别", L"床号", L"审核"};
    int width = 38;
    for (const wchar_t* label : labels) {
        width = std::max(width, textLogicalWidth(hwnd, font, label) + 8);
    }
    return width;
}

void clearLeftPanel(RegularReportState* st) {
    if (!st) return;
    for (HWND child : st->leftControls) {
        if (IsWindow(child)) DestroyWindow(child);
    }
    st->leftControls.clear();
    st->leftTabControls.clear();
    st->leftGroupFrames.clear();
    st->machineEdit = nullptr;
    st->machinePickerButton = nullptr;
    st->groupEdit = nullptr;
    st->sampleEdit = nullptr;
    st->reportNoEdit = nullptr;
    st->operNoEdit = nullptr;
    st->patientTypeCombo = nullptr;
    st->urgentEdit = nullptr;
    st->barcodeEdit = nullptr;
    st->regNoEdit = nullptr;
    st->patientNameEdit = nullptr;
    st->sexEdit = nullptr;
    st->ageEdit = nullptr;
    st->ageUnitCombo = nullptr;
    st->bedEdit = nullptr;
    st->phoneEdit = nullptr;
    st->deptEdit = nullptr;
    st->diagEdit = nullptr;
    st->reqDoctorEdit = nullptr;
    st->feeEdit = nullptr;
    st->testerEdit = nullptr;
    st->auditEdit = nullptr;
    st->noteCodeEdit = nullptr;
    st->noteEdit = nullptr;
    st->applyDatePicker = nullptr;
    st->receiveDatePicker = nullptr;
    st->machineDatePicker = nullptr;
    st->reportDatePicker = nullptr;
    st->inspectDatePicker = nullptr;
    st->collectDateEdit = nullptr;
}

void updateLeftScrollBar(RegularReportState* st) {
    if (!st || !st->leftPanel || !st->leftContent || !st->leftScrollBar) return;

    RECT rc{};
    GetClientRect(st->leftPanel, &rc);
    const int page = std::max(1, static_cast<int>(rc.bottom - rc.top));
    const int contentH = S(st->leftPanel, std::max(LEFT_CONTENT_HEIGHT, st->leftContentHeight));
    const int maxScroll = std::max(0, contentH - page);
    st->leftScrollY = std::clamp(st->leftScrollY, 0, maxScroll);
    const int scrollW = GetSystemMetrics(SM_CXVSCROLL);
    const bool needsScroll = contentH > page;
    const int contentW = std::max(0, static_cast<int>(rc.right - rc.left) - scrollW);

    MoveWindow(st->leftContent, 0, -st->leftScrollY, contentW, contentH, TRUE);
    SetWindowPos(st->leftScrollBar, HWND_TOP,
                 contentW, 0,
                 scrollW, page, SWP_SHOWWINDOW);

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = std::max(0, contentH - 1);
    si.nPage = static_cast<UINT>(page);
    si.nPos = st->leftScrollY;
    SetScrollInfo(st->leftScrollBar, SB_CTL, &si, TRUE);
    ShowWindow(st->leftScrollBar, needsScroll ? SW_SHOW : SW_HIDE);
}

void scrollLeftPanelTo(RegularReportState* st, int targetY) {
    if (!st || !st->leftPanel || !st->leftContent) return;

    RECT rc{};
    GetClientRect(st->leftPanel, &rc);
    const int page = static_cast<int>(rc.bottom - rc.top);
    const int maxScroll = std::max(0, S(st->leftPanel, std::max(LEFT_CONTENT_HEIGHT, st->leftContentHeight)) - page);
    targetY = std::clamp(targetY, 0, maxScroll);
    st->leftScrollY = targetY;
    updateLeftScrollBar(st);
}

LRESULT CALLBACK leftPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                               UINT_PTR subclassId, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_VSCROLL: {
            if (!st) break;
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(st->leftScrollBar, SB_CTL, &si);

            int target = st->leftScrollY;
            switch (LOWORD(wp)) {
                case SB_LINEUP:
                    target -= S(hwnd, LEFT_SCROLL_STEP);
                    break;
                case SB_LINEDOWN:
                    target += S(hwnd, LEFT_SCROLL_STEP);
                    break;
                case SB_PAGEUP:
                    target -= static_cast<int>(si.nPage);
                    break;
                case SB_PAGEDOWN:
                    target += static_cast<int>(si.nPage);
                    break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION:
                    target = si.nTrackPos;
                    break;
                case SB_TOP:
                    target = 0;
                    break;
                case SB_BOTTOM:
                    target = si.nMax;
                    break;
                default:
                    return 0;
            }
            scrollLeftPanelTo(st, target);
            return 0;
        }
        case WM_MOUSEWHEEL:
            if (st) {
                const int delta = GET_WHEEL_DELTA_WPARAM(wp);
                scrollLeftPanelTo(st, st->leftScrollY - (delta / WHEEL_DELTA) * S(hwnd, LEFT_SCROLL_STEP * 3));
                return 0;
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, leftPanelProc, subclassId);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void drawLeftGroupFrames(HWND hwnd, RegularReportState* st, HDC dc) {
    RECT client{};
    GetClientRect(hwnd, &client);
    FillRect(dc, &client, st && st->panelBrush ? st->panelBrush : GetSysColorBrush(COLOR_BTNFACE));
    if (!st) return;

    HBRUSH frameBrush = GetSysColorBrush(COLOR_GRAYTEXT);
    HGDIOBJ oldFont = nullptr;
    HFONT titleFont = st->groupTitleFont ? st->groupTitleFont : st->ctx.uiFont;
    if (titleFont) oldFont = SelectObject(dc, titleFont);
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, RGB(0xEF, 0xEF, 0xEF));
    SetTextColor(dc, RGB(0, 0, 0));

    for (const auto& frame : st->leftGroupFrames) {
        RECT rc = frame.rect;
        FrameRect(dc, &rc, frameBrush);

        RECT titleRc = rc;
        titleRc.left += S(hwnd, 14);
        SIZE titleSize{};
        GetTextExtentPoint32W(dc, frame.title, static_cast<int>(wcslen(frame.title)), &titleSize);
        titleRc.top = rc.top - std::max(S(hwnd, 8), static_cast<int>(titleSize.cy) * 2 / 3);
        titleRc.right = std::min(rc.right - S(hwnd, 8), titleRc.left + titleSize.cx + S(hwnd, 24));
        titleRc.bottom = titleRc.top + std::max(S(hwnd, 20), static_cast<int>(titleSize.cy) + S(hwnd, 6));
        ExtTextOutW(dc, titleRc.left, titleRc.top, ETO_OPAQUE, &titleRc, L"", 0, nullptr);
        DrawTextW(dc, frame.title, -1, &titleRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }

    if (oldFont) SelectObject(dc, oldFont);
}

LRESULT CALLBACK leftContentProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR subclassId, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_COMMAND:
            if (st && LOWORD(wp) == IDC_MACHINE_PICKER_BUTTON && HIWORD(wp) == BN_CLICKED) {
                showMachinePicker(st, reinterpret_cast<HWND>(lp));
                return 0;
            }
            break;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (st && nm && nm->idFrom == IDC_INSPECT_DATE && nm->code == DTN_DATETIMECHANGE &&
                !st->suppressInspectDateQuery && !search::trim(st->selectedMachineCode).empty()) {
                runReportQuery(st, hasSelectedReportRow(st) && inspectDateMatchesCurrentQuery(st));
                return 0;
            }
            break;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            drawLeftGroupFrames(hwnd, st, dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            HWND ctl = reinterpret_cast<HWND>(lp);
            if (GetPropW(ctl, L"RegularLeftLabel")) {
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(0x00, 0x00, 0xC4));
                return reinterpret_cast<LRESULT>(st ? st->panelBrush : nullptr);
            }
            break;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, leftContentProc, subclassId);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

COLORREF reportRowColor(const RegularReportState* st, int row) {
    if (!st || row < 0 || row >= static_cast<int>(st->reportRows.size())) {
        return RGB(0xFF, 0xFF, 0xFF);
    }
    const auto& report = st->reportRows[static_cast<size_t>(row)];
    const auto conf = search::trim(report.conf);
    if (conf == "S") {
        return RGB(0x98, 0xBB, 0x8F);
    }
    const auto chkFlag = search::trim(report.chk_flag);
    if (chkFlag == "T") {
        return RGB(0x6F, 0x94, 0xE6);
    }
    return RGB(0xFF, 0xFF, 0xFF);
}

COLORREF reportPrintCellColor(const RegularReportState* st, int row) {
    if (!st || row < 0 || row >= static_cast<int>(st->reportRows.size())) {
        return RGB(0xFF, 0xFF, 0xFF);
    }
    const auto printFlag = search::trim(st->reportRows[static_cast<size_t>(row)].zymz_print);
    return printFlag == "1" ? RGB(0xA6, 0xEC, 0x9A) : RGB(0xFF, 0xFF, 0xFF);
}

COLORREF resultTextColor(const search::ResultRow& row) {
    switch (search::result_row_tone(row)) {
        case search::ResultRowTone::High: return RGB(220, 0, 0);
        case search::ResultRowTone::Low:  return RGB(0, 0, 220);
        default:                          return CLR_INVALID;
    }
}

bool listViewRowSelected(HWND list, int row) {
    return list && row >= 0 && (ListView_GetItemState(list, row, LVIS_SELECTED) & LVIS_SELECTED);
}

bool customDrawListSelection(NMLVCUSTOMDRAW* cd, HWND list, int row) {
    if (!cd || !listViewRowSelected(list, row)) return false;
    cd->nmcd.uItemState &= ~(CDIS_SELECTED | CDIS_FOCUS);
    cd->clrTextBk = GetSysColor(COLOR_HIGHLIGHT);
    cd->clrText = GetSysColor(COLOR_HIGHLIGHTTEXT);
    return true;
}

void redrawSelectedListRow(HWND list) {
    if (!list) return;
    const int selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (selected < 0) return;
    ListView_RedrawItems(list, selected, selected);
    UpdateWindow(list);
}

std::vector<int> checkedReportIndexes(const RegularReportState* st);

void showReportContextMenu(RegularReportState* st, const NMITEMACTIVATE* item) {
    if (!st || !st->reportList || !item || item->iItem < 0 ||
        item->iItem >= static_cast<int>(st->reportRows.size())) {
        return;
    }

    ListView_SetItemState(st->reportList, item->iItem, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    st->contextReportIndex = item->iItem;
    POINT pt = item->ptAction;
    ClientToScreen(st->reportList, &pt);
    if (pt.x == 0 && pt.y == 0) {
        GetCursorPos(&pt);
    }

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, IDM_REPORT_PRINT_BARCODE, L"打印条码");
    AppendMenuW(menu,
                checkedReportIndexes(st).empty() ? (MF_STRING | MF_GRAYED) : MF_STRING,
                IDM_REPORT_PRINT_CHECKED_BARCODES, L"打印勾选条码");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | MF_GRAYED, IDM_REPORT_TODO, L"待开发");

    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                   pt.x, pt.y, 0, st->hwnd, nullptr);
    DestroyMenu(menu);
}

const search::ReportRow* contextReportRow(const RegularReportState* st) {
    if (!st || st->contextReportIndex < 0 ||
        st->contextReportIndex >= static_cast<int>(st->reportRows.size())) {
        return nullptr;
    }
    return &st->reportRows[static_cast<size_t>(st->contextReportIndex)];
}

std::vector<int> checkedReportIndexes(const RegularReportState* st) {
    std::vector<int> indexes;
    if (!st || !st->reportList) return indexes;
    const int count = ListView_GetItemCount(st->reportList);
    for (int i = 0; i < count && i < static_cast<int>(st->reportRows.size()); ++i) {
        if (ListView_GetCheckState(st->reportList, i)) {
            indexes.push_back(i);
        }
    }
    return indexes;
}

void clearReportChecks(RegularReportState* st) {
    if (!st || !st->reportList) return;
    const int count = ListView_GetItemCount(st->reportList);
    for (int i = 0; i < count; ++i) {
        ListView_SetCheckState(st->reportList, i, FALSE);
    }
}

std::string barcodeGroupNameForReport(RegularReportState* st, int reportIndex, std::string& error) {
    error.clear();
    if (!st || reportIndex < 0 || reportIndex >= static_cast<int>(st->reportRows.size())) {
        error = "invalid report row";
        return "";
    }
    const auto& row = st->reportRows[static_cast<size_t>(reportIndex)];
    return search::trim(row.group_name);
}

void appendReportBarcodeDetails(std::wstring& message, const search::ReportRow& row,
                                const std::string& barcodeGroupName) {
    auto appendLine = [&](const wchar_t* label, const std::string& value) {
        message += label;
        message += search::utf8_to_wide(value);
        message += L"\n";
    };
    appendLine(L"样本号：", row.oper_no);
    appendLine(L"组合项目：", barcodeGroupName);
    appendLine(L"条码号：", row.txm_no);
    appendLine(L"姓名：", row.name);
    appendLine(L"标本：", row.sample_name);
    appendLine(L"开单日期：", slashDate(row.chk_date));
    appendLine(L"科室代码：", row.dept_name);
    message += L"病人号：";
    message += search::utf8_to_wide(row.reg_no);
}

std::wstring configuredBarcodePrinterName() {
    std::wstring printer = search::load_module_str(L"RegularReport", L"BarcodePrinterName",
                                                   DEFAULT_BARCODE_PRINTER_NAME);
    if (printer.empty()) {
        printer = DEFAULT_BARCODE_PRINTER_NAME;
    }
    return printer;
}

#if defined(LIS_HAS_LABELPRINT)
void sendBarcodeLabel(const search::ReportRow& row, const std::string& barcodeGroupName,
                      const std::wstring& printerName) {
    labelprint::MedicalLabelData data;
    data.sampleNo = row.oper_no;
    data.testItem = barcodeGroupName;
    data.barcodeValue = row.txm_no;
    data.patientName = row.name;
    data.specimenType = row.sample_name;
    data.department = row.dept_name;
    data.patientId = row.reg_no;
    data.timestamp = slashDate(row.chk_date);

    labelprint::MedicalLabelPrintOptions options;
    options.model = labelprint::MedicalLabelPrinterModel::Auto;
    options.fallbackModel = labelprint::MedicalLabelPrinterModel::XprinterXp360b;
    options.quantity = 1;
    labelprint::printMedicalLabel(printerName, data, options);
}
#endif

std::wstring printBarcodeForContext(RegularReportState* st) {
    const search::ReportRow* row = contextReportRow(st);
    if (!row) {
        return L"请先右键选择一条报告记录。";
    }

    std::string groupError;
    const std::string barcodeGroupName = barcodeGroupNameForReport(st, st->contextReportIndex, groupError);
    if (!groupError.empty()) {
        std::wstring message = L"打印条码失败：组合项目查询失败。";
        message += L"\n";
        message += search::utf8_to_wide(groupError);
        return message;
    }

    std::wstring details;
    appendReportBarcodeDetails(details, *row, barcodeGroupName);

#if defined(LIS_HAS_LABELPRINT)
    try {
        const std::wstring printerName = configuredBarcodePrinterName();
        sendBarcodeLabel(*row, barcodeGroupName, printerName);

        std::wstring message = L"打印条码已发送。\n打印机：";
        message += printerName;
        message += L"\n\n";
        message += details;
        return message;
    } catch (const std::exception& ex) {
        std::wstring message = L"打印条码失败：";
        message += search::utf8_to_wide(ex.what());
        message += L"\n打印机：";
        message += configuredBarcodePrinterName();
        message += L"\n请在系统设置页重新选择条码打印机。";
        message += L"\n\n";
        message += details;
        return message;
    }
#else
    std::wstring message = L"打印条码功能不可用：构建时未找到 LabelPrint 项目。\n\n";
    message += details;
    return message;
#endif
}

std::wstring printCheckedBarcodes(RegularReportState* st) {
    const std::vector<int> indexes = checkedReportIndexes(st);
    if (indexes.empty()) {
        return L"请先勾选需要打印条码的报告记录。";
    }

#if defined(LIS_HAS_LABELPRINT)
    const std::wstring printerName = configuredBarcodePrinterName();
    int sent = 0;
    try {
        for (int index : indexes) {
            std::string groupError;
            const std::string barcodeGroupName = barcodeGroupNameForReport(st, index, groupError);
            if (!groupError.empty()) {
                throw std::runtime_error("组合项目查询失败: " + groupError);
            }
            sendBarcodeLabel(st->reportRows[static_cast<size_t>(index)], barcodeGroupName, printerName);
            ++sent;
        }
        clearReportChecks(st);
        std::wstring message = L"勾选条码已发送。\n打印机：";
        message += printerName;
        message += L"\n数量：";
        message += std::to_wstring(sent);
        return message;
    } catch (const std::exception& ex) {
        std::wstring message = L"批量打印条码失败：";
        message += search::utf8_to_wide(ex.what());
        message += L"\n打印机：";
        message += printerName;
        message += L"\n已发送：";
        message += std::to_wstring(sent);
        message += L" / ";
        message += std::to_wstring(indexes.size());
        if (sent < static_cast<int>(indexes.size())) {
            const auto& failed = st->reportRows[static_cast<size_t>(indexes[static_cast<size_t>(sent)])];
            message += L"\n失败记录：";
            message += search::utf8_to_wide(failed.oper_no);
            message += L" ";
            message += search::utf8_to_wide(failed.name);
        }
        message += L"\n请在系统设置页确认条码打印机。";
        clearReportChecks(st);
        return message;
    }
#else
    clearReportChecks(st);
    return L"打印条码功能不可用：构建时未找到 LabelPrint 项目。";
#endif
}

void releasePictureImage(RegularReportState* st) {
    if (!st) return;
    delete st->pictureImage;
    st->pictureImage = nullptr;
    if (st->pictureStream) {
        st->pictureStream->Release();
        st->pictureStream = nullptr;
    }
}

bool ensureGdiplus(RegularReportState* st) {
    if (!st) return false;
    if (st->gdiplusReady) return true;
    Gdiplus::GdiplusStartupInput input;
    st->gdiplusReady = Gdiplus::GdiplusStartup(&st->gdiplusToken, &input, nullptr) == Gdiplus::Ok;
    return st->gdiplusReady;
}

bool createImageFromBytes(const std::vector<unsigned char>& bytes,
                          IStream*& stream,
                          Gdiplus::Image*& image,
                          std::wstring& error) {
    stream = nullptr;
    image = nullptr;
    if (bytes.empty()) {
        return true;
    }

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!memory) {
        error = L"图像内存分配失败";
        return false;
    }
    void* data = GlobalLock(memory);
    if (!data) {
        GlobalFree(memory);
        error = L"图像内存锁定失败";
        return false;
    }
    std::memcpy(data, bytes.data(), bytes.size());
    GlobalUnlock(memory);

    if (CreateStreamOnHGlobal(memory, TRUE, &stream) != S_OK || !stream) {
        GlobalFree(memory);
        error = L"图像流创建失败";
        return false;
    }

    image = Gdiplus::Image::FromStream(stream, FALSE);
    if (!image || image->GetLastStatus() != Gdiplus::Ok || image->GetWidth() == 0 || image->GetHeight() == 0) {
        delete image;
        image = nullptr;
        stream->Release();
        stream = nullptr;
        error = L"图像解码失败";
        return false;
    }
    return true;
}

void clearPictureView(RegularReportState* st, const std::wstring& status) {
    if (!st) return;
    releasePictureImage(st);
    st->pictureStatus = status;
    if (IsWindow(st->pictureView)) {
        InvalidateRect(st->pictureView, nullptr, TRUE);
    }
}

bool loadPictureView(RegularReportState* st, const std::vector<unsigned char>& bytes, std::wstring& error) {
    if (!st) return false;
    releasePictureImage(st);
    if (bytes.empty()) {
        st->pictureStatus.clear();
        if (IsWindow(st->pictureView)) InvalidateRect(st->pictureView, nullptr, TRUE);
        return true;
    }
    if (!ensureGdiplus(st)) {
        error = L"GDI+ 初始化失败";
        st->pictureStatus = error;
        if (IsWindow(st->pictureView)) InvalidateRect(st->pictureView, nullptr, TRUE);
        return false;
    }

    IStream* stream = nullptr;
    Gdiplus::Image* image = nullptr;
    if (!createImageFromBytes(bytes, stream, image, error)) {
        st->pictureStatus = error;
        if (IsWindow(st->pictureView)) InvalidateRect(st->pictureView, nullptr, TRUE);
        return false;
    }

    st->pictureStream = stream;
    st->pictureImage = image;
    st->pictureStatus.clear();
    if (IsWindow(st->pictureView)) {
        InvalidateRect(st->pictureView, nullptr, TRUE);
    }
    return true;
}

void querySelectedPicture(RegularReportState* st, int selected);
void updatePictureViewport(RegularReportState* st);
void openPicturePopupForSelection(RegularReportState* st);

void showRightInfoPage(RegularReportState* st) {
    if (!st || !st->rightTab) return;

    const bool showInfo = TabCtrl_GetCurSel(st->rightTab) == 0;
    for (HWND ctl : st->rightInfoControls) {
        if (IsWindow(ctl)) ShowWindow(ctl, showInfo ? SW_SHOW : SW_HIDE);
    }
    if (IsWindow(st->reportList)) ShowWindow(st->reportList, showInfo ? SW_SHOW : SW_HIDE);
    InvalidateRect(st->rightPanel, nullptr, TRUE);
}

void showMiddleResultPage(RegularReportState* st) {
    if (!st || !st->middleTab) return;

    const int page = TabCtrl_GetCurSel(st->middleTab);
    const bool showResult = page == 0;
    const bool showPicture = page == 1;
    for (HWND ctl : st->middleResultControls) {
        if (IsWindow(ctl)) ShowWindow(ctl, showResult ? SW_SHOW : SW_HIDE);
    }
    for (HWND ctl : st->middlePictureControls) {
        if (IsWindow(ctl)) ShowWindow(ctl, showPicture ? SW_SHOW : SW_HIDE);
    }
    if (IsWindow(st->resultList)) ShowWindow(st->resultList, showResult ? SW_SHOW : SW_HIDE);
    if (IsWindow(st->status)) ShowWindow(st->status, showResult ? SW_SHOW : SW_HIDE);
    InvalidateRect(st->middlePanel, nullptr, TRUE);
    if (showPicture) {
        updatePictureViewport(st);
        querySelectedPicture(st, st->selectedReportIndex);
    }
}

void selectReportRow(RegularReportState* st, int index) {
    if (!st || !st->reportList || index < 0 || index >= static_cast<int>(st->reportRows.size())) return;
    ListView_SetItemState(st->reportList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(st->reportList, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(st->reportList, index, FALSE);
    SetFocus(st->reportList);
}

void selectAdjacentReportRow(RegularReportState* st, int delta) {
    if (!st || !st->reportList || st->reportRows.empty()) return;
    int current = ListView_GetNextItem(st->reportList, -1, LVNI_SELECTED);
    if (current < 0) {
        current = st->selectedReportIndex;
    }
    if (current < 0 || current >= static_cast<int>(st->reportRows.size())) {
        current = delta > 0 ? -1 : static_cast<int>(st->reportRows.size());
    }
    const int next = std::max(0, std::min(static_cast<int>(st->reportRows.size()) - 1,
                                          current + delta));
    if (next != current) {
        selectReportRow(st, next);
    }
}

void querySelectedResults(RegularReportState* st, int selected);
void sortReportRowsByColumn(RegularReportState* st, int column);
void populateLeftPanelFromReport(RegularReportState* st, int selected);
void runReportQuery(RegularReportState* st, bool preserveState);

std::string normalizeSampleNo(std::string value) {
    value = search::trim(std::move(value));
    if (value.empty()) return value;
    const bool allDigits = std::all_of(value.begin(), value.end(), [](char ch) {
        return ch >= '0' && ch <= '9';
    });
    if (!allDigits) return value;
    const auto firstNonZero = value.find_first_not_of('0');
    return firstNonZero == std::string::npos ? "0" : value.substr(firstNonZero);
}

int findReportIndexBySampleNo(const RegularReportState* st, const std::string& input) {
    if (!st) return -1;
    const std::string exact = search::trim(input);
    if (exact.empty()) return -1;
    const std::string normalized = normalizeSampleNo(exact);
    for (int i = 0; i < static_cast<int>(st->reportRows.size()); ++i) {
        const std::string sample = search::trim(st->reportRows[static_cast<size_t>(i)].oper_no);
        if (sample == exact || normalizeSampleNo(sample) == normalized) {
            return i;
        }
    }
    return -1;
}

void selectReportRowBySampleInput(RegularReportState* st) {
    if (!st || !st->operNoEdit) return;
    const std::string input = search::wide_to_utf8(windowText(st->operNoEdit));
    const int index = findReportIndexBySampleNo(st, input);
    if (index < 0) {
        MessageBoxW(st->hwnd, L"当前右侧列表中未找到该样本号。", L"常规报告", MB_ICONINFORMATION);
        return;
    }

    const int current = st->reportList ? ListView_GetNextItem(st->reportList, -1, LVNI_SELECTED) : -1;
    selectReportRow(st, index);
    if (current == index) {
        querySelectedResults(st, index);
    }
}

LRESULT CALLBACK sampleInputProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR subclassId, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_GETDLGCODE:
            return DefSubclassProc(hwnd, msg, wp, lp) | DLGC_WANTALLKEYS;
        case WM_KEYDOWN:
            if (wp == VK_RETURN) {
                selectReportRowBySampleInput(st);
                return 0;
            }
            break;
        case WM_CHAR:
            if (wp == VK_RETURN) {
                return 0;
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, sampleInputProc, subclassId);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

bool focusLeftTabControl(RegularReportState* st, HWND current, bool reverse) {
    if (!st || st->leftTabControls.empty()) return false;
    auto it = std::find(st->leftTabControls.begin(), st->leftTabControls.end(), current);
    if (it == st->leftTabControls.end()) return false;

    const int count = static_cast<int>(st->leftTabControls.size());
    int index = static_cast<int>(std::distance(st->leftTabControls.begin(), it));
    for (int step = 0; step < count; ++step) {
        index = reverse ? (index - 1 + count) % count : (index + 1) % count;
        HWND target = st->leftTabControls[static_cast<size_t>(index)];
        if (!IsWindow(target) || !IsWindowVisible(target) || !IsWindowEnabled(target)) continue;
        SetFocus(target);
        SendMessageW(target, EM_SETSEL, 0, -1);
        return true;
    }
    return false;
}

LRESULT CALLBACK leftTabControlProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                    UINT_PTR subclassId, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_GETDLGCODE:
            return DefSubclassProc(hwnd, msg, wp, lp) | DLGC_WANTTAB;
        case WM_KEYDOWN:
            if (wp == VK_TAB && focusLeftTabControl(st, hwnd, (GetKeyState(VK_SHIFT) & 0x8000) != 0)) {
                return 0;
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, leftTabControlProc, subclassId);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void registerLeftTabControls(RegularReportState* st, std::initializer_list<HWND> controls) {
    if (!st) return;
    for (HWND hwnd : controls) {
        if (!hwnd) continue;
        st->leftTabControls.push_back(hwnd);
        SetWindowSubclass(hwnd, leftTabControlProc, LEFT_TAB_SUBCLASS, reinterpret_cast<DWORD_PTR>(st));
    }
}

void beginResultEdit(RegularReportState* st, int row);

void finishResultEdit(RegularReportState* st, bool commit, bool moveNext = false,
                      bool restoreListFocus = true) {
    if (!st || !st->resultEdit) return;
    const HWND edit = st->resultEdit;
    const int row = st->resultEditRow;
    st->resultEdit = nullptr;
    st->resultEditRow = -1;

    if (commit && row >= 0 && row < static_cast<int>(st->resultRows.size())) {
        wchar_t buffer[512]{};
        GetWindowTextW(edit, buffer, static_cast<int>(std::size(buffer)));
        st->resultRows[static_cast<size_t>(row)].result = search::wide_to_utf8(buffer);
        ListView_SetItemText(st->resultList, row, RESULT_VALUE_COL, buffer);
        ListView_RedrawItems(st->resultList, row, row);
    }

    DestroyWindow(edit);
    if (restoreListFocus && st->resultList) SetFocus(st->resultList);
    if (commit && moveNext && row + 1 < static_cast<int>(st->resultRows.size())) {
        ListView_SetItemState(st->resultList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(st->resultList, row + 1, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(st->resultList, row + 1, FALSE);
        beginResultEdit(st, row + 1);
    }
}

LRESULT CALLBACK resultEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR subclassId, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_GETDLGCODE:
            return DefSubclassProc(hwnd, msg, wp, lp) | DLGC_WANTALLKEYS;
        case WM_KEYDOWN:
            if (wp == VK_RETURN) {
                finishResultEdit(st, true, true);
                return 0;
            }
            if (wp == VK_ESCAPE) {
                finishResultEdit(st, false);
                return 0;
            }
            break;
        case WM_KILLFOCUS:
            finishResultEdit(st, false, false, false);
            return 0;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, resultEditProc, subclassId);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void beginResultEdit(RegularReportState* st, int row) {
    if (!st || !st->resultList || row < 0 || row >= static_cast<int>(st->resultRows.size())) return;
    finishResultEdit(st, false);

    RECT rc{};
    if (!ListView_GetSubItemRect(st->resultList, row, RESULT_VALUE_COL, LVIR_BOUNDS, &rc)) return;
    RECT client{};
    GetClientRect(st->resultList, &client);
    if (rc.right <= client.left || rc.left >= client.right || rc.bottom <= client.top || rc.top >= client.bottom) {
        return;
    }
    rc.left = std::max(rc.left, client.left);
    rc.right = std::min(rc.right, client.right);
    InflateRect(&rc, -1, -1);

    const std::wstring text = search::utf8_to_wide(st->resultRows[static_cast<size_t>(row)].result);
    st->resultEdit = CreateWindowExW(0, L"EDIT", text.c_str(),
                                     WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                     rc.left, rc.top, std::max(24, static_cast<int>(rc.right - rc.left)),
                                     std::max(20, static_cast<int>(rc.bottom - rc.top)),
                                     st->resultList, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!st->resultEdit) return;
    st->resultEditRow = row;
    SendMessageW(st->resultEdit, WM_SETFONT, reinterpret_cast<WPARAM>(st->ctx.uiFont), TRUE);
    SetWindowSubclass(st->resultEdit, resultEditProc, RESULT_EDIT_SUBCLASS, reinterpret_cast<DWORD_PTR>(st));
    SendMessageW(st->resultEdit, EM_SETSEL, 0, -1);
    SetFocus(st->resultEdit);
}

bool beginResultEditFromPoint(RegularReportState* st, POINT pt) {
    if (!st || !st->resultList) return false;
    LVHITTESTINFO hit{};
    hit.pt = pt;
    const int row = ListView_SubItemHitTest(st->resultList, &hit);
    if (row >= 0 && hit.iSubItem == RESULT_VALUE_COL) {
        ListView_SetItemState(st->resultList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(st->resultList, row, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        beginResultEdit(st, row);
        return true;
    } else {
        finishResultEdit(st, false);
    }
    return false;
}

LRESULT CALLBACK resultListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR subclassId, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_LBUTTONDOWN: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (beginResultEditFromPoint(st, pt)) {
                return 0;
            }
            break;
        }
        case WM_HSCROLL:
        case WM_VSCROLL:
        case WM_MOUSEWHEEL:
            finishResultEdit(st, false);
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, resultListProc, subclassId);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void updatePictureViewport(RegularReportState* st);
void scrollPictureViewport(RegularReportState* st, int targetX, int targetY);

LRESULT CALLBACK pictureViewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR subclassId, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, GetSysColorBrush(COLOR_WINDOW));

            if (st && st->pictureImage) {
                const int availW = S(hwnd, PICTURE_FIXED_W);
                const int availH = S(hwnd, PICTURE_FIXED_H);
                const double imageW = static_cast<double>(st->pictureImage->GetWidth());
                const double imageH = static_cast<double>(st->pictureImage->GetHeight());
                const double scale = std::min(availW / imageW, availH / imageH);
                const int drawW = std::max(1, static_cast<int>(imageW * scale));
                const int drawH = std::max(1, static_cast<int>(imageH * scale));
                const int drawX = rc.left;
                const int drawY = rc.top;
                Gdiplus::Graphics graphics(dc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.DrawImage(st->pictureImage, drawX, drawY, drawW, drawH);
            } else if (st && !st->pictureStatus.empty()) {
                HGDIOBJ oldFont = st->ctx.uiFont ? SelectObject(dc, st->ctx.uiFont) : nullptr;
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(80, 80, 80));
                RECT textRc = rc;
                InflateRect(&textRc, -S(hwnd, 8), -S(hwnd, 8));
                DrawTextW(dc, st->pictureStatus.c_str(), -1, &textRc,
                          DT_CENTER | DT_VCENTER | DT_WORDBREAK);
                if (oldFont) SelectObject(dc, oldFont);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEWHEEL:
            if (st) {
                const int delta = GET_WHEEL_DELTA_WPARAM(wp);
                scrollPictureViewport(st, st->pictureScrollX,
                                      st->pictureScrollY - (delta / WHEEL_DELTA) * S(hwnd, LEFT_SCROLL_STEP * 3));
                return 0;
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, pictureViewProc, subclassId);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void releasePicturePopupImage(PicturePopupState* popup) {
    if (!popup) return;
    delete popup->image;
    popup->image = nullptr;
    if (popup->stream) {
        popup->stream->Release();
        popup->stream = nullptr;
    }
}

bool ensurePopupGdiplus(PicturePopupState* popup) {
    if (!popup) return false;
    if (popup->gdiplusReady) return true;
    Gdiplus::GdiplusStartupInput input;
    popup->gdiplusReady = Gdiplus::GdiplusStartup(&popup->gdiplusToken, &input, nullptr) == Gdiplus::Ok;
    return popup->gdiplusReady;
}

bool loadPicturePopupImage(PicturePopupState* popup, const std::vector<unsigned char>& bytes,
                           std::wstring& error) {
    if (!popup) return false;
    releasePicturePopupImage(popup);
    if (bytes.empty()) {
        popup->status.clear();
        return true;
    }
    if (!ensurePopupGdiplus(popup)) {
        error = L"GDI+ 初始化失败";
        popup->status = error;
        return false;
    }
    if (!createImageFromBytes(bytes, popup->stream, popup->image, error)) {
        popup->status = error;
        return false;
    }
    popup->status.clear();
    return true;
}

bool clonePicturePopupImage(PicturePopupState* popup, const RegularReportState* st,
                            std::wstring& error) {
    if (!popup || !st || !st->pictureImage) return false;
    if (!ensurePopupGdiplus(popup)) {
        error = L"GDI+ 初始化失败";
        return false;
    }
    Gdiplus::Image* cloned = st->pictureImage->Clone();
    if (!cloned || cloned->GetLastStatus() != Gdiplus::Ok) {
        delete cloned;
        error = L"图像复制失败";
        return false;
    }
    releasePicturePopupImage(popup);
    popup->image = cloned;
    popup->status.clear();
    return true;
}

void paintPicturePopup(HWND hwnd, PicturePopupState* popup, HDC dc) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    FillRect(dc, &rc, GetSysColorBrush(COLOR_WINDOW));
    if (!popup) return;

    if (popup->image) {
        const double imageW = static_cast<double>(popup->image->GetWidth());
        const double imageH = static_cast<double>(popup->image->GetHeight());
        const int clientW = std::max(1, static_cast<int>(rc.right - rc.left));
        const int clientH = std::max(1, static_cast<int>(rc.bottom - rc.top));
        const double scale = std::min(clientW / imageW, clientH / imageH);
        const int drawW = std::max(1, static_cast<int>(imageW * scale));
        const int drawH = std::max(1, static_cast<int>(imageH * scale));
        Gdiplus::Graphics graphics(dc);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.DrawImage(popup->image, rc.left, rc.top, drawW, drawH);
        return;
    }

    if (!popup->status.empty() || popup->loading) {
        const std::wstring text = popup->status.empty() ? L"正在加载图像..." : popup->status;
        HGDIOBJ oldFont = popup->font ? SelectObject(dc, popup->font) : nullptr;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(80, 80, 80));
        RECT textRc = rc;
        InflateRect(&textRc, -12, -12);
        DrawTextW(dc, text.c_str(), -1, &textRc, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        if (oldFont) SelectObject(dc, oldFont);
    }
}

void savePicturePopupSize(HWND hwnd) {
    if (!hwnd || IsIconic(hwnd)) return;
    RECT rc{};
    if (!GetWindowRect(hwnd, &rc)) return;
    const int w = static_cast<int>(rc.right - rc.left);
    const int h = static_cast<int>(rc.bottom - rc.top);
    if (w >= PICTURE_POPUP_MIN_W && h >= PICTURE_POPUP_MIN_H) {
        search::save_module_int(L"RegularReport", L"PicturePopupWidth", w);
        search::save_module_int(L"RegularReport", L"PicturePopupHeight", h);
    }
}

LRESULT CALLBACK picturePopupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* popup = reinterpret_cast<PicturePopupState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            popup = reinterpret_cast<PicturePopupState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(popup));
            if (popup) popup->hwnd = hwnd;
            return TRUE;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const int w = std::max(1, static_cast<int>(rc.right - rc.left));
            const int h = std::max(1, static_cast<int>(rc.bottom - rc.top));
            HDC memDc = CreateCompatibleDC(dc);
            HBITMAP memBmp = memDc ? CreateCompatibleBitmap(dc, w, h) : nullptr;
            if (memDc && memBmp) {
                HGDIOBJ oldBmp = SelectObject(memDc, memBmp);
                paintPicturePopup(hwnd, popup, memDc);
                BitBlt(dc, 0, 0, w, h, memDc, 0, 0, SRCCOPY);
                SelectObject(memDc, oldBmp);
                DeleteObject(memBmp);
                DeleteDC(memDc);
            } else {
                paintPicturePopup(hwnd, popup, dc);
                if (memBmp) DeleteObject(memBmp);
                if (memDc) DeleteDC(memDc);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_EXITSIZEMOVE:
            savePicturePopupSize(hwnd);
            return 0;
        case WM_POPUP_PICTURE_LOADED: {
            std::unique_ptr<PicturePopupLoadResult> result(reinterpret_cast<PicturePopupLoadResult*>(lp));
            if (!popup || !result || result->generation != popup->generation) return 0;
            popup->loading = false;
            if (!result->ok) {
                releasePicturePopupImage(popup);
                popup->status = L"图像查询失败：" + search::utf8_to_wide(result->error);
            } else {
                std::wstring error;
                if (!loadPicturePopupImage(popup, result->picture, error) && !error.empty()) {
                    popup->status = error;
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_CLOSE:
            savePicturePopupSize(hwnd);
            DestroyWindow(hwnd);
            return 0;
        case WM_NCDESTROY:
            if (popup) {
                if (popup->owner && popup->owner->picturePopup == hwnd) {
                    popup->owner->picturePopup = nullptr;
                }
                ++popup->generation;
                releasePicturePopupImage(popup);
                if (popup->gdiplusReady) {
                    Gdiplus::GdiplusShutdown(popup->gdiplusToken);
                    popup->gdiplusReady = false;
                    popup->gdiplusToken = 0;
                }
                delete popup;
            }
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void registerPicturePopupClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = picturePopupProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP),
                                               IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = PICTURE_POPUP_CLASS;
    const ATOM atom = RegisterClassExW(&wc);
    registered = atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

int currentReportIndex(const RegularReportState* st) {
    if (!st) return -1;
    if (st->reportList) {
        const int selected = ListView_GetNextItem(st->reportList, -1, LVNI_SELECTED);
        if (selected >= 0 && selected < static_cast<int>(st->reportRows.size())) {
            return selected;
        }
    }
    if (st->selectedReportIndex >= 0 && st->selectedReportIndex < static_cast<int>(st->reportRows.size())) {
        return st->selectedReportIndex;
    }
    return -1;
}

std::wstring trimWide(std::wstring value) {
    auto notSpace = [](wchar_t ch) { return std::iswspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::wstring reportListCellText(HWND list, int row, int col) {
    if (!list || row < 0 || col < 0) return L"";
    wchar_t text[256]{};
    ListView_GetItemText(list, row, col, text, static_cast<int>(std::size(text)));
    return trimWide(text);
}

std::wstring picturePopupTitle(const RegularReportState* st, int selected) {
    std::wstring sampleNo = reportListCellText(st ? st->reportList : nullptr, selected, 1);
    std::wstring patientName = reportListCellText(st ? st->reportList : nullptr, selected, 2);
    if (sampleNo.empty() && st && selected >= 0 && selected < static_cast<int>(st->reportRows.size())) {
        sampleNo = search::utf8_to_wide(search::trim(st->reportRows[static_cast<size_t>(selected)].oper_no));
    }
    if (patientName.empty() && st && selected >= 0 && selected < static_cast<int>(st->reportRows.size())) {
        patientName = search::utf8_to_wide(search::trim(st->reportRows[static_cast<size_t>(selected)].name));
    }

    std::wstring title = L"结果图";
    if (!sampleNo.empty() || !patientName.empty()) {
        title += L" - ";
        title += sampleNo;
        if (!sampleNo.empty() && !patientName.empty()) {
            title += L" ";
        }
        title += patientName;
    }
    return title;
}

void startPicturePopupQuery(PicturePopupState* popup, const std::string& connection,
                            const std::string& repNo) {
    if (!popup || !popup->hwnd) return;
    popup->loading = true;
    popup->status = L"正在加载图像...";
    releasePicturePopupImage(popup);
    InvalidateRect(popup->hwnd, nullptr, FALSE);
    const HWND hwnd = popup->hwnd;
    const int generation = ++popup->generation;
    std::thread([hwnd, connection, repNo, generation]() {
        auto* result = new PicturePopupLoadResult;
        result->generation = generation;
        result->ok = search::query_report_picture(connection, repNo, result->picture, result->error);
        if (!PostMessageW(hwnd, WM_POPUP_PICTURE_LOADED, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

void clearPicturePopup(PicturePopupState* popup, const std::wstring& title,
                       const std::wstring& status) {
    if (!popup || !popup->hwnd) return;
    ++popup->generation;
    popup->repNo.clear();
    popup->loading = false;
    releasePicturePopupImage(popup);
    popup->status = status;
    SetWindowTextW(popup->hwnd, title.c_str());
    InvalidateRect(popup->hwnd, nullptr, FALSE);
}

void refreshPicturePopupForSelection(RegularReportState* st, int selected) {
    if (!st || !IsWindow(st->picturePopup)) return;
    auto* popup = reinterpret_cast<PicturePopupState*>(GetWindowLongPtrW(st->picturePopup, GWLP_USERDATA));
    if (!popup) return;

    const std::wstring title = picturePopupTitle(st, selected);
    SetWindowTextW(st->picturePopup, title.c_str());

    if (selected < 0 || selected >= static_cast<int>(st->reportRows.size())) {
        clearPicturePopup(popup, L"结果图", L"请先选择一条报告记录。");
        return;
    }

    const std::string repNo = search::trim(st->reportRows[static_cast<size_t>(selected)].rep_no);
    if (repNo.empty()) {
        clearPicturePopup(popup, title, L"当前报告没有验单号，无法查询图像。");
        return;
    }
    if (popup->repNo == repNo && (popup->loading || popup->image || !popup->status.empty())) {
        return;
    }

    std::string connection = st->reportConnectionString;
    if (connection.empty()) {
        connection = search::wide_to_utf8(search::build_connection_string_w(st->ctx.dbSettings));
    }
    if (connection.empty()) {
        clearPicturePopup(popup, title, L"请先在“设置”中填写数据库连接信息。");
        return;
    }

    popup->repNo = repNo;
    std::wstring cloneError;
    if (st->pictureImage && st->pictureRepNo == repNo &&
        clonePicturePopupImage(popup, st, cloneError)) {
        popup->loading = false;
        InvalidateRect(st->picturePopup, nullptr, FALSE);
        return;
    }
    startPicturePopupQuery(popup, connection, repNo);
}

void openPicturePopupForSelection(RegularReportState* st) {
    const int selected = currentReportIndex(st);
    if (!st || selected < 0) {
        MessageBoxW(st ? st->hwnd : nullptr, L"请先选择一条报告记录。", L"常规报告", MB_ICONINFORMATION);
        return;
    }
    const auto& row = st->reportRows[static_cast<size_t>(selected)];
    const std::string repNo = search::trim(row.rep_no);
    if (repNo.empty()) {
        MessageBoxW(st->hwnd, L"当前报告没有验单号，无法查询图像。", L"常规报告", MB_ICONINFORMATION);
        return;
    }

    std::string connection = st->reportConnectionString;
    if (connection.empty()) {
        connection = search::wide_to_utf8(search::build_connection_string_w(st->ctx.dbSettings));
    }
    if (connection.empty()) {
        MessageBoxW(st->hwnd, L"请先在“设置”中填写数据库连接信息。", L"缺少数据库设置", MB_ICONWARNING);
        return;
    }

    registerPicturePopupClass(st->ctx.instance);
    if (IsWindow(st->picturePopup)) {
        ShowWindow(st->picturePopup, SW_SHOWNORMAL);
        SetForegroundWindow(st->picturePopup);
        refreshPicturePopupForSelection(st, selected);
        return;
    }

    auto* popup = new PicturePopupState;
    popup->owner = st;
    popup->font = st->ctx.uiFont;
    popup->repNo = repNo;
    popup->status = L"正在加载图像...";

    RECT ownerRc{};
    GetWindowRect(st->hwnd, &ownerRc);
    const int popupW = std::max(S(st->hwnd, PICTURE_POPUP_MIN_W),
                                search::load_module_int(L"RegularReport", L"PicturePopupWidth",
                                                        S(st->hwnd, PICTURE_POPUP_DEFAULT_W)));
    const int popupH = std::max(S(st->hwnd, PICTURE_POPUP_MIN_H),
                                search::load_module_int(L"RegularReport", L"PicturePopupHeight",
                                                        S(st->hwnd, PICTURE_POPUP_DEFAULT_H)));
    const int x = static_cast<int>(ownerRc.left) +
                  std::max(0, static_cast<int>(ownerRc.right - ownerRc.left - popupW) / 2);
    const int y = static_cast<int>(ownerRc.top) +
                  std::max(0, static_cast<int>(ownerRc.bottom - ownerRc.top - popupH) / 2);
    const std::wstring title = picturePopupTitle(st, selected);

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, PICTURE_POPUP_CLASS, title.c_str(),
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                x, y, popupW, popupH, st->hwnd, nullptr, st->ctx.instance, popup);
    if (!hwnd) {
        delete popup;
        MessageBoxW(st->hwnd, L"结果图窗口创建失败。", L"常规报告", MB_ICONERROR);
        return;
    }
    st->picturePopup = hwnd;
    SetWindowTextW(hwnd, title.c_str());

    std::wstring cloneError;
    if (st->pictureImage && st->pictureRepNo == repNo &&
        clonePicturePopupImage(popup, st, cloneError)) {
        popup->loading = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    startPicturePopupQuery(popup, connection, repNo);
}

void updatePictureViewport(RegularReportState* st) {
    if (!st || !st->pictureViewport || !st->pictureView) return;

    RECT rc{};
    GetClientRect(st->pictureViewport, &rc);
    const int pageW = std::max(1, static_cast<int>(rc.right - rc.left));
    const int pageH = std::max(1, static_cast<int>(rc.bottom - rc.top));
    const int contentW = S(st->pictureViewport, PICTURE_FIXED_W);
    const int contentH = S(st->pictureViewport, PICTURE_FIXED_H);
    st->pictureScrollX = std::clamp(st->pictureScrollX, 0, std::max(0, contentW - pageW));
    st->pictureScrollY = std::clamp(st->pictureScrollY, 0, std::max(0, contentH - pageH));

    MoveWindow(st->pictureView, -st->pictureScrollX, -st->pictureScrollY, contentW, contentH, TRUE);

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = std::max(0, contentW - 1);
    si.nPage = static_cast<UINT>(pageW);
    si.nPos = st->pictureScrollX;
    if (st->pictureHScroll) SetScrollInfo(st->pictureHScroll, SB_CTL, &si, TRUE);

    si.nMax = std::max(0, contentH - 1);
    si.nPage = static_cast<UINT>(pageH);
    si.nPos = st->pictureScrollY;
    if (st->pictureVScroll) SetScrollInfo(st->pictureVScroll, SB_CTL, &si, TRUE);

    const bool showPicturePage = st->middleTab && TabCtrl_GetCurSel(st->middleTab) == 1 &&
                                 IsWindowVisible(st->pictureViewport);
    if (st->pictureHScroll) ShowWindow(st->pictureHScroll, showPicturePage && contentW > pageW ? SW_SHOW : SW_HIDE);
    if (st->pictureVScroll) ShowWindow(st->pictureVScroll, showPicturePage && contentH > pageH ? SW_SHOW : SW_HIDE);
}

void scrollPictureViewport(RegularReportState* st, int targetX, int targetY) {
    if (!st || !st->pictureViewport) return;
    st->pictureScrollX = targetX;
    st->pictureScrollY = targetY;
    updatePictureViewport(st);
}

LRESULT CALLBACK pictureViewportProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                     UINT_PTR subclassId, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_SIZE:
            updatePictureViewport(st);
            return 0;
        case WM_HSCROLL: {
            if (!st) break;
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_HORZ, &si);
            int target = st->pictureScrollX;
            switch (LOWORD(wp)) {
                case SB_LINELEFT: target -= S(hwnd, LEFT_SCROLL_STEP); break;
                case SB_LINERIGHT: target += S(hwnd, LEFT_SCROLL_STEP); break;
                case SB_PAGELEFT: target -= static_cast<int>(si.nPage); break;
                case SB_PAGERIGHT: target += static_cast<int>(si.nPage); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: target = si.nTrackPos; break;
                case SB_LEFT: target = 0; break;
                case SB_RIGHT: target = si.nMax; break;
                default: return 0;
            }
            scrollPictureViewport(st, target, st->pictureScrollY);
            return 0;
        }
        case WM_VSCROLL: {
            if (!st) break;
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int target = st->pictureScrollY;
            switch (LOWORD(wp)) {
                case SB_LINEUP: target -= S(hwnd, LEFT_SCROLL_STEP); break;
                case SB_LINEDOWN: target += S(hwnd, LEFT_SCROLL_STEP); break;
                case SB_PAGEUP: target -= static_cast<int>(si.nPage); break;
                case SB_PAGEDOWN: target += static_cast<int>(si.nPage); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: target = si.nTrackPos; break;
                case SB_TOP: target = 0; break;
                case SB_BOTTOM: target = si.nMax; break;
                default: return 0;
            }
            scrollPictureViewport(st, st->pictureScrollX, target);
            return 0;
        }
        case WM_MOUSEWHEEL:
            if (st) {
                const int delta = GET_WHEEL_DELTA_WPARAM(wp);
                scrollPictureViewport(st, st->pictureScrollX,
                                      st->pictureScrollY - (delta / WHEEL_DELTA) * S(hwnd, LEFT_SCROLL_STEP * 3));
                return 0;
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, pictureViewportProc, subclassId);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

int scrollTargetFromCode(HWND hwnd, int code, const SCROLLINFO& si, int current) {
    switch (code) {
        case SB_LINELEFT:
            return current - S(hwnd, LEFT_SCROLL_STEP);
        case SB_LINERIGHT:
            return current + S(hwnd, LEFT_SCROLL_STEP);
        case SB_PAGELEFT:
            return current - static_cast<int>(si.nPage);
        case SB_PAGERIGHT:
            return current + static_cast<int>(si.nPage);
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            return si.nTrackPos;
        case SB_LEFT:
            return 0;
        case SB_RIGHT:
            return si.nMax;
        default:
            return current;
    }
}

LRESULT CALLBACK middlePanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR subclassId, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (st && nm->idFrom == IDC_MIDDLE_TAB && nm->code == TCN_SELCHANGE) {
                finishResultEdit(st, false);
                showMiddleResultPage(st);
                return 0;
            }
            if (nm->idFrom == IDC_RESULT_LIST && nm->code == NM_CUSTOMDRAW) {
                auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const int row = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (customDrawListSelection(cd, st ? st->resultList : nullptr, row)) {
                        return CDRF_NOTIFYSUBITEMDRAW;
                    }
                    return CDRF_NOTIFYSUBITEMDRAW;
                }
                if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                    const int row = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (customDrawListSelection(cd, st ? st->resultList : nullptr, row)) {
                        return CDRF_NEWFONT;
                    }
                    cd->clrTextBk = cd->iSubItem == RESULT_VALUE_COL ? RGB(0xFF, 0xFF, 0xFF)
                                                                      : RGB(0xF0, 0xF0, 0xF0);
                    const auto index = static_cast<size_t>(cd->nmcd.dwItemSpec);
                    if (st && index < st->resultRows.size()) {
                        const COLORREF color = resultTextColor(st->resultRows[index]);
                        if (color != CLR_INVALID) {
                            cd->clrText = color;
                        }
                    }
                    return CDRF_NEWFONT;
                }
            }
            if (st && nm->idFrom == IDC_RESULT_LIST && nm->code == NM_KILLFOCUS) {
                redrawSelectedListRow(st->resultList);
                return 0;
            }
            break;
        }
        case WM_HSCROLL:
            if (st && reinterpret_cast<HWND>(lp) == st->pictureHScroll) {
                SCROLLINFO si{};
                si.cbSize = sizeof(si);
                si.fMask = SIF_ALL;
                GetScrollInfo(st->pictureHScroll, SB_CTL, &si);
                const int target = scrollTargetFromCode(hwnd, LOWORD(wp), si, st->pictureScrollX);
                scrollPictureViewport(st, target, st->pictureScrollY);
                return 0;
            }
            break;
        case WM_VSCROLL:
            if (st && reinterpret_cast<HWND>(lp) == st->pictureVScroll) {
                SCROLLINFO si{};
                si.cbSize = sizeof(si);
                si.fMask = SIF_ALL;
                GetScrollInfo(st->pictureVScroll, SB_CTL, &si);
                const int target = scrollTargetFromCode(hwnd, LOWORD(wp), si, st->pictureScrollY);
                scrollPictureViewport(st, st->pictureScrollX, target);
                return 0;
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, middlePanelProc, subclassId);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK rightPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR subclassId, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            FillRect(dc, &client, st && st->panelBrush ? st->panelBrush : GetSysColorBrush(COLOR_BTNFACE));
            if (st) {
                const std::wstring summaryLine1 = rightSummaryLine1(st);
                const RightHeaderLayout header = rightHeaderLayout(hwnd, st->ctx.uiFont, client.right - client.left,
                                                                   summaryLine1);
                HGDIOBJ oldFont = st->ctx.uiFont ? SelectObject(dc, st->ctx.uiFont) : nullptr;
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(0, 0, 0xCC));
                RECT line1 = header.line1;
                DrawTextW(dc, summaryLine1.c_str(), -1, &line1, DT_WORDBREAK | DT_CENTER | DT_VCENTER);

                FillRect(dc, &header.line2,
                         st->blackBrush ? st->blackBrush : reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                SetTextColor(dc, RGB(0xFF, 0xFF, 0));
                RECT line2 = header.line2;
                DrawTextW(dc, RIGHT_SUMMARY_LINE2, -1, &line2, DT_WORDBREAK | DT_CENTER | DT_VCENTER);
                if (oldFont) SelectObject(dc, oldFont);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (st && id == IDC_REPORT_FIRST_BUTTON) {
                selectReportRow(st, 0);
                return 0;
            }
            if (st && id == IDC_REPORT_LAST_BUTTON) {
                selectReportRow(st, static_cast<int>(st->reportRows.size()) - 1);
                return 0;
            }
            if (st && id == IDC_REPORT_DATE_TODAY_BUTTON) {
                setInspectDateAndQuery(st, todayDate(), true);
                return 0;
            }
            if (st && id == IDC_REPORT_DATE_PREV_BUTTON) {
                setInspectDateAndQuery(st, addDays(datePickerSystemTime(st->inspectDatePicker), -1));
                return 0;
            }
            if (st && id == IDC_REPORT_DATE_NEXT_BUTTON) {
                setInspectDateAndQuery(st, addDays(datePickerSystemTime(st->inspectDatePicker), 1));
                return 0;
            }
            if (st && id == IDC_REPORT_AUTO_REFRESH_CHECK && HIWORD(wp) == BN_CLICKED) {
                updateAutoRefreshTimer(st);
                return 0;
            }
            if (st && id == IDC_REPORT_AUTO_REFRESH_SECONDS && HIWORD(wp) == EN_CHANGE) {
                updateAutoRefreshTimer(st);
                return 0;
            }
            break;
        }
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (st && nm->idFrom == IDC_RIGHT_TAB && nm->code == TCN_SELCHANGE) {
                showRightInfoPage(st);
                return 0;
            }
            if (st && nm->idFrom == IDC_REPORT_LIST && nm->code == LVN_ITEMCHANGED) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED) &&
                    !(lv->uOldState & LVIS_SELECTED)) {
                    if (st->suppressReportSelectionQuery) {
                        return 0;
                    }
                    querySelectedResults(st, lv->iItem);
                    return 0;
                }
            }
            if (st && nm->idFrom == IDC_REPORT_LIST && nm->code == LVN_COLUMNCLICK) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                sortReportRowsByColumn(st, lv->iSubItem);
                return 0;
            }
            if (st && nm->idFrom == IDC_REPORT_LIST && nm->code == NM_RCLICK) {
                showReportContextMenu(st, reinterpret_cast<NMITEMACTIVATE*>(lp));
                return 0;
            }
            if (st && nm->idFrom == IDC_REPORT_LIST && nm->code == NM_KILLFOCUS) {
                redrawSelectedListRow(st->reportList);
                return 0;
            }
            if (nm->idFrom == IDC_REPORT_LIST && nm->code == NM_CUSTOMDRAW) {
                auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const int row = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (customDrawListSelection(cd, st ? st->reportList : nullptr, row)) {
                        return CDRF_NOTIFYSUBITEMDRAW;
                    }
                    return CDRF_NOTIFYSUBITEMDRAW;
                }
                if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                    const int row = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (!customDrawListSelection(cd, st ? st->reportList : nullptr, row)) {
                        cd->clrTextBk = cd->iSubItem == RIGHT_REPORT_PRINT_COL
                                            ? reportPrintCellColor(st, row)
                                            : reportRowColor(st, row);
                        cd->clrText = st && row >= 0 && row < static_cast<int>(st->reportRows.size()) &&
                                              search::trim(st->reportRows[static_cast<size_t>(row)].emergency_flag) == "1"
                                          ? RGB(0xE6, 0, 0)
                                          : RGB(0, 0, 0);
                    }
                    return CDRF_NEWFONT;
                }
            }
            break;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, rightPanelProc, subclassId);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK bottomPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR subclassId, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_COMMAND:
            if (st && LOWORD(wp) == IDC_BOTTOM_MACHINE_1) {
                applyQuickMachine(st, 0);
                return 0;
            }
            if (st && LOWORD(wp) == IDC_BOTTOM_MACHINE_2) {
                applyQuickMachine(st, 1);
                return 0;
            }
            if (st && LOWORD(wp) == IDC_BOTTOM_MACHINE_3) {
                applyQuickMachine(st, 2);
                return 0;
            }
            if (st && LOWORD(wp) == IDC_BOTTOM_REFRESH) {
                if (search::trim(st->selectedMachineCode).empty()) {
                    MessageBoxW(st->hwnd, L"请先选择检验仪器。", L"常规报告", MB_ICONWARNING);
                } else {
                    runReportQuery(st, true);
                }
                return 0;
            }
            if (st && LOWORD(wp) == IDC_BOTTOM_PREV_REPORT) {
                selectAdjacentReportRow(st, -1);
                return 0;
            }
            if (st && LOWORD(wp) == IDC_BOTTOM_NEXT_REPORT) {
                selectAdjacentReportRow(st, 1);
                return 0;
            }
            if (st && LOWORD(wp) == IDC_BOTTOM_GRAPH) {
                openPicturePopupForSelection(st);
                return 0;
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, bottomPanelProc, subclassId);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void addColumns(HWND list, const ColumnDef* columns, int count, HWND scaleHost) {
    for (int i = 0; i < count; ++i) {
        search::add_list_column(list, columns[i].index, columns[i].title, S(scaleHost, columns[i].width));
    }
}

void insertTabs(HWND tab, const wchar_t* const* labels, int count) {
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    for (int i = 0; i < count; ++i) {
        item.pszText = const_cast<wchar_t*>(labels[i]);
        TabCtrl_InsertItem(tab, i, &item);
    }
    TabCtrl_SetCurSel(tab, 0);
}

void setCell(HWND list, int row, int col, const wchar_t* text) {
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(text));
}

void setCell(HWND list, int row, int col, const std::string& text) {
    const auto wide = search::utf8_to_wide(text);
    setCell(list, row, col, wide.c_str());
}

void setCellIfChanged(HWND list, int row, int col, const std::wstring& text) {
    wchar_t current[2048]{};
    ListView_GetItemText(list, row, col, current, static_cast<int>(std::size(current)));
    if (text != current) {
        setCell(list, row, col, text.c_str());
    }
}

std::string referenceRange(const search::ResultRow& row) {
    const std::string down = search::trim(row.downbound);
    const std::string up = search::trim(row.upbound);
    if (!down.empty() && !up.empty()) {
        return down + "-" + up;
    }
    return down.empty() ? up : down;
}

const wchar_t* deviationMarker(const search::ResultRow& row) {
    const std::string value = search::trim(row.normal);
    if (value == "1") return L"↑";
    if (value == "5") return L"↓";
    return L"";
}

void insertResultRow(HWND list, int row, const search::ResultRow& data, const std::string& displayGroupName) {
    LVITEMW itemDef{};
    itemDef.mask = LVIF_TEXT;
    itemDef.iItem = row;
    itemDef.pszText = const_cast<wchar_t*>(L"");
    ListView_InsertItem(list, &itemDef);
    wchar_t no[16]{};
    wsprintfW(no, L"%d", row + 1);
    setCell(list, row, 1, no);
    setCell(list, row, 2, displayGroupName);
    setCell(list, row, 3, data.item_eng);
    setCell(list, row, 4, data.item_name);
    setCell(list, row, 5, data.result);
    setCell(list, row, 6, deviationMarker(data));
    setCell(list, row, 7, referenceRange(data));
    setCell(list, row, 8, data.unit);
    setCell(list, row, 9, L"");
}

bool reportSampleLess(const search::ReportRow& a, const search::ReportRow& b) {
    const std::string left = search::trim(a.oper_no);
    const std::string right = search::trim(b.oper_no);
    char* leftEnd = nullptr;
    char* rightEnd = nullptr;
    const long leftValue = std::strtol(left.c_str(), &leftEnd, 10);
    const long rightValue = std::strtol(right.c_str(), &rightEnd, 10);
    const bool leftNumeric = leftEnd && *leftEnd == '\0' && !left.empty();
    const bool rightNumeric = rightEnd && *rightEnd == '\0' && !right.empty();
    if (leftNumeric && rightNumeric && leftValue != rightValue) {
        return leftValue < rightValue;
    }
    if (left != right) {
        return left < right;
    }
    return search::trim(a.id) < search::trim(b.id);
}

std::string reportSortValue(const search::ReportRow& row, int column) {
    switch (column) {
        case 0: return row.emergency_flag;
        case 1: return row.oper_no;
        case 2: return row.name;
        case 3: return row.sex;
        case 4: return row.age;
        case 5: return row.order_text;
        case 6: return row.dept_name;
        case 7: return row.bed_code;
        case 8: return row.zymz_print;
        case 9: return row.patient_type;
        case 10: return row.requester;
        case 11: return row.group_name;
        case 12: return row.rep_no;
        case 13: return row.chk_flag;
        case 14: return row.conf;
        case 15: return row.txm_no;
        case 16: return row.group_name;
        case 17: return row.sample_name;
        case 18: return row.note;
        case 19: return row.dean_oper;
        case 20: return row.chk_date;
        case 21: return row.collection_time;
        case 22: return row.inspect_date;
        case 23: return row.rep_time;
        case 24: return row.fee;
        case 25: return row.req_doctor;
        case 26: return row.diag_name;
        case 27: return row.reg_no;
        case 28: return row.create_time;
        case 29: return row.patient_phone;
        default: return row.id;
    }
}

bool parseSortNumber(const std::string& value, double& out) {
    const std::string trimmed = search::trim(value);
    if (trimmed.empty()) return false;
    char* end = nullptr;
    out = std::strtod(trimmed.c_str(), &end);
    return end && *end == '\0';
}

int compareReportSortValue(const search::ReportRow& a, const search::ReportRow& b, int column) {
    const std::string left = search::trim(reportSortValue(a, column));
    const std::string right = search::trim(reportSortValue(b, column));

    double leftNumber = 0.0;
    double rightNumber = 0.0;
    const bool leftNumeric = parseSortNumber(left, leftNumber);
    const bool rightNumeric = parseSortNumber(right, rightNumber);
    if (leftNumeric && rightNumeric && leftNumber != rightNumber) {
        return leftNumber < rightNumber ? -1 : 1;
    }
    if (left != right) {
        return left < right ? -1 : 1;
    }
    const std::string leftId = search::trim(a.id);
    const std::string rightId = search::trim(b.id);
    if (leftId == rightId) return 0;
    return leftId < rightId ? -1 : 1;
}

bool containsId(const std::vector<std::string>& ids, const std::string& id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

int findReportIndexById(const std::vector<search::ReportRow>& rows, const std::string& id) {
    if (id.empty()) return -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        if (search::trim(rows[static_cast<size_t>(i)].id) == id) {
            return i;
        }
    }
    return -1;
}

std::array<std::wstring, REPORT_COLUMN_COUNT> reportDisplayValues(const search::ReportRow& data) {
    auto w = [](const std::string& value) { return search::utf8_to_wide(value); };
    std::array<std::wstring, REPORT_COLUMN_COUNT> values{};
    values[0] = search::trim(data.emergency_flag) == "1" ? L"急" : L"";
    values[1] = w(data.oper_no);
    values[2] = w(data.name);
    values[3] = w(data.sex);
    values[4] = w(data.age);
    values[5] = w(data.order_text);
    values[6] = w(data.dept_name);
    values[7] = w(data.bed_code);
    values[8] = w(search::display_binary_print_flag(data.zymz_print));
    values[9] = w(data.patient_type);
    values[10] = w(data.requester);
    values[11] = w(data.group_name);
    values[12] = w(data.rep_no);
    values[13] = w(data.chk_flag);
    values[14] = w(data.conf);
    values[15] = w(data.txm_no);
    values[16] = w(data.group_name);
    values[17] = w(data.sample_name);
    values[18] = w(data.note);
    values[19] = w(data.dean_oper);
    values[20] = w(slashDateTimeMinute(data.chk_date));
    values[21] = w(slashDateTimeMinute(data.collection_time));
    values[22] = w(slashDate(data.inspect_date));
    values[23] = w(slashDateTimeMinute(data.rep_time));
    values[24] = w(data.fee);
    values[25] = w(data.req_doctor);
    values[26] = w(data.diag_name);
    values[27] = w(data.reg_no);
    values[28] = w(data.create_time);
    values[29] = w(data.patient_phone);
    return values;
}

bool reportDisplayEqual(const search::ReportRow& left, const search::ReportRow& right) {
    return reportDisplayValues(left) == reportDisplayValues(right);
}

void insertReportRow(HWND list, int row, const search::ReportRow& data) {
    const auto values = reportDisplayValues(data);
    LVITEMW itemDef{};
    itemDef.mask = LVIF_TEXT;
    itemDef.iItem = row;
    itemDef.pszText = const_cast<wchar_t*>(values[0].c_str());
    ListView_InsertItem(list, &itemDef);
    for (int col = 1; col < REPORT_COLUMN_COUNT; ++col) {
        setCell(list, row, col, values[static_cast<size_t>(col)].c_str());
    }
}

void updateReportRowCells(HWND list, int row, const search::ReportRow& data) {
    const auto values = reportDisplayValues(data);
    for (int col = 0; col < REPORT_COLUMN_COUNT; ++col) {
        setCellIfChanged(list, row, col, values[static_cast<size_t>(col)]);
    }
}

bool sameReportOrder(const std::vector<search::ReportRow>& oldRows,
                     const std::vector<search::ReportRow>& newRows) {
    if (oldRows.size() != newRows.size()) return false;
    for (size_t i = 0; i < oldRows.size(); ++i) {
        if (search::trim(oldRows[i].id) != search::trim(newRows[i].id)) {
            return false;
        }
    }
    return true;
}

void sortReportRowsForDisplay(RegularReportState* st, std::vector<search::ReportRow>& rows,
                              bool preserveSort) {
    if (preserveSort && st && st->reportSortColumn >= 0) {
        const int column = st->reportSortColumn;
        const bool ascending = st->reportSortAscending;
        std::stable_sort(rows.begin(), rows.end(),
                         [column, ascending](const search::ReportRow& a, const search::ReportRow& b) {
                             const int cmp = compareReportSortValue(a, b, column);
                             return ascending ? cmp < 0 : cmp > 0;
                         });
        return;
    }
    std::stable_sort(rows.begin(), rows.end(), reportSampleLess);
}

void presentReportRows(RegularReportState* st,
                       const std::vector<search::ReportRow>* previousRows = nullptr) {
    if (!st || !st->reportList) return;
    if (previousRows && sameReportOrder(*previousRows, st->reportRows)) {
        for (size_t i = 0; i < st->reportRows.size(); ++i) {
            updateReportRowCells(st->reportList, static_cast<int>(i), st->reportRows[i]);
        }
        InvalidateRect(st->rightPanel, nullptr, TRUE);
        return;
    }
    ListView_DeleteAllItems(st->reportList);
    for (size_t i = 0; i < st->reportRows.size(); ++i) {
        insertReportRow(st->reportList, static_cast<int>(i), st->reportRows[i]);
    }
    InvalidateRect(st->rightPanel, nullptr, TRUE);
}

void sortReportRowsByColumn(RegularReportState* st, int column) {
    if (!st || !st->reportList || st->reportRows.empty() || column < 0) return;

    if (st->reportSortColumn == column) {
        st->reportSortAscending = !st->reportSortAscending;
    } else {
        st->reportSortColumn = column;
        st->reportSortAscending = true;
    }
    const bool ascending = st->reportSortAscending;

    std::string selectedId;
    const int selected = ListView_GetNextItem(st->reportList, -1, LVNI_SELECTED);
    if (selected >= 0 && selected < static_cast<int>(st->reportRows.size())) {
        selectedId = st->reportRows[static_cast<size_t>(selected)].id;
    }

    std::vector<std::string> checkedIds;
    for (int index : checkedReportIndexes(st)) {
        checkedIds.push_back(st->reportRows[static_cast<size_t>(index)].id);
    }

    std::stable_sort(st->reportRows.begin(), st->reportRows.end(),
                     [column, ascending](const search::ReportRow& a, const search::ReportRow& b) {
                         const int cmp = compareReportSortValue(a, b, column);
                         return ascending ? cmp < 0 : cmp > 0;
                     });

    st->suppressReportSelectionQuery = true;
    presentReportRows(st);

    int restoredSelected = -1;
    for (int i = 0; i < static_cast<int>(st->reportRows.size()); ++i) {
        const std::string& id = st->reportRows[static_cast<size_t>(i)].id;
        if (containsId(checkedIds, id)) {
            ListView_SetCheckState(st->reportList, i, TRUE);
        }
        if (!selectedId.empty() && id == selectedId) {
            restoredSelected = i;
        }
    }
    if (restoredSelected >= 0) {
        ListView_SetItemState(st->reportList, restoredSelected, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(st->reportList, restoredSelected, FALSE);
        st->selectedReportIndex = restoredSelected;
        populateLeftPanelFromReport(st, restoredSelected);
    } else {
        st->selectedReportIndex = -1;
        populateLeftPanelFromReport(st, -1);
    }
    st->contextReportIndex = -1;
    st->suppressReportSelectionQuery = false;
}

void populateLeftPanelFromReport(RegularReportState* st, int selected) {
    auto clear = [&]() {
        setControlText(st->groupEdit, "");
        setControlText(st->sampleEdit, "");
        setControlText(st->reportNoEdit, "");
        setControlText(st->operNoEdit, "");
        setComboSingleText(st->patientTypeCombo, "");
        setControlText(st->urgentEdit, "");
        setControlText(st->barcodeEdit, "");
        setControlText(st->regNoEdit, "");
        setControlText(st->patientNameEdit, "");
        setControlText(st->sexEdit, "");
        setControlText(st->ageEdit, "");
        fillAgeUnitCombo(st->ageUnitCombo);
        setControlText(st->bedEdit, "");
        setControlText(st->phoneEdit, "");
        setControlText(st->deptEdit, "");
        setControlText(st->diagEdit, "");
        setControlText(st->reqDoctorEdit, "");
        setControlText(st->feeEdit, "");
        setControlText(st->testerEdit, "");
        setControlText(st->auditEdit, "");
        setControlText(st->noteCodeEdit, "");
        setControlText(st->noteEdit, "");
        clearDatePickerValue(st->applyDatePicker);
        clearDatePickerValue(st->receiveDatePicker);
        clearDatePickerValue(st->machineDatePicker);
        clearDatePickerValue(st->reportDatePicker);
        setControlText(st->collectDateEdit, "");
    };
    if (!st || selected < 0 || selected >= static_cast<int>(st->reportRows.size())) {
        if (st) clear();
        return;
    }

    const auto& row = st->reportRows[static_cast<size_t>(selected)];
    setControlText(st->groupEdit, row.group_name);
    setControlText(st->sampleEdit, row.sample_name);
    setControlText(st->reportNoEdit, row.rep_no);
    setControlText(st->operNoEdit, row.oper_no);
    setComboSingleText(st->patientTypeCombo, row.patient_type);
    setControlText(st->urgentEdit, "");
    setControlText(st->barcodeEdit, row.txm_no);
    setControlText(st->regNoEdit, row.reg_no);

    setControlText(st->patientNameEdit, row.name);
    setControlText(st->sexEdit, row.sex);
    const AgeDisplayParts age = splitAgeDisplayText(row.age);
    setControlText(st->ageEdit, age.value);
    fillAgeUnitCombo(st->ageUnitCombo, age.unit);
    setControlText(st->bedEdit, row.bed_code);
    setControlText(st->phoneEdit, row.patient_phone);
    setControlText(st->deptEdit, row.dept_name);
    setControlText(st->diagEdit, row.diag_name);
    setControlText(st->reqDoctorEdit, row.req_doctor);
    setControlText(st->feeEdit, row.fee);

    setControlText(st->testerEdit, row.requester);
    setControlText(st->auditEdit, row.dean_oper);
    setControlText(st->noteCodeEdit, "");
    setControlText(st->noteEdit, row.note);

    setDatePickerValue(st->applyDatePicker, row.chk_date);
    setDatePickerValue(st->receiveDatePicker, row.collection_time);
    setDatePickerValue(st->machineDatePicker, row.create_time);
    setDatePickerValue(st->reportDatePicker, row.rep_time);
    st->suppressInspectDateQuery = true;
    setDatePickerValue(st->inspectDatePicker, row.inspect_date);
    st->suppressInspectDateQuery = false;
    setControlText(st->collectDateEdit, "");
}

void presentResultRows(RegularReportState* st) {
    if (!st || !st->resultList) return;
    finishResultEdit(st, false);
    ListView_DeleteAllItems(st->resultList);
    std::string previousGroupName;
    for (size_t i = 0; i < st->resultRows.size(); ++i) {
        const std::string groupName = search::trim(st->resultRows[i].group_name);
        const bool sameAsPrevious = i > 0 && !groupName.empty() && groupName == previousGroupName;
        const std::string displayGroupName = sameAsPrevious ? "" : st->resultRows[i].group_name;
        insertResultRow(st->resultList, static_cast<int>(i), st->resultRows[i], displayGroupName);
        previousGroupName = groupName;
    }
}

void querySelectedPicture(RegularReportState* st, int selected) {
    if (!st || !st->middleTab || TabCtrl_GetCurSel(st->middleTab) != 1) {
        return;
    }
    if (!st || selected < 0 || selected >= static_cast<int>(st->reportRows.size())) {
        if (st) {
            st->pictureQueryLoading = false;
            ++st->pictureQueryGeneration;
            st->pictureRepNo.clear();
            clearPictureView(st, L"");
        }
        return;
    }

    const std::string repNo = st->reportRows[static_cast<size_t>(selected)].rep_no;
    if (search::trim(repNo).empty()) {
        st->pictureQueryLoading = false;
        ++st->pictureQueryGeneration;
        st->pictureRepNo.clear();
        clearPictureView(st, L"");
        return;
    }

    if (st->pictureRepNo == repNo) {
        return;
    }
    if (st->pictureQueryLoading && st->selectedReportIndex == selected) {
        return;
    }
    st->pictureQueryLoading = true;
    st->pictureRepNo = repNo;
    clearPictureView(st, L"");
    const int generation = ++st->pictureQueryGeneration;
    const HWND hwnd = st->hwnd;
    const std::string connection = st->reportConnectionString;
    std::thread([hwnd, connection, repNo, generation]() {
        auto* result = new PictureLoadResult;
        result->generation = generation;
        result->ok = search::query_report_picture(connection, repNo, result->picture, result->error);
        if (!PostMessageW(hwnd, WM_REGULAR_PICTURE_LOADED, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

void querySelectedResults(RegularReportState* st, int selected) {
    if (!st || selected < 0 || selected >= static_cast<int>(st->reportRows.size())) {
        if (st) finishResultEdit(st, false);
        if (st && st->resultList) ListView_DeleteAllItems(st->resultList);
        if (st) {
            populateLeftPanelFromReport(st, -1);
            st->resultRows.clear();
            st->selectedReportIndex = -1;
            st->contextReportIndex = -1;
            st->resultQueryLoading = false;
            st->pictureQueryLoading = false;
            st->pictureRepNo.clear();
            ++st->pictureQueryGeneration;
            clearPictureView(st, L"");
            refreshPicturePopupForSelection(st, -1);
        }
        return;
    }
    populateLeftPanelFromReport(st, selected);
    st->pictureQueryLoading = false;
    st->pictureRepNo.clear();
    ++st->pictureQueryGeneration;
    clearPictureView(st, L"");
    querySelectedPicture(st, selected);
    refreshPicturePopupForSelection(st, selected);
    if (st->resultQueryLoading && st->selectedReportIndex == selected) return;
    finishResultEdit(st, false);
    ListView_DeleteAllItems(st->resultList);
    st->resultRows.clear();
    st->selectedReportIndex = selected;
    st->resultQueryLoading = true;
    SetWindowTextW(st->status, L"正在查询项目明细...");
    const int generation = ++st->resultQueryGeneration;
    const HWND hwnd = st->hwnd;
    const std::string connection = st->reportConnectionString;
    const std::string repNo = st->reportRows[static_cast<size_t>(selected)].rep_no;
    std::thread([hwnd, connection, repNo, generation]() {
        auto* result = new ResultLoadResult;
        result->generation = generation;
        result->ok = search::load_result_rows(connection, repNo, result->rows, result->error);
        if (!PostMessageW(hwnd, WM_REGULAR_RESULTS_LOADED, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

void finishPictureQuery(RegularReportState* st, HWND hwnd, std::unique_ptr<PictureLoadResult> result) {
    if (!st || !result || result->generation != st->pictureQueryGeneration) return;
    st->pictureQueryLoading = false;
    if (!result->ok) {
        clearPictureView(st, L"图像查询失败：" + search::utf8_to_wide(result->error));
        return;
    }
    std::wstring error;
    if (!loadPictureView(st, result->picture, error) && !error.empty()) {
        clearPictureView(st, error);
    }
    (void)hwnd;
}

void finishResultQuery(RegularReportState* st, HWND hwnd, std::unique_ptr<ResultLoadResult> result) {
    if (!st || !result || result->generation != st->resultQueryGeneration) return;
    st->resultQueryLoading = false;
    if (!result->ok) {
        MessageBoxW(hwnd, search::utf8_to_wide(result->error).c_str(), L"查询项目明细失败", MB_ICONERROR);
        return;
    }
    finishResultEdit(st, false);
    st->resultRows = std::move(result->rows);
    presentResultRows(st);
    const auto status = search::utf8_to_wide(search::make_query_count_status(st->reportRows.size()));
    SetWindowTextW(st->status, status.c_str());
}

void finishReportQuery(RegularReportState* st, HWND hwnd, std::unique_ptr<ReportLoadResult> result) {
    if (!st || !result || result->generation != st->reportQueryGeneration) return;
    st->reportQueryLoading = false;
    if (!result->ok) {
        SetWindowTextW(st->status, L"样本列表查询失败");
        MessageBoxW(hwnd, search::utf8_to_wide(result->error).c_str(), L"查询失败", MB_ICONERROR);
        return;
    }
    const bool preserveState = result->preserveState;
    const std::vector<search::ReportRow> previousRows = st->reportRows;
    std::string selectedId;
    const int selected = st->reportList ? ListView_GetNextItem(st->reportList, -1, LVNI_SELECTED) : -1;
    if (selected >= 0 && selected < static_cast<int>(previousRows.size())) {
        selectedId = search::trim(previousRows[static_cast<size_t>(selected)].id);
    }

    std::vector<std::string> checkedIds;
    if (preserveState) {
        for (int index : checkedReportIndexes(st)) {
            if (index >= 0 && index < static_cast<int>(previousRows.size())) {
                checkedIds.push_back(search::trim(previousRows[static_cast<size_t>(index)].id));
            }
        }
    }
    const int topIndex = preserveState && st->reportList ? ListView_GetTopIndex(st->reportList) : -1;

    sortReportRowsForDisplay(st, result->rows, preserveState);
    st->reportRows = std::move(result->rows);
    st->reportConnectionString = std::move(result->connectionString);
    st->reportQueryDate = std::move(result->queryDate);

    st->suppressReportSelectionQuery = true;
    presentReportRows(st, preserveState ? &previousRows : nullptr);
    int restoredSelected = -1;
    if (preserveState) {
        for (int i = 0; i < static_cast<int>(st->reportRows.size()); ++i) {
            const std::string id = search::trim(st->reportRows[static_cast<size_t>(i)].id);
            if (containsId(checkedIds, id)) {
                ListView_SetCheckState(st->reportList, i, TRUE);
            }
            if (!selectedId.empty() && id == selectedId) {
                restoredSelected = i;
            }
        }
        if (topIndex >= 0 && topIndex < ListView_GetItemCount(st->reportList)) {
            ListView_EnsureVisible(st->reportList, topIndex, FALSE);
        }
    }
    if (restoredSelected >= 0) {
        ListView_SetItemState(st->reportList, restoredSelected, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(st->reportList, restoredSelected, FALSE);
    }
    st->suppressReportSelectionQuery = false;

    if (preserveState) {
        if (restoredSelected >= 0) {
            st->selectedReportIndex = restoredSelected;
            populateLeftPanelFromReport(st, restoredSelected);
            refreshPicturePopupForSelection(st, restoredSelected);
            querySelectedResults(st, restoredSelected);
        } else {
            querySelectedResults(st, -1);
        }
    } else if (!st->reportRows.empty()) {
        ListView_SetItemState(st->reportList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        querySelectedResults(st, 0);
    } else {
        querySelectedResults(st, -1);
    }
    const auto status = search::utf8_to_wide(search::make_query_count_status(st->reportRows.size()));
    SetWindowTextW(st->status, status.c_str());
}

void seedLists(RegularReportState* st) {
    const ColumnDef resultColumns[] = {
        {0, L"", 32}, {1, L"", 44}, {2, L"组合项目", 82}, {3, L"英文", 72},
        {4, L"项目名称", 132}, {5, L"结果", 96}, {6, L"偏", 36},
        {7, L"参考区间", 112}, {8, L"单位", 76}, {9, L"说明", 96},
    };
    addColumns(st->resultList, resultColumns, static_cast<int>(sizeof(resultColumns) / sizeof(resultColumns[0])), st->resultList);

    const ColumnDef reportColumns[] = {
        {0, L"标签", 26}, {1, L"样本号", 58}, {2, L"姓名", 70}, {3, L"性别", 36},
        {4, L"年龄", 66}, {5, L"医嘱内容", 110}, {6, L"科室代码", 78},
        {7, L"床号", 46}, {8, L"打印", 64}, {9, L"病人类型", 84},
        {10, L"检验者", 92}, {11, L"项目名称", 120},
        {12, L"验单号", 96}, {13, L"审核", 58}, {14, L"确认", 58},
        {15, L"条形码", 96}, {16, L"检验仪器", 92}, {17, L"标本", 58},
        {18, L"备注", 120}, {19, L"审核", 82}, {20, L"开单日期", 128},
        {21, L"签收时间", 128}, {22, L"检验日期", 128}, {23, L"报告时间", 128},
        {24, L"费用", 72}, {25, L"医生代号", 82}, {26, L"临床诊断", 150},
        {27, L"病人号", 96}, {28, L"上机时间", 128}, {29, L"电话", 110},
    };
    addColumns(st->reportList, reportColumns, static_cast<int>(sizeof(reportColumns) / sizeof(reportColumns[0])), st->reportList);
}

void createLeftPanel(HWND parent, RegularReportState* st) {
    HWND p = st->leftContent;
    auto add = [&](HWND h) { st->leftControls.push_back(h); return h; };
    auto x = [&](int value) { return S(parent, value); };
    auto y = [&](int value) { return S(parent, value); };
    RECT panelRc{};
    if (st->leftPanel) GetClientRect(st->leftPanel, &panelRc);
    const int scrollW = GetSystemMetrics(SM_CXVSCROLL);
    const float scale = std::max(0.1f, search::dpi_scale_factor(parent));
    const int panelPxW = panelRc.right > panelRc.left
                             ? static_cast<int>(panelRc.right - panelRc.left)
                             : S(parent, LEFT_PANEL_MIN_W);
    const int panelLogicalW = static_cast<int>(
        std::max(0, panelPxW - scrollW) / scale);
    const int groupX = 8;
    const int innerPad = 6;
    const int labelW = leftLabelWidth(parent, st->ctx.uiFont);
    const int groupW = std::max(240, panelLogicalW - groupX - 8);
    const int groupRight = groupX + groupW - innerPad;
    const int inputX = std::max(groupX + innerPad + labelW + 4, groupX + innerPad + 54);
    const int labelX = std::max(groupX + innerPad, inputX - labelW - 4);
    const int fullInputW = std::max(120, groupRight - inputX);
    const int rightEditW = 72;
    const int rightEditX = groupRight - rightEditW;
    const int narrowRightEditW = 48;
    const int narrowRightEditX = groupRight - narrowRightEditW;
    const int rightLabelW = rightLabelWidth(parent, st->ctx.uiFont);
    const int rightLabelX = rightEditX - rightLabelW - 4;
    const int narrowRightLabelX = narrowRightEditX - rightLabelW - 4;
    const int leftShortW = std::max(42, rightLabelX - inputX - 8);
    const int narrowLeftShortW = std::max(42, narrowRightLabelX - inputX - 8);
    const int fontH = fontLogicalHeight(parent, st->ctx.uiFont);
    const int labelH = std::max(24, fontH + 4);
    const int editH = std::max(22, fontH + 8);
    const int buttonH = std::max(26, fontH + 10);
    const int comboItemH = std::max(18, fontH + 6);
    const int rowStep = std::max(34, editH + 10);
    const int groupTopPad = std::max(14, fontH);
    const int groupBottomPad = 4;
    const int groupGap = std::max(6, fontH / 3 + 2);
    auto fitW = [&](int px, int requested, int minW = 20) {
        const int maxW = groupRight - px;
        return std::max(minW, std::min(requested, maxW));
    };
    auto group = [&](const wchar_t* text, int px, int py, int w, int h) {
        RECT rc{x(px), y(py), x(px + w), y(py + h)};
        st->leftGroupFrames.push_back({rc, text});
    };
    auto label = [&](const wchar_t* text, int px, int py, int w, int h = 24,
                     DWORD style = SS_RIGHT | SS_CENTERIMAGE | SS_ENDELLIPSIS) {
        HWND hwnd = makeStatic(p, text, x(px), y(py), x(fitW(px, w, 16)), x(std::max(h, labelH)), style | SS_CENTERIMAGE);
        SetPropW(hwnd, L"RegularLeftLabel", reinterpret_cast<HANDLE>(1));
        add(hwnd);
        return hwnd;
    };
    auto edit = [&](const wchar_t* text, int px, int py, int w, int h = 24, DWORD extra = ES_AUTOHSCROLL) {
        const bool multiline = (extra & ES_MULTILINE) != 0;
        HWND hwnd = makeEdit(p, text, x(px), y(py), x(fitW(px, w, 24)), x(multiline ? std::max(h, editH) : editH), extra | ES_CENTER);
        add(hwnd);
        return hwnd;
    };
    auto button = [&](const wchar_t* text, int px, int py, int w, int h, int id = 0) {
        HWND hwnd = makeButton(p, id, text, x(px), y(py), x(fitW(px, w, 22)), x(std::max(h, buttonH)));
        add(hwnd);
        return hwnd;
    };
    auto combo = [&](const wchar_t* text, int px, int py, int w, int h) {
        HWND combo = makeCombo(p, text, x(px), y(py), x(fitW(px, w, 42)), x(std::max(h, comboItemH * 5)));
        SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), x(comboItemH));
        SendMessageW(combo, CB_SETITEMHEIGHT, 0, x(comboItemH));
        add(combo);
        return combo;
    };
    auto datePicker = [&](int px, int py, int w, int h, const wchar_t* format, const SYSTEMTIME* value, int id = 0) {
        HWND hwnd = makeDatePicker(p, x(px), y(py), x(fitW(px, w, 80)), x(std::max(h, editH)), format, value, id);
        add(hwnd);
        return hwnd;
    };
    const int firstGroupTopPad = std::max(10, fontH * 2 / 3 + 2);
    int groupY = firstGroupTopPad;
    auto rowY = [&](int index) { return groupY + groupTopPad + index * rowStep; };
    auto groupHeight = [&](int rows) {
        return groupTopPad + std::max(0, rows - 1) * rowStep + editH + groupBottomPad;
    };

    const int sampleRows = 7;
    const int sampleH = groupHeight(sampleRows);
    group(L"标本信息", groupX, groupY, groupW, sampleH);
    st->selectedMachineCode.clear();
    st->selectedRoomCode.clear();
    label(L"检验仪器", labelX, rowY(0), labelW);
    const int pickerButtonW = 34;
    const int pickerButtonX = groupRight - pickerButtonW;
    st->machineEdit = edit(L"", inputX, rowY(0) - 2, std::max(80, pickerButtonX - inputX - 6), editH, ES_CENTER | ES_READONLY);
    st->machinePickerButton = button(L"...", pickerButtonX, rowY(0) - 2, pickerButtonW, buttonH, IDC_MACHINE_PICKER_BUTTON);
    label(L"组合项目", labelX, rowY(1), labelW); st->groupEdit = edit(L"", inputX, rowY(1) - 2, fullInputW, editH, ES_CENTER | ES_READONLY);
    label(L"标本", labelX, rowY(2), labelW); st->sampleEdit = edit(L"", inputX, rowY(2) - 2, fullInputW, editH, ES_CENTER | ES_READONLY);
    label(L"检验单号", labelX, rowY(3), labelW); st->reportNoEdit = edit(L"", inputX, rowY(3) - 2, narrowLeftShortW, editH, ES_CENTER | ES_READONLY);
    label(L"样本号", narrowRightLabelX, rowY(3), rightLabelW); st->operNoEdit = edit(L"", narrowRightEditX, rowY(3) - 2, narrowRightEditW, editH, ES_CENTER);
    SetWindowSubclass(st->operNoEdit, sampleInputProc, SAMPLE_INPUT_SUBCLASS, reinterpret_cast<DWORD_PTR>(st));
    label(L"病人类型", labelX, rowY(4), labelW); st->patientTypeCombo = combo(L"", inputX, rowY(4) - 2, narrowLeftShortW, comboItemH * 5);
    label(L"急诊", narrowRightLabelX, rowY(4), rightLabelW); st->urgentEdit = edit(L"", narrowRightEditX, rowY(4) - 2, narrowRightEditW, editH);
    label(L"条形码", labelX, rowY(5), labelW); st->barcodeEdit = edit(L"", inputX, rowY(5) - 2, fullInputW, editH, ES_CENTER);
    label(L"住院号:", labelX, rowY(6), labelW); st->regNoEdit = edit(L"", inputX, rowY(6) - 2, fullInputW, editH);

    groupY += sampleH + groupGap;
    const int patientRows = 6;
    const int patientH = groupHeight(patientRows);
    group(L"病人信息", groupX, groupY, groupW, patientH);
    label(L"姓名", labelX, rowY(0), labelW); st->patientNameEdit = edit(L"", inputX, rowY(0) - 2, narrowLeftShortW + 6, editH);
    label(L"性别", narrowRightLabelX, rowY(0), rightLabelW); st->sexEdit = edit(L"", narrowRightEditX, rowY(0) - 2, narrowRightEditW, editH);
    label(L"年龄", labelX, rowY(1), labelW);
    const int ageUnitW = 52;
    const int ageUnitX = std::max(inputX + 54, narrowRightLabelX - ageUnitW - 4);
    st->ageEdit = edit(L"", inputX, rowY(1) - 2, std::max(48, ageUnitX - inputX - 4), editH);
    st->ageUnitCombo = combo(L"岁", ageUnitX, rowY(1) - 2, ageUnitW, comboItemH * 5);
    fillAgeUnitCombo(st->ageUnitCombo);
    label(L"床号", narrowRightLabelX, rowY(1), rightLabelW); st->bedEdit = edit(L"", narrowRightEditX, rowY(1) - 2, narrowRightEditW, editH);
    label(L"电话", labelX, rowY(2), labelW); st->phoneEdit = edit(L"", inputX, rowY(2) - 2, fullInputW, editH, ES_CENTER);
    label(L"临床科室", labelX, rowY(3), labelW); st->deptEdit = edit(L"", inputX, rowY(3) - 2, fullInputW, editH);
    label(L"临床诊断", labelX, rowY(4), labelW); st->diagEdit = edit(L"", inputX, rowY(4) - 2, fullInputW, editH, ES_CENTER);
    label(L"申请医生", labelX, rowY(5), labelW); st->reqDoctorEdit = edit(L"", inputX, rowY(5) - 2, leftShortW + 6, editH, ES_CENTER);
    label(L"费用", rightLabelX, rowY(5), rightLabelW); st->feeEdit = edit(L"", rightEditX, rowY(5) - 2, rightEditW, editH, ES_CENTER);

    groupY += patientH + groupGap;
    const int orderRows = 8;
    const int orderH = groupHeight(orderRows);
    group(L"验单信息", groupX, groupY, groupW, orderH);
    label(L"检验者", labelX, rowY(0), labelW); st->testerEdit = edit(L"", inputX, rowY(0) - 2, leftShortW + 6, editH, ES_CENTER | ES_READONLY);
    label(L"审核", rightLabelX, rowY(0), rightLabelW); st->auditEdit = edit(L"", rightEditX, rowY(0) - 2, rightEditW, editH, ES_CENTER | ES_READONLY);
    label(L"备注", labelX, rowY(1), labelW); st->noteCodeEdit = edit(L"", inputX, rowY(1) - 2, 42, editH); st->noteEdit = edit(L"", inputX + 48, rowY(1) - 2, std::max(80, groupRight - inputX - 48), editH);
    SYSTEMTIME inspectDate = todayDate();
    label(L"申请日期", labelX, rowY(2), labelW); st->applyDatePicker = datePicker(inputX, rowY(2) - 2, fullInputW, editH, L"yyyy-MM-dd HH:mm", nullptr);
    label(L"签收时间", labelX, rowY(3), labelW); st->receiveDatePicker = datePicker(inputX, rowY(3) - 2, fullInputW, editH, L"yyyy-MM-dd HH:mm:ss", nullptr);
    label(L"上机时间", labelX, rowY(4), labelW); st->machineDatePicker = datePicker(inputX, rowY(4) - 2, fullInputW, editH, L"yyyy-MM-dd HH:mm", nullptr);
    label(L"报告时间", labelX, rowY(5), labelW); st->reportDatePicker = datePicker(inputX, rowY(5) - 2, fullInputW, editH, L"yyyy-MM-dd HH:mm", nullptr);
    setControlsEnabled(false, {
        st->applyDatePicker,
        st->receiveDatePicker,
        st->machineDatePicker,
        st->reportDatePicker,
    });
    label(L"检验日期", labelX, rowY(6), labelW);
    st->inspectDatePicker = datePicker(inputX, rowY(6) - 2, fullInputW, editH, L"yyyy/M/d", &inspectDate, IDC_INSPECT_DATE);
    label(L"采集日期", labelX, rowY(7), labelW); st->collectDateEdit = edit(L"", inputX, rowY(7) - 2, fullInputW, editH);

    st->leftContentHeight = std::max(LEFT_CONTENT_HEIGHT, groupY + orderH + groupGap);
    registerLeftTabControls(st, {
        st->operNoEdit,
        st->patientTypeCombo,
        st->urgentEdit,
        st->barcodeEdit,
        st->regNoEdit,
        st->patientNameEdit,
        st->sexEdit,
        st->ageEdit,
        st->ageUnitCombo,
        st->bedEdit,
        st->phoneEdit,
        st->deptEdit,
        st->diagEdit,
        st->reqDoctorEdit,
        st->feeEdit,
        st->noteCodeEdit,
        st->noteEdit,
        st->inspectDatePicker,
        st->collectDateEdit,
    });
}

void createMiddlePanel(HWND parent, RegularReportState* st) {
    HWND p = st->middlePanel;
    auto addResult = [&](HWND h) { st->middleResultControls.push_back(h); return h; };
    auto addPicture = [&](HWND h) { st->middlePictureControls.push_back(h); return h; };
    st->middleTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
                                    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                    S(parent, PAD), S(parent, 0), S(parent, 304), S(parent, TAB_H),
                                    p, win32_control_id(IDC_MIDDLE_TAB),
                                    GetModuleHandleW(nullptr), nullptr);
    const wchar_t* tabs[] = {L"检验结果", L"图象", L"手工复查[无]"};
    insertTabs(st->middleTab, tabs, static_cast<int>(sizeof(tabs) / sizeof(tabs[0])));
    const ButtonDef toolbar[] = {
        {5301, L"»  增 加"}, {5302, L"删除项目"}, {5303, L"保存(F2)"}, {5304, L"项目设置"}, {5305, L"发送(F9)"},
    };
    int x = S(parent, 10);
    for (const auto& b : toolbar) {
        addResult(makeButton(p, b.id, b.text, x, S(parent, MIDDLE_TOOLBAR_Y), S(parent, 86), S(parent, COMPACT_BUTTON_H)));
        x += S(parent, 96);
    }
    st->resultList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                     WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                     S(parent, PAD), S(parent, MIDDLE_LIST_Y), S(parent, 520), S(parent, 350),
                                     p, win32_control_id(IDC_RESULT_LIST), GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(st->resultList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    SetWindowSubclass(st->resultList, resultListProc, RESULT_LIST_SUBCLASS, reinterpret_cast<DWORD_PTR>(st));
    st->status = makeStatic(p, L"结果列表右键功能：项目复制；参数设置。[项目总数：7]", S(parent, PAD), S(parent, 424), S(parent, 520), S(parent, MIDDLE_STATUS_H));
    addResult(st->resultList);
    addResult(st->status);
    st->pictureViewport = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
                                          WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                                          S(parent, PAD), S(parent, MIDDLE_LIST_Y), S(parent, 520), S(parent, 350),
                                          p, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(st->pictureViewport, pictureViewportProc, PICTURE_VIEWPORT_SUBCLASS,
                      reinterpret_cast<DWORD_PTR>(st));
    st->pictureView = CreateWindowExW(0, L"STATIC", L"",
                                      WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                      0, 0, S(parent, PICTURE_FIXED_W), S(parent, PICTURE_FIXED_H),
                                      st->pictureViewport, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(st->pictureView, pictureViewProc, PICTURE_VIEW_SUBCLASS, reinterpret_cast<DWORD_PTR>(st));
    st->pictureHScroll = CreateWindowExW(0, L"SCROLLBAR", L"",
                                         WS_CHILD | SBS_HORZ,
                                         S(parent, PAD), S(parent, MIDDLE_LIST_Y + 350), S(parent, 520), S(parent, 18),
                                         p, nullptr, GetModuleHandleW(nullptr), nullptr);
    st->pictureVScroll = CreateWindowExW(0, L"SCROLLBAR", L"",
                                         WS_CHILD | SBS_VERT,
                                         S(parent, PAD + 520), S(parent, MIDDLE_LIST_Y), S(parent, 18), S(parent, 350),
                                         p, nullptr, GetModuleHandleW(nullptr), nullptr);
    addPicture(st->pictureViewport);
    addPicture(st->pictureHScroll);
    addPicture(st->pictureVScroll);
    updatePictureViewport(st);
    showMiddleResultPage(st);
}

void createRightPanel(HWND parent, RegularReportState* st) {
    HWND p = st->rightPanel;
    auto addInfo = [&](HWND h) { st->rightInfoControls.push_back(h); return h; };
    st->rightTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
                                   WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                   S(parent, PAD), S(parent, RIGHT_TAB_Y), S(parent, 576), S(parent, TAB_H),
                                   p, win32_control_id(IDC_RIGHT_TAB),
                                   GetModuleHandleW(nullptr), nullptr);
    const wchar_t* tabs[] = {L"信息列表", L"结果比较"};
    insertTabs(st->rightTab, tabs, static_cast<int>(sizeof(tabs) / sizeof(tabs[0])));
    st->rightSearchLabel = addInfo(makeStatic(p, L"按姓名查", S(parent, PAD), S(parent, RIGHT_SEARCH_LABEL_Y), S(parent, 70), S(parent, 24)));
    st->rightSearchEdit = addInfo(makeEdit(p, L"", S(parent, 82), S(parent, RIGHT_SEARCH_CONTROL_Y), S(parent, 84), S(parent, 26)));
    st->rightSearchIndexButton = addInfo(makeButton(p, 0, L"1", S(parent, 174), S(parent, RIGHT_SEARCH_CONTROL_Y), S(parent, 38), S(parent, COMPACT_BUTTON_H)));
    st->rightSearchUpButton = addInfo(makeButton(p, IDC_REPORT_FIRST_BUTTON, L"⇧", S(parent, 220), S(parent, RIGHT_SEARCH_CONTROL_Y), S(parent, 38), S(parent, COMPACT_BUTTON_H)));
    st->rightSearchDownButton = addInfo(makeButton(p, IDC_REPORT_LAST_BUTTON, L"⇩", S(parent, 266), S(parent, RIGHT_SEARCH_CONTROL_Y), S(parent, 38), S(parent, COMPACT_BUTTON_H)));
    st->rightSearchMenuButton = addInfo(makeButton(p, 0, L"▼", S(parent, 548), S(parent, RIGHT_SEARCH_CONTROL_Y), S(parent, 36), S(parent, COMPACT_BUTTON_H)));
    st->reportList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                     WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                     S(parent, PAD), S(parent, RIGHT_LIST_Y), S(parent, 576), S(parent, 588),
                                     p, win32_control_id(IDC_REPORT_LIST), GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(st->reportList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES |
                                                       LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES);
    st->rightDateTodayButton = addInfo(makeButton(p, IDC_REPORT_DATE_TODAY_BUTTON, L"今天",
                                                  S(parent, PAD), S(parent, RIGHT_LIST_Y + 596),
                                                  S(parent, RIGHT_DATE_BUTTON_W), S(parent, RIGHT_DATE_BUTTON_H)));
    st->rightDatePrevButton = addInfo(makeButton(p, IDC_REPORT_DATE_PREV_BUTTON, L"前一天",
                                                 S(parent, PAD + RIGHT_DATE_BUTTON_W + 8), S(parent, RIGHT_LIST_Y + 596),
                                                 S(parent, RIGHT_DATE_BUTTON_W), S(parent, RIGHT_DATE_BUTTON_H)));
    st->rightDateNextButton = addInfo(makeButton(p, IDC_REPORT_DATE_NEXT_BUTTON, L"后一天",
                                                 S(parent, PAD + (RIGHT_DATE_BUTTON_W + 8) * 2), S(parent, RIGHT_LIST_Y + 596),
                                                 S(parent, RIGHT_DATE_BUTTON_W), S(parent, RIGHT_DATE_BUTTON_H)));
    st->rightAutoRefreshCheck = addInfo(CreateWindowExW(0, L"BUTTON", L"",
                                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | BS_AUTOCHECKBOX,
                                                        S(parent, PAD + (RIGHT_DATE_BUTTON_W + 8) * 3 + 6),
                                                        S(parent, RIGHT_LIST_Y + 600),
                                                        S(parent, 18), S(parent, 20),
                                                        p, win32_control_id(IDC_REPORT_AUTO_REFRESH_CHECK),
                                                        GetModuleHandleW(nullptr), nullptr));
    st->rightAutoRefreshLabel = addInfo(makeStatic(p, L"自动刷新",
                                                   S(parent, PAD + (RIGHT_DATE_BUTTON_W + 8) * 3 + 26),
                                                   S(parent, RIGHT_LIST_Y + 598),
                                                   S(parent, 66), S(parent, 24),
                                                   SS_LEFT | SS_CENTERIMAGE));
    st->rightAutoRefreshEdit = addInfo(makeEdit(p, L"10",
                                                S(parent, PAD + (RIGHT_DATE_BUTTON_W + 8) * 3 + 88),
                                                S(parent, RIGHT_LIST_Y + 598),
                                                S(parent, 38), S(parent, 24),
                                                ES_CENTER | ES_NUMBER));
    st->rightAutoRefreshUnitLabel = addInfo(makeStatic(p, L"秒",
                                                       S(parent, PAD + (RIGHT_DATE_BUTTON_W + 8) * 3 + 130),
                                                       S(parent, RIGHT_LIST_Y + 598),
                                                       S(parent, 24), S(parent, 24),
                                                       SS_LEFT | SS_CENTERIMAGE));
    showRightInfoPage(st);
}

void createBottomPanel(HWND parent, RegularReportState* st) {
    HWND p = st->bottomPanel;
    const ButtonDef row1[] = {
        {IDC_BOTTOM_MACHINE_1, L"1"}, {IDC_BOTTOM_REFRESH, L"⟳ 刷新(F5)"}, {5403, L"▣ 保存(F1)"}, {5404, L"✓ 审核(F3)"},
        {5405, L"预览(V)"}, {5406, L"打印(F4)"}, {5407, L"✕ 删除(D)"},
        {IDC_BOTTOM_PREV_REPORT, L"⇧ 上一个"}, {IDC_BOTTOM_NEXT_REPORT, L"⇩ 下一个"}, {5410, L"审核打印"},
    };
    const ButtonDef row2[] = {
        {IDC_BOTTOM_MACHINE_2, L"2"}, {5412, L"批审核"}, {5413, L"批取消"}, {5414, L"批录入"},
        {5415, L"批调整"}, {5416, L"批打印"}, {5417, L"批删除"},
        {5418, L"医嘱"}, {5419, L"汇总(F6)"},
    };
    const ButtonDef row3[] = {
        {IDC_BOTTOM_MACHINE_3, L"3"}, {5421, L"追踪(Z)"}, {5422, L"计算(F8)"}, {5423, L"合并(U)"},
        {5424, L"图形(T)"}, {5425, L"项目分析"}, {5426, L"日统计"},
        {5427, L"设置"}, {5428, L"审核规则"}, {5429, L"批修改"},
    };
    auto createRow = [&](const ButtonDef* row, int count, int y) {
        int x = S(parent, PAD);
        for (int i = 0; i < count; ++i) {
            int w = (i == 0) ? S(parent, 42) : S(parent, 98);
            makeButton(p, row[i].id, row[i].text, x, S(parent, y), w, S(parent, COMPACT_BUTTON_H));
            x += w + S(parent, PAD);
        }
    };
    createRow(row1, static_cast<int>(sizeof(row1) / sizeof(row1[0])), 4);
    createRow(row2, static_cast<int>(sizeof(row2) / sizeof(row2[0])), 36);
    createRow(row3, static_cast<int>(sizeof(row3) / sizeof(row3[0])), 68);
    updateQuickMachineButtonLabels(st);
}

void createControls(HWND hwnd, RegularReportState* st) {
    st->leftPanel = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
                                    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                    0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(st->leftPanel, leftPanelProc, LEFT_PANEL_SUBCLASS, reinterpret_cast<DWORD_PTR>(st));
    st->leftContent = CreateWindowExW(0, L"STATIC", L"",
                                      WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                      0, 0, 0, 0, st->leftPanel, nullptr,
                                      GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(st->leftContent, leftContentProc, LEFT_CONTENT_SUBCLASS, reinterpret_cast<DWORD_PTR>(st));
    st->leftScrollBar = CreateWindowExW(0, L"SCROLLBAR", L"",
                                        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SBS_VERT,
                                        0, 0, 0, 0, st->leftPanel,
                                        win32_control_id(IDC_LEFT_SCROLL),
                                        GetModuleHandleW(nullptr), nullptr);
    st->middlePanel = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
                                      WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                      0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(st->middlePanel, middlePanelProc, MIDDLE_PANEL_SUBCLASS, reinterpret_cast<DWORD_PTR>(st));
    st->rightPanel = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
                                     WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                     0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(st->rightPanel, rightPanelProc, RIGHT_PANEL_SUBCLASS, reinterpret_cast<DWORD_PTR>(st));
    st->bottomPanel = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                      0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(st->bottomPanel, bottomPanelProc, BOTTOM_PANEL_SUBCLASS, reinterpret_cast<DWORD_PTR>(st));
    st->splitter = search::create_splitter(hwnd, IDC_SPLITTER, 0, 0, 0, 0, st->ctx.instance);
    createLeftPanel(hwnd, st);
    createMiddlePanel(hwnd, st);
    createRightPanel(hwnd, st);
    createBottomPanel(hwnd, st);
    seedLists(st);
    applyFont(hwnd, st->ctx.uiFont);
    refreshLeftGroupTitleFont(st);
    updateLeftScrollBar(st);
}

void layout(HWND hwnd, RegularReportState* st) {
    if (!st) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const int bottomH = S(hwnd, BOTTOM_PANEL_H);
    const int gap = S(hwnd, GAP);
    const int splitterW = S(hwnd, SPLITTER_W);
    const int leftW = std::min(std::max(S(hwnd, LEFT_PANEL_MIN_W), w * 21 / 100),
                               S(hwnd, LEFT_PANEL_MAX_W));
    const int topH = std::max(S(hwnd, 420), h - bottomH - gap);
    const int centerX = leftW + gap;
    const int minCenterW = S(hwnd, 460);
    const int minRightW = S(hwnd, 360);
    const int availableW = std::max(S(hwnd, 760), w - centerX - gap);
    const int defaultCenterW = std::max(S(hwnd, 500), availableW * 46 / 100);
    const int minSplitterX = centerX + minCenterW;
    int maxSplitterX = w - gap - minRightW - splitterW - gap;
    if (maxSplitterX < minSplitterX) maxSplitterX = minSplitterX;
    if (!st->splitterUserSet) st->splitterX = centerX + defaultCenterW;
    st->splitterX = std::clamp(st->splitterX, minSplitterX, maxSplitterX);

    const int centerW = st->splitterX - centerX;
    const int rightX = st->splitterX + splitterW + gap;
    const int rightW = std::max(S(hwnd, 200), w - rightX - gap);

    MoveWindow(st->leftPanel, 0, 0, leftW, h, TRUE);
    scrollLeftPanelTo(st, st->leftScrollY);
    MoveWindow(st->middlePanel, centerX, 0, centerW, topH, TRUE);
    MoveWindow(st->splitter, st->splitterX, 0, splitterW, topH, TRUE);
    MoveWindow(st->rightPanel, rightX, 0, rightW, topH, TRUE);
    MoveWindow(st->bottomPanel, centerX, topH + gap, w - centerX - gap, bottomH, TRUE);

    MoveWindow(st->middleTab, S(hwnd, PAD), S(hwnd, 0), centerW - S(hwnd, PAD * 2), S(hwnd, TAB_H), TRUE);
    MoveWindow(st->resultList, S(hwnd, PAD), S(hwnd, MIDDLE_LIST_Y),
               centerW - S(hwnd, PAD * 2), topH - S(hwnd, MIDDLE_LIST_BOTTOM_MARGIN), TRUE);
    const int pictureHostX = S(hwnd, PAD);
    const int pictureHostY = S(hwnd, MIDDLE_LIST_Y);
    const int pictureHostW = centerW - S(hwnd, PAD * 2);
    const int pictureHostH = topH - S(hwnd, MIDDLE_LIST_BOTTOM_MARGIN);
    const int pictureScrollW = GetSystemMetrics(SM_CXVSCROLL);
    const int pictureScrollH = GetSystemMetrics(SM_CYHSCROLL);
    const int pictureContentW = S(hwnd, PICTURE_FIXED_W);
    const int pictureContentH = S(hwnd, PICTURE_FIXED_H);
    bool needPictureV = pictureContentH > pictureHostH;
    bool needPictureH = pictureContentW > (pictureHostW - (needPictureV ? pictureScrollW : 0));
    needPictureV = pictureContentH > (pictureHostH - (needPictureH ? pictureScrollH : 0));
    const int pictureViewW = std::max(1, pictureHostW - (needPictureV ? pictureScrollW : 0));
    const int pictureViewH = std::max(1, pictureHostH - (needPictureH ? pictureScrollH : 0));
    MoveWindow(st->pictureViewport, pictureHostX, pictureHostY, pictureViewW, pictureViewH, TRUE);
    MoveWindow(st->pictureHScroll, pictureHostX, pictureHostY + pictureViewH, pictureViewW, pictureScrollH, TRUE);
    MoveWindow(st->pictureVScroll, pictureHostX + pictureViewW, pictureHostY, pictureScrollW, pictureViewH, TRUE);
    ShowWindow(st->pictureHScroll, needPictureH ? SW_SHOW : SW_HIDE);
    ShowWindow(st->pictureVScroll, needPictureV ? SW_SHOW : SW_HIDE);
    updatePictureViewport(st);
    MoveWindow(st->status, S(hwnd, PAD), topH - S(hwnd, MIDDLE_STATUS_BOTTOM),
               centerW - S(hwnd, PAD * 2), S(hwnd, MIDDLE_STATUS_H), TRUE);
    const int rightInnerX = S(hwnd, PAD);
    const int rightInnerW = std::max(S(hwnd, 80), rightW - S(hwnd, PAD * 2));
    const RightHeaderLayout rightHeader = rightHeaderLayout(hwnd, st->ctx.uiFont, rightW, rightSummaryLine1(st));
    const int rightTabY = rightHeader.bottom + S(hwnd, 6);
    const int rightSearchLabelY = rightTabY + S(hwnd, RIGHT_SEARCH_LABEL_Y - RIGHT_TAB_Y);
    const int rightSearchControlY = rightTabY + S(hwnd, RIGHT_SEARCH_CONTROL_Y - RIGHT_TAB_Y);
    const int rightListY = rightTabY + S(hwnd, RIGHT_LIST_Y - RIGHT_TAB_Y);
    MoveWindow(st->rightTab, rightInnerX, rightTabY, rightInnerW, S(hwnd, TAB_H), TRUE);
    MoveWindow(st->rightSearchLabel, S(hwnd, PAD), rightSearchLabelY, S(hwnd, 70), S(hwnd, 24), TRUE);
    MoveWindow(st->rightSearchEdit, S(hwnd, 82), rightSearchControlY, S(hwnd, 84), S(hwnd, 26), TRUE);
    MoveWindow(st->rightSearchIndexButton, S(hwnd, 174), rightSearchControlY, S(hwnd, 38), S(hwnd, COMPACT_BUTTON_H), TRUE);
    MoveWindow(st->rightSearchUpButton, S(hwnd, 220), rightSearchControlY, S(hwnd, 38), S(hwnd, COMPACT_BUTTON_H), TRUE);
    MoveWindow(st->rightSearchDownButton, S(hwnd, 266), rightSearchControlY, S(hwnd, 38), S(hwnd, COMPACT_BUTTON_H), TRUE);
    MoveWindow(st->rightSearchMenuButton, rightW - S(hwnd, PAD + 36), rightSearchControlY, S(hwnd, 36), S(hwnd, COMPACT_BUTTON_H), TRUE);
    const int rightDateButtonY = topH - S(hwnd, PAD + RIGHT_DATE_BUTTON_H);
    const int rightDateButtonGap = S(hwnd, 8);
    const int rightDateButtonW = S(hwnd, RIGHT_DATE_BUTTON_W);
    const int autoRefreshX = rightInnerX + (rightDateButtonW + rightDateButtonGap) * 3 + S(hwnd, 6);
    MoveWindow(st->reportList, rightInnerX, rightListY,
               rightInnerW, std::max(S(hwnd, 80), rightDateButtonY - rightListY - S(hwnd, GAP)), TRUE);
    MoveWindow(st->rightDateTodayButton, rightInnerX, rightDateButtonY,
               rightDateButtonW, S(hwnd, RIGHT_DATE_BUTTON_H), TRUE);
    MoveWindow(st->rightDatePrevButton, rightInnerX + rightDateButtonW + rightDateButtonGap, rightDateButtonY,
               rightDateButtonW, S(hwnd, RIGHT_DATE_BUTTON_H), TRUE);
    MoveWindow(st->rightDateNextButton, rightInnerX + (rightDateButtonW + rightDateButtonGap) * 2, rightDateButtonY,
               rightDateButtonW, S(hwnd, RIGHT_DATE_BUTTON_H), TRUE);
    MoveWindow(st->rightAutoRefreshCheck, autoRefreshX, rightDateButtonY + S(hwnd, 4),
               S(hwnd, 18), S(hwnd, 20), TRUE);
    MoveWindow(st->rightAutoRefreshLabel, autoRefreshX + S(hwnd, 20), rightDateButtonY + S(hwnd, 2),
               S(hwnd, 66), S(hwnd, 24), TRUE);
    MoveWindow(st->rightAutoRefreshEdit, autoRefreshX + S(hwnd, 82), rightDateButtonY + S(hwnd, 2),
               S(hwnd, 38), S(hwnd, 24), TRUE);
    MoveWindow(st->rightAutoRefreshUnitLabel, autoRefreshX + S(hwnd, 124), rightDateButtonY + S(hwnd, 2),
               S(hwnd, 24), S(hwnd, 24), TRUE);
    InvalidateRect(st->rightPanel, nullptr, TRUE);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<RegularReportState*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE:
            st = g_pending;
            g_pending = nullptr;
            SetPropW(hwnd, PROP_STATE, reinterpret_cast<HANDLE>(st));
            st->hwnd = hwnd;
            st->bgBrush = CreateSolidBrush(RGB(0xB8, 0xB8, 0xB8));
            st->panelBrush = CreateSolidBrush(RGB(0xEF, 0xEF, 0xEF));
            st->blackBrush = CreateSolidBrush(RGB(0, 0, 0));
            st->pendingSplitterX = search::load_module_int(L"RegularReport", L"SplitterX", 0);
            createControls(hwnd, st);
            layout(hwnd, st);
            return 0;
        case WM_REGULAR_REPORTS_LOADED:
            if (st) {
                std::unique_ptr<ReportLoadResult> result(reinterpret_cast<ReportLoadResult*>(lp));
                finishReportQuery(st, hwnd, std::move(result));
            } else {
                delete reinterpret_cast<ReportLoadResult*>(lp);
            }
            return 0;
        case WM_REGULAR_RESULTS_LOADED:
            if (st) {
                std::unique_ptr<ResultLoadResult> result(reinterpret_cast<ResultLoadResult*>(lp));
                finishResultQuery(st, hwnd, std::move(result));
            } else {
                delete reinterpret_cast<ResultLoadResult*>(lp);
            }
            return 0;
        case WM_REGULAR_PICTURE_LOADED:
            if (st) {
                std::unique_ptr<PictureLoadResult> result(reinterpret_cast<PictureLoadResult*>(lp));
                finishPictureQuery(st, hwnd, std::move(result));
            } else {
                delete reinterpret_cast<PictureLoadResult*>(lp);
            }
            return 0;
        case WM_SIZE:
            if (st && st->pendingSplitterX > 0 && IsZoomed(hwnd)) {
                st->splitterX = st->pendingSplitterX;
                st->splitterUserSet = true;
                st->pendingSplitterX = 0;
            }
            layout(hwnd, st);
            return 0;
        case WM_TIMER:
            if (st && wp == IDT_REPORT_AUTO_REFRESH) {
                if (isAutoRefreshChecked(st)) {
                    runAutoRefreshQuery(st);
                } else {
                    stopAutoRefreshTimer(st);
                }
                return 0;
            }
            break;
        case search::WM_SPLITTER_DRAG:
            if (st && reinterpret_cast<HWND>(lp) == st->splitter) {
                st->splitterX = static_cast<int>(wp);
                st->splitterUserSet = true;
                layout(hwnd, st);
            }
            return 0;
        case search::WM_SPLITTER_RELEASED:
            if (st && reinterpret_cast<HWND>(lp) == st->splitter) {
                st->splitterX = static_cast<int>(wp);
                st->splitterUserSet = true;
                layout(hwnd, st);
                search::save_module_int(L"RegularReport", L"SplitterX", st->splitterX);
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == IDM_REPORT_PRINT_BARCODE) {
                const std::wstring message = printBarcodeForContext(st);
                MessageBoxW(hwnd, message.c_str(), L"常规报告", MB_ICONINFORMATION);
                return 0;
            }
            if (LOWORD(wp) == IDM_REPORT_PRINT_CHECKED_BARCODES) {
                const std::wstring message = printCheckedBarcodes(st);
                MessageBoxW(hwnd, message.c_str(), L"常规报告", MB_ICONINFORMATION);
                return 0;
            }
            break;
        case app::WM_APP_FONT_CHANGED:
            if (st && lp) {
                if (IsWindow(st->machinePickerPopup)) DestroyWindow(st->machinePickerPopup);
                st->ctx.uiFont = reinterpret_cast<HFONT>(lp);
                clearLeftPanel(st);
                createLeftPanel(hwnd, st);
                applyFont(hwnd, st->ctx.uiFont);
                refreshLeftGroupTitleFont(st);
                layout(hwnd, st);
            }
            return 0;
        case app::WM_APP_SETTINGS_CHANGED:
            if (st) {
                updateQuickMachineButtonLabels(st);
            }
            return 0;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            HWND ctl = reinterpret_cast<HWND>(lp);
            if (GetPropW(ctl, L"RegularLeftLabel")) {
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(0x00, 0x00, 0xC4));
                return reinterpret_cast<LRESULT>(st ? st->panelBrush : nullptr);
            }
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(0, 0, 0xCC));
            return reinterpret_cast<LRESULT>(st ? st->panelBrush : nullptr);
        }
        case WM_CTLCOLORDLG:
            return reinterpret_cast<LRESULT>(st ? st->bgBrush : nullptr);
        case WM_DESTROY:
            RemovePropW(hwnd, PROP_STATE);
            if (st) {
                stopAutoRefreshTimer(st);
                finishResultEdit(st, false);
                if (IsWindow(st->machinePickerPopup)) DestroyWindow(st->machinePickerPopup);
                if (IsWindow(st->picturePopup)) DestroyWindow(st->picturePopup);
                releasePictureImage(st);
                if (st->gdiplusReady) {
                    Gdiplus::GdiplusShutdown(st->gdiplusToken);
                    st->gdiplusReady = false;
                    st->gdiplusToken = 0;
                }
                if (st->bgBrush) DeleteObject(st->bgBrush);
                if (st->panelBrush) DeleteObject(st->panelBrush);
                if (st->blackBrush) DeleteObject(st->blackBrush);
                if (st->groupTitleFont) DeleteObject(st->groupTitleFont);
                delete st;
            }
            break;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_regular_report_module(const ModuleContext& ctx) {
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

    auto* st = new RegularReportState;
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
