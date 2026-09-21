#include "scheduled_result_check_module.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "main_app.h"
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
#include <cstdlib>
#include <limits>
#include <shellapi.h>
#include <string>
#include <vector>
#include <windowsx.h>

namespace {

constexpr const wchar_t *TITLE = L"定时核查结果";
constexpr const wchar_t *CLASS_NAME = L"ScheduledResultCheckWindow";
constexpr const wchar_t *REMINDER_CLASS_NAME = L"ScheduledResultReminderWindow";
constexpr const wchar_t *CONFIG_SECTION = L"ScheduledResultCheck";
constexpr UINT REMINDER_ICON_ID = 0x5343;
constexpr int IDC_NAME = 8101, IDC_LEFT = 8102, IDC_OP = 8103, IDC_RIGHT = 8104,
              IDC_SAVE = 8105;
constexpr int IDC_DELETE = 8106, IDC_TOGGLE = 8107, IDC_SCAN = 8108,
              IDC_RULES = 8109, IDC_ALERTS = 8110;
constexpr int IDC_HANDLED = 8111, IDC_STATUS = 8112;
constexpr int IDC_NEW = 8113, IDC_RULE_SETTINGS = 8114, IDC_VALUE_MODE = 8115,
              IDC_VALUE = 8116;

struct ScanResult {
  bool ok = false;
  std::string error;
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
  HWND reminder = nullptr;
  bool reminder_expanded = true;
  bool notification_icon_added = false;
  std::vector<scheduled_check::Alert> unhandled;
  bool reminder_has_custom_position = false;
  int reminder_x = 0;
  int reminder_y = 0;
  bool reminder_dragging = false;
  bool reminder_drag_moved = false;
  POINT reminder_drag_cursor{};
  POINT reminder_drag_origin{};
} g_monitor;
HWND g_page = nullptr;

struct State {
  ModuleContext ctx;
  HWND nameLabel = nullptr, leftLabel = nullptr, opLabel = nullptr,
       rightLabel = nullptr, rulesTitle = nullptr, alertsTitle = nullptr;
  HWND name = nullptr, left = nullptr, op = nullptr, right = nullptr,
       newRule = nullptr, save = nullptr, del = nullptr, toggle = nullptr,
       scan = nullptr, ruleSettings = nullptr, valueMode = nullptr,
       value = nullptr;
  HWND rulesList = nullptr, alertsList = nullptr, handled = nullptr,
       status = nullptr;
  std::vector<search::ScheduledCheckItemOption> items;
  std::vector<scheduled_check::Rule> rules;
  std::vector<scheduled_check::Alert> alerts;
  app::WindowTask itemTask;
  bool rulesExpanded = false;
  bool valueModeChecked = false;
};

void refresh(State *st);
void updateReminder(bool emphasize = false);
void applyRuleEditorVisibility(State *st);

std::wstring w(const std::string &s) { return search::utf8_to_wide(s); }
std::wstring alertLine(const scheduled_check::Alert &a) {
  const std::wstring right = a.compare_with_value
                                 ? w(a.right_result_text)
                                 : w(a.right_item_name) + L" " +
                                       w(a.right_result_text);
  return w(a.left_item_name) + L" " + w(a.left_result_text) + L" " + w(a.op) +
         L" " + right;
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
  create_scheduled_result_check_module(g_monitor.ctx);
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
  if (!report || !PostMessageW(report, WM_REGULAR_OPEN_REPORT, 0,
                               reinterpret_cast<LPARAM>(target))) {
    delete target;
  }
}

int reminderHeaderHeight(HWND hwnd) {
  return static_cast<int>(48 * search::dpi_scale_factor(hwnd) + 0.5f);
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
  const int rowHeight = static_cast<int>(38 * scale + 0.5f);
  const int top =
      reminderHeaderHeight(hwnd) + static_cast<int>(8 * scale + 0.5f);
  RECT rc{};
  GetClientRect(hwnd, &rc);
  if (x < pad || x > rc.right - pad || y < top)
    return -1;
  const size_t count = (std::min)(g_monitor.unhandled.size(), size_t{3});
  const int index = (y - top) / rowHeight;
  if (index < 0 || index >= static_cast<int>(count))
    return -1;
  if (y >= top + (index + 1) * rowHeight)
    return -1;
  return index;
}

void positionReminder() {
  if (!g_monitor.reminder)
    return;
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  const float scale = search::dpi_scale_factor(g_monitor.reminder);
  const int width = static_cast<int>(420 * scale + 0.5f);
  const int height =
      static_cast<int>((g_monitor.reminder_expanded ? 210 : 62) * scale + 0.5f);
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
  const int width = static_cast<int>(420 * scale + 0.5f);
  const int height =
      static_cast<int>((g_monitor.reminder_expanded ? 210 : 62) * scale + 0.5f);
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
    RECT header = rc;
    header.bottom = headerHeight;
    HBRUSH headerBrush = CreateSolidBrush(RGB(216, 78, 55));
    FillRect(dc, &header, headerBrush);
    DeleteObject(headerBrush);
    HGDIOBJ oldFont = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
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
      SetTextColor(dc, RGB(55, 48, 42));
      const size_t count = (std::min)(g_monitor.unhandled.size(), size_t{3});
      int top = headerHeight + static_cast<int>(8 * scale);
      const int rowHeight = static_cast<int>(38 * scale);
      for (size_t i = 0; i < count; ++i) {
        const auto &alert = g_monitor.unhandled[i];
        const std::wstring line =
            w(alert.discovered_at.substr(11, 5)) + L"  " + alertLine(alert);
        RECT row{pad, top, rc.right - pad, top + rowHeight};
        DrawTextW(dc, line.c_str(), -1, &row,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        top += rowHeight;
      }
      SetTextColor(dc, RGB(130, 72, 42));
      RECT footer{pad, rc.bottom - static_cast<int>(32 * scale), rc.right - pad,
                  rc.bottom - static_cast<int>(5 * scale)};
      DrawTextW(dc, L"点击打开待处理列表", -1, &footer,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(dc, oldFont);
    HBRUSH border = CreateSolidBrush(RGB(216, 78, 55));
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
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
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

ScanResult executeScan(const ModuleContext &ctx) {
  ScanResult out;
  std::vector<scheduled_check::Rule> rules;
  if (!scheduled_check::load_rules(rules, out.error))
    return out;
  std::vector<std::string> codes;
  for (const auto &r : rules)
    if (r.enabled) {
      codes.push_back(r.left_item_code);
      codes.push_back(r.right_item_code);
    }
  std::sort(codes.begin(), codes.end());
  codes.erase(std::unique(codes.begin(), codes.end()), codes.end());
  codes.erase(std::remove_if(codes.begin(), codes.end(),
                             [](const std::string &code) { return code.empty(); }),
              codes.end());
  if (codes.empty()) {
    out.ok = true;
    return out;
  }
  const auto connection =
      search::wide_to_utf8(search::build_connection_string_w(ctx.dbSettings));
  if (connection.empty()) {
    out.error = "数据库连接未配置";
    return out;
  }
  std::vector<search::ScheduledCheckResultRow> source;
  if (!search::query_scheduled_check_results(connection, codes, source,
                                             out.error))
    return out;
  out.row_count = static_cast<int>(source.size());
  std::vector<scheduled_check::ResultRow> rows;
  rows.reserve(source.size());
  for (const auto &s : source) {
    scheduled_check::ResultRow r;
    r.entry_id = s.entry_id;
    r.rep_no = s.rep_no;
    r.oper_no = s.oper_no;
    r.room_code = s.room_code;
    r.mach_code = s.mach_code;
    r.mach_name = s.mach_name;
    r.inspect_date = s.inspect_date;
    r.item_code = s.item_code;
    r.item_name = s.item_name;
    r.result = s.result;
    rows.push_back(std::move(r));
  }
  auto matches = scheduled_check::evaluate(rules, rows, &out.skipped);
  out.match_count = static_cast<int>(matches.size());
  if (!scheduled_check::record_matches(rules, matches, out.fresh, out.error))
    return out;
  out.ok = true;
  return out;
}

bool triggerScan() {
  if (!g_monitor.main)
    return false;
  if (g_monitor.task.active()) {
    g_monitor.rescan_requested = true;
    return false;
  }
  const ModuleContext ctx = g_monitor.ctx;
  return g_monitor.task.start<ScanResult>(
      [ctx] { return executeScan(ctx); },
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
              status = L"扫描完成：读取 " + std::to_wstring(result->row_count) +
                       L" 行，命中 " + std::to_wstring(result->match_count) +
                       L" 条，新增 " + std::to_wstring(result->fresh.size()) +
                       L" 条，跳过非数值 " + std::to_wstring(result->skipped) +
                       L" 组。";
            }
            SetWindowTextW(st->status, status.c_str());
          }
        }
        const bool hasFresh = !error && result && result->ok &&
                              !result->fresh.empty() && g_monitor.main;
        updateReminder(hasFresh);
        if (hasFresh)
          showSystemNotification(result->fresh);
        if (g_monitor.rescan_requested && g_monitor.main) {
          g_monitor.rescan_requested = false;
          triggerScan();
        }
      });
}

void loadRules(State *st) {
  std::string e;
  if (!scheduled_check::load_rules(st->rules, e)) {
    SetWindowTextW(st->status, w(e).c_str());
    return;
  }
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
  }
}
void loadAlerts(State *st) {
  std::string e;
  if (!scheduled_check::load_review_alerts(st->alerts, e)) {
    SetWindowTextW(st->status, w(e).c_str());
    return;
  }
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
void refresh(State *st) {
  loadRules(st);
  loadAlerts(st);
  SetWindowTextW(st->status,
                 L"主程序运行期间每分钟自动扫描当日结果；LIS 数据库只读。");
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
      const std::wstring message = std::wstring(side) + L"未找到项目代码 “" +
                                   w(code) + L"”，请核对后重新输入。";
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
  int match = -1;
  for (size_t i = 0; i < st->items.size(); ++i) {
    const auto &item = st->items[i];
    if (item.item_code == key || item.item_name == key || item.item_eng == key)
      return static_cast<int>(i);
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
  SetWindowTextW(st->status, L"项目字典已加载，可新建核查规则。");
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
        st->items = std::move(result->first);
        fillItems(st);
      });
}

void saveRule(HWND hwnd, State *st) {
  const int l = resolveItem(st, st->left);
  const int o = comboSelection(st->op);
  if (l < 0 || o < 0 || l >= static_cast<int>(st->items.size())) {
    MessageBoxW(hwnd, L"请选择项目 A 和比较关系；项目 A 支持输入唯一的项目代码或完整名称。",
                TITLE, MB_ICONINFORMATION);
    return;
  }
  scheduled_check::Rule rule;
  const int sel = selected(st->rulesList);
  if (sel >= 0 && sel < static_cast<int>(st->rules.size()))
    rule = st->rules[sel];
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
      MessageBoxW(hwnd, L"请选择项目 B；也可以输入唯一的项目代码或完整名称。",
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
  if (!report || !PostMessageW(report, WM_REGULAR_OPEN_REPORT, 0,
                               reinterpret_cast<LPARAM>(target))) {
    delete target;
    MessageBoxW(st->rulesList, L"常规报告页面打开失败。", TITLE, MB_ICONERROR);
  }
}

void applyRuleEditorVisibility(State *st) {
  const int show = st->rulesExpanded ? SW_SHOW : SW_HIDE;
  for (HWND control :
       {st->nameLabel, st->leftLabel, st->opLabel, st->rightLabel, st->name,
        st->left, st->op, st->valueMode, st->newRule, st->save, st->del,
        st->toggle, st->rulesTitle, st->rulesList})
    ShowWindow(control, show);
  ShowWindow(st->right, show && !st->valueModeChecked ? SW_SHOW : SW_HIDE);
  ShowWindow(st->value, show && st->valueModeChecked ? SW_SHOW : SW_HIDE);
  SetWindowTextW(st->rightLabel,
                 st->valueModeChecked ? L"固定值：" : L"项目 B：");
  SetWindowTextW(st->ruleSettings,
                 st->rulesExpanded ? L"收起规则设置" : L"规则设置");
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
       search::measure_control_text_width(hwnd, st->leftLabel, 72, 6),
       search::measure_control_text_width(hwnd, st->opLabel, 72, 6),
       search::measure_control_text_width(hwnd, st->rightLabel, 72, 6)});
  const int controlH = S(26);
  const int titleH = S(22);
  const int statusH = S(22);
  const int contentW = (std::max)(S(300), w - pad * 2);
  int y = pad;

  if (!st->rulesExpanded) {
    const int settingsW =
        search::measure_control_text_width(hwnd, st->ruleSettings, 96, 18);
    const int scanW =
        search::measure_control_text_width(hwnd, st->scan, 86, 18);
    MoveWindow(st->ruleSettings, pad, y, settingsW, controlH, TRUE);
    MoveWindow(st->scan, pad + settingsW + gap, y, scanW, controlH, TRUE);
    y += controlH + S(10);
    MoveWindow(st->alertsTitle, pad, y + S(3), contentW - S(125), titleH, TRUE);
    MoveWindow(st->handled, w - pad - S(115), y, S(115), controlH, TRUE);
    y += controlH + S(5);
    const int statusY = (std::max)(y + S(100), h - pad - statusH);
    MoveWindow(st->alertsList, pad, y, contentW,
               (std::max)(S(80), statusY - y - S(5)), TRUE);
    MoveWindow(st->status, pad, statusY, contentW, statusH, TRUE);
    return;
  }

  // Use two filter rows instead of one long fixed-width row. This remains
  // readable with larger fonts, high DPI, and narrower MDI client areas.
  const int row1LeftW = (std::max)(S(220), contentW * 34 / 100);
  MoveWindow(st->nameLabel, pad, y + S(4), labelW, controlH, TRUE);
  MoveWindow(st->name, pad + labelW, y, row1LeftW - labelW - gap, controlH,
             TRUE);
  const int leftGroupX = pad + row1LeftW + gap;
  MoveWindow(st->leftLabel, leftGroupX, y + S(4), labelW, controlH, TRUE);
  MoveWindow(st->left, leftGroupX + labelW, y,
             (std::max)(S(160), w - pad - leftGroupX - labelW), S(320), TRUE);

  y += controlH + gap;
  const int opGroupW = (std::max)(S(190), contentW * 24 / 100);
  MoveWindow(st->opLabel, pad, y + S(4), labelW, controlH, TRUE);
  MoveWindow(st->op, pad + labelW, y, opGroupW - labelW - gap, S(200), TRUE);
  const int valueModeX = pad + opGroupW + gap;
  const int valueModeW =
      search::measure_control_text_width(hwnd, st->valueMode, 96, 18);
  MoveWindow(st->valueMode, valueModeX, y, valueModeW, controlH, TRUE);
  const int rightGroupX = valueModeX + valueModeW + gap;
  MoveWindow(st->rightLabel, rightGroupX, y + S(4), labelW, controlH, TRUE);
  const int rightW = (std::max)(S(120), w - pad - rightGroupX - labelW);
  MoveWindow(st->right, rightGroupX + labelW, y, rightW, S(320), TRUE);
  MoveWindow(st->value, rightGroupX + labelW, y, rightW, controlH, TRUE);

  y += controlH + gap;
  int buttonX = pad;
  struct ButtonLayout {
    HWND hwnd;
    int width;
  } buttons[] = {
      {st->ruleSettings,
       search::measure_control_text_width(hwnd, st->ruleSettings, 104, 18)},
      {st->newRule,
       search::measure_control_text_width(hwnd, st->newRule, 70, 18)},
      {st->save, search::measure_control_text_width(hwnd, st->save, 86, 18)},
      {st->del, search::measure_control_text_width(hwnd, st->del, 70, 18)},
      {st->toggle,
       search::measure_control_text_width(hwnd, st->toggle, 92, 18)},
      {st->scan, search::measure_control_text_width(hwnd, st->scan, 86, 18)}};
  for (const auto &button : buttons) {
    MoveWindow(button.hwnd, buttonX, y, button.width, controlH, TRUE);
    buttonX += button.width + gap;
  }

  y += controlH + S(10);
  MoveWindow(st->rulesTitle, pad, y, contentW, titleH, TRUE);
  y += titleH;

  const int statusY = (std::max)(y + S(180), h - pad - statusH);
  const int availableForLists = (std::max)(S(220), statusY - y - S(42));
  const int rulesH = (std::max)(S(110), availableForLists * 38 / 100);
  MoveWindow(st->rulesList, pad, y, contentW, rulesH, TRUE);

  y += rulesH + S(8);
  MoveWindow(st->alertsTitle, pad, y + S(3), contentW - S(125), titleH, TRUE);
  MoveWindow(st->handled, w - pad - S(105), y, S(105), controlH, TRUE);
  y += controlH + S(5);
  const int alertsH = (std::max)(S(90), statusY - y - S(5));
  MoveWindow(st->alertsList, pad, y, contentW, alertsH, TRUE);
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
                                                         LVS_EX_DOUBLEBUFFER);
    const wchar_t *rh[] = {L"状态", L"规则名称", L"项目 A", L"关系", L"项目 B"};
    int rw[] = {70, 180, 250, 60, 250};
    for (int i = 0; i < 5; ++i)
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
    search::apply_font_to_children(hwnd, st->ctx.uiFont);
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
      st->items.clear();
      startItemLoad(hwnd, st);
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
    switch (LOWORD(wp)) {
    case IDC_RULE_SETTINGS:
      st->rulesExpanded = !st->rulesExpanded;
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
        run_scheduled_result_check_now();
      }
      return 0;
    }
    case IDC_SCAN:
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
      if (n->idFrom == IDC_RULES && n->code == LVN_ITEMCHANGED)
        selectRule(st);
      if (n->idFrom == IDC_ALERTS && n->code == NM_DBLCLK)
        openAlert(st, selected(st->alertsList));
    }
    break;
  case WM_NCDESTROY:
    if (st) {
      st->itemTask.cancel();
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
  triggerScan();
}
void stop_scheduled_result_check_monitor() {
  g_monitor.task.cancel();
  g_monitor.rescan_requested = false;
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
  g_monitor.unhandled.clear();
  g_monitor.main = nullptr;
  g_page = nullptr;
}
bool run_scheduled_result_check_now() { return triggerScan(); }
void handle_scheduled_result_check_notification(LPARAM event_code) {
  if (event_code == WM_LBUTTONUP || event_code == WM_LBUTTONDBLCLK ||
      event_code == NIN_BALLOONUSERCLICK)
    openReminderCenter();
}

#endif
