#include "microbiology_report_state.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "quick_machine_keys.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"

#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>

namespace {

// ============================================================================
// Quick machine arrays (used only in this file)
// ============================================================================

constexpr std::array<int, MICROBIOLOGY_QUICK_MACHINE_COUNT> QUICK_MACHINE_BUTTON_IDS = {
    MICROBIOLOGY_IDC_BOTTOM_MACHINE_1, MICROBIOLOGY_IDC_BOTTOM_MACHINE_2, MICROBIOLOGY_IDC_BOTTOM_MACHINE_3};

}  // namespace

// ============================================================================
// Summary lines
// ============================================================================

std::wstring microbiologyRightSummaryLine1(const MicrobiologyReportState* st) {
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

std::wstring microbiologyRightSummaryLine2(const MicrobiologyReportState* st) {
    int criticalCount = 0;
    int reviewedCriticalCount = 0;
    int emergencyCount = 0;
    int reviewedEmergencyCount = 0;
    if (st) {
        for (const auto& row : st->reportRows) {
            const std::string reportType = search::trim(row.report_type);
            const bool reviewedAndSent = search::trim(row.chk_flag) == "T" && search::trim(row.conf) == "S";
            if (reportType == "9") {
                ++criticalCount;
                if (reviewedAndSent) ++reviewedCriticalCount;
            }
            if (reportType == "0" || search::trim(row.barcode_jz_flag) == "1") {
                ++emergencyCount;
                if (reviewedAndSent) ++reviewedEmergencyCount;
            }
        }
    }
    return L"危急报告数:" + std::to_wstring(criticalCount) +
           L"；危急报告已审:" + std::to_wstring(reviewedCriticalCount) +
           L"；急诊报告数:" + std::to_wstring(emergencyCount) +
           L"；急诊报告已审:" + std::to_wstring(reviewedEmergencyCount) + L"；";
}

// ============================================================================
// Quick machine helpers
// ============================================================================

const wchar_t* microbiologyQuickMachineCodeKey(int slot) {
    return quick_machine_code_key(slot);
}

const wchar_t* microbiologyQuickMachineNameKey(int slot) {
    return quick_machine_name_key(slot);
}

const wchar_t* microbiologyQuickMachineRoomKey(int slot) {
    return quick_machine_room_key(slot);
}

bool microbiologyQuickMachineMatchesCurrent(const MicrobiologyReportState* st, int slot) {
    if (!st || slot < 0 || slot >= MICROBIOLOGY_QUICK_MACHINE_COUNT) return false;
    const auto loadSetting = [](const wchar_t* key) {
        const std::wstring fallback =
            search::load_module_str(L"RegularReport", key, L"");
        return search::load_module_str(
            L"MicrobiologyReport", key, fallback.c_str());
    };
    const std::string code = search::wide_to_utf8(
        loadSetting(microbiologyQuickMachineCodeKey(slot)));
    const std::string room = search::wide_to_utf8(
        loadSetting(microbiologyQuickMachineRoomKey(slot)));
    const std::string currentCode = search::trim(st->selectedMachineCode);
    const std::string currentRoom = search::trim(st->selectedRoomCode);
    return !currentCode.empty() && currentCode == search::trim(code) &&
           currentRoom == search::trim(room);
}

void microbiologyUpdateQuickMachineButtonLabels(MicrobiologyReportState* st) {
    if (!st || !st->bottomPanel) return;
    for (int i = 0; i < MICROBIOLOGY_QUICK_MACHINE_COUNT; ++i) {
        const bool active = microbiologyQuickMachineMatchesCurrent(st, i);
        const std::wstring text = active ? L"[" + std::to_wstring(i + 1) + L"]"
                                         : std::to_wstring(i + 1);
        HWND button = GetDlgItem(st->bottomPanel, QUICK_MACHINE_BUTTON_IDS[static_cast<size_t>(i)]);
        if (button) SetWindowTextW(button, text.c_str());
    }
}

bool microbiologyIsAllowedMachineCode(const std::string& machineCode) {
    const std::string code = search::trim(machineCode);
    return code == "3001" || code == "7002";
}

// ============================================================================
// DPI + window helpers
// ============================================================================

int microbiologyS(HWND hwnd, int value) {
    return static_cast<int>(value * search::dpi_scale_factor(hwnd));
}

HWND microbiologyAddClipSiblings(HWND hwnd) {
    if (!hwnd) return hwnd;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, style | WS_CLIPSIBLINGS);
    return hwnd;
}

// ============================================================================
// Win32 control factories
// ============================================================================

HWND microbiologyMakeStatic(HWND parent, const wchar_t* text, int x, int y, int w, int h, DWORD style) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND microbiologyMakeEdit(HWND parent, const wchar_t* text, int x, int y, int w, int h, DWORD extra) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | extra,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

// ============================================================================
// Date helpers
// ============================================================================

SYSTEMTIME microbiologyTodayDate() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    st.wHour = 0;
    st.wMinute = 0;
    st.wSecond = 0;
    st.wMilliseconds = 0;
    return st;
}

SYSTEMTIME microbiologyNormalizeDate(SYSTEMTIME st) {
    st.wHour = 0;
    st.wMinute = 0;
    st.wSecond = 0;
    st.wMilliseconds = 0;
    return st;
}

SYSTEMTIME microbiologyAddDays(SYSTEMTIME st, int days) {
    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft)) return microbiologyTodayDate();
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    constexpr ULONGLONG TICKS_PER_DAY = 24ULL * 60ULL * 60ULL * 10000000ULL;
    if (days >= 0)
        value.QuadPart += static_cast<ULONGLONG>(days) * TICKS_PER_DAY;
    else
        value.QuadPart -= static_cast<ULONGLONG>(-days) * TICKS_PER_DAY;
    ft.dwLowDateTime = value.LowPart;
    ft.dwHighDateTime = value.HighPart;
    SYSTEMTIME result{};
    if (!FileTimeToSystemTime(&ft, &result)) return microbiologyTodayDate();
    return microbiologyNormalizeDate(result);
}

// ============================================================================
// Date picker helpers
// ============================================================================

HWND microbiologyMakeDatePicker(HWND parent, int x, int y, int w, int h,
                            const wchar_t* format, const SYSTEMTIME* value, int id) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | DTS_SHORTDATECENTURYFORMAT;
    HWND picker = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", style,
                                  x, y, w, h, parent, win32_control_id(id),
                                  GetModuleHandleW(nullptr), nullptr);
    if (format) SetPropW(picker, MICROBIOLOGY_REPORT_PROP_DATE_FORMAT,
                          reinterpret_cast<HANDLE>(const_cast<wchar_t*>(format)));
    if (value) {
        SendMessageW(picker, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(format));
        SendMessageW(picker, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(value));
    } else {
        SYSTEMTIME blankValue = microbiologyTodayDate();
        SendMessageW(picker, DTM_SETFORMATW, 0,
                      reinterpret_cast<LPARAM>(MICROBIOLOGY_REPORT_BLANK_DATE_FORMAT));
        SendMessageW(picker, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&blankValue));
    }
    return picker;
}

std::string microbiologyDatePickerValue(HWND hwnd) {
    SYSTEMTIME st{};
    if (!hwnd || DateTime_GetSystemtime(hwnd, &st) != GDT_VALID) return "";
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
    return buffer;
}

SYSTEMTIME microbiologyDatePickerSystemTime(HWND hwnd) {
    SYSTEMTIME st{};
    if (!hwnd || DateTime_GetSystemtime(hwnd, &st) != GDT_VALID) return microbiologyTodayDate();
    return microbiologyNormalizeDate(st);
}

std::string microbiologySlashDate(const std::string& value) {
    int year = 0, month = 0, day = 0;
    if ((std::sscanf(value.c_str(), "%d-%d-%d", &year, &month, &day) == 3 ||
         std::sscanf(value.c_str(), "%d/%d/%d", &year, &month, &day) == 3) &&
        year > 0 && month > 0 && day > 0) {
        char buffer[16]{};
        std::snprintf(buffer, sizeof(buffer), "%d/%d/%d", year, month, day);
        return buffer;
    }
    return value;
}

std::string microbiologySlashDateTimeMinute(const std::string& value) {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
    if ((std::sscanf(value.c_str(), "%d-%d-%d %d:%d", &year, &month, &day, &hour, &minute) == 5 ||
         std::sscanf(value.c_str(), "%d/%d/%d %d:%d", &year, &month, &day, &hour, &minute) == 5) &&
        year > 0 && month > 0 && day > 0) {
        char buffer[24]{};
        std::snprintf(buffer, sizeof(buffer), "%d/%d/%d %d:%02d", year, month, day, hour, minute);
        return buffer;
    }
    return value;
}

bool microbiologyParseDateTimeText(const std::string& value, SYSTEMTIME& out) {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    const int matched = std::sscanf(value.c_str(), "%d-%d-%d %d:%d:%d",
                                    &year, &month, &day, &hour, &minute, &second);
    if (matched < 3) {
        const int slashMatched = std::sscanf(value.c_str(), "%d/%d/%d %d:%d:%d",
                                             &year, &month, &day, &hour, &minute, &second);
        if (slashMatched < 3) return false;
        if (slashMatched < 5) { hour = 0; minute = 0; second = 0; }
        else if (slashMatched < 6) { second = 0; }
    } else if (matched < 5) {
        hour = 0; minute = 0; second = 0;
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

// ============================================================================
// Control text helpers
// ============================================================================

void microbiologySetControlText(HWND hwnd, const std::string& text) {
    if (!hwnd) return;
    const auto wide = search::utf8_to_wide(text);
    SetWindowTextW(hwnd, wide.c_str());
}

std::wstring microbiologyWindowText(HWND hwnd) {
    if (!hwnd) return L"";
    const int length = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0) GetWindowTextW(hwnd, text.data(), static_cast<int>(text.size()));
    text.resize(static_cast<size_t>(length));
    return text;
}

void microbiologySetComboSingleText(HWND hwnd, const std::string& text) {
    if (!hwnd) return;
    SendMessageW(hwnd, CB_RESETCONTENT, 0, 0);
    const auto wide = search::utf8_to_wide(text);
    SendMessageW(hwnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
    SendMessageW(hwnd, CB_SETCURSEL, 0, 0);
}

MicrobiologyAgeDisplayParts microbiologySplitAgeDisplayText(const std::string& text) {
    MicrobiologyAgeDisplayParts parts;
    std::string trimmed = search::trim(text);
    if (trimmed.empty()) return parts;
    const char* units[] = {"小时", "岁", "月", "天", "分"};
    for (const char* unit : units) {
        const size_t unitLen = std::strlen(unit);
        if (trimmed.size() >= unitLen &&
            trimmed.compare(trimmed.size() - unitLen, unitLen, unit) == 0) {
            parts.value = search::trim(trimmed.substr(0, trimmed.size() - unitLen));
            parts.unit = unit;
            return parts;
        }
    }
    parts.value = trimmed;
    return parts;
}

void microbiologyFillAgeUnitCombo(HWND combo, const std::string& selectedUnit) {
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

void microbiologySetDatePickerValue(HWND hwnd, const std::string& text) {
    if (!hwnd) return;
    SYSTEMTIME st{};
    if (microbiologyParseDateTimeText(text, st)) {
        auto* format = reinterpret_cast<const wchar_t*>(
            GetPropW(hwnd, MICROBIOLOGY_REPORT_PROP_DATE_FORMAT));
        SendMessageW(hwnd, DTM_SETFORMATW, 0,
                      reinterpret_cast<LPARAM>(format ? format : L"yyyy-MM-dd"));
        DateTime_SetSystemtime(hwnd, GDT_VALID, &st);
    } else {
        SYSTEMTIME blankValue = microbiologyTodayDate();
        SendMessageW(hwnd, DTM_SETFORMATW, 0,
                      reinterpret_cast<LPARAM>(MICROBIOLOGY_REPORT_BLANK_DATE_FORMAT));
        DateTime_SetSystemtime(hwnd, GDT_VALID, &blankValue);
    }
}

void microbiologyClearDatePickerValue(HWND hwnd) {
    if (!hwnd) return;
    SYSTEMTIME blankValue = microbiologyTodayDate();
    SendMessageW(hwnd, DTM_SETFORMATW, 0,
                  reinterpret_cast<LPARAM>(MICROBIOLOGY_REPORT_BLANK_DATE_FORMAT));
    DateTime_SetSystemtime(hwnd, GDT_VALID, &blankValue);
}

void microbiologySetControlsEnabled(bool enabled, std::initializer_list<HWND> controls) {
    for (HWND hwnd : controls) {
        if (hwnd) EnableWindow(hwnd, enabled ? TRUE : FALSE);
    }
}

// ============================================================================
// Font helpers
// ============================================================================

void microbiologyApplyFont(HWND hwnd, HFONT font) {
    if (!font) return;
    EnumChildWindows(hwnd, [](HWND child, LPARAM p) -> BOOL {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(p), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font));
}

HFONT microbiologyCreateBoldFont(HFONT base) {
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

void microbiologyRefreshLeftGroupTitleFont(MicrobiologyReportState* st) {
    if (!st) return;
    if (st->groupTitleFont) {
        DeleteObject(st->groupTitleFont);
        st->groupTitleFont = nullptr;
    }
    st->groupTitleFont = microbiologyCreateBoldFont(st->ctx.uiFont);
    if (st->leftContent) InvalidateRect(st->leftContent, nullptr, TRUE);
}

int microbiologyTextLogicalWidth(HWND hwnd, HFONT font, const wchar_t* text) {
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

int microbiologyFontLogicalHeight(HWND hwnd, HFONT font) {
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

int microbiologyWrappedTextHeightPx(HWND hwnd, HFONT font, const wchar_t* text,
                                int widthPx, int minHeightPx) {
    if (!text || widthPx <= 0) return minHeightPx;
    HDC dc = GetDC(hwnd);
    if (!dc) return minHeightPx;
    HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    RECT rc{0, 0, widthPx, 0};
    DrawTextW(dc, text, -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_CENTER);
    if (old) SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    return std::max(minHeightPx, static_cast<int>(rc.bottom - rc.top) + microbiologyS(hwnd, 2));
}

MicrobiologyRightHeaderLayout microbiologyRightHeaderLayout(HWND hwnd, HFONT font, int panelWidthPx,
                                            const std::wstring& summaryLine1,
                                            const std::wstring& summaryLine2) {
    const int innerX = microbiologyS(hwnd, MICROBIOLOGY_PAD);
    const int innerW = std::max(microbiologyS(hwnd, 80), panelWidthPx - microbiologyS(hwnd, MICROBIOLOGY_PAD * 2));
    const int top = microbiologyS(hwnd, 4);
    const int gap = microbiologyS(hwnd, 2);
    const int line1H = microbiologyWrappedTextHeightPx(hwnd, font, summaryLine1.c_str(),
                                                   innerW, microbiologyS(hwnd, 20));
    const int line2Y = top + line1H + gap;
    const int line2H = microbiologyWrappedTextHeightPx(hwnd, font, summaryLine2.c_str(),
                                                   innerW, microbiologyS(hwnd, 24));
    MicrobiologyRightHeaderLayout layout{};
    layout.line1 = RECT{innerX, top, innerX + innerW, top + line1H};
    layout.line2 = RECT{innerX, line2Y, innerX + innerW, line2Y + line2H};
    layout.bottom = line2Y + line2H;
    return layout;
}

int microbiologyLeftLabelWidth(HWND hwnd, HFONT font) {
    const wchar_t* labels[] = {
        L"检验仪器", L"组合项目", L"检验单号", L"病人类型", L"临床科室",
        L"临床诊断", L"申请医生", L"申请日期", L"签收时间", L"上机时间",
        L"报告时间", L"检验日期", L"采集日期",
    };
    int width = 50;
    for (const wchar_t* label : labels)
        width = std::max(width, microbiologyTextLogicalWidth(hwnd, font, label) + 8);
    return width;
}

int microbiologyRightLabelWidth(HWND hwnd, HFONT font) {
    const wchar_t* labels[] = {L"样本号", L"性别", L"床号", L"审核"};
    int width = 38;
    for (const wchar_t* label : labels)
        width = std::max(width, microbiologyTextLogicalWidth(hwnd, font, label) + 8);
    return width;
}

// ============================================================================
// Combo helpers
// ============================================================================

void microbiologyComboReset(HWND combo) { SendMessageW(combo, CB_RESETCONTENT, 0, 0); }

void microbiologyComboAdd(HWND combo, const std::wstring& text) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
}

void microbiologyComboSelectFirst(HWND combo) {
    if (SendMessageW(combo, CB_GETCOUNT, 0, 0) > 0)
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

// ============================================================================
// Color helpers for NM_CUSTOMDRAW (shared between panels and module)
// ============================================================================

bool microbiologyReportIsSent(const search::ReportRow& report) {
    return search::trim(report.conf) == "S";
}

bool microbiologyReportIsReviewed(const search::ReportRow& report) {
    return search::trim(report.chk_flag) == "T";
}

bool microbiologyReportIsEmergencyReport(const search::ReportRow& report) {
    return search::trim(report.report_type) == "0";
}

bool microbiologyReportHasBarcodeEmergencyLabel(const search::ReportRow& report) {
    return search::trim(report.barcode_jz_flag) == "1";
}

bool microbiologyReportUsesEmergencyTextColor(const search::ReportRow& report) {
    return microbiologyReportIsEmergencyReport(report) || microbiologyReportHasBarcodeEmergencyLabel(report);
}

bool microbiologyReportIsCriticalReport(const search::ReportRow& report) {
    return search::trim(report.report_type) == "9";
}

COLORREF microbiologyReportRowColor(const MicrobiologyReportState* st, int row) {
    if (!st || row < 0 || row >= static_cast<int>(st->reportRows.size()))
        return MICROBIOLOGY_COLOR_WHITE;
    const auto& report = st->reportRows[static_cast<size_t>(row)];
    const bool sent = microbiologyReportIsSent(report);
    const bool reviewed = microbiologyReportIsReviewed(report);
    const bool critical = microbiologyReportIsCriticalReport(report);
    if (sent && reviewed && critical) return MICROBIOLOGY_COLOR_CRITICAL_FINAL;
    if (critical && !sent && !reviewed) return MICROBIOLOGY_COLOR_CRITICAL_PENDING;
    if (sent) return MICROBIOLOGY_COLOR_REPORT_SENT;
    if (reviewed) return MICROBIOLOGY_COLOR_REPORT_REVIEWED;
    return MICROBIOLOGY_COLOR_WHITE;
}

COLORREF microbiologyReportPrintCellColor(const MicrobiologyReportState* st, int row) {
    if (!st || row < 0 || row >= static_cast<int>(st->reportRows.size()))
        return MICROBIOLOGY_COLOR_WHITE;
    const auto printFlag = search::trim(st->reportRows[static_cast<size_t>(row)].zymz_print);
    return printFlag == "1" ? RGB(0xA6, 0xEC, 0x9A) : MICROBIOLOGY_COLOR_WHITE;
}

COLORREF microbiologyResultTextColor(const search::ResultRow& row) {
    switch (search::result_row_tone(row)) {
        case search::ResultRowTone::High: return RGB(220, 0, 0);
        case search::ResultRowTone::Low:  return RGB(0, 0, 220);
        default:                          return CLR_INVALID;
    }
}

namespace {

bool parseNumericText(const std::string& value, double& out) {
    const std::string text = search::trim(value);
    const char* p = text.c_str();
    while (*p && *p != '+' && *p != '-' && *p != '.' && (*p < '0' || *p > '9')) ++p;
    if (!*p) return false;
    char* end = nullptr;
    out = std::strtod(p, &end);
    return end && end != p;
}

bool resultValueCrossesCriticalScope(const search::ResultRow& row) {
    double result = 0.0;
    if (!parseNumericText(row.result, result)) return false;
    double high = 0.0;
    if (parseNumericText(row.critical_high_bound, high) && result >= high) return true;
    double low = 0.0;
    if (parseNumericText(row.critical_low_bound, low) && result <= low) return true;
    return false;
}

}  // namespace

bool microbiologyResultRowHasCriticalValue(const search::ResultRow& row) {
    return search::trim(row.normal_wj) == "9" || resultValueCrossesCriticalScope(row);
}

bool microbiologyListViewRowSelected(HWND list, int row) {
    return list && row >= 0 && (ListView_GetItemState(list, row, LVIS_SELECTED) & LVIS_SELECTED);
}

bool microbiologyCustomDrawListSelection(NMLVCUSTOMDRAW* cd, HWND list, int row) {
    if (!cd || !microbiologyListViewRowSelected(list, row)) return false;
    cd->nmcd.uItemState &= ~(CDIS_SELECTED | CDIS_FOCUS);
    cd->clrTextBk = GetSysColor(COLOR_HIGHLIGHT);
    cd->clrText = GetSysColor(COLOR_HIGHLIGHTTEXT);
    return true;
}

void microbiologyRedrawSelectedListRow(HWND list) {
    if (!list) return;
    const int selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (selected < 0) return;
    ListView_RedrawItems(list, selected, selected);
    UpdateWindow(list);
}

#endif
