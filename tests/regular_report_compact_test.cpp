#include "regular_report_module.h"
#include "barcode_label_printing.h"
#include "regular_report_cancel_audit_dialog.h"
#include "regular_report_state.h"
#include "main_app.h"
#include "resource.h"
#include "search_text.h"
#include <algorithm>

#include <cstdlib>
#include <fstream>
#include <iostream>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void checkManualReviewTemplates() {
    wchar_t directory[MAX_PATH]{}, name[MAX_PATH]{};
    check(GetTempPathW(MAX_PATH, directory) != 0 && GetTempFileNameW(directory, L"sgt", 0, name) != 0,
          "manual-review template fixture path allocated");
    const std::filesystem::path path(name);
    const std::wstring original = LR"(<?xml version="1.0" encoding="gb2312"?>
<root><node zjf="复查" sgfc="第一行&#xD;&#xA;第二行&amp;&quot;" /></root>)";
    const int bytes = WideCharToMultiByte(936, 0, original.data(), static_cast<int>(original.size()),
                                         nullptr, 0, nullptr, nullptr);
    check(bytes > 0, "fixture can be encoded using the legacy template code page");
    std::string encoded(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(936, 0, original.data(), static_cast<int>(original.size()),
                        encoded.data(), bytes, nullptr, nullptr);
    {
        std::ofstream output(path, std::ios::binary);
        output.write(encoded.data(), bytes);
        check(static_cast<bool>(output), "legacy template fixture written");
    }
    std::vector<ManualReviewTemplate> rows;
    check(regularLoadManualReviewTemplates(path, rows), "legacy encoded template opens");
    check(rows.size() == 1 && rows[0].mnemonic == L"复查", "legacy template mnemonic reads correctly");
    check(rows[0].text == L"第一行\r\n第二行&\"", "legacy template XML escapes and newline read correctly");
    check(regularAppendManualReviewTemplate(path, {L"复查", L"另一个 \"模板\"\r\n下一行"}),
          "template can be appended without altering earlier template rows");
    check(regularLoadManualReviewTemplates(path, rows) && rows.size() == 2 &&
          rows[0].text == L"第一行\r\n第二行&\"" && rows[1].text == L"另一个 \"模板\"\r\n下一行",
          "XML output preserves escapes, multiline text and duplicate mnemonic order");
    DeleteFileW(path.c_str());
}

void CALLBACK checkCancelAuditDialog(HWND owner, UINT, UINT_PTR timer, DWORD) {
    const HWND dialog = GetLastActivePopup(owner);
    const HWND types = GetDlgItem(dialog, IDC_CANCEL_AUDIT_TYPE);
    if (!types || !IsWindowVisible(dialog)) return;
    KillTimer(owner, timer);

    check(SendMessageW(types, CB_GETCOUNT, 0, 0) == 2, "both cancel reasons are offered");
    wchar_t text[64]{};
    SendMessageW(types, CB_GETLBTEXT, 0, reinterpret_cast<LPARAM>(text));
    check(std::wstring_view(text) == L"采样问题", "a repeated cancel reason is displayed once");
    SendMessageW(types, CB_GETLBTEXT, 1, reinterpret_cast<LPARAM>(text));
    check(std::wstring_view(text) == L"采样问题  标本不合格", "a distinct reason is shown with its type");
    SendMessageW(types, CB_SETCURSEL, 1, 0);
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDC_CANCEL_AUDIT_TYPE, CBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(types));
    GetDlgItemTextW(dialog, IDC_CANCEL_AUDIT_REASON, text, static_cast<int>(std::size(text)));
    check(std::wstring_view(text) == L"标本不合格", "selecting a type fills the reason text");
    SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
}
struct ControlSnapshot { HWND hwnd; RECT rect; LONG_PTR exStyle; };
std::vector<ControlSnapshot> normalLayout;
RECT relativeRect(HWND hwnd) {
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    MapWindowPoints(nullptr, GetParent(hwnd), reinterpret_cast<POINT*>(&rect), 2);
    return rect;
}
void checkRestoredLayout() {
    for (const auto& saved : normalLayout) {
        RECT actual = relativeRect(saved.hwnd);
        check(EqualRect(&actual, &saved.rect) != FALSE, "normal-page control position and size restored");
        check(GetWindowLongPtrW(saved.hwnd, GWL_EXSTYLE) == saved.exStyle, "original control styles restored");
    }
}
LRESULT CALLBACK frameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_SIZE) {
        if (HWND mdi = GetDlgItem(hwnd, 1))
            MoveWindow(mdi, 0, 32, LOWORD(lp), std::max(1, int(HIWORD(lp)) - 56), TRUE);
    }
    return DefFrameProcW(hwnd, GetDlgItem(hwnd, 1), msg, wp, lp);
}
void enter(RegularReportState* st) {
    SendMessageW(st->compactButton, BM_CLICK, 0, 0);
    check(st->compactWindow && st->compactAttached, "entered compact mode");
}
void checkStoolResultColor(RegularReportState* st) {
    auto textColor = [st](int column) {
        NMLVCUSTOMDRAW draw{};
        draw.nmcd.hdr.hwndFrom = st->resultList;
        draw.nmcd.hdr.idFrom = REGULAR_IDC_RESULT_LIST;
        draw.nmcd.hdr.code = NM_CUSTOMDRAW;
        draw.nmcd.dwDrawStage = CDDS_ITEMPREPAINT | CDDS_SUBITEM;
        draw.clrText = RGB(0, 0, 0);
        draw.iSubItem = column;
        SendMessageW(GetParent(st->resultList), WM_NOTIFY, draw.nmcd.hdr.idFrom,
                     reinterpret_cast<LPARAM>(&draw));
        return draw.clrText;
    };
    const int columnCount = Header_GetItemCount(ListView_GetHeader(st->resultList));
    auto checkRowColor = [&](COLORREF expected, const char* message) {
        for (int column = 0; column < columnCount; ++column)
            check(textColor(column) == expected, message);
    };
    auto& result = st->resultRows.emplace_back();
    result.group_code = "62209";
    result.upbound = "未见";
    result.result = " 已找到 ";
    for (const char* itemName : {"肝吸虫卵", "蛲虫卵", "蛔虫卵", "鞭虫卵"}) {
        result.item_name = itemName;
        checkRowColor(RGB(220, 0, 0), "matching group row shown in red");
    }
    ListView_SetItemState(st->resultList, 0, LVIS_SELECTED, LVIS_SELECTED);
    checkRowColor(RGB(220, 0, 0), "selected matching row retains the same red");
    result.result = " 未见 ";
    checkRowColor(GetSysColor(COLOR_HIGHLIGHTTEXT), "selected excluded row uses the selection text color");
    ListView_SetItemState(st->resultList, 0, 0, LVIS_SELECTED);
    checkRowColor(RGB(0, 0, 0), "excluded result keeps its text color");
    for (const char* value : {"未找到", "未见虫卵", "已找到（待确认）"}) {
        result.result = value;
        checkRowColor(RGB(220, 0, 0), "nonempty result differing from the unseen reference shown in red");
    }
    result.result = " ";
    checkRowColor(RGB(0, 0, 0), "blank result keeps its text color");

    const struct {
        const char* value;
        const char* reference;
    } otherStoolResults[] = {
        {"黄色", "黄褐色"}, {"软便", "软"}, {"阴性", "无"},
        {"阴性", "阴性"}, {"阴性", "偶见"}, {"阴性", ""}
    };
    for (const auto& entry : otherStoolResults) {
        result.result = entry.value;
        result.upbound = entry.reference;
        checkRowColor(RGB(0, 0, 0), "stool results with a different reference keep their text color");
    }
    result.upbound.clear();
    result.downbound = "未见";
    result.group_code = "62210";
    result.result = "已找到";
    checkRowColor(RGB(0, 0, 0), "different group keeps its text color");

    result.group_code = "62209";
    result.result = "未见";
    result.entry_id = "101";
    st->resultCache.current = regular_comparison::LoadPhase::Ready;
    RECT cell{};
    check(ListView_GetSubItemRect(st->resultList, 0, REGULAR_RESULT_VALUE_COL, LVIR_BOUNDS, &cell),
          "result cell bounds available");
    SendMessageW(st->resultList, WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM((cell.left + cell.right) / 2, (cell.top + cell.bottom) / 2));
    check(st->resultEdit != nullptr, "result editor opened");
    SetWindowTextW(st->resultEdit, L"已找到");
    SendMessageW(st->resultEdit, WM_KILLFOCUS, 0, 0);
    check(!st->resultEdit && result.result == "已找到", "temporary result edit committed");
    checkRowColor(RGB(220, 0, 0), "edited row shown in red");
    ListView_SetItemState(st->resultList, 0, 0, LVIS_SELECTED | LVIS_FOCUSED);
    st->resultRows.clear();
    st->resultCache.invalidate();
}
void restored(RegularReportState* st, HWND middle, HWND right, HWND results, HWND reports, int split) {
    check(!st->compactWindow && !st->compactAttached, "compact mode ended");
    check(st->middlePanel == middle && st->rightPanel == right &&
          st->resultList == results && st->reportList == reports, "original controls preserved");
    check(GetParent(middle) == st->hwnd && GetParent(right) == st->hwnd &&
          GetParent(st->splitter) == st->hwnd, "all panels returned to MDI child");
    check(st->splitterX == split, "full-page divider preserved");
    check(IsWindowVisible(GetAncestor(st->hwnd, GA_ROOT)), "main frame restored");
    checkRestoredLayout();
    RECT middleClient{};
    GetClientRect(middle, &middleClient);
    check(middleClient.bottom - relativeRect(results).bottom == regularS(st->hwnd, REGULAR_PAD),
          "normal-page result list uses the former status area");
    for (int column = 0; column < REGULAR_REPORT_COLUMN_COUNT; ++column)
        check(ListView_GetColumnWidth(reports, column) > 0, "full-page columns restored");
    check(ListView_GetItemCount(results) == 1 && ListView_GetItemCount(reports) == 1 &&
          ListView_GetNextItem(reports, -1, LVNI_SELECTED) == 0, "data and selection preserved");
}
}

int main() {
    checkManualReviewTemplates();
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES | ICC_DATE_CLASSES};
    check(InitCommonControlsEx(&controls) != FALSE, "common controls initialized");
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = frameProc;
    wc.lpszClassName = L"RegularReportCompactTestFrame";
    check(RegisterClassW(&wc) != 0, "test frame registered");
    HWND frame = CreateWindowW(wc.lpszClassName, L"Report window tests", WS_OVERLAPPEDWINDOW,
                               0, 0, 1440, 900, nullptr, nullptr, instance, nullptr);
    CLIENTCREATESTRUCT client{};
    HWND mdi = CreateWindowW(L"MDICLIENT", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                             0, 0, 1420, 840, frame, reinterpret_cast<HMENU>(1), instance, &client);
    check(frame && mdi, "MDI fixture created");
    ShowWindow(frame, SW_SHOW);
    search::CancelAuditPreview cancelPreview;
    cancelPreview.reasons = {{"1", "10", "采样问题", "采样问题"},
                             {"2", "10", "采样问题", "标本不合格"}};
    RegularCancelAuditConfirmation cancelConfirmation;
    constexpr UINT_PTR cancelDialogTimer = 6301;
    check(SetTimer(frame, cancelDialogTimer, 10, checkCancelAuditDialog) != 0,
          "scheduled the offline cancel audit dialog check");
    check(regularShowCancelAuditDialog(frame, cancelPreview, {}, L"测试验单", L"fixture", 1,
                                       cancelConfirmation) == RegularCancelAuditDialogResult::cancelled,
          "closing the cancel audit dialog exits without authentication");
    HWND other = CreateWindowW(L"STATIC", L"Other application window", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               10, 10, 240, 120, nullptr, nullptr, instance, nullptr);
    check(other != nullptr, "additional application window created");
    ModuleContext context;
    context.instance = instance;
    context.mdiClient = mdi;
    context.uiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HWND report = create_regular_report_module(context);
    auto* st = reinterpret_cast<RegularReportState*>(GetPropW(report, REGULAR_REPORT_PROP_STATE));
    check(st != nullptr, "report module created");
    // Keep the fixture offline: do not dispatch the initial machine timer or query on selection.
    KillTimer(report, IDT_REPORT_INITIAL_QUICK_MACHINE);
    st->initialQuickMachineTimerActive = false;
    st->suppressReportSelectionQuery = true;
    search::ReportRow row;
    row.id = "synthetic";
    row.rep_no = "synthetic";
    row.oper_no = "123";
    row.name = "Test Patient";
    st->reportRows.push_back(row);
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.pszText = const_cast<wchar_t*>(L"Synthetic row");
    check(ListView_InsertItem(st->reportList, &item) == 0, "report row inserted");
    check(ListView_InsertItem(st->resultList, &item) == 0, "result row inserted");
    ListView_SetItemState(st->reportList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    st->selectedReportIndex = 0;
    HWND middle = st->middlePanel, right = st->rightPanel;
    HWND results = st->resultList, reports = st->reportList;
    check(Header_GetItemCount(ListView_GetHeader(reports)) == REGULAR_REPORT_COLUMN_COUNT,
          "report list has no label column");
    wchar_t sampleHeader[16]{};
    LVCOLUMNW sampleColumn{};
    sampleColumn.mask = LVCF_TEXT;
    sampleColumn.pszText = sampleHeader;
    sampleColumn.cchTextMax = static_cast<int>(std::size(sampleHeader));
    check(ListView_GetColumn(reports, 0, &sampleColumn) && std::wstring_view(sampleHeader) == L"样本号",
          "sample number is the first report column");
    const auto checkInspectionColors = [&] {
        auto drawReport = [&](int column) {
            NMLVCUSTOMDRAW draw{};
            draw.nmcd.hdr = {reports, REGULAR_IDC_REPORT_LIST, NM_CUSTOMDRAW};
            draw.nmcd.dwDrawStage = CDDS_ITEMPREPAINT | CDDS_SUBITEM;
            draw.iSubItem = column;
            SendMessageW(right, WM_NOTIFY, draw.nmcd.hdr.idFrom, reinterpret_cast<LPARAM>(&draw));
            return draw;
        };
        ListView_SetItemState(reports, 0, 0, LVIS_SELECTED);
        auto& reportRow = st->reportRows.front();
        const auto original = reportRow;
        reportRow.need_inspect = true;
        reportRow.report_type = "0";
        reportRow.zymz_print = "1";
        const auto marked = drawReport(1);
        check(marked.clrTextBk == RGB(211, 211, 211) && marked.clrText == RGB(0, 0, 0),
              "report needing inspection is light gray with black text");
        check(drawReport(REGULAR_RIGHT_REPORT_PRINT_COL).clrTextBk == RGB(166, 236, 154),
              "printed cell retains its own background");
        reportRow.chk_flag = "T";
        reportRow.conf = "T";
        check(drawReport(1).clrTextBk == REGULAR_COLOR_REPORT_REVIEWED,
              "reviewed unsent inspection report remains blue");
        reportRow.chk_flag.clear();
        reportRow.conf = "F";
        reportRow.need_inspect = false;
        const auto cleared = drawReport(1);
        check(cleared.clrTextBk == REGULAR_COLOR_WHITE && cleared.clrText == RGB(230, 0, 0),
              "clearing inspection restores normal emergency text and report background");
        reportRow = original;
        ListView_SetItemState(reports, 0, LVIS_SELECTED, LVIS_SELECTED);
    };
    checkInspectionColors();
    const HWND addItems = GetDlgItem(middle, REGULAR_IDC_ADD_ITEMS);
    const HWND deleteItems = GetDlgItem(middle, REGULAR_IDC_DELETE_ITEMS);
    const HWND saveResults = GetDlgItem(middle, REGULAR_IDC_SAVE_RESULTS);
    const HWND sendReport = GetDlgItem(middle, REGULAR_IDC_SEND_REPORT);
    check(addItems && IsWindowVisible(addItems), "add items visible on the results page");
    check(relativeRect(addItems).bottom <= relativeRect(results).top, "add items sits above result list");
    check(deleteItems && IsWindowVisible(deleteItems), "delete items visible on the results page");
    check(relativeRect(deleteItems).left >= relativeRect(addItems).right &&
          relativeRect(deleteItems).top == relativeRect(addItems).top,
          "delete items sits to the right of add items");
    check(saveResults && IsWindowVisible(saveResults) &&
          relativeRect(saveResults).left >= relativeRect(deleteItems).right &&
          relativeRect(saveResults).top == relativeRect(deleteItems).top,
          "save results sits to the right of delete items");
    check(sendReport && IsWindowVisible(sendReport) &&
          relativeRect(sendReport).left >= relativeRect(saveResults).right &&
          relativeRect(sendReport).top == relativeRect(saveResults).top,
          "send sits immediately to the right of save");
    const auto selectMiddlePage = [&](int page) {
        TabCtrl_SetCurSel(st->middleTab, page);
        NMHDR changed{st->middleTab, REGULAR_IDC_MIDDLE_TAB, TCN_SELCHANGE};
        SendMessageW(middle, WM_NOTIFY, changed.idFrom, reinterpret_cast<LPARAM>(&changed));
    };
    check(TabCtrl_GetItemCount(st->middleTab) == 3, "manual review follows results and picture");
    st->reportConnectionString = "offline";
    st->reportRows[0].chk_flag = "C";
    st->reportRows[0].manual_review = "首行\n第二行";
    selectMiddlePage(REGULAR_MANUAL_REVIEW_TAB);
    auto& manual = st->manualReview;
    check(IsWindowVisible(manual.text) && !IsWindowVisible(results) && IsWindowEnabled(manual.save),
          "preliminary review report allows entering a manual review note");
    check(regularWindowText(manual.text) == L"首行\r\n第二行", "manual review displays multiple lines");
    wchar_t title[24]{};
    TCITEMW tabItem{};
    tabItem.mask = TCIF_TEXT;
    tabItem.pszText = title;
    tabItem.cchTextMax = static_cast<int>(std::size(title));
    check(TabCtrl_GetItem(st->middleTab, REGULAR_MANUAL_REVIEW_TAB, &tabItem) &&
          std::wstring_view(title) == L"手工复查[有]", "saved note changes the tab caption");
    SetWindowTextW(manual.text, L"未保存草稿");
    st->reportRows[0].manual_review = "其他工作站的修改";
    regularSyncManualReviewPage(st, 0);
    check(regularWindowText(manual.text) == L"未保存草稿" && manual.expectedText == "首行\n第二行",
          "refresh preserves the draft and its original database snapshot");
    regularSyncManualReviewPage(st, -1);
    st->reportRows[0].chk_flag = "T";
    regularSyncManualReviewPage(st, 0);
    check(regularWindowText(manual.text) == L"其他工作站的修改" && !IsWindowEnabled(manual.save) &&
          (GetWindowLongPtrW(manual.text, GWL_STYLE) & ES_READONLY),
          "audited notes remain visible and read-only after returning to a report");
    st->reportRows[0] = row;
    st->reportConnectionString.clear();
    regularSyncManualReviewPage(st, -1);
    st->selectedReportIndex = -1;
    selectMiddlePage(1);
    check(!IsWindowVisible(addItems), "add items hidden on the picture page");
    check(!IsWindowVisible(deleteItems), "delete items hidden on the picture page");
    check(!IsWindowVisible(sendReport), "send hidden on the picture page");
    check(!IsWindowVisible(saveResults), "save results hidden on the picture page");
    check(!IsWindowVisible(manual.text), "manual review is hidden on the picture page");
    selectMiddlePage(0);
    st->selectedReportIndex = 0;
    check(IsWindowVisible(addItems), "add items restored on the results page");
    check(IsWindowVisible(deleteItems), "delete items restored on the results page");
    check(IsWindowVisible(saveResults), "save results restored on the results page");
    SetWindowTextW(st->machineEdit, L"Offline Analyzer");
    const int split = st->splitterX;
    const int queryGeneration = st->reportQueryGeneration;
    ListView_SetColumnWidth(reports, REGULAR_RIGHT_REPORT_SAMPLE_COL, 58);
    ListView_SetColumnWidth(reports, 4, 165);
    ListView_SetColumnWidth(reports, 5, 91);
    ListView_SetItemText(reports, 0, REGULAR_RIGHT_REPORT_SAMPLE_COL, const_cast<wchar_t*>(L"123"));
    ListView_SetItemText(reports, 0, REGULAR_RIGHT_REPORT_NAME_COL, const_cast<wchar_t*>(L"Test Patient"));
    ListView_SetItemText(reports, 0, 2, const_cast<wchar_t*>(L"M"));
    ListView_SetItemText(reports, 0, 3, const_cast<wchar_t*>(L"40"));
    ListView_SetItemText(results, 0, 2, const_cast<wchar_t*>(L"ALT"));
    ListView_SetItemText(results, 0, 4, const_cast<wchar_t*>(L"52"));
    ListView_SetItemText(results, 0, 6, const_cast<wchar_t*>(L"0~40"));
    ListView_SetItemText(results, 0, 7, const_cast<wchar_t*>(L"U/L"));
    st->suppressReportSelectionQuery = true;
    item.iItem = 1;
    check(ListView_InsertItem(results, &item) == 1, "second editable result inserted");
    check(ListView_InsertItem(reports, &item) == 1, "second report inserted to catch accidental selection");
    st->reportRows.push_back(search::ReportRow{});
    st->resultRows.resize(2);
    st->resultRows[0].entry_id = "101";
    st->resultRows[0].result = "52";
    st->resultRows[0].result_type = "1";
    st->resultRows[1].entry_id = "102";
    st->resultRows[1].result = "阴性";
    ListView_SetItemText(results, 1, REGULAR_RESULT_VALUE_COL, const_cast<wchar_t*>(L"阴性"));
    st->resultRows[1].result_type = "3";
    st->resultRows[1].result_options = {{"1", "阴性"}, {"2", "阳性"}};
    st->reportConnectionString = "offline";
    st->resultCache.select({"offline", "synthetic", "", "", "", ""});
    st->resultCache.current = regular_comparison::LoadPhase::Ready;
    std::array<int, REGULAR_RESULT_VALUE_COL> leadingWidths{};
    for (int column = 0; column < REGULAR_RESULT_VALUE_COL; ++column) {
        leadingWidths[column] = ListView_GetColumnWidth(results, column);
        ListView_SetColumnWidth(results, column, 24);
    }
    RECT firstResult{};
    check(ListView_GetSubItemRect(results, 0, REGULAR_RESULT_VALUE_COL, LVIR_BOUNDS, &firstResult),
          "first result cell bounds available");
    SendMessageW(results, WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM(firstResult.left + 5, (firstResult.top + firstResult.bottom) / 2));
    check(st->resultEdit != nullptr, "editor opened in the regular results page");
    SendMessageW(st->resultEdit, WM_CHAR, L'a', 0);
    check(regularWindowText(st->resultEdit) == L"52", "numeric result blocks a letter");
    SendMessageW(st->resultEdit, WM_CHAR, L'5', 0);
    check(regularWindowText(st->resultEdit) == L"5", "numeric result accepts a digit");
    SetWindowTextW(st->resultEdit, L"53");
    SendMessageW(st->resultEdit, WM_KILLFOCUS, reinterpret_cast<WPARAM>(saveResults), 0);
    const auto& firstDraft = st->resultDrafts.at({"offline", "synthetic"}).at("101");
    check(firstDraft.expected_result == "52" && firstDraft.result == "53",
          "focus loss stages the current value with its original database value");
    SendMessageW(results, WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM(firstResult.left + 5, (firstResult.top + firstResult.bottom) / 2));
    SetWindowTextW(st->resultEdit, L"52");
    SendMessageW(st->resultEdit, WM_KEYDOWN, VK_RETURN, 0);
    check(st->resultEdit && st->resultEditRow == 1 &&
          ListView_GetNextItem(reports, -1, LVNI_SELECTED) == 0,
          "Enter on the unchanged result advances without writing or changing the report");
    check(st->resultOptions && IsWindowVisible(st->resultOptions) &&
          Header_GetItemCount(ListView_GetHeader(st->resultOptions)) == 2 &&
          ListView_GetNextItem(st->resultOptions, -1, LVNI_SELECTED) == -1,
          "entering a dictionary result opens two columns without preselecting a value");
    wchar_t optionText[32]{};
    ListView_GetItemText(st->resultOptions, 0, 0, optionText, 32);
    check(std::wstring(optionText) == L"阴性", "candidate name uses the primary column");
    ListView_GetItemText(st->resultOptions, 0, 1, optionText, 32);
    check(std::wstring(optionText) == L"1", "candidate code uses the secondary column");
    SendMessageW(st->resultEdit, WM_KEYDOWN, VK_DOWN, 0);
    check(st->resultOptions && IsWindowVisible(st->resultOptions) &&
          ListView_GetItemCount(st->resultOptions) == 2 &&
          ListView_GetNextItem(st->resultOptions, -1, LVNI_SELECTED) == 0,
          "Down opens the dictionary with its first option selected");
    SetWindowTextW(st->resultEdit, L"阳");
    check(IsWindowVisible(st->resultOptions) && ListView_GetItemCount(st->resultOptions) == 1,
          "qualitative result filters the dictionary by name");
    SetWindowTextW(st->resultEdit, L"2");
    ListView_GetItemText(st->resultOptions, 0, 0, optionText, 32);
    check(ListView_GetItemCount(st->resultOptions) == 1 && std::wstring(optionText) == L"阳性",
          "qualitative result filters the dictionary by code");
    SetWindowTextW(st->resultEdit, L"");
    check(IsWindowVisible(st->resultOptions), "clearing the dictionary search exposes all options");
    SetFocus(results);
    check(!st->resultEdit && !st->resultOptions && st->resultRows[1].result == "阴性" &&
          st->resultDrafts.empty(),
          "leaving an empty dictionary search neither clears the result nor stages a draft");
    RECT secondResult{};
    check(ListView_GetSubItemRect(results, 1, REGULAR_RESULT_VALUE_COL, LVIR_BOUNDS, &secondResult),
          "qualitative result cell bounds available");
    const auto reopenQualitativeEditor = [&] {
        SendMessageW(results, WM_LBUTTONDOWN, MK_LBUTTON,
                     MAKELPARAM(secondResult.left + 5, (secondResult.top + secondResult.bottom) / 2));
        check(st->resultEdit && regularWindowText(st->resultEdit) == L"阴性",
              "reopening an abandoned search restores the original result");
        SendMessageW(st->resultEdit, WM_KEYDOWN, VK_DOWN, 0);
        check(IsWindowVisible(st->resultOptions) && ListView_GetItemCount(st->resultOptions) == 2,
              "dictionary can be reopened after an outside click");
    };
    reopenQualitativeEditor();
    NMITEMACTIVATE doubleClick{};
    doubleClick.hdr = {st->resultOptions, 0, NM_DBLCLK};
    doubleClick.iItem = 0;
    SendMessageW(results, WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&doubleClick));
    check(!st->resultEdit && !st->resultOptions && st->resultDrafts.empty(),
          "double-clicking the original candidate confirms it without creating a change");
    reopenQualitativeEditor();
    SetWindowTextW(st->resultEdit, L"阳");
    SetFocus(st->resultOptions);
    check(GetFocus() == st->resultOptions, "candidate list receives focus from the edit control");
    SetFocus(results);
    check(!st->resultEdit && !st->resultOptions && st->resultRows[1].result == "阴性" &&
          st->resultDrafts.empty(),
          "leaving a focused candidate list discards the search text");
    reopenQualitativeEditor();
    SetWindowTextW(st->resultEdit, L"无匹配");
    check(!IsWindowVisible(st->resultOptions), "nonmatching filter hides the candidate list");
    SendMessageW(results, WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM(firstResult.left + 5, (firstResult.top + firstResult.bottom) / 2));
    check(st->resultEdit && st->resultEditRow == 0 && !st->resultOptions &&
          st->resultRows[1].result == "阴性" && st->resultDrafts.empty(),
          "clicking another result discards an unmatched dictionary search");
    reopenQualitativeEditor();
    SendMessageW(st->resultEdit, WM_KEYDOWN, VK_ESCAPE, 0);
    SendMessageW(st->resultEdit, WM_KEYDOWN, VK_ESCAPE, 0);
    st->resultRows[1].result_options.clear();
    SendMessageW(results, WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM(secondResult.left + 5, (secondResult.top + secondResult.bottom) / 2));
    SetWindowTextW(st->resultEdit, L"58");
    SendMessageW(st->resultEdit, WM_KILLFOCUS, reinterpret_cast<WPARAM>(saveResults), 0);
    const auto& draft = st->resultDrafts.at({"offline", "synthetic"});
    check(!st->resultEdit && !st->resultOptions && draft.count("101") == 0 &&
          draft.at("102").expected_result == "阴性" && draft.at("102").result == "58",
          "focus loss preserves free text and closes the result controls");
    st->resultDrafts.clear();
    st->reportConnectionString.clear();
    st->resultRows.clear();
    st->resultCache.invalidate();
    st->reportRows.pop_back();
    ListView_DeleteItem(results, 1);
    ListView_DeleteItem(reports, 1);
    ListView_SetItemText(results, 0, REGULAR_RESULT_VALUE_COL, const_cast<wchar_t*>(L"52"));
    for (int column = 0; column < REGULAR_RESULT_VALUE_COL; ++column)
        ListView_SetColumnWidth(results, column, leadingWidths[column]);
    std::vector<int> resultWidths;
    for (int column = 0; column < Header_GetItemCount(ListView_GetHeader(results)); ++column)
        resultWidths.push_back(ListView_GetColumnWidth(results, column));
    std::vector<int> fullWidths;
    for (int column = 0; column < REGULAR_REPORT_COLUMN_COUNT; ++column)
        fullWidths.push_back(ListView_GetColumnWidth(reports, column));
    for (HWND control : {middle, right, results, reports, st->rightTab, st->rightSearchEdit,
                         st->rightDateTodayButton, st->rightDatePrevButton, st->rightDateNextButton,
                         st->rightAutoRefreshEdit})
        normalLayout.push_back({control, relativeRect(control), GetWindowLongPtrW(control, GWL_EXSTYLE)});
    for (const int id : {5403, 5405, 5407, 5410, 5412, 5413, 5414, 5415, 5417,
                         5418, 5419, 5422, 5423, 5426, 5427, 5428, 5429}) {
        const HWND button = GetDlgItem(st->bottomPanel, id);
        check(button && IsWindowVisible(button) && !IsWindowEnabled(button),
              "unimplemented bottom action remains visible and disabled");
    }
    for (const int id : {REGULAR_IDC_BOTTOM_MACHINE_1, REGULAR_IDC_BOTTOM_MACHINE_2,
                         REGULAR_IDC_BOTTOM_MACHINE_3, REGULAR_IDC_BOTTOM_REFRESH,
                         REGULAR_IDC_BOTTOM_AUDIT, REGULAR_IDC_BOTTOM_PREV_REPORT,
                         REGULAR_IDC_BOTTOM_NEXT_REPORT, REGULAR_IDC_BOTTOM_TRACKING,
                         REGULAR_IDC_BOTTOM_GRAPH, REGULAR_IDC_BOTTOM_TREND, REGULAR_IDC_COMPACT}) {
        const HWND button = GetDlgItem(st->bottomPanel, id);
        check(button && IsWindowEnabled(button), "implemented bottom action remains enabled");
    }
    for (const int id : {REGULAR_IDC_BOTTOM_PRINT_BARCODE, REGULAR_IDC_BOTTOM_BATCH_PRINT_BARCODE}) {
        const HWND button = GetDlgItem(st->bottomPanel, id);
        check(button && (IsWindowEnabled(button) != FALSE) == search::barcode_label_printing_available(),
              "barcode buttons reflect build-time printer support");
    }
    RECT entry{}, audit{};
    GetWindowRect(st->compactButton, &entry);
    GetWindowRect(GetDlgItem(st->bottomPanel, 5410), &audit);
    check(entry.left == audit.left && entry.top > audit.top, "entry below audit-print button");
    enter(st);
    checkInspectionColors();
    HWND floating = st->compactWindow;
    check(!IsWindowVisible(frame) && !IsWindowVisible(other), "only compact window remains visible");
    check(!IsWindowVisible(addItems), "add items hidden in compact mode");
    check(!IsWindowVisible(deleteItems), "delete items hidden in compact mode");
    check(!IsWindowVisible(saveResults), "save results hidden in compact mode");
    check(IsWindowVisible(sendReport) && relativeRect(sendReport).bottom < relativeRect(results).top,
          "send remains available above results in compact mode");
    check((GetWindowLongPtrW(floating, GWL_EXSTYLE) & WS_EX_APPWINDOW) != 0 &&
          GetWindow(floating, GW_OWNER) == nullptr, "compact host has its own taskbar window");
    for (int column = 0; column < REGULAR_REPORT_COLUMN_COUNT; ++column)
        check((ListView_GetColumnWidth(reports, column) > 0) == (column <= 3), "only sample/name/sex/age columns visible");
    RECT defaultReportClient{}, sexColumn{};
    GetClientRect(reports, &defaultReportClient);
    check(defaultReportClient.right == ListView_GetColumnWidth(reports, REGULAR_RIGHT_REPORT_SAMPLE_COL) +
          ListView_GetColumnWidth(reports, REGULAR_RIGHT_REPORT_NAME_COL),
          "default report viewport fits exactly sample number and name");
    ListView_GetSubItemRect(reports, 0, 2, LVIR_BOUNDS, &sexColumn);
    check(sexColumn.left >= defaultReportClient.right, "sex and age start outside the initial report viewport");
    for (int column : {0, 3, 8}) {
        check(ListView_GetColumnWidth(results, column) == 0, "leading result column, name and note hidden");
        ListView_SetColumnWidth(results, column, 80);
        check(ListView_GetColumnWidth(results, column) == 0, "hidden result columns cannot be resized open");
    }
    RECT defaultResultClient{};
    GetClientRect(results, &defaultResultClient);
    const int mainColumnsWidth = ListView_GetColumnWidth(results, 1) +
        ListView_GetColumnWidth(results, 2) + ListView_GetColumnWidth(results, 4);
    check(defaultResultClient.right == mainColumnsWidth,
          "default result viewport fits exactly group, English and result columns");
    checkStoolResultColor(st);
    RECT nextResultColumn{};
    ListView_GetSubItemRect(results, 0, 5, LVIR_BOUNDS, &nextResultColumn);
    check(nextResultColumn.left >= defaultResultClient.right,
          "deviation and remaining result columns start outside the initial viewport");
    for (int column : {1, 2, 4, 5, 6, 7})
        check(ListView_GetColumnWidth(results, column) > 0, "remaining result columns available by scrolling");
    for (HWND date : {st->rightDateTodayButton, st->rightDatePrevButton, st->rightDateNextButton})
        check((GetWindowLongPtrW(date, GWL_STYLE) & WS_VISIBLE) == 0, "compact date buttons hidden");
    for (const auto& saved : normalLayout) {
        if (saved.hwnd == st->rightTab || saved.hwnd == st->rightSearchEdit ||
            saved.hwnd == st->rightDateTodayButton || saved.hwnd == st->rightDatePrevButton ||
            saved.hwnd == st->rightDateNextButton) {
            RECT current = relativeRect(saved.hwnd);
            check(EqualRect(&current, &saved.rect) != FALSE, "hidden full-page toolbar geometry unchanged");
        }
    }
    RECT compactMiddleClient{};
    GetClientRect(middle, &compactMiddleClient);
    check(compactMiddleClient.bottom - relativeRect(results).bottom == regularS(floating, REGULAR_PAD),
          "compact result list reaches the panel bottom padding");
    ListView_SetColumnWidth(reports, 6, 80);
    check(ListView_GetColumnWidth(reports, 6) == 0, "hidden columns cannot be resized open");
    check((GetWindowLongPtrW(st->rightSearchEdit, GWL_STYLE) & WS_VISIBLE) == 0,
          "right-side search toolbar hidden");
    RECT refresh{}, reportRect{}, rightRect{};
    GetWindowRect(st->compactRefreshButton, &refresh);
    GetWindowRect(reports, &reportRect);
    GetWindowRect(right, &rightRect);
    check(reportRect.top - rightRect.top < regularS(right, 20), "report list fills removed statistics area");
    RECT unit{};
    GetWindowRect(st->rightAutoRefreshUnitLabel, &unit);
    check(reportRect.right - reportRect.left == unit.right - refresh.left,
          "report list width equals the auto-refresh row");
    check(reportRect.right - reportRect.left < regularS(right, 260), "right list narrowed after removing order column");
    check(refresh.bottom < unit.top, "refresh and auto-refresh controls use separate rows");
    MINMAXINFO limits{};
    SendMessageW(floating, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
    check(limits.ptMinTrackSize.x < regularS(floating, 800), "compact minimum width reduced");
    SetWindowPos(floating, nullptr, 0, 0, limits.ptMinTrackSize.x, limits.ptMinTrackSize.y,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT narrowed{};
    GetWindowRect(reports, &narrowed);
    check(narrowed.right - narrowed.left == reportRect.right - reportRect.left,
          "right pane stays at button-row width when compact window shrinks");
    check((GetWindowLongPtrW(floating, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0, "floating window is topmost");
    check(GetParent(middle) == floating && GetParent(right) == floating &&
          GetParent(st->splitter) == floating, "panels detached into floating host");
    check((GetWindowLongPtrW(st->leftPanel, GWL_STYLE) & WS_VISIBLE) == 0 &&
          (GetWindowLongPtrW(st->bottomPanel, GWL_STYLE) & WS_VISIBLE) == 0, "full-page extras hidden");
    check(st->reportQueryGeneration == queryGeneration, "switching did not launch a report query");
    check(regularWindowText(st->compactSummary) == L"Offline Analyzer    " +
          search::utf8_to_wide(regularDatePickerValue(st->inspectDatePicker)),
          "summary only contains instrument name and date");
    check(create_regular_report_module(context) == report && st->compactWindow == floating,
          "reopening module reuses existing floating window");
    check(st->splitterX == split, "compact resize does not change the full-page divider");
    check((GetWindowLongPtrW(st->splitter, GWL_STYLE) & WS_VISIBLE) == 0,
          "fixed-width compact pane has no draggable divider");
    MSG key{};
    key.hwnd = reports;
    key.message = WM_KEYDOWN;
    SendMessageW(report, app::WM_APP_FONT_CHANGED, 0, reinterpret_cast<LPARAM>(context.uiFont));
    check(st->compactWindow == floating && st->resultList == results, "font broadcast preserves detached controls");
    SendMessageW(floating, WM_CLOSE, 0, 0);
    restored(st, middle, right, results, reports, split);
    check(IsWindowVisible(addItems) && IsWindowVisible(deleteItems), "result toolbar restored after compact mode");
    check(!IsWindow(floating), "close destroyed floating host");
    for (int column = 0; column < REGULAR_REPORT_COLUMN_COUNT; ++column)
        check(ListView_GetColumnWidth(reports, column) == fullWidths[column], "all custom column widths restored exactly");
    for (int column = 0; column < static_cast<int>(resultWidths.size()); ++column)
        check(ListView_GetColumnWidth(results, column) == resultWidths[column], "all original result column widths restored");
    check(IsZoomed(report) != FALSE, "maximized MDI report restored");
    check(IsWindowVisible(other), "other application window restored");
    for (int cycle = 0; cycle < 4; ++cycle) {
        enter(st);
        SendMessageW(st->compactWindow, WM_CLOSE, 0, 0);
        restored(st, middle, right, results, reports, split);
    }
    enter(st);
    key.hwnd = reports;
    key.wParam = VK_ESCAPE;
    check(regular_report_translate_compact_message(&key), "Escape handled");
    restored(st, middle, right, results, reports, split);
    enter(st);
    DestroyWindow(st->compactWindow);
    restored(st, middle, right, results, reports, split);
    enter(st);
    RECT originalFrame{};
    GetWindowRect(frame, &originalFrame);
    SetWindowPos(frame, nullptr, 0, 0, 1280, 780, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SendMessageW(st->compactWindow, WM_CLOSE, 0, 0);
    RECT fullClient{}, pane = relativeRect(right);
    GetClientRect(report, &fullClient);
    check(pane.right == fullClient.right - regularS(report, REGULAR_GAP),
          "restored right panel follows the resized full-page viewport");
    RECT normalList = relativeRect(reports), searchBox = relativeRect(st->rightSearchEdit);
    check(normalList.top > searchBox.bottom, "restored list does not overlap its normal search toolbar");
    check(IsWindowVisible(st->rightSearchEdit) && IsWindowVisible(st->rightDateNextButton),
          "normal right-side toolbar restored after hidden frame resize");
    SetWindowPos(frame, nullptr, 0, 0, originalFrame.right - originalFrame.left,
                 originalFrame.bottom - originalFrame.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    // Preserve hidden tabs across a round trip too, without loading any database data.
    st->reportRows.clear();
    ListView_DeleteAllItems(reports);
    st->selectedReportIndex = -1;
    TabCtrl_SetCurSel(st->middleTab, 1);
    TabCtrl_SetCurSel(st->rightTab, 1);
    enter(st);
    check(TabCtrl_GetCurSel(st->middleTab) == 0 && TabCtrl_GetCurSel(st->rightTab) == 0,
          "compact mode forces the two requested list pages");
    SendMessageW(st->compactWindow, WM_CLOSE, 0, 0);
    check(TabCtrl_GetCurSel(st->middleTab) == 1 && TabCtrl_GetCurSel(st->rightTab) == 1,
          "original tabs restored");
    auto suspended = app::window_task_detail::suspendSessionTasks();
    SendMessageW(st->compactButton, BM_CLICK, 0, 0);
    check(!st->compactWindow, "suspended session cannot open compact window");
    app::window_task_detail::discardSuspendedTasks();
    enter(st);
    floating = st->compactWindow;
    SendMessageW(mdi, WM_MDIDESTROY, reinterpret_cast<WPARAM>(report), 0);
    check(!IsWindow(report) && !IsWindow(floating), "closing module also destroys floating host");
    check(IsWindowVisible(frame) && IsWindowVisible(other), "closing only the report restores other windows");
    report = create_regular_report_module(context);
    st = reinterpret_cast<RegularReportState*>(GetPropW(report, REGULAR_REPORT_PROP_STATE));
    check(st != nullptr, "second report module created");
    KillTimer(report, IDT_REPORT_INITIAL_QUICK_MACHINE);
    st->initialQuickMachineTimerActive = false;
    enter(st);
    floating = st->compactWindow;
    app::window_task_detail::invalidateSession();
    SendMessageW(mdi, WM_MDIDESTROY, reinterpret_cast<WPARAM>(report), 0);
    check(!IsWindow(floating) && !IsWindowVisible(frame) && !IsWindowVisible(other),
          "session teardown does not re-show old windows");
    DestroyWindow(other);
    DestroyWindow(frame);
    std::cout << "Regular report compact window lifecycle passed\n";
}
