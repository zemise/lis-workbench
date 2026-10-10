#include "scheduled_result_check_module.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "main_app.h"
#include "machine_picker_popup.h"
#include "regular_report_module.h"
#include "resource.h"
#include "scheduled_result_check_core.h"
#include "scheduled_result_check_store.h"
#include "search_core.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"
#include "window_task.h"

#include <algorithm>
#include <array>
#include <commctrl.h>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <map>
#include <shellapi.h>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windowsx.h>
#include <dwmapi.h>

namespace {

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

constexpr const wchar_t *TITLE = L"定时核查结果";
constexpr const wchar_t *CLASS_NAME = L"ScheduledResultCheckWindow";
constexpr const wchar_t *REMINDER_CLASS_NAME = L"ScheduledResultReminderWindow";
constexpr const wchar_t *CONFIG_SECTION = L"ScheduledResultCheck";
constexpr UINT REMINDER_ICON_ID = 0x5343;
constexpr int REMINDER_WIDTH = 390;
constexpr int REMINDER_EXPANDED_HEIGHT = 240;
constexpr int REMINDER_COLLAPSED_HEIGHT = 62;
constexpr size_t REMINDER_VISIBLE_ROWS = 3;
constexpr int IDC_NAME = 8101, IDC_LEFT = 8102, IDC_OP = 8103, IDC_RIGHT = 8104,
              IDC_SAVE = 8105;
constexpr int IDC_DELETE = 8106, IDC_SCAN = 8108,
              IDC_RULES = 8109, IDC_ALERTS = 8110;
constexpr int IDC_HANDLED = 8111, IDC_STATUS = 8112;
constexpr int IDC_NEW = 8113, IDC_VALUE_MODE = 8115,
              IDC_VALUE = 8116, IDC_MACHINE = 8117, IDC_ALERT_SEARCH = 8118,
              IDC_HANDLED_ALL = 8119, IDC_TABS = 8120, IDC_PROGRESS = 8121,
              IDC_MULTIPLIER = 8122, IDC_TOLERANCE = 8123,
              IDC_GROUP_MODE = 8124, IDC_ADD_CONDITION = 8126,
              IDC_REMOVE_CONDITION = 8127,
              IDC_ROW_OPTIONS = 8129, IDC_ROW_MORE = 8130, IDC_EDIT_RULE = 8131, IDC_BACK = 8132;

// Each condition row and the persisted operator use the same stable order.
struct ComparisonOption {
  const char *op;
  const wchar_t *symbol;
};
constexpr ComparisonOption COMPARISONS[] = {
    {">", L">"}, {">=", L">="}, {"<", L"<"},
    {"<=", L"<="}, {"=", L"="}, {"!=", L"!="},
};
constexpr int COMPARISON_COUNT = static_cast<int>(std::size(COMPARISONS));

struct ScanResult {
  bool ok = false;
  std::string error;
  std::string dictionary_warning;
  std::string progress_warning;
  int row_count = 0;
  int match_count = 0;
  int skipped = 0;
  std::vector<scheduled_check::Alert> fresh;
};
struct Monitor {
  HWND main = nullptr;
  ModuleContext ctx;
  app::WindowTask task;
  bool rescan_requested = false;
  bool full_rescan_requested = false;
  HWND reminder = nullptr;
  HFONT reminder_font = nullptr;
  bool reminder_expanded = true;
  bool notification_icon_added = false;
  std::vector<scheduled_check::Alert> unhandled;
  bool reminder_has_custom_position = false;
  int reminder_x = 0;
  int reminder_y = 0;
  bool reminder_dragging = false;
  bool reminder_drag_moved = false;
  int reminder_pressed_alert_id = 0;
  POINT reminder_drag_cursor{};
  POINT reminder_drag_origin{};
} g_monitor;
HWND g_page = nullptr;

struct ConditionRow {
  scheduled_check::Condition data;
  HWND number = nullptr, left = nullptr, op = nullptr, kind = nullptr,
       right = nullptr, value = nullptr, options = nullptr, more = nullptr,
       remove = nullptr, multiplierLabel = nullptr, multiplier = nullptr,
       toleranceLabel = nullptr, tolerance = nullptr, separator = nullptr;
  std::string leftDraft, rightDraft;
  bool optionsExpanded = false;
};

struct State {
  ModuleContext ctx;
  HWND nameLabel = nullptr, machineLabel = nullptr, rulesTitle = nullptr,
       alertsTitle = nullptr;
  HWND name = nullptr, newRule = nullptr, save = nullptr, del = nullptr,
       editRule = nullptr, back = nullptr, scan = nullptr,
       machine = nullptr, machineButton = nullptr;
  HWND ruleDialog = nullptr, formPane = nullptr, dialogStatus = nullptr;
  HWND rulesPane = nullptr, groupPrefix = nullptr, groupSuffix = nullptr;
  HWND groupMode = nullptr, addCondition = nullptr;
  std::vector<ConditionRow> conditionRows;
  int editingRuleId = 0;
  bool editing = false;
  bool loadingCondition = false;
  int rulesScroll = 0;
  int formScroll = 0;
  HWND rulesList = nullptr, alertsList = nullptr, handled = nullptr,
       handledAll = nullptr, status = nullptr, tabs = nullptr,
       alertSearch = nullptr, progress = nullptr;
  std::vector<search::ScheduledCheckItemOption> items;
  std::vector<search::ScheduledCheckItemOption> allItems;
  std::vector<scheduled_check::Rule> rules;
  std::vector<scheduled_check::Alert> alerts;
  std::vector<scheduled_check::Alert> allAlerts;
  app::WindowTask itemTask;
  app::WindowTask machineItemTask;
  std::string roomCode, machCode, machName;
  std::unordered_set<std::string> machineItemCodes;
  bool machineItemsLoaded = false;
  bool dictionaryLoaded = false;
  std::wstring alertFilter;
  int alertsSortColumn = 0;
  bool alertsSortAscending = true;
  bool rulesExpanded = false;
  bool syncingRules = false;
};

void refresh(State *st);
void loadAlerts(State *st);
void updateReminder(bool emphasize = false);
void applyRuleEditorVisibility(State *st);
void layout(HWND hwnd, State *st);
void updateConditionRows(State *st);
void layoutRuleDialog(State *st);
bool createRuleDialog(State *st);
void showRuleDialog(State *st);
void closeRuleDialog(State *st);

COLORREF mixColor(COLORREF a, COLORREF b, int aWeight, int bWeight) {
  return RGB((GetRValue(a) * aWeight + GetRValue(b) * bWeight) /
                 (aWeight + bWeight),
             (GetGValue(a) * aWeight + GetGValue(b) * bWeight) /
                 (aWeight + bWeight),
             (GetBValue(a) * aWeight + GetBValue(b) * bWeight) /
                 (aWeight + bWeight));
}

COLORREF reminderAccentColor() {
  DWORD value = 0;
  BOOL opaque = FALSE;
  if (SUCCEEDED(DwmGetColorizationColor(&value, &opaque))) {
    const COLORREF color = static_cast<COLORREF>(value) & 0x00FFFFFF;
    if (color != 0)
      return color;
  }
  return RGB(216, 78, 55);
}

bool reminderAccentIsLight(COLORREF accent) {
  return (GetRValue(accent) * 299 + GetGValue(accent) * 587 +
          GetBValue(accent) * 114) /
             1000 >
         150;
}

std::wstring w(const std::string &s) { return search::utf8_to_wide(s); }
std::wstring multiplierSuffix(const std::string &text) {
  return text == "1" ? L"" : L" × " + w(text);
}
std::wstring toleranceSuffix(const std::string &text) {
  double percent = 0.0;
  if (!scheduled_check::parse_number(text, percent) || percent == 0.0)
    return L"";
  return L"（±" + w(text) + L"%）";
}
std::wstring alertLine(const scheduled_check::Alert &a) {
  if (!a.condition_summary.empty()) return w(a.condition_summary);
  const std::wstring right = a.compare_with_value
                                 ? w(a.right_result_text)
                                 : w(a.right_item_name) + L" " +
                                       w(a.right_result_text) +
                                       multiplierSuffix(a.right_multiplier_text);
  return w(a.left_item_name) + L" " + w(a.left_result_text) + L" " + w(a.op) +
         L" " + right + toleranceSuffix(a.tolerance_percent_text);
}
std::wstring reminderItemName(const std::string &english,
                              const std::string &chinese,
                              const std::string &code) {
  return w(scheduled_check::item_display_name(english, chinese, code));
}
std::wstring reminderAlertLine(const scheduled_check::Alert &a) {
  if (!a.condition_summary.empty()) return w(a.condition_summary);
  const std::wstring right =
      a.compare_with_value
          ? w(a.right_result_text)
          : reminderItemName(a.right_item_eng, a.right_item_name,
                             a.right_item_code) +
                L" " + w(a.right_result_text) +
                multiplierSuffix(a.right_multiplier_text);
  return reminderItemName(a.left_item_eng, a.left_item_name,
                          a.left_item_code) +
         L" " + w(a.left_result_text) + L" " + w(a.op) + L" " + right +
         toleranceSuffix(a.tolerance_percent_text);
}
std::string windowText(HWND h) {
  int n = GetWindowTextLengthW(h);
  std::wstring x(static_cast<size_t>(n) + 1, L'\0');
  if (n)
    GetWindowTextW(h, x.data(), n + 1);
  x.resize(static_cast<size_t>(n));
  return search::wide_to_utf8(x);
}
void setItem(HWND list, int row, int col, const std::wstring &value) {
  if (col == 0) {
    LVITEMW it{};
    it.mask = LVIF_TEXT;
    it.iItem = row;
    it.pszText = const_cast<wchar_t *>(value.c_str());
    ListView_InsertItem(list, &it);
  } else
    ListView_SetItemText(list, row, col, const_cast<wchar_t *>(value.c_str()));
}
int selected(HWND list) {
  return ListView_GetNextItem(list, -1, LVNI_SELECTED);
}

template <size_t N>
void copyFixed(wchar_t (&target)[N], const std::wstring &value) {
  wcsncpy(target, value.c_str(), N - 1);
  target[N - 1] = L'\0';
}

void activateMainWindow() {
  if (!g_monitor.main)
    return;
  if (IsIconic(g_monitor.main))
    ShowWindow(g_monitor.main, SW_RESTORE);
  ShowWindow(g_monitor.main, SW_MAXIMIZE);
  SetForegroundWindow(g_monitor.main);
}

void openReminderCenter() {
  if (!g_monitor.main)
    return;
  activateMainWindow();
  if (HWND page = create_scheduled_result_check_module(g_monitor.ctx))
    SendMessageW(g_monitor.ctx.mdiClient, WM_MDIMAXIMIZE,
                 reinterpret_cast<WPARAM>(page), 0);
}

// Jump from a floating reminder row straight to the corresponding regular
// report page; the pending-list click path stays in openReminderCenter().
void openReminderAlert(size_t row) {
  if (!g_monitor.main || row >= g_monitor.unhandled.size())
    return;
  activateMainWindow();
  const auto &a = g_monitor.unhandled[row];
  std::vector<std::string> highlightCodes{a.left_item_code};
  if (!a.compare_with_value && !a.right_item_code.empty())
    highlightCodes.push_back(a.right_item_code);
  if (!a.condition_item_codes.empty()) {
    highlightCodes.clear();
    std::istringstream codes(a.condition_item_codes);
    std::string code;
    while (std::getline(codes, code)) if (!code.empty()) highlightCodes.push_back(code);
  }
  auto *target =
      new RegularReportOpenTarget{a.rep_no,    a.oper_no,   a.inspect_date,
                                  a.mach_code, a.mach_name, a.room_code,
                                  highlightCodes};
  HWND report = create_regular_report_module(g_monitor.ctx);
  if (report)
    SendMessageW(g_monitor.ctx.mdiClient, WM_MDIMAXIMIZE,
                 reinterpret_cast<WPARAM>(report), 0);
  if (!report || !PostMessageW(report, WM_REGULAR_OPEN_REPORT, 0,
                               reinterpret_cast<LPARAM>(target))) {
    delete target;
  }
}

int reminderHeaderHeight(HWND hwnd) {
  return static_cast<int>(48 * search::dpi_scale_factor(hwnd) + 0.5f);
}

int reminderRowHeight(HWND hwnd) {
  return static_cast<int>(48 * search::dpi_scale_factor(hwnd) + 0.5f);
}

int reminderRowsTop(HWND hwnd) {
  return reminderHeaderHeight(hwnd) +
         static_cast<int>(8 * search::dpi_scale_factor(hwnd) + 0.5f);
}

RECT reminderActionRect(HWND hwnd, int row) {
  RECT client{};
  GetClientRect(hwnd, &client);
  const float scale = search::dpi_scale_factor(hwnd);
  const int pad = static_cast<int>(14 * scale + 0.5f);
  const int width = static_cast<int>(100 * scale + 0.5f);
  const int inset = static_cast<int>(7 * scale + 0.5f);
  const int top = reminderRowsTop(hwnd) + row * reminderRowHeight(hwnd);
  return {client.right - pad - width, top + inset, client.right - pad,
          top + reminderRowHeight(hwnd) - inset};
}

void drawReminderActionButton(HDC dc, const RECT &rect, bool pressed,
                              COLORREF accent) {
  const float scale = search::dpi_scale_factor(WindowFromDC(dc));
  HBRUSH fill = CreateSolidBrush(
      pressed ? mixColor(accent, RGB(255, 255, 255), 55, 45)
              : mixColor(accent, RGB(255, 255, 255), 25, 75));
  HPEN border = CreatePen(
      PS_SOLID, 1,
      pressed ? mixColor(accent, RGB(0, 0, 0), 70, 30) : accent);
  HGDIOBJ oldBrush = SelectObject(dc, fill);
  HGDIOBJ oldPen = SelectObject(dc, border);
  const int radius = static_cast<int>(10 * scale + 0.5f);
  RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
  SelectObject(dc, oldPen);
  SelectObject(dc, oldBrush);
  DeleteObject(border);
  DeleteObject(fill);
}

int reminderToggleWidth(HWND hwnd) {
  return static_cast<int>(90 * search::dpi_scale_factor(hwnd) + 0.5f);
}

bool reminderHitToggle(HWND hwnd, int x, int y) {
  RECT rc{};
  GetClientRect(hwnd, &rc);
  return y >= 0 && y < reminderHeaderHeight(hwnd) &&
         x >= rc.right - reminderToggleWidth(hwnd);
}

bool reminderHitDragArea(HWND hwnd, int x, int y) {
  RECT rc{};
  GetClientRect(hwnd, &rc);
  return y >= 0 && y < reminderHeaderHeight(hwnd) &&
         x < rc.right - reminderToggleWidth(hwnd);
}

// Returns the index of the expanded reminder row under the client point, or
// -1 when the point is outside every drawn row.
int reminderRowAt(HWND hwnd, int x, int y) {
  if (!g_monitor.reminder_expanded || g_monitor.unhandled.empty())
    return -1;
  const float scale = search::dpi_scale_factor(hwnd);
  const int pad = static_cast<int>(14 * scale + 0.5f);
  const int rowHeight = reminderRowHeight(hwnd);
  const int top = reminderRowsTop(hwnd);
  RECT rc{};
  GetClientRect(hwnd, &rc);
  if (x < pad || x > rc.right - pad || y < top)
    return -1;
  const size_t count = (std::min)(g_monitor.unhandled.size(), REMINDER_VISIBLE_ROWS);
  const int index = (y - top) / rowHeight;
  if (index < 0 || index >= static_cast<int>(count))
    return -1;
  if (y >= top + (index + 1) * rowHeight)
    return -1;
  return index;
}

bool reminderHitAction(HWND hwnd, int row, int x, int y) {
  const RECT action = reminderActionRect(hwnd, row);
  return x >= action.left && x < action.right && y >= action.top &&
         y < action.bottom;
}

void markReminderAlertHandled(HWND hwnd, int alertId) {
  std::string error;
  if (!scheduled_check::set_alert_handled(alertId, true, error)) {
    MessageBoxW(hwnd, w(error).c_str(), TITLE, MB_ICONERROR);
    updateReminder();
    return;
  }
  if (g_page) {
    auto *st = reinterpret_cast<State *>(GetWindowLongPtrW(g_page, GWLP_USERDATA));
    if (st)
      loadAlerts(st);
  }
  updateReminder();
}

void positionReminder() {
  if (!g_monitor.reminder)
    return;
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  const float scale = search::dpi_scale_factor(g_monitor.reminder);
  const int width = static_cast<int>(REMINDER_WIDTH * scale + 0.5f);
  const int height =
      static_cast<int>((g_monitor.reminder_expanded ? REMINDER_EXPANDED_HEIGHT
                                                   : REMINDER_COLLAPSED_HEIGHT) *
                           scale +
                       0.5f);
  const int margin = static_cast<int>(14 * scale + 0.5f);
  POINT desired{g_monitor.reminder_x, g_monitor.reminder_y};
  const HMONITOR monitor =
      g_monitor.reminder_has_custom_position
          ? MonitorFromPoint(desired, MONITOR_DEFAULTTONEAREST)
          : MonitorFromWindow(g_monitor.main, MONITOR_DEFAULTTONEAREST);
  if (!GetMonitorInfoW(monitor, &info))
    return;
  int x = g_monitor.reminder_has_custom_position
              ? g_monitor.reminder_x
              : info.rcWork.right - width - margin;
  int y = g_monitor.reminder_has_custom_position
              ? g_monitor.reminder_y
              : info.rcWork.bottom - height - margin;
  x = (std::max)(static_cast<int>(info.rcWork.left),
                 (std::min)(x, static_cast<int>(info.rcWork.right) - width));
  y = (std::max)(static_cast<int>(info.rcWork.top),
                 (std::min)(y, static_cast<int>(info.rcWork.bottom) - height));
  if (g_monitor.reminder_has_custom_position) {
    g_monitor.reminder_x = x;
    g_monitor.reminder_y = y;
  }
  SetWindowPos(g_monitor.reminder, HWND_TOPMOST, x, y, width, height,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

// When the user has placed the reminder window manually, expand/collapse
// should keep the window's bottom edge fixed so toggling does not shift the
// bar away from the spot the user chose. The stored ReminderX/Y remains the
// current top-left corner and is only persisted on drag end.
void resizeReminderKeepingBottom(HWND hwnd) {
  if (!g_monitor.reminder_has_custom_position) {
    positionReminder();
    return;
  }
  RECT windowRect{};
  GetWindowRect(hwnd, &windowRect);
  const int bottom = windowRect.bottom;
  const float scale = search::dpi_scale_factor(hwnd);
  const int width = static_cast<int>(REMINDER_WIDTH * scale + 0.5f);
  const int height =
      static_cast<int>((g_monitor.reminder_expanded ? REMINDER_EXPANDED_HEIGHT
                                                   : REMINDER_COLLAPSED_HEIGHT) *
                           scale +
                       0.5f);
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  POINT anchor{windowRect.left, windowRect.top};
  const HMONITOR monitor =
      MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
  if (!GetMonitorInfoW(monitor, &info)) {
    positionReminder();
    return;
  }
  const int x = (std::max)(
      static_cast<int>(info.rcWork.left),
      (std::min)(static_cast<int>(windowRect.left),
                 static_cast<int>(info.rcWork.right) - width));
  const int y = (std::max)(
      static_cast<int>(info.rcWork.top),
      (std::min)(bottom - height,
                 static_cast<int>(info.rcWork.bottom) - height));
  g_monitor.reminder_x = x;
  g_monitor.reminder_y = y;
  SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

LRESULT CALLBACK reminderProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_MOUSEACTIVATE:
    return MA_NOACTIVATE;
  case WM_SETCURSOR: {
    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(hwnd, &cursor);
    const bool dragArea = reminderHitDragArea(hwnd, cursor.x, cursor.y);
    SetCursor(LoadCursorW(nullptr, dragArea ? IDC_SIZEALL : IDC_HAND));
    return TRUE;
  }
  case WM_LBUTTONDOWN: {
    const int x = GET_X_LPARAM(lp);
    const int y = GET_Y_LPARAM(lp);
    const int row = reminderRowAt(hwnd, x, y);
    if (row >= 0 && reminderHitAction(hwnd, row, x, y)) {
      g_monitor.reminder_pressed_alert_id =
          g_monitor.unhandled[static_cast<size_t>(row)].id;
      SetCapture(hwnd);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    if (reminderHitDragArea(hwnd, x, y)) {
      RECT windowRect{};
      GetWindowRect(hwnd, &windowRect);
      GetCursorPos(&g_monitor.reminder_drag_cursor);
      g_monitor.reminder_drag_origin = {windowRect.left, windowRect.top};
      g_monitor.reminder_dragging = true;
      g_monitor.reminder_drag_moved = false;
      SetCapture(hwnd);
    }
    return 0;
  }
  case WM_MOUSEMOVE:
    if (g_monitor.reminder_dragging && (wp & MK_LBUTTON)) {
      POINT cursor{};
      GetCursorPos(&cursor);
      const int dx = cursor.x - g_monitor.reminder_drag_cursor.x;
      const int dy = cursor.y - g_monitor.reminder_drag_cursor.y;
      if (std::abs(dx) > 3 || std::abs(dy) > 3)
        g_monitor.reminder_drag_moved = true;
      RECT windowRect{};
      GetWindowRect(hwnd, &windowRect);
      const int width = windowRect.right - windowRect.left;
      const int height = windowRect.bottom - windowRect.top;
      POINT desired{g_monitor.reminder_drag_origin.x + dx,
                    g_monitor.reminder_drag_origin.y + dy};
      MONITORINFO info{};
      info.cbSize = sizeof(info);
      const HMONITOR monitor =
          MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
      if (GetMonitorInfoW(monitor, &info)) {
        const int maxX = static_cast<int>(info.rcWork.right) - width;
        const int maxY = static_cast<int>(info.rcWork.bottom) - height;
        desired.x = (std::max)(static_cast<int>(info.rcWork.left),
                               (std::min)(static_cast<int>(desired.x), maxX));
        desired.y = (std::max)(static_cast<int>(info.rcWork.top),
                               (std::min)(static_cast<int>(desired.y), maxY));
      }
      g_monitor.reminder_has_custom_position = true;
      g_monitor.reminder_x = static_cast<int>(desired.x);
      g_monitor.reminder_y = static_cast<int>(desired.y);
      SetWindowPos(hwnd, HWND_TOPMOST, static_cast<int>(desired.x),
                   static_cast<int>(desired.y), 0, 0,
                   SWP_NOSIZE | SWP_NOACTIVATE);
    }
    return 0;
  case WM_LBUTTONUP: {
    const int x = GET_X_LPARAM(lp);
    const int y = GET_Y_LPARAM(lp);
    if (g_monitor.reminder_pressed_alert_id != 0) {
      const int pressedId = g_monitor.reminder_pressed_alert_id;
      g_monitor.reminder_pressed_alert_id = 0;
      if (GetCapture() == hwnd)
        ReleaseCapture();
      const int row = reminderRowAt(hwnd, x, y);
      if (row >= 0 && reminderHitAction(hwnd, row, x, y) &&
          g_monitor.unhandled[static_cast<size_t>(row)].id == pressedId)
        markReminderAlertHandled(hwnd, pressedId);
      else
        InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    if (g_monitor.reminder_dragging) {
      const bool moved = g_monitor.reminder_drag_moved;
      g_monitor.reminder_dragging = false;
      g_monitor.reminder_drag_moved = false;
      if (GetCapture() == hwnd)
        ReleaseCapture();
      if (moved) {
        search::save_module_int(CONFIG_SECTION, L"ReminderX",
                                g_monitor.reminder_x);
        search::save_module_int(CONFIG_SECTION, L"ReminderY",
                                g_monitor.reminder_y);
      }
      return 0;
    }
    if (reminderHitToggle(hwnd, x, y)) {
      g_monitor.reminder_expanded = !g_monitor.reminder_expanded;
      resizeReminderKeepingBottom(hwnd);
      InvalidateRect(hwnd, nullptr, TRUE);
    } else if (const int row = reminderRowAt(hwnd, x, y); row >= 0) {
      openReminderAlert(static_cast<size_t>(row));
    } else if (y >= reminderHeaderHeight(hwnd)) {
      openReminderCenter();
    }
    return 0;
  }
  case WM_CAPTURECHANGED:
    g_monitor.reminder_dragging = false;
    g_monitor.reminder_drag_moved = false;
    g_monitor.reminder_pressed_alert_id = 0;
    return 0;
  case WM_PAINT: {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc{};
    GetClientRect(hwnd, &rc);
    HBRUSH background = CreateSolidBrush(RGB(255, 250, 240));
    FillRect(dc, &rc, background);
    DeleteObject(background);
    const float scale = search::dpi_scale_factor(hwnd);
    const int pad = static_cast<int>(14 * scale + 0.5f);
    const int headerHeight = static_cast<int>(48 * scale + 0.5f);
    const COLORREF accent = reminderAccentColor();
    const bool accentLight = reminderAccentIsLight(accent);
    RECT header = rc;
    header.bottom = headerHeight;
    HBRUSH headerBrush = CreateSolidBrush(accent);
    FillRect(dc, &header, headerBrush);
    DeleteObject(headerBrush);
    HGDIOBJ oldFont = SelectObject(
        dc, g_monitor.reminder_font ? g_monitor.reminder_font
                                    : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, accentLight ? RGB(55, 48, 42) : RGB(255, 255, 255));
    RECT titleRect{pad, 0, rc.right - static_cast<int>(100 * scale),
                   headerHeight};
    const std::wstring title = L"! 定时核查：" +
                               std::to_wstring(g_monitor.unhandled.size()) +
                               L" 条待处理";
    DrawTextW(dc, title.c_str(), -1, &titleRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT toggleRect{rc.right - static_cast<int>(90 * scale), 0, rc.right - pad,
                    headerHeight};
    DrawTextW(dc, g_monitor.reminder_expanded ? L"收起" : L"展开", -1,
              &toggleRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    if (g_monitor.reminder_expanded) {
      const size_t count = (std::min)(g_monitor.unhandled.size(), REMINDER_VISIBLE_ROWS);
      for (size_t i = 0; i < count; ++i) {
        const auto &alert = g_monitor.unhandled[i];
        const int top = reminderRowsTop(hwnd) +
                        static_cast<int>(i) * reminderRowHeight(hwnd);
        const RECT action = reminderActionRect(hwnd, static_cast<int>(i));
        const int textRight = action.left - static_cast<int>(10 * scale + 0.5f);
        RECT sampleRow{pad, top, textRight,
                       top + static_cast<int>(22 * scale + 0.5f)};
        const std::wstring sample =
            alert.oper_no.empty() ? L"样本号未记录"
                                  : L"样本号 " + w(alert.oper_no);
        const std::wstring time =
            alert.discovered_at.size() >= 16
                ? L"  ·  " + w(alert.discovered_at.substr(11, 5))
                : L"";
        SetTextColor(dc, RGB(112, 47, 20));
        const std::wstring heading = sample + time;
        DrawTextW(dc, heading.c_str(), -1, &sampleRow,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT detailRow{pad, sampleRow.bottom, textRight,
                       top + reminderRowHeight(hwnd)};
        SetTextColor(dc, RGB(55, 48, 42));
        const std::wstring detail = reminderAlertLine(alert);
        DrawTextW(dc, detail.c_str(), -1, &detailRow,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        const bool pressed = g_monitor.reminder_pressed_alert_id == alert.id;
        drawReminderActionButton(dc, action, pressed, accent);
        SetTextColor(dc, RGB(112, 47, 20));
        RECT buttonText = action;
        DrawTextW(dc, L"标记已处理", -1, &buttonText,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      }
      SetTextColor(dc, RGB(130, 72, 42));
      RECT footer{pad, rc.bottom - static_cast<int>(32 * scale), rc.right - pad,
                  rc.bottom - static_cast<int>(5 * scale)};
      DrawTextW(dc, L"点击打开待处理列表", -1, &footer,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(dc, oldFont);
    HBRUSH border = CreateSolidBrush(accent);
    FrameRect(dc, &rc, border);
    DeleteObject(border);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_DESTROY:
    if (g_monitor.reminder == hwnd)
      g_monitor.reminder = nullptr;
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

bool ensureReminderWindow() {
  if (g_monitor.reminder && IsWindow(g_monitor.reminder))
    return true;
  if (!g_monitor.reminder_font) {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
      g_monitor.reminder_font = CreateFontIndirectW(&ncm.lfMessageFont);
    if (!g_monitor.reminder_font)
      g_monitor.reminder_font = CreateFontW(
          -12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  }
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_DROPSHADOW;
  wc.lpfnWndProc = reminderProc;
  wc.hInstance = g_monitor.ctx.instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = REMINDER_CLASS_NAME;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return false;
  g_monitor.reminder =
      CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                      REMINDER_CLASS_NAME, TITLE, WS_POPUP, 0, 0, 0, 0, nullptr,
                      nullptr, g_monitor.ctx.instance, nullptr);
  if (g_monitor.reminder) {
    const int corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_monitor.reminder, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &corner, sizeof(corner));
  }
  return g_monitor.reminder != nullptr;
}

void ensureNotificationIcon() {
  if (g_monitor.notification_icon_added || !g_monitor.main)
    return;
  NOTIFYICONDATAW data{};
  data.cbSize = sizeof(data);
  data.hWnd = g_monitor.main;
  data.uID = REMINDER_ICON_ID;
  data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  data.uCallbackMessage = WM_SCHEDULED_RESULT_NOTIFICATION;
  data.hIcon = LoadIconW(g_monitor.ctx.instance, MAKEINTRESOURCEW(IDI_APP));
  copyFixed(data.szTip, L"LIS 工作台 - 定时核查");
  g_monitor.notification_icon_added =
      Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
}

void showSystemNotification(const std::vector<scheduled_check::Alert> &fresh) {
  ensureNotificationIcon();
  if (g_monitor.notification_icon_added) {
    std::wstring body = L"发现 " + std::to_wstring(fresh.size()) +
                        L" 条新的核查结果，请及时处理。";
    if (!fresh.empty()) {
      body += L"\r\n" + alertLine(fresh.front());
    }
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_monitor.main;
    data.uID = REMINDER_ICON_ID;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_WARNING;
    copyFixed(data.szInfoTitle, L"定时核查结果");
    copyFixed(data.szInfo, body);
    Shell_NotifyIconW(NIM_MODIFY, &data);
  }
  MessageBeep(MB_ICONEXCLAMATION);
  FLASHWINFO flash{sizeof(flash), g_monitor.main,
                   FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0};
  FlashWindowEx(&flash);
}

void updateReminder(bool emphasize) {
  if (!g_monitor.main)
    return;
  std::string error;
  std::vector<scheduled_check::Alert> alerts;
  if (!scheduled_check::load_unhandled_alerts(alerts, error))
    return;
  g_monitor.unhandled = std::move(alerts);
  if (g_monitor.unhandled.empty()) {
    if (g_monitor.reminder)
      ShowWindow(g_monitor.reminder, SW_HIDE);
    if (g_monitor.notification_icon_added) {
      NOTIFYICONDATAW data{};
      data.cbSize = sizeof(data);
      data.hWnd = g_monitor.main;
      data.uID = REMINDER_ICON_ID;
      Shell_NotifyIconW(NIM_DELETE, &data);
      g_monitor.notification_icon_added = false;
    }
    return;
  }
  const bool hadReminder = g_monitor.reminder && IsWindow(g_monitor.reminder);
  if (!ensureReminderWindow())
    return;
  ensureNotificationIcon();
  const bool expandFromCollapsed =
      hadReminder && emphasize && !g_monitor.reminder_expanded &&
      g_monitor.reminder_has_custom_position;
  if (emphasize)
    g_monitor.reminder_expanded = true;
  if (expandFromCollapsed)
    resizeReminderKeepingBottom(g_monitor.reminder);
  else
    positionReminder();
  InvalidateRect(g_monitor.reminder, nullptr, TRUE);
}

std::string ruleSignature(const std::vector<scheduled_check::Rule> &rules) {
  std::ostringstream out;
  const auto field = [&out](const std::string &value) {
    out << value.size() << ':' << value;
  };
  for (const auto &rule : rules) {
    out << rule.id << ':' << rule.enabled << ':' << rule.compare_with_value;
    field(rule.name);
    field(rule.left_item_code);
    field(rule.left_item_name);
    field(rule.left_item_unit);
    field(rule.op);
    field(rule.right_item_code);
    field(rule.right_item_name);
    field(rule.right_item_unit);
    field(rule.right_value_text);
    field(rule.right_multiplier_text);
    field(rule.tolerance_percent_text);
    field(rule.room_code);
    field(rule.mach_code);
    field(scheduled_check::serialize_condition_group(rule));
  }
  return out.str();
}

std::uint64_t reportNumber(const std::string &value) {
  return value.empty() ? 0 : std::strtoull(value.c_str(), nullptr, 10);
}

using ItemLookup = std::unordered_map<
    std::string, const search::ScheduledCheckItemOption *>;

scheduled_check::ResultRow makeResultRow(
    const search::ScheduledCheckResultRow &source, const ItemLookup &items) {
  scheduled_check::ResultRow row;
  row.entry_id = source.entry_id;
  row.rep_no = source.rep_no;
  row.oper_no = source.oper_no;
  row.room_code = source.room_code;
  row.mach_code = source.mach_code;
  row.mach_name = source.mach_name;
  row.inspect_date = source.inspect_date;
  row.item_code = search::trim(source.item_code);
  row.item_name = search::trim(source.item_name);
  row.item_eng = search::trim(source.item_eng);
  if (const auto found = items.find(row.item_code); found != items.end()) {
    if (!found->second->item_name.empty())
      row.item_name = found->second->item_name;
    if (!found->second->item_eng.empty())
      row.item_eng = found->second->item_eng;
  }
  row.result = source.result;
  return row;
}

ScanResult executeScan(const ModuleContext &ctx, bool forceFull) {
  ScanResult out;
  std::vector<scheduled_check::Rule> rules;
  if (!scheduled_check::load_rules(rules, out.error))
    return out;
  std::vector<scheduled_check::Rule> legacyRules;
  std::map<std::pair<std::string, std::string>,
           std::vector<scheduled_check::Rule>> machineRules;
  for (const auto &r : rules)
    if (r.enabled) {
      if (r.room_code.empty() || r.mach_code.empty())
        legacyRules.push_back(r);
      else
        machineRules[{r.room_code, r.mach_code}].push_back(r);
    }
  std::vector<std::string> codes;
  for (const auto &r : legacyRules)
    if (r.enabled) {
      const auto items = scheduled_check::rule_item_codes(r);
      codes.insert(codes.end(), items.begin(), items.end());
    }
  std::sort(codes.begin(), codes.end());
  codes.erase(std::unique(codes.begin(), codes.end()), codes.end());
  codes.erase(std::remove_if(codes.begin(), codes.end(),
                             [](const std::string &code) { return code.empty(); }),
              codes.end());
  if (codes.empty() && machineRules.empty()) {
    out.ok = true;
    return out;
  }
  const auto connection =
      search::wide_to_utf8(search::build_connection_string_w(ctx.dbSettings));
  if (connection.empty()) {
    out.error = "数据库连接未配置";
    return out;
  }
  std::vector<search::ScheduledCheckItemOption> items;
  std::string dictionaryError;
  const bool dictionaryLoaded =
      search::query_scheduled_check_items(connection, items, dictionaryError);
  if (!dictionaryLoaded)
    out.dictionary_warning = dictionaryError.empty()
                                 ? "项目字典加载失败"
                                 : dictionaryError;
  ItemLookup itemsByCode;
  itemsByCode.reserve(items.size());
  for (const auto &item : items)
    itemsByCode.emplace(item.item_code, &item);
  std::vector<scheduled_check::Match> matches;
  scheduled_check::ScanProgress progress;
  std::vector<scheduled_check::PendingReport> pending;
  if (!legacyRules.empty()) {
  std::string day, minRepNo, maxRepNo;
  if (!search::query_scheduled_check_report_bounds(
          connection, day, minRepNo, maxRepNo, out.error))
    return out;
  if (!scheduled_check::load_scan_progress(progress, pending, out.error))
    return out;
  const std::string signature = ruleSignature(legacyRules);
  const std::uint64_t maxNumber = reportNumber(maxRepNo);
  const std::uint64_t oldHigh = reportNumber(progress.high_watermark);
  const bool full = forceFull || progress.day != day ||
                    progress.rule_signature != signature ||
                    progress.high_watermark.empty() || maxNumber < oldHigh;
  if (!full && !minRepNo.empty() &&
      (progress.day_min_rep_no.empty() || progress.day_min_rep_no == "0" ||
       reportNumber(minRepNo) < reportNumber(progress.day_min_rep_no)))
    progress.day_min_rep_no = minRepNo;
  if (!full && progress.sweep_max_rep_no == "0" && !maxRepNo.empty())
    progress.sweep_max_rep_no = maxRepNo;
  std::vector<search::ScheduledCheckResultRow> source;
  const auto appendQuery = [&](const search::ScheduledCheckResultQuery &query) {
    std::vector<search::ScheduledCheckResultRow> batch;
    if (!search::query_scheduled_check_results(connection, codes, batch,
                                               out.error, {}, query))
      return false;
    source.insert(source.end(), std::make_move_iterator(batch.begin()),
                  std::make_move_iterator(batch.end()));
    return true;
  };
  std::unordered_set<std::string> dueReports;
  if (full) {
    search::ScheduledCheckResultQuery query;
    query.include_empty_reports = true;
    if (!appendQuery(query))
      return out;
    pending.clear();
    progress.day_min_rep_no = minRepNo.empty() ? "0" : minRepNo;
    progress.sweep_max_rep_no = maxRepNo.empty() ? "0" : maxRepNo;
    progress.sweep_step = 0;
  } else {
    // Recheck a small overlap: REP_NO allocation and transaction commit order
    // need not be identical. Older unfinished reports are handled below.
    const std::uint64_t overlapLow = oldHigh > 100 ? oldHigh - 100 : 0;
    search::ScheduledCheckResultQuery recent;
    recent.lower_exclusive = std::to_string(overlapLow);
    recent.upper_inclusive = maxRepNo.empty() ? "0" : maxRepNo;
    recent.include_empty_reports = true;
    if (maxNumber > overlapLow && !appendQuery(recent))
      return out;

    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    std::vector<std::string> due;
    for (const auto &item : pending) {
      const std::uint64_t no = reportNumber(item.rep_no);
      if (item.next_scan <= now && no <= overlapLow) {
        due.push_back(item.rep_no);
        dueReports.insert(item.rep_no);
      }
    }
    for (size_t begin = 0; begin < due.size(); begin += 200) {
      search::ScheduledCheckResultQuery query;
      query.include_empty_reports = true;
      query.report_nos.assign(due.begin() + begin,
                              due.begin() + (std::min)(due.size(), begin + 200));
      if (!appendQuery(query))
        return out;
    }

    // Patrol one fifteenth of today's numeric REP_NO range per minute.
    // The upper bound stays fixed until all segments of this cycle complete.
    const std::uint64_t sweepMin = reportNumber(progress.day_min_rep_no);
    const std::uint64_t sweepMax = reportNumber(progress.sweep_max_rep_no);
    if (sweepMin > 0 && sweepMax >= sweepMin) {
      const std::uint64_t width = (sweepMax - sweepMin + 15) / 15;
      const int step = (std::max)(0, (std::min)(14, progress.sweep_step));
      const std::uint64_t first = sweepMin + step * width;
      if (first <= sweepMax) {
        search::ScheduledCheckResultQuery query;
        query.lower_exclusive = std::to_string(first - 1);
        query.upper_inclusive = std::to_string(
            (std::min)(sweepMax, first + width - 1));
        if (!appendQuery(query))
          return out;
      }
    }
    progress.sweep_step = (progress.sweep_step + 1) % 15;
    if (progress.sweep_step == 0)
      progress.sweep_max_rep_no = maxRepNo.empty() ? "0" : maxRepNo;
  }
  progress.day = day;
  progress.rule_signature = signature;
  progress.high_watermark = maxRepNo.empty() ? "0" : maxRepNo;
  if (progress.day_min_rep_no.empty())
    progress.day_min_rep_no = minRepNo.empty() ? "0" : minRepNo;
  out.row_count = static_cast<int>(std::count_if(
      source.begin(), source.end(),
      [](const auto &row) { return !row.entry_id.empty(); }));
  std::vector<scheduled_check::ResultRow> rows;
  rows.reserve(source.size());
  std::map<std::string, std::vector<scheduled_check::ResultRow>> byReport;
  for (const auto &s : source) {
    auto &reportRows = byReport[s.rep_no];
    if (s.entry_id.empty())
      continue; // LEFT JOIN marker for a report with no selected project yet.
    auto r = makeResultRow(s, itemsByCode);
    reportRows.push_back(r);
    rows.push_back(std::move(r));
  }
  auto legacyMatches = scheduled_check::evaluate(legacyRules, rows, &out.skipped);
  matches.insert(matches.end(), std::make_move_iterator(legacyMatches.begin()),
                 std::make_move_iterator(legacyMatches.end()));
  std::unordered_map<std::string, scheduled_check::PendingReport> pendingByNo;
  for (const auto &item : pending)
    pendingByNo[item.rep_no] = item;
  for (const auto &no : dueReports)
    if (byReport.count(no) == 0)
      pendingByNo.erase(no); // Report was deleted or moved out of today.
  const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
  const std::uint64_t recentFloor = maxNumber > 100 ? maxNumber - 100 : 0;
  for (const auto &report : byReport) {
    const bool recent = reportNumber(report.first) > recentFloor;
    const auto existing = pendingByNo.find(report.first);
    const bool tracked = existing != pendingByNo.end();
    const bool unfinished = scheduled_check::needs_result_followup(
                                legacyRules, report.second) ||
                            (report.second.empty() && (recent || tracked));
    if (!unfinished || (!recent && !tracked)) {
      pendingByNo.erase(report.first);
      continue;
    }
    scheduled_check::PendingReport item = tracked
        ? existing->second : scheduled_check::PendingReport{};
    item.rep_no = report.first;
    if (item.first_seen == 0)
      item.first_seen = now;
    const std::int64_t age = now - item.first_seen;
    item.next_scan = now + (age < 30 * 60 ? 60 : age < 4 * 60 * 60 ? 300 : 900);
    pendingByNo[report.first] = std::move(item);
  }
  pending.clear();
  pending.reserve(pendingByNo.size());
  for (auto &item : pendingByNo)
    pending.push_back(std::move(item.second));
  }
  for (const auto &[machine, groupRules] : machineRules) {
    std::vector<std::string> machineCodes;
    for (const auto &rule : groupRules) {
      const auto items = scheduled_check::rule_item_codes(rule);
      machineCodes.insert(machineCodes.end(), items.begin(), items.end());
    }
    std::sort(machineCodes.begin(), machineCodes.end());
    machineCodes.erase(std::unique(machineCodes.begin(), machineCodes.end()),
                       machineCodes.end());
    search::ScheduledCheckResultQuery query;
    query.room_code = machine.first;
    query.mach_code = machine.second;
    std::vector<search::ScheduledCheckResultRow> source;
    if (!search::query_scheduled_check_results(connection, machineCodes, source,
                                               out.error, {}, query))
      return out;
    std::vector<scheduled_check::ResultRow> machineRows;
    machineRows.reserve(source.size());
    for (const auto &s : source) {
      if (s.entry_id.empty())
        continue;
      machineRows.push_back(makeResultRow(s, itemsByCode));
      ++out.row_count;
    }
    auto found = scheduled_check::evaluate(groupRules, machineRows, &out.skipped);
    matches.insert(matches.end(), std::make_move_iterator(found.begin()),
                   std::make_move_iterator(found.end()));
  }
  out.match_count = static_cast<int>(matches.size());
  if (!scheduled_check::record_matches(rules, matches, out.fresh, out.error))
    return out;
  if (!legacyRules.empty() &&
      !scheduled_check::save_scan_progress(progress, pending,
                                           out.progress_warning)) {
    // Matches have already been committed; retry this range next time.
  }
  out.ok = true;
  return out;
}

bool triggerScan(bool forceFull = false) {
  if (!g_monitor.main)
    return false;
  if (g_monitor.task.active()) {
    g_monitor.rescan_requested = true;
    g_monitor.full_rescan_requested |= forceFull;
    return false;
  }
  const ModuleContext ctx = g_monitor.ctx;
  return g_monitor.task.start<ScanResult>(
      [ctx, forceFull] { return executeScan(ctx, forceFull); },
      [](std::optional<ScanResult> result, std::exception_ptr error) {
        if (g_page) {
          auto *st = reinterpret_cast<State *>(
              GetWindowLongPtrW(g_page, GWLP_USERDATA));
          if (st) {
            refresh(st);
            std::wstring status;
            if (error || !result) {
              status = L"扫描任务异常，下一分钟将自动重试。";
            } else if (!result->ok) {
              status = L"扫描失败：" + w(result->error);
            } else {
              status = result->dictionary_warning.empty()
                           ? L""
                           : L"项目字典加载失败，英文名可能缺失。";
              status += L"扫描完成：";
              status += L"读取 " + std::to_wstring(result->row_count) +
                       L" 行，命中 " + std::to_wstring(result->match_count) +
                       L" 条，新增 " + std::to_wstring(result->fresh.size()) +
                       L" 条，跳过非数值 " + std::to_wstring(result->skipped) +
                       L" 组。";
              if (!result->progress_warning.empty())
                status += L" 扫描进度未保存，下轮将重试：" +
                          w(result->progress_warning);
            }
            SetWindowTextW(st->status, status.c_str());
            if (st->progress)
              ShowWindow(st->progress, SW_HIDE);
          }
        }
        const bool hasFresh = !error && result && result->ok &&
                              !result->fresh.empty() && g_monitor.main;
        updateReminder(hasFresh);
        if (hasFresh)
          showSystemNotification(result->fresh);
        if (g_monitor.rescan_requested && g_monitor.main) {
          g_monitor.rescan_requested = false;
          const bool full = g_monitor.full_rescan_requested;
          g_monitor.full_rescan_requested = false;
          triggerScan(full);
        }
      });
}

void loadRules(State *st) {
  std::string e;
  if (!scheduled_check::load_rules(st->rules, e)) {
    SetWindowTextW(st->status, w(e).c_str());
    return;
  }
  st->syncingRules = true;
  ListView_DeleteAllItems(st->rulesList);
  for (size_t i = 0; i < st->rules.size(); ++i) {
    const auto &r = st->rules[i];
    setItem(st->rulesList, i, 0, r.enabled ? L"启用" : L"停用");
    setItem(st->rulesList, i, 1, w(r.name));
    setItem(st->rulesList, i, 2, r.mach_code.empty() ? L"未限定（旧规则）" : w(r.mach_name));
    std::string summary = std::string(r.match_any ? "任一 · " : "全部 · ") +
                          std::to_string(r.extra_conditions.size() + 1) + " 条：";
    const auto conditions = scheduled_check::rule_conditions(r);
    for (size_t index = 0; index < conditions.size(); ++index) {
      if (index) summary += "；";
      summary += scheduled_check::condition_description(conditions[index]);
    }
    setItem(st->rulesList, i, 3, w(summary));
    ListView_SetCheckState(st->rulesList, static_cast<int>(i), r.enabled);
  }
  st->syncingRules = false;
}
std::string alertSortValue(const scheduled_check::Alert &a, int column) {
  switch (column) {
  case 0: return a.discovered_at;
  case 1: return a.handled ? "已处理" : "未处理";
  case 2: return a.rule_name;
  case 3: return a.rep_no;
  case 4: return a.oper_no;
  case 10: return a.condition_summary;
  case 5: return a.left_item_name;
  case 6: return a.left_result_text;
  case 7: return a.op + " " + a.tolerance_percent_text;
  case 8:
    return a.compare_with_value
               ? "固定值"
               : a.right_item_name + " × " + a.right_multiplier_text;
  default: return a.right_result_text;
  }
}

void applyAlertFilter(State *st) {
  st->alerts.clear();
  const std::wstring filter = st->alertFilter;
  for (const auto &a : st->allAlerts) {
    if (!filter.empty()) {
      const std::wstring rep = w(a.rep_no);
      const std::wstring oper = w(a.oper_no);
      if (rep.find(filter) == std::wstring::npos &&
          oper.find(filter) == std::wstring::npos)
        continue;
    }
    st->alerts.push_back(a);
  }
  std::stable_sort(
      st->alerts.begin(), st->alerts.end(),
      [st](const scheduled_check::Alert &a, const scheduled_check::Alert &b) {
        const std::string left = alertSortValue(a, st->alertsSortColumn);
        const std::string right = alertSortValue(b, st->alertsSortColumn);
        return st->alertsSortAscending ? left < right : left > right;
      });
  ListView_DeleteAllItems(st->alertsList);
  size_t pending = 0;
  for (size_t i = 0; i < st->alerts.size(); ++i) {
    const auto &a = st->alerts[i];
    if (!a.handled)
      ++pending;
    setItem(st->alertsList, i, 0, w(a.discovered_at));
    setItem(st->alertsList, i, 1, a.handled ? L"已处理" : L"未处理");
    setItem(st->alertsList, i, 2, w(a.rule_name));
    setItem(st->alertsList, i, 3, w(a.rep_no));
    setItem(st->alertsList, i, 4, w(a.oper_no));
    setItem(st->alertsList, i, 10, w(a.condition_summary));
    setItem(st->alertsList, i, 5, w(a.left_item_name));
    setItem(st->alertsList, i, 6, w(a.left_result_text));
    setItem(st->alertsList, i, 7, (a.condition_summary.empty() ? w(a.op) + toleranceSuffix(a.tolerance_percent_text) : L"条件组"));
    setItem(st->alertsList, i, 8,
            a.compare_with_value ? L"固定值" : w(a.right_item_name) +
                multiplierSuffix(a.right_multiplier_text));
    setItem(st->alertsList, i, 9, w(a.right_result_text));
  }
  const std::wstring title =
      L"待处理 " + std::to_wstring(pending) + L" 条（并显示今日已处理）";
  SetWindowTextW(st->alertsTitle, title.c_str());
}

void loadAlerts(State *st) {
  std::string e;
  if (!scheduled_check::load_review_alerts(st->allAlerts, e)) {
    SetWindowTextW(st->status, w(e).c_str());
    return;
  }
  applyAlertFilter(st);
}
void refresh(State *st) {
  loadRules(st);
  loadAlerts(st);
  SetWindowTextW(st->status,
                 L"每分钟核查当日结果，规则保存在本机。");
}

int comboSelection(HWND combo) {
  return static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
}

int exactItemCodeIndex(const State *st, const std::string &code,
                       bool waitForLongerPrefix) {
  int exact = -1;
  bool hasLongerPrefix = false;
  for (size_t i = 0; i < st->items.size(); ++i) {
    const auto &candidate = st->items[i].item_code;
    if (candidate == code)
      exact = static_cast<int>(i);
    else if (waitForLongerPrefix && candidate.size() > code.size() &&
             candidate.compare(0, code.size(), code) == 0)
      hasLongerPrefix = true;
  }
  return hasLongerPrefix ? -1 : exact;
}

void autoMatchItemCode(State *st, HWND combo, const wchar_t *side,
                       bool forceExact) {
  const std::string code = search::trim(windowText(combo));
  if (code.empty())
    return;
  const int index = exactItemCodeIndex(st, code, !forceExact);
  if (index < 0) {
    if (forceExact) {
      const std::wstring message = std::wstring(side) + L"的代码 “" +
                                   w(code) + L"”不在该仪器的可选项目中。";
      SetWindowTextW(st->status, message.c_str());
    }
    return;
  }
  SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
  const auto &item = st->items[static_cast<size_t>(index)];
  std::wstring message = std::wstring(side) + L"已精确匹配：" +
                         w(item.item_name) + L" [" + w(item.item_code) + L"]";
  if (!item.item_eng.empty())
    message += L"  " + w(item.item_eng);
  if (!item.unit.empty())
    message += L"  " + w(item.unit);
  SetWindowTextW(st->status, message.c_str());
}

int resolveItem(State *st, HWND combo) {
  const int selectedIndex = comboSelection(combo);
  if (selectedIndex >= 0 && selectedIndex < static_cast<int>(st->items.size()))
    return selectedIndex;
  const std::string key = search::trim(windowText(combo));
  if (key.empty())
    return -1;
  const bool numericCode = std::all_of(
      key.begin(), key.end(), [](unsigned char ch) { return ch >= '0' && ch <= '9'; });
  int match = -1;
  for (size_t i = 0; i < st->items.size(); ++i) {
    const auto &item = st->items[i];
    if (item.item_code == key || item.item_name == key || item.item_eng == key)
      return static_cast<int>(i);
    if (numericCode)
      continue; // A typed ITEM_CODE must match exactly, never by substring.
    if (item.item_code.find(key) != std::string::npos ||
        item.item_name.find(key) != std::string::npos ||
        item.item_eng.find(key) != std::string::npos) {
      if (match >= 0)
        return -1;
      match = static_cast<int>(i);
    }
  }
  return match;
}
std::array<HWND, 14> rowControls(const ConditionRow &row) {
  return {row.number, row.left, row.op, row.kind, row.right, row.value,
          row.options, row.more, row.remove, row.multiplierLabel, row.multiplier,
          row.toleranceLabel, row.tolerance, row.separator};
}

void rememberRow(State *st, ConditionRow &row) {
  const auto draft = [st](HWND combo, const std::string &previous) {
    const int index = resolveItem(st, combo);
    if (index >= 0) return st->items[index].item_code;
    const auto typed = search::trim(windowText(combo));
    return typed.empty() && !st->machineItemsLoaded ? previous : typed;
  };
  row.leftDraft = draft(row.left, row.leftDraft);
  row.rightDraft = draft(row.right, row.rightDraft);
  const int op = comboSelection(row.op);
  row.data.op = op >= 0 && op < COMPARISON_COUNT ? COMPARISONS[op].op : "";
  row.data.compare_with_value = comboSelection(row.kind) == 1;
  row.data.right_value_text = search::trim(windowText(row.value));
  row.data.right_multiplier_text = search::trim(windowText(row.multiplier));
  row.data.tolerance_percent_text = search::trim(windowText(row.tolerance));
}

bool readCondition(State *st, ConditionRow &row, scheduled_check::Condition &condition,
                   std::string &error) {
  rememberRow(st, row);
  const int left = resolveItem(st, row.left), right = resolveItem(st, row.right);
  if (left < 0 || (!row.data.compare_with_value && right < 0)) {
    error = "请选择当前仪器的项目";
    return false;
  }
  condition = row.data;
  const auto &item = st->items[left];
  condition.left_item_code = item.item_code;
  condition.left_item_name = item.item_name;
  condition.left_item_unit = item.unit;
  if (condition.compare_with_value) {
    condition.right_item_code.clear();
    condition.right_item_name.clear();
    condition.right_item_unit.clear();
    condition.right_multiplier_text = "1";
    condition.tolerance_percent_text = "0";
  } else {
    const auto &target = st->items[right];
    condition.right_item_code = target.item_code;
    condition.right_item_name = target.item_name;
    condition.right_item_unit = target.unit;
    condition.right_value_text.clear();
  }
  return scheduled_check::validate_condition(condition, error);
}

void orderEditorControls(State *st) {
  const auto last = [](HWND control) {
    SetWindowPos(control, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  };
  for (HWND control : {st->machineButton, st->name, st->groupMode}) last(control);
  for (const auto &row : st->conditionRows)
    for (HWND control : {row.left, row.op, row.kind, row.right, row.value,
                        row.options, row.multiplier, row.tolerance, row.more, row.remove}) last(control);
  for (HWND control : {st->addCondition, st->save, st->back}) last(control);
}

void updateConditionRows(State *st) {
  if (st->loadingCondition) return;
  st->loadingCondition = true;
  for (size_t index = 0; index < st->conditionRows.size(); ++index) {
    auto &row = st->conditionRows[index];
    rememberRow(st, row);
    double percent = 0.0;
    const bool range = !row.data.compare_with_value &&
        scheduled_check::parse_number(row.data.tolerance_percent_text, percent) &&
        percent > 0.0 && percent <= 100.0;
    SetWindowTextW(row.number, (std::to_wstring(index + 1) +
                               (row.data.negate ? L" · 取反" : L"")).c_str());
    std::wstring options = row.optionsExpanded ? L"收起参数" : L"倍数 / 误差";
    if (!row.optionsExpanded) {
      double multiple = 1.0;
      if (scheduled_check::parse_number(row.data.right_multiplier_text, multiple) &&
          (multiple != 1.0 || range))
        options = L"×" + w(row.data.right_multiplier_text) + toleranceSuffix(row.data.tolerance_percent_text);
    }
    SetWindowTextW(row.options, options.c_str());
    EnableWindow(row.remove, st->conditionRows.size() > 1);
  }
  st->loadingCondition = false;
}

void fillItems(State *st) {
  const bool wasLoading = st->loadingCondition;
  st->loadingCondition = true;
  for (auto &row : st->conditionRows) rememberRow(st, row);
  st->items.clear();
  if (st->machineItemsLoaded && st->dictionaryLoaded)
    for (const auto &item : st->allItems)
      if (st->machineItemCodes.count(item.item_code)) st->items.push_back(item);
  for (auto &row : st->conditionRows) {
    const auto populate = [&](HWND combo, const std::string &draft) {
      SendMessageW(combo, CB_RESETCONTENT, 0, 0);
      int selected = -1;
      for (size_t index = 0; index < st->items.size(); ++index) {
        const auto &item = st->items[index];
        std::wstring label = w(item.item_name + "  [" + item.item_code + "]");
        if (!item.item_eng.empty()) label += L"  " + w(item.item_eng);
        if (!item.unit.empty()) label += L"  " + w(item.unit);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        if (item.item_code == draft) selected = static_cast<int>(index);
      }
      if (selected >= 0) SendMessageW(combo, CB_SETCURSEL, selected, 0);
      else SetWindowTextW(combo, w(draft).c_str());
      EnableWindow(combo, st->machineItemsLoaded && !st->items.empty());
    };
    populate(row.left, row.leftDraft);
    populate(row.right, row.rightDraft);
  }
  EnableWindow(st->save, st->machineItemsLoaded && !st->items.empty());
  st->loadingCondition = wasLoading;
  if (st->editing) {
    if (!st->machineItemsLoaded)
      SetWindowTextW(st->status, L"请先选择仪器。");
    else if (!st->dictionaryLoaded)
      SetWindowTextW(st->status, L"正在加载项目字典...");
    else if (st->items.empty())
      SetWindowTextW(st->status, L"该仪器没有可选项目，请核对 LIS 配置。");
    else
      SetWindowTextW(st->status, (L"可选项目 " + std::to_wstring(st->items.size()) + L" 项").c_str());
  }
  updateConditionRows(st);
  if (st->rulesPane) {
    applyRuleEditorVisibility(st);
    layout(g_page, st);
  }
}

void destroyConditionRows(State *st) {
  const bool wasLoading = st->loadingCondition;
  st->loadingCondition = true;
  for (const auto &row : st->conditionRows)
    for (HWND control : rowControls(row)) DestroyWindow(control);
  st->conditionRows.clear();
  st->loadingCondition = wasLoading;
}

void appendConditionRow(State *st, const scheduled_check::Condition &condition = {}) {
  st->loadingCondition = true;
  ConditionRow row;
  row.data = condition;
  row.leftDraft = condition.left_item_code;
  row.rightDraft = condition.right_item_code;
  double multiple = 1.0, percent = 0.0;
  row.optionsExpanded = !condition.compare_with_value &&
      ((!scheduled_check::parse_number(condition.right_multiplier_text, multiple) || multiple != 1.0) ||
       (!scheduled_check::parse_number(condition.tolerance_percent_text, percent) || percent != 0.0));
  const HWND parent = st->formPane;
  row.number = search::create_label(parent, L"", 0, 0, 0, 0);
  row.left = search::create_combo(parent, IDC_LEFT, 0, 0, 0, 0, true);
  row.op = CreateWindowExW(0, WC_COMBOBOXW, L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
          CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
      0, 0, 0, 0, parent, win32_control_id(IDC_OP), st->ctx.instance, nullptr);
  for (const auto &option : COMPARISONS)
    SendMessageW(row.op, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option.symbol));
  int op = 0;
  for (int i = 0; i < COMPARISON_COUNT; ++i) if (condition.op == COMPARISONS[i].op) op = i;
  SendMessageW(row.op, CB_SETCURSEL, op, 0);
  row.kind = search::create_combo(parent, IDC_VALUE_MODE, 0, 0, 0, 0, false);
  for (const wchar_t *kind : {L"项目", L"固定值"})
    SendMessageW(row.kind, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kind));
  SendMessageW(row.kind, CB_SETCURSEL, condition.compare_with_value ? 1 : 0, 0);
  row.right = search::create_combo(parent, IDC_RIGHT, 0, 0, 0, 0, true);
  for (HWND combo : {row.left, row.right}) {
    COMBOBOXINFO info{sizeof(info)};
    if (GetComboBoxInfo(combo, &info))
      SendMessageW(info.hwndItem, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"选择项目或输入代码"));
  }
  row.value = search::create_edit(parent, IDC_VALUE, 0, 0, 0, 0);
  SendMessageW(row.value, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"输入数值"));
  row.options = search::create_button(parent, IDC_ROW_OPTIONS, L"倍数 / 误差", 0, 0, 0, 0);
  row.more = search::create_button(parent, IDC_ROW_MORE, L"更多", 0, 0, 0, 0);
  row.remove = search::create_button(parent, IDC_REMOVE_CONDITION, L"删除", 0, 0, 0, 0);
  row.multiplierLabel = search::create_label(parent, L"倍数 ×", 0, 0, 0, 0);
  row.multiplier = search::create_edit(parent, IDC_MULTIPLIER, 0, 0, 0, 0);
  row.toleranceLabel = search::create_label(parent, L"误差 ±%", 0, 0, 0, 0);
  row.tolerance = search::create_edit(parent, IDC_TOLERANCE, 0, 0, 0, 0);
  row.separator = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_ETCHEDHORZ,
      0, 0, 0, 0, parent, nullptr, st->ctx.instance, nullptr);
  SetWindowTextW(row.left, w(row.leftDraft).c_str());
  SetWindowTextW(row.right, w(row.rightDraft).c_str());
  SetWindowTextW(row.value, w(condition.right_value_text).c_str());
  SetWindowTextW(row.multiplier, w(condition.right_multiplier_text).c_str());
  SetWindowTextW(row.tolerance, w(condition.tolerance_percent_text).c_str());
  for (HWND control : rowControls(row))
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(st->ctx.uiFont), TRUE);
  st->conditionRows.push_back(std::move(row));
  orderEditorControls(st);
  st->loadingCondition = false;
}

void loadMachineItems(HWND hwnd, State *st) {
  st->machineItemCodes.clear();
  st->machineItemsLoaded = false;
  fillItems(st);
  if (st->roomCode.empty() || st->machCode.empty()) {
    return;
  }
  const auto connection = search::wide_to_utf8(
      search::build_connection_string_w(st->ctx.dbSettings));
  const auto room = st->roomCode, machine = st->machCode;
  if (connection.empty()) {
    SetWindowTextW(st->status, L"请先配置数据库连接，无法加载仪器项目。");
    return;
  }
  SetWindowTextW(st->status, L"正在加载所选仪器的项目...");
  if (!st->machineItemTask.start<
      std::pair<std::vector<std::string>, std::string>>(
      [connection, room, machine] {
        std::pair<std::vector<std::string>, std::string> result;
        search::query_scheduled_check_machine_item_codes(
            connection, room, machine, result.first, result.second);
        return result;
      },
      [hwnd, room, machine](auto result, std::exception_ptr) {
        if (!IsWindow(hwnd)) return;
        auto *state = reinterpret_cast<State *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!state || state->roomCode != room ||
            state->machCode != machine) return;
        if (!result) {
          SetWindowTextW(state->status, L"仪器项目加载异常，请重选仪器后重试。");
          return;
        }
        if (!result->second.empty()) {
          SetWindowTextW(state->status, L"仪器项目加载失败，请重选仪器后重试；已禁止保存规则。");
          return;
        }
        state->machineItemCodes.insert(result->first.begin(),
                                       result->first.end());
        state->machineItemsLoaded = true;
        fillItems(state);
      }))
    SetWindowTextW(st->status, L"仪器项目任务启动失败，请重选仪器后重试。");
}
void startItemLoad(HWND hwnd, State *st) {
  const auto connection = search::wide_to_utf8(
      search::build_connection_string_w(st->ctx.dbSettings));
  if (connection.empty()) {
    SetWindowTextW(st->status, L"请先在系统设置中配置数据库连接。");
    return;
  }
  st->itemTask.start<
      std::pair<std::vector<search::ScheduledCheckItemOption>, std::string>>(
      [connection] {
        std::pair<std::vector<search::ScheduledCheckItemOption>, std::string> r;
        search::query_scheduled_check_items(connection, r.first, r.second);
        return r;
      },
      [hwnd](auto result, std::exception_ptr) {
        auto *st =
            reinterpret_cast<State *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!st || !result)
          return;
        if (!result->second.empty()) {
          SetWindowTextW(st->status, w(result->second).c_str());
          return;
        }
        st->allItems = std::move(result->first);
        st->dictionaryLoaded = true;
        fillItems(st);
      });
}

void saveRule(HWND hwnd, State *st) {
  hwnd = st->ruleDialog ? st->ruleDialog : hwnd;
  if (st->roomCode.empty() || st->machCode.empty()) {
    MessageBoxW(hwnd, L"请先选择检验仪器；旧规则更新前也需要绑定仪器。",
                TITLE, MB_ICONINFORMATION);
    return;
  }
  if (!st->machineItemsLoaded || st->items.empty()) {
    MessageBoxW(hwnd, L"该仪器的项目尚未加载成功，不能保存规则。请核对 LIS 配置并重选仪器。",
                TITLE, MB_ICONINFORMATION);
    return;
  }
  std::vector<scheduled_check::Condition> conditions;
  for (size_t i = 0; i < st->conditionRows.size(); ++i) {
    scheduled_check::Condition condition;
    std::string error;
    if (!readCondition(st, st->conditionRows[i], condition, error)) {
      MessageBoxW(hwnd, w("条件 " + std::to_string(i + 1) + "：" + error).c_str(),
                  TITLE, MB_ICONINFORMATION);
      auto &row = st->conditionRows[i];
      HWND invalid = row.left;
      double number = 0.0;
      if (resolveItem(st, row.left) >= 0) {
        if (row.data.compare_with_value) invalid = row.value;
        else if (resolveItem(st, row.right) < 0) invalid = row.right;
        else if (!scheduled_check::parse_number(row.data.right_multiplier_text, number) || number <= 0.0) {
          row.optionsExpanded = true;
          invalid = row.multiplier;
        } else if (!scheduled_check::parse_number(row.data.tolerance_percent_text, number) || number < 0.0 || number > 100.0) {
          row.optionsExpanded = true;
          invalid = row.tolerance;
        }
      }
      updateConditionRows(st);
      applyRuleEditorVisibility(st);
      layout(g_page, st);
      SetFocus(invalid);
      return;
    }
    conditions.push_back(std::move(condition));
  }
  if (conditions.empty()) return;
  scheduled_check::Rule rule;
  const auto existing = std::find_if(st->rules.begin(), st->rules.end(),
      [st](const scheduled_check::Rule &r) { return r.id == st->editingRuleId; });
  if (st->editingRuleId > 0 && existing == st->rules.end()) {
    MessageBoxW(hwnd, L"规则已不存在，请重新选择或新建。", TITLE, MB_ICONINFORMATION);
    return;
  }
  if (existing != st->rules.end()) rule = *existing;
  static_cast<scheduled_check::Condition &>(rule) = conditions.front();
  rule.extra_conditions.assign(conditions.begin() + 1, conditions.end());
  rule.match_any = comboSelection(st->groupMode) == 1;
  rule.room_code = st->roomCode;
  rule.mach_code = st->machCode;
  rule.mach_name = st->machName;
  rule.name = search::trim(windowText(st->name));
  if (rule.name.empty()) {
    rule.name = scheduled_check::condition_description(rule);
    if (!rule.extra_conditions.empty())
      rule.name += std::string(rule.match_any ? " 等任一 " : " 等全部 ") +
                   std::to_string(conditions.size()) + " 条条件";
  }
  if (rule.id <= 0)
    rule.enabled = true;
  std::string e;
  if (!scheduled_check::save_rule(rule, e)) {
    MessageBoxW(hwnd, w(e).c_str(), TITLE, MB_ICONERROR);
    return;
  }
  st->editingRuleId = rule.id;
  refresh(st);
  closeRuleDialog(st);
  SetWindowTextW(st->status, L"规则已保存。");
  updateReminder();
  run_scheduled_result_check_now();
}

void selectRule(State *st) {
  const int index = selected(st->rulesList);
  if (index < 0 || index >= static_cast<int>(st->rules.size())) return;
  const auto rule = st->rules[index];
  if (!createRuleDialog(st)) return;
  SetWindowTextW(st->ruleDialog, L"编辑规则");
  st->editingRuleId = rule.id;
  st->editing = true;
  st->formScroll = 0;
  SetWindowTextW(st->name, w(rule.name).c_str());
  st->roomCode = rule.room_code; st->machCode = rule.mach_code; st->machName = rule.mach_name;
  SetWindowTextW(st->machine, rule.mach_code.empty() ? L"未选择（旧规则）" : w(rule.mach_name).c_str());
  SendMessageW(st->groupMode, CB_SETCURSEL, rule.match_any ? 1 : 0, 0);
  destroyConditionRows(st);
  for (const auto &condition : scheduled_check::rule_conditions(rule)) appendConditionRow(st, condition);
  loadMachineItems(g_page, st);
  applyRuleEditorVisibility(st);
  layout(g_page, st);
  showRuleDialog(st);
}

void clearEditor(State *st) {
  if (!createRuleDialog(st)) return;
  SetWindowTextW(st->ruleDialog, L"新建规则");
  st->editingRuleId = 0;
  st->editing = true;
  st->formScroll = 0;
  SetWindowTextW(st->name, L"");
  st->roomCode.clear(); st->machCode.clear(); st->machName.clear();
  SetWindowTextW(st->machine, L"未选择");
  SendMessageW(st->groupMode, CB_SETCURSEL, 0, 0);
  destroyConditionRows(st);
  appendConditionRow(st);
  loadMachineItems(g_page, st);
  applyRuleEditorVisibility(st);
  layout(g_page, st);
  showRuleDialog(st);
}
void openAlert(State *st, int row) {
  if (row < 0 || row >= static_cast<int>(st->alerts.size()))
    return;
  const auto &a = st->alerts[row];
  std::vector<std::string> highlightCodes{a.left_item_code};
  if (!a.compare_with_value && !a.right_item_code.empty())
    highlightCodes.push_back(a.right_item_code);
  if (!a.condition_item_codes.empty()) {
    highlightCodes.clear();
    std::istringstream codes(a.condition_item_codes);
    std::string code;
    while (std::getline(codes, code)) if (!code.empty()) highlightCodes.push_back(code);
  }
  auto *target =
      new RegularReportOpenTarget{a.rep_no,    a.oper_no,   a.inspect_date,
                                  a.mach_code, a.mach_name, a.room_code,
                                  highlightCodes};
  HWND report = create_regular_report_module(st->ctx);
  if (report)
    SendMessageW(st->ctx.mdiClient, WM_MDIMAXIMIZE,
                 reinterpret_cast<WPARAM>(report), 0);
  if (!report || !PostMessageW(report, WM_REGULAR_OPEN_REPORT, 0,
                               reinterpret_cast<LPARAM>(target))) {
    delete target;
    MessageBoxW(st->rulesList, L"常规报告页面打开失败。", TITLE, MB_ICONERROR);
  }
}

void applyRuleEditorVisibility(State *st) {
  const bool form = st->editing;
  const bool list = st->rulesExpanded;
  const auto show = [](HWND control, bool visible) { ShowWindow(control, visible ? SW_SHOW : SW_HIDE); };
  for (HWND control : {st->nameLabel, st->machineLabel, st->machine, st->machineButton,
                      st->name, st->groupMode, st->groupPrefix, st->groupSuffix,
                      st->addCondition, st->save, st->back}) show(control, form);
  for (HWND control : {st->newRule, st->editRule, st->del, st->rulesTitle, st->rulesList}) show(control, list);
  for (HWND control : {st->alertSearch, st->handled, st->handledAll, st->alertsTitle, st->alertsList})
    show(control, !st->rulesExpanded);
  show(st->rulesPane, st->rulesExpanded);
  SetParent(st->scan, st->rulesExpanded ? st->rulesPane : g_page);
  show(st->scan, true);
  show(st->formPane, form);
  for (auto &row : st->conditionRows) {
    for (HWND control : rowControls(row)) show(control, form);
    show(row.right, form && !row.data.compare_with_value);
    show(row.value, form && row.data.compare_with_value);
    show(row.options, form && !row.data.compare_with_value);
    for (HWND control : {row.multiplierLabel, row.multiplier, row.toleranceLabel, row.tolerance})
      show(control, form && !row.data.compare_with_value && row.optionsExpanded);
  }
  const int selection = selected(st->rulesList);
  const bool selectedRule = selection >= 0 && selection < static_cast<int>(st->rules.size());
  EnableWindow(st->editRule, selectedRule);
  EnableWindow(st->del, selectedRule);
}

void layout(HWND hwnd, State *st) {
  layoutRuleDialog(st);
  RECT rc{};
  GetClientRect(hwnd, &rc);
  const int w = rc.right;
  const int h = rc.bottom;
  const float scale = search::dpi_scale_factor(hwnd);
  const auto S = [scale](int value) {
    return (std::max)(1, static_cast<int>(value * scale + 0.5f));
  };

  const int pad = S(12);
  const int gap = S(8);
  HDC fontDc = GetDC(hwnd);
  HGDIOBJ oldFont = st->ctx.uiFont ? SelectObject(fontDc, st->ctx.uiFont) : nullptr;
  TEXTMETRICW metrics{};
  GetTextMetricsW(fontDc, &metrics);
  if (oldFont) SelectObject(fontDc, oldFont);
  ReleaseDC(hwnd, fontDc);
  const int textH = static_cast<int>(metrics.tmHeight);
  const int controlH = (std::max)(S(26), textH + S(8));
  const int titleH = (std::max)(S(22), textH + S(4));
  const int statusH = titleH;
  const int contentW = (std::max)(S(300), w - pad * 2);
  int y = pad;

  const int tabH = S(30);
  MoveWindow(st->tabs, pad, y, contentW, tabH, TRUE);
  y += tabH + S(10);
  const int statusY = (std::max)(y + S(120), h - pad - statusH);
  MoveWindow(st->progress, pad, h - pad - statusH - S(28), contentW, S(18),
             TRUE);

  if (!st->rulesExpanded) {
    const int searchW = S(220);
    const int scanW =
        search::measure_control_text_width(hwnd, st->scan, 86, 18);
    const int handledAllW = S(130);
    const int handledW = S(105);
    MoveWindow(st->alertSearch, pad, y, searchW, controlH, TRUE);
    MoveWindow(st->scan, pad + searchW + gap, y, scanW, controlH, TRUE);
    MoveWindow(st->handledAll, w - pad - handledW - gap - handledAllW, y,
               handledAllW, controlH, TRUE);
    MoveWindow(st->handled, w - pad - handledW, y, handledW, controlH, TRUE);
    y += controlH + S(8);
    MoveWindow(st->alertsTitle, pad, y + S(3), contentW - S(125), titleH, TRUE);
    y += titleH + S(2);
    MoveWindow(st->alertsList, pad, y, contentW,
               (std::max)(S(80), statusY - y - S(5)), TRUE);
    MoveWindow(st->status, pad, statusY, contentW, statusH, TRUE);
    return;
  }

  // Keep the saved rules and toolbar on the main page while the editor is modal.
  const int paneH = (std::max)(S(80), statusY - y - S(36));
  MoveWindow(st->rulesPane, pad, y, contentW, paneH, TRUE);
  RECT paneRc{};
  GetClientRect(st->rulesPane, &paneRc);
  const int listW = (std::max)(S(180), static_cast<int>(paneRc.right) - S(12));
  const auto place = [&](HWND control, int x, int top, int width, int height) {
    MoveWindow(control, x, top - st->rulesScroll,
               (std::max)(1, width), (std::max)(1, height), TRUE);
  };
  int top = S(6);
  {
    int buttonX = 0;
    for (HWND button : {st->newRule, st->editRule, st->del, st->scan}) {
      const int width = search::measure_control_text_width(hwnd, button, 86, 18);
      if (buttonX && buttonX + width > listW) { buttonX = 0; top += controlH + gap; }
      place(button, buttonX, top, width, controlH);
      buttonX += width + gap;
    }
    top += controlH + gap;
    place(st->rulesTitle, 0, top, listW, titleH);
    top += titleH + S(4);
    place(st->rulesList, 0, top, listW, (std::max)(S(100), paneH - top - S(8)));
    top += (std::max)(S(100), paneH - top - S(8)) + S(8);
  }
  const int totalH = top;
  SCROLLINFO scroll{sizeof(scroll), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
  scroll.nMin = 0; scroll.nMax = totalH - 1;
  scroll.nPage = paneH; scroll.nPos = st->rulesScroll;
  SetScrollInfo(st->rulesPane, SB_VERT, &scroll, TRUE);
  const int clamped = GetScrollPos(st->rulesPane, SB_VERT);
  MoveWindow(st->status, pad, statusY, contentW, statusH, TRUE);
  if (clamped != st->rulesScroll) {
    st->rulesScroll = clamped;
    layout(hwnd, st);
  }

}

void layoutRuleDialog(State *st) {
  if (!st->ruleDialog) return;
  HWND hwnd = st->ruleDialog;
  RECT rc{};
  GetClientRect(hwnd, &rc);
  const float scale = search::dpi_scale_factor(hwnd);
  const auto S = [scale](int value) { return (std::max)(1, static_cast<int>(value * scale + 0.5f)); };
  const int pad = S(12), gap = S(6);
  HDC dc = GetDC(hwnd);
  HGDIOBJ old = st->ctx.uiFont ? SelectObject(dc, st->ctx.uiFont) : nullptr;
  TEXTMETRICW metrics{};
  GetTextMetricsW(dc, &metrics);
  if (old) SelectObject(dc, old);
  ReleaseDC(hwnd, dc);
  const int textH = metrics.tmHeight;
  const int controlH = (std::max)(S(26), textH + S(6));
  const int titleH = (std::max)(S(20), textH + S(2));
  const int footerY = (std::max)(pad, static_cast<int>(rc.bottom) - pad - controlH);
  const int paneH = (std::max)(1, footerY - gap - pad);
  MoveWindow(st->formPane, pad, pad, (std::max)(1, static_cast<int>(rc.right) - pad * 2), paneH, TRUE);
  RECT paneRc{};
  GetClientRect(st->formPane, &paneRc);
  const int formW = (std::max)(S(180), static_cast<int>(paneRc.right) - S(4));
  const bool wide = formW >= S(660);
  const int half = (formW - S(12)) / 2;
  const int rightX = half + S(12);
  const auto place = [&](HWND control, int x, int top, int width, int height) {
    MoveWindow(control, x, top - st->formScroll, (std::max)(1, width), (std::max)(1, height), TRUE);
  };
  int top = 0;
  const int pickerW = search::measure_control_text_width(hwnd, st->machineButton, 96, 18);
  const int fieldH = titleH + S(4) + controlH;
  const int scopeW = wide ? half : formW;
  place(st->machineLabel, 0, top, scopeW, titleH);
  place(st->machine, 0, top + titleH + S(4), scopeW - pickerW - gap, controlH);
  place(st->machineButton, scopeW - pickerW, top + titleH + S(4), pickerW, controlH);
  if (!wide) top += fieldH + gap;
  place(st->nameLabel, wide ? rightX : 0, top, wide ? half : formW, titleH);
  place(st->name, wide ? rightX : 0, top + titleH + S(4), wide ? half : formW, controlH);
  top += fieldH + S(8);
  const int prefixW = (std::max)(S(40), textH * 2 + S(8));
  const int suffixW = (std::max)(S(70), textH * 3 + S(8));
  const int groupW = (std::min)(S(175), formW - prefixW - suffixW - gap * 2);
  place(st->groupPrefix, 0, top + S(4), prefixW, titleH);
  place(st->groupMode, prefixW + gap, top, groupW, S(160));
  place(st->groupSuffix, prefixW + gap * 2 + groupW, top + S(4), suffixW, titleH);
  top += controlH + S(8);

  int numberW = S(30);
  const int opW = (std::max)(S(60), textH * 2 + GetSystemMetrics(SM_CXVSCROLL) + S(8));
  for (const auto &row : st->conditionRows) {
    numberW = (std::max)(numberW, search::measure_control_text_width(hwnd, row.number, row.data.negate ? 75 : 30, 4));
    // Set both the collapsed field and popup row height after font changes.
    SendMessageW(row.op, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), textH + S(4));
    SendMessageW(row.op, CB_SETITEMHEIGHT, 0, textH + S(6));
  }
  for (auto &row : st->conditionRows) {
    const int kindW = (std::max)(S(76), textH * 3 + S(22));
    const int optionsW = (std::min)(formW / 2, search::measure_control_text_width(hwnd, row.options, 88, 12));
    const int moreW = search::measure_control_text_width(hwnd, row.more, 44, 12);
    const int removeW = search::measure_control_text_width(hwnd, row.remove, 44, 12);
    const int actions = (row.data.compare_with_value ? 0 : optionsW + gap) + moreW + removeW;
    const bool inlineRow = formW >= S(760) &&
        formW >= numberW + opW + kindW + actions + gap * 6 + S(360);
    place(row.number, 0, top + S(4), numberW, titleH);
    if (inlineRow) {
      const int optionsSpace = row.data.compare_with_value ? 0 : optionsW + gap;
      const int fieldsW = formW - numberW - opW - kindW - optionsSpace - moreW - removeW - gap * 6;
      const int leftW = fieldsW / 2;
      int x = numberW + gap;
      place(row.left, x, top, leftW, S(260)); x += leftW + gap;
      place(row.op, x, top, opW, S(220)); x += opW + gap;
      place(row.kind, x, top, kindW, S(160)); x += kindW + gap;
      const int targetW = fieldsW - leftW;
      place(row.right, x, top, targetW, S(260));
      place(row.value, x, top, targetW, controlH); x += targetW + gap;
      if (!row.data.compare_with_value) { place(row.options, x, top, optionsW, controlH); x += optionsW + gap; }
      place(row.more, x, top, moreW, controlH); x += moreW + gap;
      place(row.remove, x, top, removeW, controlH);
      top += controlH + gap;
    } else {
      const int firstW = formW - numberW - gap;
      const bool compact = firstW < S(500);
      const int leftW = compact ? firstW : firstW - opW - kindW - gap * 2;
      place(row.left, numberW + gap, top, leftW, S(260));
      if (compact) top += controlH + gap;
      const int relationX = compact ? numberW + gap : numberW + gap * 2 + leftW;
      const bool stackKind = compact && firstW < opW + kindW + gap;
      const int relationW = opW;
      place(row.op, relationX, top, relationW, S(220));
      if (stackKind) top += controlH + gap;
      place(row.kind, stackKind ? numberW + gap : relationX + relationW + gap, top, kindW, S(160));
      top += controlH + gap;
      const int targetX = numberW + gap;
      const int targetW = formW - targetX;
      const int actionsW = moreW + removeW + gap + (row.data.compare_with_value ? 0 : optionsW + gap);
      const bool actionsBelow = targetW - actionsW - gap < S(180);
      const int inputW = actionsBelow ? targetW : targetW - actionsW - gap;
      place(row.right, targetX, top, inputW, S(260));
      place(row.value, targetX, top, inputW, controlH);
      if (actionsBelow) top += controlH + gap;
      int x = actionsBelow ? targetX : targetX + inputW + gap;
      const auto action = [&](HWND control, int width) {
        if (x > targetX && x + width > formW) { x = targetX; top += controlH + gap; }
        place(control, x, top, width, controlH);
        x += width + gap;
      };
      if (!row.data.compare_with_value) action(row.options, optionsW);
      action(row.more, moreW);
      action(row.remove, removeW);
      top += controlH + gap;
    }
    if (!row.data.compare_with_value && row.optionsExpanded) {
      const int x = numberW + gap;
      const int labelW = (std::max)(S(60), textH * 4);
      const int errorLabelW = (std::max)(S(85), textH * 5);
      const int inputW = S(85);
      place(row.multiplierLabel, x, top + S(4), labelW, titleH);
      place(row.multiplier, x + labelW + S(4), top, inputW, controlH);
      int errorX = x + labelW + inputW + gap * 2;
      if (errorX + errorLabelW + inputW + S(25) > formW) { top += controlH + gap; errorX = x; }
      place(row.toleranceLabel, errorX, top + S(4), errorLabelW, titleH);
      place(row.tolerance, errorX + errorLabelW + S(4), top, inputW, controlH);
      top += controlH + gap;
    }
    place(row.separator, 0, top + S(4), formW, S(2));
    top += S(8);
  }
  place(st->addCondition, 0, top,
        search::measure_control_text_width(hwnd, st->addCondition, 115, 18), controlH);
  top += controlH + S(8);
  SCROLLINFO scroll{sizeof(scroll), SIF_RANGE | SIF_PAGE | SIF_POS};
  scroll.nMin = 0; scroll.nMax = top - 1; scroll.nPage = paneH; scroll.nPos = st->formScroll;
  SetScrollInfo(st->formPane, SB_VERT, &scroll, TRUE);
  const int clamped = GetScrollPos(st->formPane, SB_VERT);
  const int buttonW = search::measure_control_text_width(hwnd, st->save, 86, 20);
  MoveWindow(st->save, rc.right - pad - buttonW, footerY, buttonW, controlH, TRUE);
  MoveWindow(st->back, rc.right - pad - buttonW * 2 - gap, footerY, buttonW, controlH, TRUE);
  MoveWindow(st->dialogStatus, pad, footerY + S(3), (std::max)(1, static_cast<int>(rc.right) - pad * 2 - buttonW * 2 - gap * 2), titleH, TRUE);
  SetWindowTextW(st->dialogStatus, w(windowText(st->status)).c_str());
  if (clamped != st->formScroll) { st->formScroll = clamped; layoutRuleDialog(st); }
}

LRESULT CALLBACK scrollPaneProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR id, DWORD_PTR data) {
  auto *st = reinterpret_cast<State *>(data);
  if (msg == WM_COMMAND || msg == WM_NOTIFY || msg == WM_DRAWITEM || msg == WM_MEASUREITEM)
    return SendMessageW(g_page, msg, wp, lp);
  if (msg == WM_VSCROLL || msg == WM_MOUSEWHEEL) {
    SCROLLINFO info{sizeof(info), SIF_ALL};
    GetScrollInfo(hwnd, SB_VERT, &info);
    int position = info.nPos;
    const int step = static_cast<int>(48 * search::dpi_scale_factor(hwnd));
    if (msg == WM_MOUSEWHEEL) {
      position -= MulDiv(GET_WHEEL_DELTA_WPARAM(wp), step, WHEEL_DELTA);
    } else {
      switch (LOWORD(wp)) {
      case SB_LINEUP: position -= step; break;
      case SB_LINEDOWN: position += step; break;
      case SB_PAGEUP: position -= info.nPage; break;
      case SB_PAGEDOWN: position += info.nPage; break;
      case SB_THUMBTRACK: case SB_THUMBPOSITION: position = info.nTrackPos; break;
      case SB_TOP: position = info.nMin; break;
      case SB_BOTTOM: position = info.nMax; break;
      default: return 0;
      }
    }
    int &offset = hwnd == st->formPane ? st->formScroll : st->rulesScroll;
    offset = (std::max)(0, (std::min)(position, info.nMax - static_cast<int>(info.nPage) + 1));
    layout(g_page, st);
    return 0;
  }
  if (msg == WM_NCDESTROY)
    RemoveWindowSubclass(hwnd, scrollPaneProc, id);
  return DefSubclassProc(hwnd, msg, wp, lp);
}

void closeRuleDialog(State *st) {
  HWND dialog = st->ruleDialog;
  if (!dialog) return;
  st->editing = false;
  st->ruleDialog = nullptr;
  st->dialogStatus = nullptr;
  // Keep the reusable controls and their event handlers with the module.
  for (HWND control : {st->formPane, st->save, st->back}) {
    ShowWindow(control, SW_HIDE);
    SetParent(control, g_page);
  }
  DestroyWindow(dialog);
  applyRuleEditorVisibility(st);
  layout(g_page, st);
}

LRESULT CALLBACK ruleDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto *st = reinterpret_cast<State *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    st = reinterpret_cast<State *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
  }
  if (!st) return DefWindowProcW(hwnd, msg, wp, lp);
  switch (msg) {
  case WM_SIZE:
    layoutRuleDialog(st);
    return 0;
  case WM_GETMINMAXINFO: {
    const float scale = search::dpi_scale_factor(hwnd);
    auto *limits = reinterpret_cast<MINMAXINFO *>(lp);
    limits->ptMinTrackSize = {static_cast<LONG>(520 * scale), static_cast<LONG>(300 * scale)};
    return 0;
  }
  case WM_MOUSEWHEEL:
    return SendMessageW(st->formPane, msg, wp, lp);
  case DM_GETDEFID:
    return MAKELONG(IDC_SAVE, DC_HASDEFID);
  case WM_COMMAND:
    if (LOWORD(wp) == IDCANCEL) { closeRuleDialog(st); return 0; }
    if (LOWORD(wp) == IDOK) {
      return SendMessageW(g_page, WM_COMMAND, MAKEWPARAM(IDC_SAVE, BN_CLICKED), reinterpret_cast<LPARAM>(st->save));
    }
    return SendMessageW(g_page, msg, wp, lp);
  case WM_CLOSE:
    closeRuleDialog(st);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

bool createRuleDialog(State *st) {
  if (st->ruleDialog) return false;
  constexpr const wchar_t *className = L"ScheduledCheckRuleDialog";
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = ruleDialogProc;
  wc.hInstance = st->ctx.instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.lpszClassName = className;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
  HWND owner = GetAncestor(g_page, GA_ROOT);
  st->ruleDialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
      className, L"新建规则", WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
      CW_USEDEFAULT, CW_USEDEFAULT, 980, 360, owner, nullptr, st->ctx.instance, st);
  if (!st->ruleDialog) {
    MessageBoxW(g_page, L"规则窗口打开失败，请重试。", TITLE, MB_ICONERROR);
    return false;
  }
  SetParent(st->formPane, st->ruleDialog);
  SetParent(st->save, st->ruleDialog);
  SetParent(st->back, st->ruleDialog);
  st->dialogStatus = search::create_label(st->ruleDialog, L"", 0, 0, 0, 0);
  SetWindowLongPtrW(st->dialogStatus, GWL_STYLE, SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS | WS_CHILD | WS_VISIBLE);
  SendMessageW(st->save, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);
  search::apply_font_to_children(st->ruleDialog, st->ctx.uiFont);
  return true;
}

void showRuleDialog(State *st) {
  HWND dialog = st->ruleDialog;
  HWND owner = GetWindow(dialog, GW_OWNER);
  RECT ownerRect{}, work{};
  GetWindowRect(owner, &ownerRect);
  MONITORINFO monitor{sizeof(monitor)};
  GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor);
  work = monitor.rcWork;
  const float scale = search::dpi_scale_factor(owner);
  const int width = (std::min)(static_cast<int>(980 * scale), static_cast<int>(work.right - work.left));
  // A small editor for a single condition; grow only for existing longer rules.
  const int desiredHeight = 300 + static_cast<int>((std::min)(st->conditionRows.size(), size_t{6})) * 46;
  const int height = (std::min)(static_cast<int>(desiredHeight * scale), static_cast<int>(work.bottom - work.top));
  const int x = (std::max)(static_cast<int>(work.left), (std::min)(static_cast<int>(work.right) - width, static_cast<int>((ownerRect.left + ownerRect.right - width) / 2)));
  const int y = (std::max)(static_cast<int>(work.top), (std::min)(static_cast<int>(work.bottom) - height, static_cast<int>((ownerRect.top + ownerRect.bottom - height) / 2)));
  SetWindowPos(dialog, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
  layoutRuleDialog(st);
  const bool ownerEnabled = IsWindowEnabled(owner);
  if (ownerEnabled) EnableWindow(owner, FALSE);
  ShowWindow(dialog, SW_SHOW);
  SetForegroundWindow(dialog);
  SetFocus(st->editingRuleId ? st->name : st->machineButton);
  MSG message{};
  while (IsWindow(dialog)) {
    const BOOL result = GetMessageW(&message, nullptr, 0, 0);
    if (result <= 0) {
      if (IsWindow(dialog)) SendMessageW(dialog, WM_CLOSE, 0, 0);
      if (result == 0) PostQuitMessage(static_cast<int>(message.wParam));
      break;
    }
    if (!IsDialogMessageW(dialog, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  if (ownerEnabled && IsWindow(owner)) {
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    if (IsWindow(g_page)) SetFocus(g_page);
  }
}

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto *st = reinterpret_cast<State *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
  case WM_CREATE: {
    auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
    auto *mcs = reinterpret_cast<MDICREATESTRUCTW *>(cs->lpCreateParams);
    st = reinterpret_cast<State *>(mcs->lParam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
    g_page = hwnd;
    st->groupMode = search::create_combo(hwnd, IDC_GROUP_MODE, 0, 0, 0, 0, false);
    for (const wchar_t *mode : {L"全部条件", L"任一条件"})
      SendMessageW(st->groupMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mode));
    SendMessageW(st->groupMode, CB_SETCURSEL, 0, 0);
    st->groupPrefix = search::create_label(hwnd, L"满足", 0, 0, 0, 0);
    st->groupSuffix = search::create_label(hwnd, L"时提醒", 0, 0, 0, 0);
    st->addCondition = search::create_button(hwnd, IDC_ADD_CONDITION, L"＋ 添加条件", 0, 0, 0, 0);
    st->nameLabel = search::create_label(hwnd, L"规则名称", 0, 0, 0, 0);
    st->machineLabel = search::create_label(hwnd, L"仪器", 0, 0, 0, 0);
    st->machine = search::create_label(hwnd, L"未选择", 0, 0, 0, 0);
    st->machineButton = search::create_button(hwnd, IDC_MACHINE, L"选择仪器", 0, 0, 0, 0);
    st->name = search::create_edit(hwnd, IDC_NAME, 0, 0, 0, 0);
    SendMessageW(st->name, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"选填，自动命名"));
    st->newRule = search::create_button(hwnd, IDC_NEW, L"新建规则", 0, 0, 0, 0);
    st->editRule = search::create_button(hwnd, IDC_EDIT_RULE, L"编辑", 0, 0, 0, 0);
    st->back = search::create_button(hwnd, IDC_BACK, L"取消", 0, 0, 0, 0);
    st->save = search::create_button(hwnd, IDC_SAVE, L"保存规则", 0, 0, 0, 0);
    st->del = search::create_button(hwnd, IDC_DELETE, L"删除规则", 0, 0, 0, 0);
    st->scan = search::create_button(hwnd, IDC_SCAN, L"立即扫描", 0, 0, 0, 0);
    st->rulesTitle = search::create_label(hwnd, L"已保存规则", 0, 0, 0, 0);
    st->rulesList = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL, 0, 0, 0, 0, hwnd,
        win32_control_id(IDC_RULES), st->ctx.instance, nullptr);
    ListView_SetExtendedListViewStyle(st->rulesList, LVS_EX_FULLROWSELECT |
                                                         LVS_EX_GRIDLINES |
                                                         LVS_EX_DOUBLEBUFFER |
                                                         LVS_EX_CHECKBOXES);
    const wchar_t *rh[] = {L"状态", L"规则名称", L"仪器", L"条件摘要"};
    int rw[] = {70, 220, 180, 650};
    for (int i = 0; i < 4; ++i)
      search::add_list_column(st->rulesList, i, rh[i], rw[i]);
    st->handled =
        search::create_button(hwnd, IDC_HANDLED, L"标记已处理", 0, 0, 0, 0);
    st->alertsTitle = search::create_label(hwnd, L"今日命中记录", 0, 0, 0, 0);
    st->alertsList = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL, 0, 0, 0, 0, hwnd,
        win32_control_id(IDC_ALERTS), st->ctx.instance, nullptr);
    ListView_SetExtendedListViewStyle(st->alertsList, LVS_EX_FULLROWSELECT |
                                                          LVS_EX_GRIDLINES |
                                                          LVS_EX_DOUBLEBUFFER);
    const wchar_t *ah[] = {L"发现时间", L"状态",   L"规则",   L"报告号",
                           L"样本号",   L"项目 A", L"A 结果", L"关系",
                           L"项目 B",   L"B 结果", L"条件详情"};
    int aw[] = {145, 70, 160, 95, 95, 180, 80, 130, 180, 80, 500};
    for (int i = 0; i < 11; ++i)
      search::add_list_column(st->alertsList, i, ah[i], aw[i]);
    st->status = search::create_label(hwnd, L"正在加载项目字典...", 0, 0, 0, 0);
    st->tabs = CreateWindowExW(
        0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0,
        hwnd, win32_control_id(IDC_TABS), st->ctx.instance, nullptr);
    TCITEMW tab{};
    tab.mask = TCIF_TEXT;
    tab.pszText = const_cast<wchar_t *>(L"待处理");
    TabCtrl_InsertItem(st->tabs, 0, &tab);
    tab.pszText = const_cast<wchar_t *>(L"规则设置");
    TabCtrl_InsertItem(st->tabs, 1, &tab);
    TabCtrl_SetCurSel(st->tabs, st->rulesExpanded ? 1 : 0);
    st->alertSearch = search::create_edit(hwnd, IDC_ALERT_SEARCH, 0, 0, 0, 0);
    SendMessageW(st->alertSearch, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"搜索样本号 / 报告号"));
    st->handledAll = search::create_button(hwnd, IDC_HANDLED_ALL,
                                           L"全部标记已处理", 0, 0, 0, 0);
    st->progress = CreateWindowExW(
        0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_MARQUEE, 0, 0, 0, 0, hwnd,
        win32_control_id(IDC_PROGRESS), st->ctx.instance, nullptr);
    SendMessageW(st->progress, PBM_SETMARQUEE, TRUE, 30);
    st->rulesPane = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"",
        WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL, 0, 0, 0, 0, hwnd,
        nullptr, st->ctx.instance, nullptr);
    SetWindowSubclass(st->rulesPane, scrollPaneProc, 1, reinterpret_cast<DWORD_PTR>(st));
    for (HWND label : {st->nameLabel, st->machineLabel, st->machine,
                      st->rulesTitle, st->groupPrefix, st->groupSuffix})
      SetWindowLongPtrW(label, GWL_STYLE,
                       (GetWindowLongPtrW(label, GWL_STYLE) & ~SS_TYPEMASK) | SS_LEFT | SS_NOPREFIX);
    SetWindowLongPtrW(st->machine, GWL_STYLE,
                     GetWindowLongPtrW(st->machine, GWL_STYLE) | SS_ENDELLIPSIS);
    st->formPane = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"",
        WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL, 0, 0, 0, 0, hwnd,
        nullptr, st->ctx.instance, nullptr);
    SetWindowSubclass(st->formPane, scrollPaneProc, 1, reinterpret_cast<DWORD_PTR>(st));
    for (HWND control : {st->nameLabel, st->machineLabel, st->machine, st->machineButton,
                         st->name, st->groupMode, st->groupPrefix, st->groupSuffix,
                         st->addCondition}) SetParent(control, st->formPane);
    for (HWND control : {st->newRule, st->editRule, st->del, st->rulesTitle,
                         st->rulesList}) SetParent(control, st->rulesPane);
    search::apply_font_to_children(hwnd, st->ctx.uiFont);
    fillItems(st);
    applyRuleEditorVisibility(st);
    refresh(st);
    layout(hwnd, st);
    startItemLoad(hwnd, st);
    return 0;
  }
  case WM_SIZE:
    if (st)
      layout(hwnd, st);
    return 0;
  case app::WM_APP_SETTINGS_CHANGED:
    if (st && st->ctx.appContext) {
      auto *context = static_cast<app::Context *>(st->ctx.appContext);
      st->ctx.dbSettings = context->dbSettings;
      for (auto &row : st->conditionRows) rememberRow(st, row);
      st->allItems.clear();
      st->dictionaryLoaded = false;
      st->machineItemCodes.clear();
      st->machineItemsLoaded = false;
      fillItems(st);
      startItemLoad(hwnd, st);
      if (!st->machCode.empty()) loadMachineItems(hwnd, st);
    }
    return 0;
  case app::WM_APP_FONT_CHANGED:
    if (st) {
      st->ctx.uiFont = reinterpret_cast<HFONT>(lp);
      search::apply_font_to_children(hwnd, st->ctx.uiFont);
      if (st->ruleDialog) search::apply_font_to_children(st->ruleDialog, st->ctx.uiFont);
      layout(hwnd, st);
    }
    return 0;
  case WM_MEASUREITEM: {
    auto *item = reinterpret_cast<MEASUREITEMSTRUCT *>(lp);
    if (!st || !item || item->CtlType != ODT_COMBOBOX || item->CtlID != IDC_OP) break;
    HDC dc = GetDC(hwnd);
    HGDIOBJ old = st->ctx.uiFont ? SelectObject(dc, st->ctx.uiFont) : nullptr;
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    if (old) SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    item->itemHeight = metrics.tmHeight + static_cast<UINT>(6 * search::dpi_scale_factor(hwnd));
    return TRUE;
  }
  case WM_DRAWITEM: {
    auto *item = reinterpret_cast<DRAWITEMSTRUCT *>(lp);
    if (!item || item->CtlType != ODT_COMBOBOX || item->CtlID != IDC_OP) break;
    const bool selectedItem = (item->itemState & ODS_SELECTED) != 0;
    const int savedDc = SaveDC(item->hDC);
    FillRect(item->hDC, &item->rcItem, GetSysColorBrush(selectedItem ? COLOR_HIGHLIGHT : COLOR_WINDOW));
    SetTextColor(item->hDC, GetSysColor((item->itemState & ODS_DISABLED) ? COLOR_GRAYTEXT :
                                      selectedItem ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    SetBkMode(item->hDC, TRANSPARENT);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(item->hwndItem, WM_GETFONT, 0, 0));
    if (font) SelectObject(item->hDC, font);
    if (item->itemID < static_cast<UINT>(COMPARISON_COUNT))
      DrawTextW(item->hDC, COMPARISONS[item->itemID].symbol, -1, &item->rcItem,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if ((item->itemState & ODS_FOCUS) && !(item->itemState & ODS_NOFOCUSRECT))
      DrawFocusRect(item->hDC, &item->rcItem);
    RestoreDC(item->hDC, savedDc);
    return TRUE;
  }
  case WM_COMMAND: {
    if (!st)
      break;
    if (st->loadingCondition) return 0;
    wchar_t controlClass[32]{};
    if (lp) GetClassNameW(reinterpret_cast<HWND>(lp), controlClass, 32);
    const bool comboFocused = _wcsicmp(controlClass, L"ComboBox") == 0 && HIWORD(wp) == CBN_SETFOCUS;
    const bool editFocused = _wcsicmp(controlClass, L"Edit") == 0 && HIWORD(wp) == EN_SETFOCUS;
    // Focus notifications are not button actions. Opening the picker transfers
    // focus and must not recursively open it again on BN_KILLFOCUS.
    if (_wcsicmp(controlClass, L"Button") == 0 && HIWORD(wp) != BN_CLICKED)
      return 0;
    if (lp && st->editing && GetParent(reinterpret_cast<HWND>(lp)) == st->formPane &&
        (comboFocused || editFocused)) {
      RECT fieldRect{}, paneRect{};
      GetWindowRect(reinterpret_cast<HWND>(lp), &fieldRect);
      MapWindowPoints(nullptr, st->formPane, reinterpret_cast<POINT *>(&fieldRect), 2);
      GetClientRect(st->formPane, &paneRect);
      // Combo window bounds include the dropdown; scroll only its edit/header.
      const int visibleH = comboFocused
                               ? static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lp), CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0)) +
                                     static_cast<int>(8 * search::dpi_scale_factor(hwnd))
                               : static_cast<int>(fieldRect.bottom - fieldRect.top);
      if (fieldRect.top < 0)
        st->formScroll = (std::max)(0, st->formScroll + static_cast<int>(fieldRect.top));
      else if (fieldRect.top + visibleH > paneRect.bottom)
        st->formScroll += fieldRect.top + visibleH - paneRect.bottom;
      if (GetScrollPos(st->formPane, SB_VERT) != st->formScroll)
        layout(hwnd, st);
      return 0;
    }
    for (size_t index = 0; index < st->conditionRows.size(); ++index) {
      auto &row = st->conditionRows[index];
      const HWND source = reinterpret_cast<HWND>(lp);
      const auto controls = rowControls(row);
      if (std::find(controls.begin(), controls.end(), source) == controls.end()) continue;
      if ((source == row.left || source == row.right) &&
          (HIWORD(wp) == CBN_EDITCHANGE || HIWORD(wp) == CBN_KILLFOCUS)) {
        autoMatchItemCode(st, source, source == row.left ? L"项目" : L"比较项目",
                          HIWORD(wp) == CBN_KILLFOCUS);
      } else if (source == row.options && HIWORD(wp) == BN_CLICKED) {
        row.optionsExpanded = !row.optionsExpanded;
      } else if (source == row.more && HIWORD(wp) == BN_CLICKED) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (row.data.negate ? MF_CHECKED : MF_UNCHECKED),
                    1, L"取反此条件");
        RECT anchor{};
        GetWindowRect(row.more, &anchor);
        const UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN,
                                           anchor.left, anchor.bottom, 0, st->ruleDialog ? st->ruleDialog : hwnd, nullptr);
        DestroyMenu(menu);
        if (!IsWindow(hwnd) || reinterpret_cast<State *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)) != st)
          return 0;
        if (choice == 1) row.data.negate = !row.data.negate;
      } else if (source == row.remove && HIWORD(wp) == BN_CLICKED) {
        if (st->conditionRows.size() <= 1) return 0;
        st->loadingCondition = true;
        for (HWND control : controls) DestroyWindow(control);
        st->conditionRows.erase(st->conditionRows.begin() + index);
        orderEditorControls(st);
        st->loadingCondition = false;
        updateConditionRows(st);
        applyRuleEditorVisibility(st);
        layout(hwnd, st);
        auto &next = st->conditionRows[(std::min)(index, st->conditionRows.size() - 1)];
        SetFocus(IsWindowEnabled(next.left) ? next.left : next.more);
        return 0;
      } else if (!((HIWORD(wp) == CBN_SELCHANGE &&
                    (source == row.left || source == row.right || source == row.op || source == row.kind)) ||
                   (HIWORD(wp) == EN_CHANGE &&
                    (source == row.value || source == row.multiplier || source == row.tolerance)))) {
        return 0;
      }
      updateConditionRows(st);
      applyRuleEditorVisibility(st);
      layout(hwnd, st);
      return 0;
    }
    if (LOWORD(wp) == IDC_ALERT_SEARCH && HIWORD(wp) == EN_CHANGE) {
      wchar_t buffer[256]{};
      GetWindowTextW(st->alertSearch, buffer, 256);
      st->alertFilter = buffer;
      applyAlertFilter(st);
      return 0;
    }
    switch (LOWORD(wp)) {
    case IDC_GROUP_MODE:
      return 0;
    case IDC_ADD_CONDITION:
      if (st->conditionRows.size() >= 64) {
        MessageBoxW(st->ruleDialog ? st->ruleDialog : hwnd, L"每条规则最多支持 64 个条件。", TITLE, MB_ICONINFORMATION);
        return 0;
      }
      appendConditionRow(st);
      fillItems(st);
      SetFocus(st->conditionRows.back().left);
      return 0;
    case IDC_EDIT_RULE:
      selectRule(st);
      return 0;
    case IDC_BACK:
      closeRuleDialog(st);
      return 0;
    case IDC_NEW:
      clearEditor(st);
      return 0;
    case IDC_MACHINE: {
      if (HIWORD(wp) != BN_CLICKED) return 0;
      search::MachinePickerPopupOptions options;
      options.owner = st->ruleDialog ? st->ruleDialog : hwnd;
      options.anchor = st->machineButton;
      options.font = st->ctx.uiFont;
      options.db_settings = st->ctx.dbSettings;
      options.current_room_code = st->roomCode;
      options.current_mach_code = st->machCode;
      options.include_all_rooms = true;
      options.on_accept = [hwnd](const search::MachineOption &machine) {
        if (!IsWindow(hwnd)) return;
        auto *state = reinterpret_cast<State *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!state) return;
        for (auto &row : state->conditionRows) rememberRow(state, row);
        state->roomCode = machine.room_code;
        state->machCode = machine.mach_code;
        state->machName = machine.mach_name;
        SetWindowTextW(state->machine, w(machine.mach_name).c_str());
        loadMachineItems(hwnd, state);
      };
      search::show_machine_picker_popup(options);
      return 0;
    }
    case IDC_SAVE:
      saveRule(hwnd, st);
      return 0;
    case IDC_DELETE: {
      int x = selected(st->rulesList);
      if (x >= 0 && x < static_cast<int>(st->rules.size()) &&
          MessageBoxW(hwnd, L"确定删除选中的核查规则及其历史记录吗？", TITLE,
                      MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) == IDYES) {
        std::string e;
        if (!scheduled_check::delete_rule(st->rules[x].id, e)) {
          MessageBoxW(hwnd, w(e).c_str(), TITLE, MB_ICONERROR);
          return 0;
        }
        st->editing = false;
        refresh(st);
        applyRuleEditorVisibility(st);
        layout(hwnd, st);
        updateReminder();
      }
      return 0;
    }
    case IDC_SCAN:
      if (st->progress)
        ShowWindow(st->progress, SW_SHOW);
      SetWindowTextW(st->status, run_scheduled_result_check_now()
                                     ? L"正在扫描当日结果..."
                                     : L"已有扫描正在运行，已安排随后重扫。");
      return 0;
    case IDC_HANDLED: {
      int x = selected(st->alertsList);
      if (x >= 0 && x < static_cast<int>(st->alerts.size())) {
        std::string e;
        if (!scheduled_check::set_alert_handled(st->alerts[x].id, true, e)) {
          MessageBoxW(hwnd, w(e).c_str(), TITLE, MB_ICONERROR);
          return 0;
        }
        loadAlerts(st);
        updateReminder();
      }
      return 0;
    }
    case IDC_HANDLED_ALL: {
      std::string e;
      int count = 0;
      for (const auto &a : st->allAlerts) {
        if (a.handled)
          continue;
        if (!scheduled_check::set_alert_handled(a.id, true, e)) {
          MessageBoxW(hwnd, w(e).c_str(), TITLE, MB_ICONERROR);
          loadAlerts(st);
          updateReminder();
          return 0;
        }
        ++count;
      }
      SetWindowTextW(st->status,
                     (L"已标记 " + std::to_wstring(count) +
                      L" 条为已处理。").c_str());
      loadAlerts(st);
      updateReminder();
      return 0;
    }
    }
    break;
  }
  case WM_NOTIFY:
    if (st) {
      auto *n = reinterpret_cast<NMHDR *>(lp);
      if (n->idFrom == IDC_ALERTS && n->code == NM_CUSTOMDRAW) {
        auto *draw = reinterpret_cast<NMLVCUSTOMDRAW *>(lp);
        if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
          return CDRF_NOTIFYITEMDRAW;
        if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
          const size_t row = static_cast<size_t>(draw->nmcd.dwItemSpec);
          if (row < st->alerts.size() && !st->alerts[row].handled) {
            draw->clrTextBk = RGB(255, 242, 218);
            draw->clrText = RGB(112, 47, 20);
          }
          return CDRF_DODEFAULT;
        }
      }
      if (n->idFrom == IDC_TABS && n->code == TCN_SELCHANGE) {
        st->rulesExpanded = TabCtrl_GetCurSel(st->tabs) == 1;
        applyRuleEditorVisibility(st);
        layout(hwnd, st);
        return 0;
      }
      if (n->idFrom == IDC_ALERTS && n->code == LVN_COLUMNCLICK) {
        auto *col = reinterpret_cast<NMLISTVIEW *>(lp);
        if (st->alertsSortColumn == col->iSubItem)
          st->alertsSortAscending = !st->alertsSortAscending;
        else {
          st->alertsSortColumn = col->iSubItem;
          st->alertsSortAscending = true;
        }
        applyAlertFilter(st);
        return 0;
      }
      if (n->idFrom == IDC_RULES && n->code == LVN_ITEMCHANGED) {
        auto *item = reinterpret_cast<NMLISTVIEW *>(lp);
        if (!st->syncingRules && (item->uChanged & LVIF_STATE) &&
            ((item->uNewState ^ item->uOldState) & LVIS_STATEIMAGEMASK)) {
          const int row = item->iItem;
          if (row >= 0 && row < static_cast<int>(st->rules.size())) {
            const bool checked =
                ((item->uNewState & LVIS_STATEIMAGEMASK) >> 12) == 2;
            std::string e;
            if (!scheduled_check::set_rule_enabled(st->rules[row].id, checked,
                                                   e)) {
              MessageBoxW(hwnd, w(e).c_str(), TITLE, MB_ICONERROR);
              loadRules(st);
              return 0;
            }
            st->rules[row].enabled = checked;
            // The row already exists here. setItem() inserts a new row for
            // column 0 and is only intended for list population; using it in
            // LVN_ITEMCHANGED re-enters this notification and shifts the UI
            // rows away from st->rules.
            st->syncingRules = true;
            ListView_SetItemText(st->rulesList, row, 0,
                                 const_cast<wchar_t *>(checked ? L"启用"
                                                               : L"停用"));
            st->syncingRules = false;
            updateReminder();
            run_scheduled_result_check_now();
            return 0;
          }
        }
        if (!st->syncingRules) applyRuleEditorVisibility(st);
      }
      if (n->idFrom == IDC_RULES && n->code == NM_DBLCLK) selectRule(st);
      if (n->idFrom == IDC_ALERTS && n->code == NM_DBLCLK)
        openAlert(st, selected(st->alertsList));
    }
    break;
  case WM_NCDESTROY:
    if (st) {
      closeRuleDialog(st);
      st->itemTask.cancel();
      st->machineItemTask.cancel();
      if (g_page == hwnd)
        g_page = nullptr;
      delete st;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
    break;
  }
  return DefMDIChildProcW(hwnd, msg, wp, lp);
}

} // namespace

HWND create_scheduled_result_check_module(const ModuleContext &ctx) {
  if (HWND e = activate_existing_mdi_child_by_title(ctx.mdiClient, TITLE))
    return e;
  REGISTER_MDI_CHILD_CLASS(ctx.instance, proc, CLASS_NAME,
                           reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
  auto *st = new State;
  st->ctx = ctx;
  MDICREATESTRUCTW m{};
  m.szTitle = TITLE;
  m.szClass = CLASS_NAME;
  m.hOwner = ctx.instance;
  m.x = m.y = m.cx = m.cy = CW_USEDEFAULT;
  m.lParam = reinterpret_cast<LPARAM>(st);
  HWND h = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0,
                                               reinterpret_cast<LPARAM>(&m)));
  if (!h) {
    delete st;
    return nullptr;
  }
  SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(h), 0);
  return h;
}
void start_scheduled_result_check_monitor(HWND main_window,
                                          const ModuleContext &ctx) {
  const bool alreadyRunning = g_monitor.main != nullptr;
  g_monitor.main = main_window;
  g_monitor.ctx = ctx;
  // Only restore the persisted position on the first start; later calls
  // (e.g. settings changed) keep the window where the user just dragged it.
  if (!alreadyRunning) {
    const int missing = (std::numeric_limits<int>::min)();
    const int savedX =
        search::load_module_int(CONFIG_SECTION, L"ReminderX", missing);
    const int savedY =
        search::load_module_int(CONFIG_SECTION, L"ReminderY", missing);
    if (savedX != missing && savedY != missing) {
      g_monitor.reminder_has_custom_position = true;
      g_monitor.reminder_x = savedX;
      g_monitor.reminder_y = savedY;
    }
  }
  std::string e;
  scheduled_check::ensure_store(e);
  updateReminder();
  triggerScan(true);
}
void stop_scheduled_result_check_monitor() {
  g_monitor.task.cancel();
  g_monitor.rescan_requested = false;
  g_monitor.full_rescan_requested = false;
  g_monitor.reminder_pressed_alert_id = 0;
  if (g_monitor.notification_icon_added) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_monitor.main;
    data.uID = REMINDER_ICON_ID;
    Shell_NotifyIconW(NIM_DELETE, &data);
    g_monitor.notification_icon_added = false;
  }
  if (g_monitor.reminder)
    DestroyWindow(g_monitor.reminder);
  if (g_monitor.reminder_font) {
    DeleteObject(g_monitor.reminder_font);
    g_monitor.reminder_font = nullptr;
  }
  g_monitor.unhandled.clear();
  g_monitor.main = nullptr;
  g_page = nullptr;
}
bool run_scheduled_result_check_now() { return triggerScan(true); }
bool run_scheduled_result_check_timer() { return triggerScan(false); }
void handle_scheduled_result_check_notification(LPARAM event_code) {
  if (event_code == WM_LBUTTONUP || event_code == WM_LBUTTONDBLCLK ||
      event_code == NIN_BALLOONUSERCLICK)
    openReminderCenter();
}

#endif
