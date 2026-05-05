# Qt 5.15 迁移指南

## 目标

将 Win32 界面层替换为 Qt 5.15 Widgets，C++ 核心层原样复用。

约束：最低兼容 Windows 7 SP1，Qt 使用 LGPL 许可。

## 文件职责分类

### 可直接复用（零修改）

| 文件 | 职责 |
|------|------|
| `search_core.*` | 数据库查询层 — ODBC 连接、SQL 执行、结果行映射 |
| `search_app.*` | 应用规则层 — `QueryInput` 结构、筛选器组装、状态文案映射 |
| `search_controller.*` | 控制层 — 测试连接、加载字典、执行查询 |
| `search_text.*` | 文本工具 — `trim`、UTF-8 ↔ 宽字符转换 |
| `app_settings.*` | 配置读写 — `result_search.ini`、连接串生成 |
| `search_view_state.h` | 状态聚合 — `ViewState` 纯数据结构，IO 逻辑已迁至 UI 层 |
| `version.h` | 版本号与标题常量 |
| `search_ui_columns.h` | 列号常量 — Qt 中列号由 model 管理，此文件仅保留语义参考 |
| `trend_core.*` | 趋势数据查询 — 独立于 UI 层 |

### 需适配替换（接口保留，实现改为 Qt widget）

| 文件 | Win32 实现 | Qt 替换 |
|------|-----------|---------|
| `search_input_view_model.*` | HWND 控件读写、下拉填充 | QLineEdit / QComboBox 读写，下拉列表填充逻辑可复用 |
| `search_ui_events.*` | Win32 消息分发 → 回调 | Qt signal/slot 连接 |
| `search_ui_presenter.*` | ListView 列定义与行填充 | QTableView + QStandardItemModel |
| `search_ui_layout.*` | 硬编码像素布局 + splitter | Qt Layout 系统 + QSplitter |
| `search_settings_dialog.*` | Win32 模式对话框 | QDialog |
| `app_settings_io.*` | Win32 INI 读写 | QSettings |
| `trend_window.*` | Win32 趋势窗口 + GDI+ 图表 | QDialog + QwtPlot |
| `trend_chart_renderer.*` | GDI+ 离屏位图 | QwtPlotRenderer → PNG/PDF |

### 需新建

| 文件 | 职责 |
|------|------|
| `src_qt/main_window.cpp` | Qt 主窗口 — 继承 QMainWindow，组装菜单/工具栏/状态栏 |
| `src_qt/main.cpp` | Qt 入口 — QApplication + 主窗口创建，与 Win32 入口共存 |
| `src_qt/settings_dialog.cpp` | Qt 设置对话框 — 继承 QDialog，表单布局 |
| `src_qt/trend_window.cpp` | Qt 趋势窗口 — QDialog 嵌入 QwtPlot |
| `src_qt/input_view_model.cpp` | Qt 输入读取 — QLineEdit/QComboBox/QDateEdit → QueryInput |

## 关键边界：QueryInput

`QueryInput` 结构体（定义在 `search_app.h`）是界面层与核心层的唯一数据契约。Win32 版通过 `build_query_input(MainUiHandles, ViewState)` 从控件句柄组装，Qt 版仅需实现对应函数从 Qt widget 中读取字段值填入同一个 `QueryInput`。

```cpp
// Qt 版本接口（在 src_qt/ 中实现）
QueryInput build_query_input_qt(QLineEdit* patient_id, QComboBox* room, ...);
```

## 当前已完成准备

- **核心库提取**：`search_core` 静态库已从单体外分离，包含 7 个核心文件。
- **持久化分离**：`app_settings_io.*` 独立承担 INI 读写，核心层只保留 `build_connection_string_w`（纯字符串操作）。
- **ViewState 纯数据化**：`search_view_state.h` 仅含 `ViewState` 结构体，IO 逻辑移至 UI 层。
- **Qt 编译链路**：空窗口已跑通 — macOS 交叉编译 Win32 ✅ / Windows VS 2026 + Qt 5.15 ✅ / CI 双绿 ✅
- **构建脚本**：`scripts/build_qt.ps1` 一键配置/编译/部署/运行
- **MSVC UTF-8**：`/utf-8` 标志已添加，源文件中文字符正常编译

## Visual Studio 版本

| VS 版本 | CMake Generator | CI / 实机 |
|---------|----------------|-----------|
| VS 2022 (17.x) | `Visual Studio 17 2022` | CI (`windows-2022`) |
| VS 2026 (18.x) | `Visual Studio 18 2026` | 实机开发 |

构建脚本 (`scripts/build_qt.ps1`) 会自动检测可用 VS 版本。

## 构建命令（Windows 实机）

```powershell
.\scripts\build_qt.ps1           # 构建 + 部署 Qt DLL
.\scripts\build_qt.ps1 -Run      # 构建 + 运行
.\scripts\build_qt.ps1 -Clean -Run  # 全新构建 + 运行
```

## 迁移顺序（建议）

1. ~~**提取 search_core 为静态库**~~ ✅ 已完成
2. ~~**搭建 Qt CMake + 空窗口**~~ ✅ 已完成 — 编译链路双绿
3. **实现设置对话框** — 最小验证：连接测试 + 保存配置（用 `QSettings` 替代 `app_settings_io`）
4. **实现主窗口布局** — QSplitter + 查询条件 + 报告列表 + 明细列表
5. **实现查询流程** — QTableView 填充 + 报告选择联动明细
6. **实现趋势窗口** — QwtPlot 集成
7. **回归测试 + 并行维护** — 两套界面共用核心，直到 Qt 版稳定

## Win32 版保留策略

Win32 入口和界面文件保留在 `src/`，通过 CMake option 切换默认构建目标：

```cmake
option(BUILD_QT_GUI "Build Qt GUI instead of Win32" OFF)
```

迁移期间默认仍构建 Win32 版，Qt 版通过 `-DBUILD_QT_GUI=ON` 显式启用。

## 不引入的依赖

- Qt Charts（需要商业许可或 GPL）→ 用 Qwt（LGPL）
- Qt Quick / QML（学习曲线 + 不适合表单密集型应用）
- 任何 C++ 以外的语言或运行时
