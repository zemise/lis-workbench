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
constexpr int IDC_DELETE = 8106, IDC_TOGGLE = 8107, IDC_SCAN = 8108,
              IDC_RULES = 8109, IDC_ALERTS = 8110;
constexpr int IDC_HANDLED = 8111, IDC_STATUS = 8112;
constexpr int IDC_NEW = 8113, IDC_RULE_SETTINGS = 8114, IDC_VALUE_MODE = 8115,
              IDC_VALUE = 8116, IDC_MACHINE = 8117, IDC_ALERT_SEARCH = 8118,
              IDC_HANDLED_ALL = 8119, IDC_TABS = 8120, IDC_PROGRESS = 8121;

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

struct State {
  ModuleContext ctx;
  HWND nameLabel = nullptr, leftLabel = nullptr, opLabel = nullptr,
       rightLabel = nullptr, machineLabel = nullptr, rulesTitle = nullptr,
       alertsTitle = nullptr;
  HWND name = nullptr, left = nullptr, op = nullptr, right = nullptr,
       newRule = nullptr, save = nullptr, del = nullptr, toggle = nullptr,
       scan = nullptr, ruleSettings = nullptr, valueMode = nullptr,
       value = nullptr, machine = nullptr, machineButton = nullptr;
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
  std::string pendingLeftCode, pendingRightCode;
  std::wstring alertFilter;
  int alertsSortColumn = 0;
  bool alertsSortAscending = true;
  bool rulesExpanded = false;
  bool valueModeChecked = false;
  bool syncingRules = false;
};

void refresh(State *st);
void loadAlerts(State *st);
void updateReminder(bool emphasize = false);
void applyRuleEditorVisibility(State *st);

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
std::wstring alertLine(const scheduled_check::Alert &a) {
  const std::wstring right = a.compare_with_value
                                 ? w(a.right_result_text)
                                 : w(a.right_item_name) + L" " +
                                       w(a.right_result_text);
  return w(a.left_item_name) + L" " + w(a.left_result_text) + L" " + w(a.op) +
         L" " + right;
}
std::wstring reminderItemName(const std::string &english,
                              const std::string &chinese,
                              const std::string &code) {
  return w(scheduled_check::item_display_name(english, chinese, code));
}
std::wstring reminderAlertLine(const scheduled_check::Alert &a) {
  const std::wstring right =
      a.compare_with_value
          ? w(a.right_result_text)
          : reminderItemName(a.right_item_eng, a.right_item_name,
                             a.right_item_code) +
                L" " + w(a.right_result_text);
  return reminderItemName(a.left_item_eng, a.left_item_name,
                          a.left_item_code) +
         L" " + w(a.left_result_text) + L" " + w(a.op) + L" " + right;
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
    field(rule.room_code);
    field(rule.mach_code);
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
      codes.push_back(r.left_item_code);
      codes.push_back(r.right_item_code);
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
      machineCodes.push_back(rule.left_item_code);
      if (!rule.compare_with_value)
        machineCodes.push_back(rule.right_item_code);
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
    setItem(st->rulesList, i, 2,
            w(r.left_item_name + " (" + r.left_item_code + ")"));
    setItem(st->rulesList, i, 3, w(r.op));
    setItem(st->rulesList, i, 4,
            r.compare_with_value
                ? L"固定值 " + w(r.right_value_text)
                : w(r.right_item_name + " (" + r.right_item_code + ")"));
    setItem(st->rulesList, i, 5,
            r.mach_code.empty() ? L"未限定（旧规则）"
                                : w(r.mach_name + " [" + r.room_code + "/" +
                                    r.mach_code + "]"));
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
  case 5: return a.left_item_name;
  case 6: return a.left_result_text;
  case 7: return a.op;
  case 8: return a.compare_with_value ? "固定值" : a.right_item_name;
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
    setItem(st->alertsList, i, 5, w(a.left_item_name));
    setItem(st->alertsList, i, 6, w(a.left_result_text));
    setItem(st->alertsList, i, 7, w(a.op));
    setItem(st->alertsList, i, 8,
            a.compare_with_value ? L"固定值" : w(a.right_item_name));
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
                 L"仪器规则每分钟核查该仪器当日结果；旧规则沿用增量轮巡。LIS 数据库只读。");
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
void fillItems(State *st) {
  const int oldLeft = resolveItem(st, st->left);
  const int oldRight = resolveItem(st, st->right);
  const auto selectedCode = [&](const std::string &pending, int index) {
    if (!pending.empty()) return pending;
    return index >= 0 ? st->items[index].item_code : std::string{};
  };
  const std::string leftCode = selectedCode(st->pendingLeftCode, oldLeft);
  const std::string rightCode = selectedCode(st->pendingRightCode, oldRight);
  st->items.clear();
  if (st->machineItemsLoaded && st->dictionaryLoaded)
    for (const auto &item : st->allItems)
      if (st->machineItemCodes.count(item.item_code))
        st->items.push_back(item);
  for (HWND c : {st->left, st->right}) {
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    for (const auto &i : st->items) {
      std::wstring label = w(i.item_name + "  [" + i.item_code + "]");
      if (!i.item_eng.empty())
        label += L"  " + w(i.item_eng);
      if (!i.unit.empty())
        label += L"  " + w(i.unit);
      SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
  }
  EnableWindow(st->left, st->machineItemsLoaded && !st->items.empty());
  EnableWindow(st->right, st->machineItemsLoaded && !st->items.empty());
  EnableWindow(st->save, st->machineItemsLoaded && !st->items.empty());
  for (size_t i = 0; i < st->items.size(); ++i) {
    if (st->items[i].item_code == leftCode)
      SendMessageW(st->left, CB_SETCURSEL, i, 0);
    if (st->items[i].item_code == rightCode)
      SendMessageW(st->right, CB_SETCURSEL, i, 0);
  }
  if (st->machineItemsLoaded && st->dictionaryLoaded) {
    st->pendingLeftCode.clear();
    st->pendingRightCode.clear();
  }
  if (!st->machineItemsLoaded)
    SetWindowTextW(st->status, L"请先选择检验仪器，再选择该仪器的项目。");
  else if (!st->dictionaryLoaded)
    SetWindowTextW(st->status, L"项目字典尚未加载，暂不能选择项目。");
  else if (st->items.empty())
    SetWindowTextW(st->status, L"该仪器在 LS_AS_GROUP_ITEM 中没有可选项目，请核对 LIS 配置。");
  else {
    const std::wstring message = L"该仪器可选 " +
        std::to_wstring(st->items.size()) +
        L" 个项目；可输入项目代码精确匹配。";
    SetWindowTextW(st->status, message.c_str());
  }
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
  const int l = resolveItem(st, st->left);
  const int o = comboSelection(st->op);
  if (l < 0 || o < 0 || l >= static_cast<int>(st->items.size())) {
    MessageBoxW(hwnd, L"请选择该仪器的项目 A 和比较关系；项目代码须属于所选仪器。",
                TITLE, MB_ICONINFORMATION);
    return;
  }
  scheduled_check::Rule rule;
  const int sel = selected(st->rulesList);
  if (sel >= 0 && sel < static_cast<int>(st->rules.size()))
    rule = st->rules[sel];
  rule.room_code = st->roomCode;
  rule.mach_code = st->machCode;
  rule.mach_name = st->machName;
  const auto &li = st->items[l];
  rule.name = search::trim(windowText(st->name));
  static const char *ops[] = {">", ">=", "<", "<=", "=", "!="};
  rule.op = ops[o];
  rule.left_item_code = li.item_code;
  rule.left_item_name = li.item_name;
  rule.left_item_unit = li.unit;
  rule.compare_with_value = st->valueModeChecked;
  if (st->valueModeChecked) {
    const std::string valueText = search::trim(windowText(st->value));
    double threshold = 0.0;
    if (!scheduled_check::parse_number(valueText, threshold)) {
      MessageBoxW(hwnd, L"固定值必须是有效数字，例如 5.0、-1.5。", TITLE,
                  MB_ICONINFORMATION);
      return;
    }
    rule.right_value_text = valueText;
    rule.right_item_code.clear();
    rule.right_item_name.clear();
    rule.right_item_unit.clear();
    if (rule.name.empty())
      rule.name = li.item_name + " " + rule.op + " " + valueText;
  } else {
    const int r = resolveItem(st, st->right);
    if (r < 0 || r >= static_cast<int>(st->items.size())) {
      MessageBoxW(hwnd, L"请选择该仪器的项目 B；项目代码须属于所选仪器。",
                  TITLE, MB_ICONINFORMATION);
      return;
    }
    if (l == r) {
      MessageBoxW(hwnd, L"项目 A 和项目 B 不能相同。", TITLE,
                  MB_ICONINFORMATION);
      return;
    }
    const auto &ri = st->items[r];
    rule.right_item_code = ri.item_code;
    rule.right_item_name = ri.item_name;
    rule.right_item_unit = ri.unit;
    rule.right_value_text.clear();
    if (rule.name.empty())
      rule.name = li.item_name + " " + rule.op + " " + ri.item_name;
  }
  if (rule.id <= 0)
    rule.enabled = true;
  std::string e;
  if (!scheduled_check::save_rule(rule, e)) {
    MessageBoxW(hwnd, w(e).c_str(), TITLE, MB_ICONERROR);
    return;
  }
  refresh(st);
  updateReminder();
  run_scheduled_result_check_now();
}

void selectRule(State *st) {
  int row = selected(st->rulesList);
  if (row < 0 || row >= static_cast<int>(st->rules.size()))
    return;
  const auto &r = st->rules[row];
  SetWindowTextW(st->name, w(r.name).c_str());
  st->roomCode = r.room_code;
  st->machCode = r.mach_code;
  st->machName = r.mach_name;
  SetWindowTextW(st->machine,
                 r.mach_code.empty()
                     ? L"未限定（旧规则）"
                     : w(r.mach_name + " [" + r.room_code + "/" + r.mach_code + "]").c_str());
  st->pendingLeftCode.clear();
  st->pendingRightCode.clear();
  loadMachineItems(g_page, st);
  st->pendingLeftCode = r.left_item_code;
  st->pendingRightCode = r.right_item_code;
  auto pick = [&](HWND c, const std::string &code) {
    for (size_t i = 0; i < st->items.size(); ++i)
      if (st->items[i].item_code == code) {
        SendMessageW(c, CB_SETCURSEL, i, 0);
        break;
      }
  };
  pick(st->left, r.left_item_code);
  pick(st->right, r.right_item_code);
  static const char *ops[] = {">", ">=", "<", "<=", "=", "!="};
  for (int i = 0; i < 6; ++i)
    if (r.op == ops[i])
      SendMessageW(st->op, CB_SETCURSEL, i, 0);
  st->valueModeChecked = r.compare_with_value;
  SendMessageW(st->valueMode, BM_SETCHECK,
               st->valueModeChecked ? BST_CHECKED : BST_UNCHECKED, 0);
  SetWindowTextW(st->value, w(r.right_value_text).c_str());
  applyRuleEditorVisibility(st);
  SetWindowTextW(st->save, L"更新规则");
}
void clearEditor(State *st) {
  ListView_SetItemState(st->rulesList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
  SetWindowTextW(st->name, L"");
  st->roomCode.clear(); st->machCode.clear(); st->machName.clear();
  st->pendingLeftCode.clear(); st->pendingRightCode.clear();
  SetWindowTextW(st->machine, L"尚未选择检验仪器");
  loadMachineItems(g_page, st);
  SendMessageW(st->left, CB_SETCURSEL, -1, 0);
  SendMessageW(st->right, CB_SETCURSEL, -1, 0);
  SendMessageW(st->op, CB_SETCURSEL, 0, 0);
  st->valueModeChecked = false;
  SendMessageW(st->valueMode, BM_SETCHECK, BST_UNCHECKED, 0);
  SetWindowTextW(st->value, L"");
  applyRuleEditorVisibility(st);
  SetWindowTextW(st->save, L"保存规则");
}
void openAlert(State *st, int row) {
  if (row < 0 || row >= static_cast<int>(st->alerts.size()))
    return;
  const auto &a = st->alerts[row];
  std::vector<std::string> highlightCodes{a.left_item_code};
  if (!a.compare_with_value && !a.right_item_code.empty())
    highlightCodes.push_back(a.right_item_code);
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
  const int rulesShow = st->rulesExpanded ? SW_SHOW : SW_HIDE;
  const int alertsShow = st->rulesExpanded ? SW_HIDE : SW_SHOW;
  for (HWND control :
       {st->nameLabel, st->leftLabel, st->opLabel, st->rightLabel,
        st->machineLabel, st->machine, st->machineButton, st->name,
        st->left, st->op, st->valueMode, st->newRule, st->save, st->del,
        st->toggle, st->rulesTitle, st->rulesList})
    ShowWindow(control, rulesShow);
  ShowWindow(st->right,
             rulesShow && !st->valueModeChecked ? SW_SHOW : SW_HIDE);
  ShowWindow(st->value,
             rulesShow && st->valueModeChecked ? SW_SHOW : SW_HIDE);
  for (HWND control :
       {st->alertSearch, st->handled, st->handledAll, st->alertsTitle,
        st->alertsList})
    ShowWindow(control, alertsShow);
  // The legacy expand/collapse button is replaced by the tab control.
  ShowWindow(st->ruleSettings, SW_HIDE);
  SetWindowTextW(st->rightLabel,
                 st->valueModeChecked ? L"固定值：" : L"项目 B：");
}

void layout(HWND hwnd, State *st) {
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
  const int labelW = (std::max)(
      {search::measure_control_text_width(hwnd, st->nameLabel, 72, 6),
       search::measure_control_text_width(hwnd, st->machineLabel, 72, 6),
       search::measure_control_text_width(hwnd, st->leftLabel, 72, 6),
       search::measure_control_text_width(hwnd, st->opLabel, 72, 6),
       search::measure_control_text_width(hwnd, st->rightLabel, 72, 6)});
  const int controlH = S(26);
  const int titleH = S(22);
  const int statusH = S(22);
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

  // Wide pages use two columns; narrow pages keep one task per row so labels
  // and editable project fields never compete for the same horizontal space.
  const int pickerW = search::measure_control_text_width(
      hwnd, st->machineButton, 96, 18);
  const int valueModeW =
      search::measure_control_text_width(hwnd, st->valueMode, 96, 18);
  if (contentW >= S(900)) {
    const int half = (contentW - gap) / 2;
    const int rightX = pad + half + gap;
    MoveWindow(st->machineLabel, pad, y + S(4), labelW, controlH, TRUE);
    MoveWindow(st->machine, pad + labelW, y,
               half - labelW - pickerW - gap, controlH, TRUE);
    MoveWindow(st->machineButton, pad + half - pickerW, y,
               pickerW, controlH, TRUE);
    MoveWindow(st->nameLabel, rightX, y + S(4), labelW, controlH, TRUE);
    MoveWindow(st->name, rightX + labelW, y,
               contentW - half - gap - labelW, controlH, TRUE);
    y += controlH + gap;
    MoveWindow(st->leftLabel, pad, y + S(4), labelW, controlH, TRUE);
    MoveWindow(st->left, pad + labelW, y, half - labelW, S(320), TRUE);
    MoveWindow(st->rightLabel, rightX, y + S(4), labelW, controlH, TRUE);
    MoveWindow(st->right, rightX + labelW, y,
               contentW - half - gap - labelW, S(320), TRUE);
    MoveWindow(st->value, rightX + labelW, y,
               contentW - half - gap - labelW, controlH, TRUE);
    y += controlH + gap;
    MoveWindow(st->opLabel, pad, y + S(4), labelW, controlH, TRUE);
    MoveWindow(st->op, pad + labelW, y, S(110), S(200), TRUE);
    MoveWindow(st->valueMode, pad + labelW + S(110) + gap, y,
               valueModeW, controlH, TRUE);
    y += controlH + gap;
  } else {
    MoveWindow(st->machineLabel, pad, y + S(4), labelW, controlH, TRUE);
    MoveWindow(st->machine, pad + labelW, y,
               (std::max)(S(80), contentW - labelW - pickerW - gap), controlH,
               TRUE);
    MoveWindow(st->machineButton, w - pad - pickerW, y,
               pickerW, controlH, TRUE);
    y += controlH + gap;
    MoveWindow(st->nameLabel, pad, y + S(4), labelW, controlH, TRUE);
    MoveWindow(st->name, pad + labelW, y, contentW - labelW, controlH, TRUE);
    y += controlH + gap;
    MoveWindow(st->leftLabel, pad, y + S(4), labelW, controlH, TRUE);
    MoveWindow(st->left, pad + labelW, y, contentW - labelW, S(320), TRUE);
    y += controlH + gap;
    MoveWindow(st->opLabel, pad, y + S(4), labelW, controlH, TRUE);
    MoveWindow(st->op, pad + labelW, y, S(110), S(200), TRUE);
    MoveWindow(st->valueMode, pad + labelW + S(110) + gap, y,
               valueModeW, controlH, TRUE);
    y += controlH + gap;
    MoveWindow(st->rightLabel, pad, y + S(4), labelW, controlH, TRUE);
    MoveWindow(st->right, pad + labelW, y, contentW - labelW, S(320), TRUE);
    MoveWindow(st->value, pad + labelW, y, contentW - labelW, controlH, TRUE);
    y += controlH + gap;
  }
  int buttonX = pad;
  struct ButtonLayout {
    HWND hwnd;
    int width;
  } buttons[] = {
      {st->newRule,
       search::measure_control_text_width(hwnd, st->newRule, 70, 18)},
      {st->save, search::measure_control_text_width(hwnd, st->save, 86, 18)},
      {st->del, search::measure_control_text_width(hwnd, st->del, 70, 18)},
      {st->toggle,
       search::measure_control_text_width(hwnd, st->toggle, 92, 18)},
      {st->scan, search::measure_control_text_width(hwnd, st->scan, 86, 18)}};
  for (const auto &button : buttons) {
    if (buttonX > pad && buttonX + button.width > w - pad) {
      buttonX = pad;
      y += controlH + gap;
    }
    MoveWindow(button.hwnd, buttonX, y, button.width, controlH, TRUE);
    buttonX += button.width + gap;
  }

  y += controlH + S(10);
  MoveWindow(st->rulesTitle, pad, y, contentW, titleH, TRUE);
  y += titleH;

  const int rulesH = (std::max)(S(110), statusY - y - S(5));
  MoveWindow(st->rulesList, pad, y, contentW, rulesH, TRUE);
  MoveWindow(st->status, pad, statusY, contentW, statusH, TRUE);
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
    st->nameLabel = search::create_label(hwnd, L"规则名称：", 0, 0, 0, 0);
    st->machineLabel = search::create_label(hwnd, L"检验仪器：", 0, 0, 0, 0);
    st->machine = search::create_label(hwnd, L"尚未选择检验仪器", 0, 0, 0, 0);
    st->machineButton = search::create_button(
        hwnd, IDC_MACHINE, L"选择仪器", 0, 0, 0, 0);
    st->leftLabel = search::create_label(hwnd, L"项目 A：", 0, 0, 0, 0);
    st->opLabel = search::create_label(hwnd, L"比较关系：", 0, 0, 0, 0);
    st->rightLabel = search::create_label(hwnd, L"项目 B：", 0, 0, 0, 0);
    st->name = search::create_edit(hwnd, IDC_NAME, 0, 0, 0, 0);
    SendMessageW(st->name, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"规则名称（可选）"));
    st->left = search::create_combo(hwnd, IDC_LEFT, 0, 0, 0, 0, true);
    st->op = search::create_combo(hwnd, IDC_OP, 0, 0, 0, 0, false);
    for (const wchar_t *x : {L">", L">=", L"<", L"<=", L"=", L"!="})
      SendMessageW(st->op, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(x));
    SendMessageW(st->op, CB_SETCURSEL, 0, 0);
    st->right = search::create_combo(hwnd, IDC_RIGHT, 0, 0, 0, 0, true);
    st->valueMode = CreateWindowExW(
        0, L"BUTTON", L"与固定值比较",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 0, 0, 0, 0, hwnd,
        win32_control_id(IDC_VALUE_MODE), st->ctx.instance, nullptr);
    st->value = search::create_edit(hwnd, IDC_VALUE, 0, 0, 0, 0);
    SendMessageW(st->value, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"如 5.0"));
    st->ruleSettings =
        search::create_button(hwnd, IDC_RULE_SETTINGS, L"规则设置", 0, 0, 0, 0);
    st->newRule = search::create_button(hwnd, IDC_NEW, L"新建", 0, 0, 0, 0);
    st->save = search::create_button(hwnd, IDC_SAVE, L"保存规则", 0, 0, 0, 0);
    st->del = search::create_button(hwnd, IDC_DELETE, L"删除", 0, 0, 0, 0);
    st->toggle =
        search::create_button(hwnd, IDC_TOGGLE, L"启用/停用", 0, 0, 0, 0);
    st->scan = search::create_button(hwnd, IDC_SCAN, L"立即扫描", 0, 0, 0, 0);
    st->rulesTitle = search::create_label(hwnd, L"核查规则", 0, 0, 0, 0);
    st->rulesList = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL, 0, 0, 0, 0, hwnd,
        win32_control_id(IDC_RULES), st->ctx.instance, nullptr);
    ListView_SetExtendedListViewStyle(st->rulesList, LVS_EX_FULLROWSELECT |
                                                         LVS_EX_GRIDLINES |
                                                         LVS_EX_DOUBLEBUFFER |
                                                         LVS_EX_CHECKBOXES);
    const wchar_t *rh[] = {L"状态", L"规则名称", L"项目 A", L"关系", L"项目 B", L"检验仪器"};
    int rw[] = {70, 180, 250, 60, 250, 180};
    for (int i = 0; i < 6; ++i)
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
                           L"项目 B",   L"B 结果"};
    int aw[] = {145, 70, 160, 95, 95, 180, 80, 55, 180, 80};
    for (int i = 0; i < 10; ++i)
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
      st->allItems.clear();
      st->dictionaryLoaded = false;
      st->items.clear();
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
      layout(hwnd, st);
    }
    return 0;
  case WM_COMMAND:
    if (!st)
      break;
    if ((LOWORD(wp) == IDC_LEFT || LOWORD(wp) == IDC_RIGHT) &&
        (HIWORD(wp) == CBN_EDITCHANGE || HIWORD(wp) == CBN_KILLFOCUS)) {
      autoMatchItemCode(st, reinterpret_cast<HWND>(lp),
                        LOWORD(wp) == IDC_LEFT ? L"项目 A" : L"项目 B",
                        HIWORD(wp) == CBN_KILLFOCUS);
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
    case IDC_RULE_SETTINGS:
      st->rulesExpanded = !st->rulesExpanded;
      if (st->tabs)
        TabCtrl_SetCurSel(st->tabs, st->rulesExpanded ? 1 : 0);
      applyRuleEditorVisibility(st);
      layout(hwnd, st);
      return 0;
    case IDC_VALUE_MODE:
      st->valueModeChecked =
          SendMessageW(st->valueMode, BM_GETCHECK, 0, 0) == BST_CHECKED;
      applyRuleEditorVisibility(st);
      layout(hwnd, st);
      return 0;
    case IDC_NEW:
      clearEditor(st);
      return 0;
    case IDC_MACHINE: {
      search::MachinePickerPopupOptions options;
      options.owner = hwnd;
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
        const bool sameMachine = state->roomCode == machine.room_code &&
                                 state->machCode == machine.mach_code;
        const int leftIndex = sameMachine ? resolveItem(state, state->left) : -1;
        const int rightIndex = sameMachine ? resolveItem(state, state->right) : -1;
        state->pendingLeftCode = leftIndex >= 0
                                     ? state->items[leftIndex].item_code : "";
        state->pendingRightCode = rightIndex >= 0
                                      ? state->items[rightIndex].item_code : "";
        state->roomCode = machine.room_code;
        state->machCode = machine.mach_code;
        state->machName = machine.mach_name;
        SetWindowTextW(state->machine,
                       w(machine.mach_name + " [" + machine.room_code + "/" +
                         machine.mach_code + "]").c_str());
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
        clearEditor(st);
        refresh(st);
        updateReminder();
      }
      return 0;
    }
    case IDC_TOGGLE: {
      int x = selected(st->rulesList);
      if (x >= 0 && x < static_cast<int>(st->rules.size())) {
        std::string e;
        if (!scheduled_check::set_rule_enabled(st->rules[x].id,
                                               !st->rules[x].enabled, e)) {
          MessageBoxW(hwnd, w(e).c_str(), TITLE, MB_ICONERROR);
          return 0;
        }
        refresh(st);
        updateReminder();
        run_scheduled_result_check_now();
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
        selectRule(st);
      }
      if (n->idFrom == IDC_ALERTS && n->code == NM_DBLCLK)
        openAlert(st, selected(st->alertsList));
    }
    break;
  case WM_NCDESTROY:
    if (st) {
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
