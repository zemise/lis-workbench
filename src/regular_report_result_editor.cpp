#include "regular_report_result_editor.h"

#ifdef _WIN32

#include "regular_report_state.h"
#include "search_text.h"

#include <windowsx.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <optional>
#include <string>
#include <utility>

void regularFinishResultEdit(RegularReportState* st, RegularResultEditExit exit, bool restore) {
    if (!st || !st->resultEdit) return;
    const HWND ed = st->resultEdit;
    const int row = st->resultEditRow;
    const auto key = std::exchange(st->resultEditKey, std::nullopt);
    // The dictionary editor also holds the search filter. Only an explicit
    // confirmation can turn that text into a result.
    const bool stage = row >= 0 && row < static_cast<int>(st->resultRows.size()) &&
        (exit == RegularResultEditExit::confirm ||
         (exit == RegularResultEditExit::leave && st->resultRows[row].result_options.empty()));
    auto text = stage ? search::trim(search::wide_to_utf8(regularWindowText(ed))) : std::string{};
    st->resultEdit = nullptr; st->resultEditRow = -1;
    if (const HWND choices = std::exchange(st->resultOptions, nullptr)) DestroyWindow(choices);
    if (stage) {
        auto& item = st->resultRows[static_cast<size_t>(row)];
        if (key && !item.entry_id.empty() && item.result != text) {
            auto& draft = st->resultDrafts[*key];
            auto [entry, inserted] = draft.try_emplace(item.entry_id,
                search::ReportResultChange{item.entry_id, item.result, text});
            if (!inserted) entry->second.result = text;
            if (entry->second.expected_result == text) draft.erase(entry);
            if (draft.empty()) st->resultDrafts.erase(*key);
        }
        item.result = std::move(text);
        auto display = search::utf8_to_wide(item.result);
        ListView_SetItemText(st->resultList, row, REGULAR_RESULT_VALUE_COL, display.data());
        ListView_RedrawItems(st->resultList, row, row);
    }
    DestroyWindow(ed);
    if (restore && st->resultList &&
        (!st->compactAttached || GetForegroundWindow() == st->compactWindow))
        SetFocus(st->resultList);
}

namespace {

constexpr UINT_PTR REGULAR_RESULT_EDIT_SUBCLASS = 6213;
constexpr UINT_PTR REGULAR_RESULT_OPTIONS_SUBCLASS = 6217;

bool numericResultCharacter(wchar_t ch) {
    return (ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L'-' ||
           ch == L'+' || ch == L'<' || ch == L'>' || ch == L'|' || ch == L' ';
}

bool numericResultClipboard(HWND hwnd) {
    if (!OpenClipboard(hwnd)) return false;
    const HANDLE data = GetClipboardData(CF_UNICODETEXT);
    const auto* value = data ? static_cast<const wchar_t*>(GlobalLock(data)) : nullptr;
    const bool allowed = value && std::all_of(value, value + std::wcslen(value), numericResultCharacter);
    if (value) GlobalUnlock(data);
    CloseClipboard();
    return allowed;
}

void acceptResultOption(RegularReportState* st, int clickedRow = -1) {
    if (!st->resultEdit || st->resultEditRow < 0 ||
        app::window_task_detail::sessionSuspended()) return;
    const auto& options = st->resultRows[st->resultEditRow].result_options;
    if (st->resultOptions && IsWindowVisible(st->resultOptions)) {
        const int selected = clickedRow >= 0 ? clickedRow :
            ListView_GetNextItem(st->resultOptions, -1, LVNI_SELECTED);
        if (selected >= 0) {
            LVITEMW item{};
            item.mask = LVIF_PARAM;
            item.iItem = selected;
            if (ListView_GetItem(st->resultOptions, &item) &&
                static_cast<size_t>(item.lParam) < options.size()) {
                SetFocus(st->resultEdit);
                SetWindowTextW(st->resultEdit, search::utf8_to_wide(options[item.lParam].name).c_str());
            }
        } else {
            const auto value = search::trim(search::wide_to_utf8(regularWindowText(st->resultEdit)));
            const auto match = std::find_if(options.begin(), options.end(),
                [&](const auto& option) { return !value.empty() && option.code == value; });
            if (value != st->resultRows[st->resultEditRow].result &&
                match != options.end() && std::count_if(options.begin(), options.end(), [&](const auto& option) {
                    return option.code == value;
                }) == 1 && std::none_of(options.begin(), options.end(), [&](const auto& option) {
                    return option.name == value;
                })) {
                SetWindowTextW(st->resultEdit, search::utf8_to_wide(match->name).c_str());
            }
        }
    }
    regularSaveEditedResultAndAdvance(st);
}

LRESULT CALLBACK resultOptionsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                   UINT_PTR sid, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_GETDLGCODE: return DefSubclassProc(hwnd, msg, wp, lp) | DLGC_WANTALLKEYS;
        case WM_KEYDOWN:
            if (wp == VK_RETURN) {
                if ((lp & (1LL << 30)) == 0) acceptResultOption(st);
                return 0;
            }
            if (wp == VK_ESCAPE) {
                SetFocus(st->resultEdit);
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;
        case WM_CHAR:
            if (wp == L'\r' || wp == 27) return 0;
            break;
        case WM_KILLFOCUS:
            if (st->resultOptions == hwnd && reinterpret_cast<HWND>(wp) != st->resultEdit &&
                !app::window_task_detail::sessionSuspended()) {
                regularFinishResultEdit(st, RegularResultEditExit::leave, false);
                return 0;
            }
            break;
        case WM_NCDESTROY: RemoveWindowSubclass(hwnd, resultOptionsProc, sid); break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT drawResultOptions(NMLVCUSTOMDRAW& draw) {
    const auto stage = draw.nmcd.dwDrawStage;
    if (stage == CDDS_PREPAINT) {
        HIGHCONTRASTW contrast{sizeof(contrast)};
        if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
            (contrast.dwFlags & HCF_HIGHCONTRASTON)) return CDRF_DODEFAULT;
        return CDRF_NOTIFYITEMDRAW;
    }
    if (stage != CDDS_ITEMPREPAINT && stage != (CDDS_ITEMPREPAINT | CDDS_SUBITEM))
        return CDRF_DODEFAULT;

    const int row = static_cast<int>(draw.nmcd.dwItemSpec);
    const bool selected = ListView_GetItemState(draw.nmcd.hdr.hwndFrom, row, LVIS_SELECTED) != 0;
    draw.nmcd.uItemState &= ~CDIS_SELECTED;
    draw.clrTextBk = selected ? RGB(220, 237, 251) : (row % 2 ? RGB(244, 248, 251) : RGB(255, 255, 255));
    draw.clrText = selected ? RGB(25, 73, 115) : (draw.iSubItem == 1 ? RGB(80, 99, 117) : RGB(32, 48, 68));
    return stage == CDDS_ITEMPREPAINT ? CDRF_NOTIFYSUBITEMDRAW : CDRF_DODEFAULT;
}

void showResultOptions(RegularReportState* st, bool allOptions) {
    if (!st->resultEdit || st->resultEditRow < 0) return;
    const auto& options = st->resultRows[st->resultEditRow].result_options;
    if (options.empty()) return;
    if (!st->resultOptions) {
        st->resultOptions = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_BORDER | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SINGLESEL |
            LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
            0, 0, regularS(st->hwnd, 280), regularS(st->hwnd, 120), st->resultList,
            nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!st->resultOptions) return;
        ListView_SetExtendedListViewStyle(st->resultOptions,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        SetWindowSubclass(st->resultOptions, resultOptionsProc, REGULAR_RESULT_OPTIONS_SUBCLASS,
                          reinterpret_cast<DWORD_PTR>(st));
        SendMessageW(st->resultOptions, WM_SETFONT, reinterpret_cast<WPARAM>(st->ctx.uiFont), FALSE);
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.cx = regularS(st->hwnd, 208);
        column.pszText = const_cast<wchar_t*>(L"候选结果");
        ListView_InsertColumn(st->resultOptions, 0, &column);
        column.cx = regularS(st->hwnd, 64);
        column.pszText = const_cast<wchar_t*>(L"编号");
        ListView_InsertColumn(st->resultOptions, 1, &column);
    }
    const HWND choices = st->resultOptions;
    const auto foldCase = [](std::wstring value) {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return std::towlower(ch); });
        return value;
    };
    const auto filter = allOptions ? std::wstring{} : foldCase(regularWindowText(st->resultEdit));
    const bool redraw = IsWindowVisible(choices);
    if (redraw) SendMessageW(choices, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(choices);
    int nameWidth = regularS(st->hwnd, 204);
    int codeWidth = regularS(st->hwnd, 64);
    const int textPadding = regularS(st->hwnd, 20);
    for (size_t i = 0; i < options.size(); ++i) {
        auto name = search::utf8_to_wide(options[i].name);
        auto code = search::utf8_to_wide(options[i].code);
        nameWidth = std::max(nameWidth, ListView_GetStringWidth(choices, name.c_str()) + textPadding);
        codeWidth = std::max(codeWidth, ListView_GetStringWidth(choices, code.c_str()) + textPadding);
        if (foldCase(name).find(filter) == std::wstring::npos && foldCase(code).compare(0, filter.size(), filter) != 0)
            continue;
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = ListView_GetItemCount(choices);
        item.pszText = name.data();
        item.lParam = static_cast<LPARAM>(i);
        const int index = ListView_InsertItem(choices, &item);
        if (index < 0) continue;
        ListView_SetItemText(choices, index, 1, code.data());
    }
    const int count = ListView_GetItemCount(choices);
    if (redraw) SendMessageW(choices, WM_SETREDRAW, TRUE, 0);
    if (count <= 0) {
        ShowWindow(choices, SW_HIDE);
        return;
    }
    RECT cell{}, client{}, header{};
    GetWindowRect(st->resultEdit, &cell);
    MapWindowPoints(nullptr, st->resultList, reinterpret_cast<POINT*>(&cell), 2);
    GetClientRect(st->resultList, &client);
    if (GetWindowRect(ListView_GetHeader(st->resultList), &header))
        MapWindowPoints(nullptr, st->resultList, reinterpret_cast<POINT*>(&header), 2);
    RECT optionRow{}, optionHeader{};
    ListView_GetItemRect(choices, 0, &optionRow, LVIR_BOUNDS);
    GetWindowRect(ListView_GetHeader(choices), &optionHeader);
    const int itemHeight = optionRow.bottom - optionRow.top;
    const int verticalChrome = optionHeader.bottom - optionHeader.top + 2 * GetSystemMetrics(SM_CYBORDER);
    const int desiredHeight = verticalChrome + std::min(8, count) * itemHeight;
    const int below = client.bottom - cell.bottom;
    const int above = cell.top - header.bottom;
    const bool openAbove = below < desiredHeight && above > below;
    const int room = openAbove ? above : below;
    if (itemHeight <= 0 || room < verticalChrome + itemHeight || client.right <= 0) {
        ShowWindow(choices, SW_HIDE);
        return;
    }
    const int visibleRows = std::min({8, count, (room - verticalChrome) / itemHeight});
    const int height = verticalChrome + visibleRows * itemHeight;
    const int horizontalChrome = 2 * GetSystemMetrics(SM_CXBORDER) +
        (visibleRows < count ? GetSystemMetrics(SM_CXVSCROLL) : 0);
    const int desiredWidth = std::min(regularS(st->hwnd, 440), nameWidth + codeWidth + horizontalChrome);
    const int width = std::min<int>(client.right, std::max<int>(cell.right - cell.left, desiredWidth));
    const int columnsWidth = std::max(1, width - horizontalChrome);
    codeWidth = std::min(codeWidth, columnsWidth / 3);
    ListView_SetColumnWidth(choices, 0, columnsWidth - codeWidth);
    ListView_SetColumnWidth(choices, 1, codeWidth);
    SetWindowPos(choices, HWND_TOP, std::min<int>(cell.left, client.right - width),
                 openAbove ? cell.top - height : cell.bottom, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(choices, nullptr, TRUE);
}

LRESULT CALLBACK resultEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR sid, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    const bool numeric = st->resultEditRow >= 0 &&
                         st->resultRows[st->resultEditRow].numeric_result();
    switch (msg) {
        case WM_GETDLGCODE:
            return DefSubclassProc(hwnd, msg, wp, lp) | DLGC_WANTALLKEYS;
        case WM_KEYDOWN:
            if (wp == VK_RETURN) {
                if ((lp & (1LL << 30)) == 0) acceptResultOption(st);
                return 0;
            }
            if ((wp == VK_UP || wp == VK_DOWN) && !st->resultRows[st->resultEditRow].result_options.empty()) {
                if (!IsWindowVisible(st->resultOptions)) showResultOptions(st, true);
                if (st->resultOptions && IsWindowVisible(st->resultOptions)) {
                    const int count = ListView_GetItemCount(st->resultOptions);
                    const int current = ListView_GetNextItem(st->resultOptions, -1, LVNI_SELECTED);
                    const int next = current < 0 ? (wp == VK_DOWN ? 0 : count - 1) :
                        std::clamp(current + (wp == VK_DOWN ? 1 : -1), 0, count - 1);
                    ListView_SetItemState(st->resultOptions, next, LVIS_SELECTED | LVIS_FOCUSED,
                                          LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_EnsureVisible(st->resultOptions, next, FALSE);
                    return 0;
                }
            }
            if (wp == VK_ESCAPE && IsWindowVisible(st->resultOptions)) {
                ShowWindow(st->resultOptions, SW_HIDE);
                return 0;
            }
            if (wp == VK_ESCAPE) { regularFinishResultEdit(st, RegularResultEditExit::discard); return 0; }
            break;
        case WM_CHAR:
            if (wp == L'\r' || wp == 27 || (numeric && wp >= L' ' && !numericResultCharacter(wp))) return 0;
            break;
        case WM_PASTE:
            if (numeric && !numericResultClipboard(hwnd)) return 0;
            break;
        case WM_IME_STARTCOMPOSITION: case WM_IME_COMPOSITION: case WM_IME_CHAR:
            if (numeric) return 0;
            break;
        case WM_KILLFOCUS:
            if (st->resultEdit == hwnd &&
                (!st->resultOptions || reinterpret_cast<HWND>(wp) != st->resultOptions) &&
                !app::window_task_detail::sessionSuspended()) {
                regularFinishResultEdit(st, RegularResultEditExit::leave, false);
                return 0;
            }
            break;
        case WM_NCDESTROY: RemoveWindowSubclass(hwnd, resultEditProc, sid); break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

}  // namespace

void regularBeginResultEdit(RegularReportState* st, int row) {
    if (!st || !st->resultList || row < 0 ||
        row >= static_cast<int>(st->resultRows.size()) ||
        st->resultCache.current != regular_comparison::LoadPhase::Ready ||
        st->resultRows[row].entry_id.empty()) return;
    regularFinishResultEdit(st, RegularResultEditExit::leave, false);
    ListView_SetItemState(st->resultList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(st->resultList, row, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(st->resultList, row, FALSE);
    RECT rc{};
    if (!ListView_GetSubItemRect(st->resultList, row, REGULAR_RESULT_VALUE_COL,
                                  LVIR_BOUNDS, &rc)) return;
    RECT cl{}; GetClientRect(st->resultList, &cl);
    if (rc.right <= cl.left || rc.left >= cl.right ||
        rc.bottom <= cl.top || rc.top >= cl.bottom) return;
    rc.left = std::max(rc.left, cl.left); rc.right = std::min(rc.right, cl.right);
    InflateRect(&rc, -1, -1);
    const std::wstring txt =
        search::utf8_to_wide(st->resultRows[static_cast<size_t>(row)].result);
    st->resultEdit = CreateWindowExW(0, L"EDIT", txt.c_str(),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        rc.left, rc.top, std::max(24, static_cast<int>(rc.right - rc.left)),
        std::max(20, static_cast<int>(rc.bottom - rc.top)),
        st->resultList, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!st->resultEdit) return;
    st->resultEditRow = row;
    if (st->resultCache.key) {
        const auto& key = *st->resultCache.key;
        st->resultEditKey.emplace(key[0], key[1]);
    }
    SendMessageW(st->resultEdit, EM_SETLIMITTEXT, 1000, 0);
    SendMessageW(st->resultEdit, WM_SETFONT, reinterpret_cast<WPARAM>(st->ctx.uiFont), TRUE);
    SetWindowSubclass(st->resultEdit, resultEditProc, REGULAR_RESULT_EDIT_SUBCLASS,
                      reinterpret_cast<DWORD_PTR>(st));
    SendMessageW(st->resultEdit, EM_SETSEL, 0, -1);
    SetFocus(st->resultEdit);
    showResultOptions(st, true);
}

LRESULT CALLBACK regularResultListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR sid, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_COMMAND:
            if (st->resultEdit && reinterpret_cast<HWND>(lp) == st->resultEdit && HIWORD(wp) == EN_CHANGE)
                showResultOptions(st, false);
            break;
        case WM_NOTIFY: {
            auto* notification = reinterpret_cast<NMHDR*>(lp);
            if (!notification || notification->hwndFrom != st->resultOptions) break;
            if (notification->code == NM_CUSTOMDRAW)
                return drawResultOptions(*reinterpret_cast<NMLVCUSTOMDRAW*>(lp));
            if (notification->code == NM_DBLCLK) {
                const int row = reinterpret_cast<NMITEMACTIVATE*>(lp)->iItem;
                if (row >= 0) acceptResultOption(st, row);
                return 0;
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            LVHITTESTINFO hit{};
            hit.pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            const int row = ListView_SubItemHitTest(hwnd, &hit);
            if (row >= 0 && hit.iSubItem == REGULAR_RESULT_VALUE_COL) {
                regularBeginResultEdit(st, row);
                return 0;
            }
            regularFinishResultEdit(st, RegularResultEditExit::leave, false);
            break;
        }
        case WM_HSCROLL: case WM_VSCROLL: case WM_MOUSEWHEEL:
            regularFinishResultEdit(st, RegularResultEditExit::leave, false); break;
        case WM_NCDESTROY: RemoveWindowSubclass(hwnd, regularResultListProc, sid); break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

#endif
