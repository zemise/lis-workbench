# Changelog

## v2026.05.06

- **主程序壳**：新建 `main_app.exe`（Win32），全屏平台窗口。
- 菜单栏：检验管理 + 工具 + 系统（含关于对话框）。
- **MenuToolbar 组件**：自绘菜单风格工具栏，键盘导航、分隔线、禁用态、拉伸占位、图标支持。
- 状态栏：系统字体 + 百分比宽度，本机 IP + 实时时钟。
- `result_search.exe` 构建隔离；`scripts/build_main.ps1` 一键构建。
- `MAIN_APP_PLAN.md`：开发计划含模块接口、Qt 迁移预留。
- **MDI 客户区**：完整 MDI 子窗口容器，窗口菜单（层叠/平铺/排列/关闭）+ 子窗口默认最大化 + Ctrl+F6/F4 键盘导航。
- `DefFrameProcW` 替代 `DefWindowProcW`，`TranslateMDISysAccel` 处理 MDI 快捷键。
- 工具栏"关闭"按钮接入 MDI，关闭活动子窗口。
- 所有菜单项创建 MDI 占位子窗口（阶段 2 将替换为真实模块）。
- **系统设置**：`IDM_SETTINGS` 打开配置 MDI 子窗口（服务器/数据库/用户名/密码/字号），测试连接 + INI 持久化。单实例模式，重复点击激活已有窗口。
- **检验结果查询接入**：`search_child.cpp` — 独立 `SearchQueryChild` 类，每实例一个 `QueryState`，复用全部查询/趋势逻辑，分割器位置 INI 持久化。
- 版本号 v2026.05.06。

## v2026.05.05

- **趋势图**：纯 QPainter 手绘（参照 Win32 `trend_chart_renderer.cpp`），零图表依赖。
- 折线图 + 散点（白边实心圆按 normal 着色）+ 参考区间带（灰色填充 + 虚线边框）+ 坐标轴向外刻度线。
- 图例自定义 QPainter 绘制，完全匹配 Win32 布局与颜色。
- 布局全部基于 `QFontMetrics` 动态计算，无硬编码像素值，适配任意字体/DPI。
- Y 轴标签独立列 + 刻度数值列分隔，永不重叠。
- 高清导出 `grab()` → 3200×1800 PNG。
- 导出按钮无勾选时提示"请先勾选项目"，不会导出空文件。
- `--demo` 交互模式：mock 数据即时预览图表效果，无需数据库。
- `RESULTSEARCH_*` 环境变量自动注入数据库配置，免手填。
- 设置界面字体调整即时生效，按钮宽度自适应字号。
- 暂时停用导出图片按钮在非 Qwt 环境下的提示，待后续恢复。
- 移除 gnuplot、QCustomPlot、Qwt 三个图表库及所有相关构建脚本。
- 清理死代码：未用变量、死逻辑、冗余 include、`.gitignore` 补全。

## v2026.05.04

- CI 双绿通过、Qt 编译链路验证、UTF-8 编译选项、一键构建脚本。
- 设置对话框 + 主窗口三栏布局 + 查询流程 + 趋势窗口（四组件全部实现）。
- QSettings 使用绝对 exe 目录路径，与 Win32 版共享 `result_search.ini`。

## v2026.05.03

- 添加应用图标（16+32px），所有窗口标题栏/任务栏/Alt+Tab 统一显示。
- 程序编译为 Windows GUI 子系统，启动不再弹出终端窗口。
- 安装包（NSIS）嵌入应用图标。
- 增加 DPI 感知支持（PerMonitorV2），高 DPI 显示器下界面清晰不模糊。
- 所有布局坐标按 DPI 缩放，适配 100%/125%/150%/200% 等多种缩放比。
- 拖拽 splitter 时防闪烁优化（`DeferWindowPos` 批量移动 + `WS_CLIPCHILDREN` + `WM_SETREDRAW`）。
- 整理 `search_text.cpp` 非 Win32 fallback，标注 Qt 迁移路径。
- 编写 `QT_MIGRATION_GUIDE.md`，明确 Qt 5.15 迁移方案与文件分类。
- 更新 `PROJECT_STRUCTURE.md`，标注每个文件的 Qt 迁移状态。
- 提取 `search_core` 静态库（228KB），核心层与 Win32 界面层在 CMake 中分离。
- 拆分 `app_settings_io.*`：INI 持久化独立为 UI 层文件，核心层仅保留纯字符串 `build_connection_string_w`。
- `search_view_state` 改为 header-only 纯数据结构，IO 逻辑移至主入口。
- 修复 `LoadCursorW` 与 `ListView_SetItemText` 的 UNICODE 兼容问题。
- CMake 预留 `BUILD_QT_GUI` 选项，移除 `search_view_state.cpp`（已无实现体）。
- 版本号更新为 v2026.05.03。

## v2026.04.30.2

- 完成 `LS_AS_REPORT` / `LS_AS_REPENTRY` 双列表查询链路。
- 设置页支持数据库连接信息与字号持久化。
- `检验科室`、`检验仪器`、`病人类型`、`报告状态` 下拉联动完善。
- 主列表增加 `打印`、`自助机` 列并接入 `ZYMZ_PRINT`、`ZZJ_PRINT`。
- 主列表按审核/打印状态着色。
- 右侧明细按 `NORMAL` 着色，并去除 `偏` 列。
- 主界面支持拖动 splitter，自定义中间区和右侧区宽度，并持久化保存。
- 查询取消默认 500 条上限，不再自动 `TOP N` 截断。
- 主列表增加 `审核者` 列，确认关系为 `LS_AS_REPORT.REP_OPER = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID`。
- 增加 `search_text.*`，统一 `trim` 和 UTF-8 / 宽字符转换，删除多处重复实现。
- 增加 `search_ui_columns.h`，集中管理报告列表和项目明细列表列号。
- `search_ui_events.*` 改为通过 `MainUiIds` 注入控件 ID，事件层不再硬编码按钮和列表 ID。
- 新增趋势图第一阶段：
  - 底部增加 `趋势图` 按钮。
  - 新增 `trend_core.*` 趋势数据查询层。
  - 新增 `trend_window.*` 趋势窗口，支持右侧项目平铺列表、折线图和趋势明细表。
  - 新增 `trend_chart_renderer.*`，集中处理趋势图绘制和离屏重绘。
  - 趋势图使用上一次成功查询条件；上一次查询必须使用病人姓名或病人号。
  - 趋势数据改为后台线程加载，避免宽日期范围下打开趋势窗口时卡住主界面。
  - 趋势图横轴按有效结果点顺序等距绘制，日期和时间分两行显示。
  - 趋势图支持参考区间背景、异常点着色、图例和动态纵轴。
  - 右侧项目列表支持勾选，`导出勾选项目` 可导出趋势明细 CSV。
  - `导出勾选图片` 可选择文件夹，将勾选项目分别导出为 PNG 图片。
  - PNG 图片导出固定按 `1600x1000` 离屏重绘输出，不直接截取窗口。
