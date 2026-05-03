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
| `trend_window.*` | Win32 趋势窗口 + GDI+ 图表 | QMainWindow + QCustomPlot |
| `trend_chart_renderer.*` | GDI+ 离屏位图 | QCustomPlot 内置离屏导出 |

### 需新建

| 文件 | 职责 |
|------|------|
| `src_qt/main_window.cpp` | Qt 主窗口 — 继承 QMainWindow，组装菜单/工具栏/状态栏 |
| `src_qt/main.cpp` | Qt 入口 — QApplication + 主窗口创建，与 Win32 入口共存 |
| `src_qt/settings_dialog.cpp` | Qt 设置对话框 — 继承 QDialog，表单布局 |
| `src_qt/trend_window.cpp` | Qt 趋势窗口 — QMainWindow 嵌入 QCustomPlot |
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
- **CMake 已就绪**：`BUILD_QT_GUI` 选项已预留，核心库和 Win32 前端通过 `target_link_libraries(result_search PRIVATE search_core)` 连接。

## CMake 结构（当前）

```cmake
# 核心库 — 无 Qt 依赖，已实现
add_library(search_core STATIC
    src/search_core.cpp
    src/search_app.cpp
    src/search_controller.cpp
    src/search_text.cpp
    src/app_settings.cpp
    src/trend_core.cpp
)
target_include_directories(search_core PUBLIC src)
target_link_libraries(search_core PUBLIC odbc32)

# Win32 前端 — 当前默认构建
add_executable(result_search WIN32
    src/main.cpp
    src/app_settings_io.cpp        # Win32 INI 持久化
    src/search_input_view_model.cpp
    src/search_ui_events.cpp
    src/search_ui_presenter.cpp
    src/search_settings_dialog.cpp
    src/search_ui_layout.cpp
    src/trend_window.cpp
    src/trend_chart_renderer.cpp
    resource/app.rc
)
target_link_libraries(result_search PRIVATE search_core comctl32 comdlg32 gdiplus shell32 ole32)

# Qt 前端 — 待实现
# option(BUILD_QT_GUI "Build Qt 5.15 GUI" OFF)
# add_executable(result_search_qt WIN32 ...)
# target_link_libraries(result_search_qt PRIVATE search_core Qt5::Widgets)
```

Qt 编译时指定 Windows 7 兼容：
```cmake
set(CMAKE_CXX_STANDARD 17)
add_definitions(-DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601)
```

## 迁移顺序（建议）

1. ~~**提取 search_core 为静态库**~~ ✅ 已完成 — 验证 Win32 版仍正常编译运行
2. **搭建 Qt CMake + 空窗口** — 验证 Qt 5.15 交叉编译链路，链接 `search_core`
3. **实现设置对话框** — 最小验证：连接测试 + 保存配置（用 `QSettings` 替代 `app_settings_io`）
4. **实现主窗口布局** — QSplitter + 查询条件 + 报告列表 + 明细列表
5. **实现查询流程** — QTableView 填充 + 报告选择联动明细
6. **实现趋势窗口** — QCustomPlot 集成
7. **回归测试 + 并行维护** — 两套界面共用核心，直到 Qt 版稳定

## Win32 版保留策略

Win32 入口和界面文件保留在 `src/`，通过 CMake option 切换默认构建目标：

```cmake
option(BUILD_QT_GUI "Build Qt GUI instead of Win32" OFF)
```

迁移期间默认仍构建 Win32 版，Qt 版通过 `-DBUILD_QT_GUI=ON` 显式启用。

## 不引入的依赖

- Qt Charts（需要商业许可或 GPL）→ 用 QCustomPlot（MIT）
- Qt Quick / QML（学习曲线 + 不适合表单密集型应用）
- 任何 C++ 以外的语言或运行时
