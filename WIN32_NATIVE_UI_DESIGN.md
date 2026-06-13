# Win32 原生界面设计思路

本文档记录本项目在保持 Win32 原生控件前提下做界面优化时的经验，重点避免为了视觉效果过早进入 owner-draw、自绘控件或额外 UI 框架。

## 总原则

- 优先使用系统原生控件：`BUTTON`、`EDIT`、`COMBOBOX`、`STATIC`、`LISTVIEW`、`DATETIMEPICK_CLASSW`。
- 视觉分组可以由父窗口或容器窗口绘制，但可交互控件本身尽量保持原生行为。
- 不要把“看起来是白色背景”的 GDI 绘制区域等同于“控件父窗口背景就是白色”。
- 如果原生控件需要融入白色区域，优先把控件放进真实白色子窗口容器，而不是自绘控件本身。
- `WM_CTLCOLOR*` 适合处理标签、只读编辑框、简单背景刷；不要假设它能稳定覆盖所有启用视觉样式的控件外观。

## 复选框背景结论

Win32 原生复选框建议使用：

```cpp
CreateWindowExW(
    0,
    L"BUTTON",
    L"自动检查更新",
    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
    x, y, w, h,
    parent,
    win32_control_id(IDC_SET_UPDATE_AUTO_CHECK),
    GetModuleHandleW(nullptr),
    nullptr);
```

不要为了改背景色改成 `BS_OWNERDRAW`，也不要手动维护点击后的勾选状态。保存配置时继续用：

```cpp
SendMessageW(checkBox, BM_GETCHECK, 0, 0) == BST_CHECKED
```

## 为什么普通白色卡片上会出现灰底

系统设置页的卡片是父窗口用 GDI 画出来的白色圆角矩形。对 Win32 子控件来说，这个白色卡片并不是真实父窗口背景；复选框仍然挂在 MDI 子窗口上，而 MDI 子窗口背景是浅灰页面色。

因此，视觉样式启用时，原生复选框可能继续按父窗口或主题背景绘制，出现一块灰色区域。单靠 `WM_CTLCOLORBTN` 不一定能稳定消除这个灰底。

## Decoder 项目的参考做法

`/Users/zemise/Local/Code/001_Re/01_Decoder/Decoder` 中“写入数据库”复选框能正常显示，关键不是自绘，而是层级结构：

- 复选框是原生 `BUTTON + BS_AUTOCHECKBOX`。
- 控件挂在 `g_settings_content` 内容容器下，不是直接挂在外层设置窗口上。
- `g_settings_content` 的窗口过程在 `WM_ERASEBKGND` 中用 `COLOR_WINDOW` 擦白底。
- 该容器会转发 `WM_COMMAND / WM_NOTIFY / WM_CTLCOLORSTATIC`，让内部控件保持正常消息链路。

核心思路是：让复选框的真实父窗口背景就是白色，而不是把白色背景只画在外层父窗口上。

## 本项目推荐实现

当某个原生控件需要放在自绘白色卡片区域内，并且控件默认背景与卡片不一致时，优先创建一个白色 host 子窗口：

```cpp
LRESULT CALLBACK whiteHostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, GetSysColorBrush(COLOR_WINDOW));
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_COMMAND:
            if (HWND parent = GetParent(hwnd)) {
                return SendMessageW(parent, msg, wp, lp);
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
```

然后把复选框创建到该 host 下：

```cpp
HWND host = CreateWindowExW(
    0, WHITE_HOST_CLASS, L"",
    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
    x, y, w, h,
    parent, nullptr, GetModuleHandleW(nullptr), nullptr);

HWND check = CreateWindowExW(
    0, L"BUTTON", L"自动检查更新",
    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
    0, 0, w, h,
    host, win32_control_id(IDC_SET_UPDATE_AUTO_CHECK),
    GetModuleHandleW(nullptr), nullptr);
```

这样复选框仍是纯原生控件，背景问题由真实白色父容器解决。

## 什么时候可以父窗口自绘

适合父窗口自绘：

- 卡片背景、分组边框、分组标题装饰。
- 非交互的摘要栏、状态提示区。
- 滚动区域内的大面积装饰性分组，避免大量 `GROUPBOX` 子窗口造成残影。
- 顶部自定义工具栏这类项目自有 command bar，但仍应保留窗口、键盘、焦点和命令消息的 Win32 行为。

不适合父窗口自绘替代控件：

- 复选框、按钮、编辑框、下拉框等需要键盘焦点、Tab 顺序、可访问性和系统状态反馈的控件。
- 需要系统主题、输入法、选择状态、快捷键语义的控件。

## 自定义工具栏设计结论

项目顶部区域分为两层：

- 原生 `HMENU`：负责完整功能入口、Alt 助记键和系统菜单行为。
- 自定义工具栏 `MenuToolbar`：负责高频入口和当前 MDI 页面操作。

不要强行把业务入口图标化。`标本签收中心`、`常规报告`、`输血查询` 这类业务名本身比通用图标更准确；图标如果不够清晰，反而会增加理解成本和 DPI 维护成本。

当前工具栏推荐做法：

- 使用真实 Win32 子窗口承载工具栏，不替换系统菜单。
- 使用 GDI 双缓冲绘制背景、按钮底色、边框和文字，减少闪烁。
- 背景采用浅灰蓝，底部用 1px 分隔线区分 MDI 工作区。
- 业务入口使用文字按钮，hover/pressed/active 分别用浅蓝底色和细边框表达状态。
- 当前活动 MDI 页面要映射回工具栏入口，保持 active 高亮。
- `关闭当前` 属于当前页面操作，不是业务入口；使用右侧文字型次要按钮，不使用模糊图标。
- 没有活动 MDI 页面时，`关闭当前` 保持禁用态。
- 保留键盘焦点和方向键/Tab 导航，按钮点击继续向父窗口发送 `WM_COMMAND`。
- 鼠标 hover 不应触发键盘 focus 边框；键盘 focus 只在 Tab/方向键导航时显示，避免第一个按钮残留蓝色边框。

推荐色彩保持克制：

```text
工具栏背景：#F5F8FB
底部分隔线：#D6E0EA
业务按钮 Hover：#E8F2FF
业务按钮 Pressed：#D6E9FF
业务按钮 Active：#DDEEFF
文字：#1F2933
禁用文字：#9AA5B1
关闭按钮普通边框：#CDD6E0
关闭按钮 Hover 背景：#FFF1F2
关闭按钮 Hover 文字：#B91C1C
关闭按钮 Pressed 背景：#FFE4E6
```

实现上不要用按钮文案判断特殊样式；应使用显式 style flag，例如 `MTBS_CLOSE`，避免以后修改文案导致样式失效。

## 实施检查清单

新增或优化 Win32 原生页面时，按以下顺序检查：

1. 控件是否保持原生 class 和 style。
2. 控件真实父窗口背景是否与视觉区域一致。
3. 父窗口或 host 是否正确处理 `WM_ERASEBKGND`。
4. `WM_CTLCOLORSTATIC / WM_CTLCOLOREDIT / WM_CTLCOLORBTN` 是否只处理必要控件。
5. 是否避免了 `BS_OWNERDRAW` 和手动维护控件状态。
6. 保存逻辑是否仍读取原生控件状态，如 `BM_GETCHECK`、`GetWindowTextW`、`CB_GETCURSEL`。
7. DPI 缩放后 host 和内部控件是否一起移动、大小一致。
8. 自定义工具栏是否仍保留 `WM_COMMAND`、键盘焦点、禁用态和活动页状态同步。

## 当前设置页经验

系统设置页中的 `自动检查更新` 复选框应保持：

- 原生 `BUTTON + BS_AUTOCHECKBOX`。
- 由白色 host 子窗口承载。
- 不使用 `BS_OWNERDRAW`。
- 不使用手动点击切换状态。
- 保存时读取 `BM_GETCHECK`。

这与 Decoder 项目的“写入数据库”复选框思路一致：问题不在复选框控件本身，而在控件父背景是否真实匹配视觉区域。
