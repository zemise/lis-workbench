# cpp_search 项目结构

## 目录约定

- `src/`
  - Win32 界面 + 跨平台核心层源码
- `src_qt/`（新建，待实现）
  - Qt 5.15 界面层，与 Win32 共存
- `cmake/`
  - `toolchains/` — Windows 交叉编译工具链
- `scripts/`
  - `build_windows_package.sh` — Windows 便携包/安装包构建脚本
- `packaging/`
  - `ResultSearch.nsi` — NSIS 安装包脚本
  - `README_windows_installer.md` — 安装包构建说明
- `resource/`
  - `app.ico` — 应用图标（16+32px）
  - `app.rc` — Windows 资源脚本
  - `app.manifest` — DPI 感知 + Common Controls 清单
  - `resource.h` — 资源 ID 定义
- `build/` — 编译中间产物，不入版本管理
- `out/` — 打包输出，不入版本管理

## 文件层级 — 按迁移状态

### 核心层（新 UI 可直接复用）

| 文件 | 职责 | Qt 复用方式 |
|------|------|-----------|
| `search_core.*` | ODBC 数据库查询 | 切换到 Qt SQL 后端，接口不变 |
| `search_app.*` | `QueryInput` 结构、筛选器组装、状态文案映射 | 原样复用 |
| `search_controller.*` | 测试连接、加载字典、执行查询 | 原样复用（ODBC 切换后） |
| `search_text.*` | `trim`、UTF-8 ↔ 宽字符转换 | Qt 下替换为 `QString`，过渡期保持兼容 |
| `app_settings.*` | `result_search.ini` 读写、连接串生成 | 切换到 `QSettings`，接口保留 |
| `search_view_state.*` | `ViewState` 聚合运行时状态 | 原样复用 |
| `version.h` | 版本号与标题 | 原样复用 |
| `search_ui_columns.h` | 列号常量 | Qt 中列语义由 model 管理，参考此文件 |
| `trend_core.*` | 趋势数据查询 | 原样复用（ODBC 切换后） |

### 边界层（接口保留，实现替换）

| 文件 | Win32 实现 | Qt 替换 |
|------|-----------|---------|
| `search_input_view_model.*` | HWND 控件读写、下拉填充 | QLineEdit/QComboBox/QDateEdit 读写 |
| `search_ui_events.*` | Win32 消息分发 → 回调 | Qt signal/slot |
| `search_ui_presenter.*` | ListView 列定义与行填充 | QTableView + QStandardItemModel |
| `search_ui_layout.*` | 硬编码像素布局 + splitter | QLayout + QSplitter |
| `search_settings_dialog.*` | Win32 模式对话框 | QDialog |
| `trend_window.*` | GDI+ 图表 + ListView | QCustomPlot + QTableView |
| `trend_chart_renderer.*` | GDI+ 离屏位图 | QPainter + QCustomPlot |

### 入口层（Win32 独有，Qt 新建等价物）

| 文件 | 职责 |
|------|------|
| `main.cpp` | Win32 入口、消息循环、窗口过程、全局状态 |
| `search_ui_context.h` | Win32 句柄集合、字体上下文 |

## 代码分层约定

- 核心层禁止包含 `<windows.h>`、`HWND`、`HDC` 等 Win32 类型。
- 核心层只使用 C++17 标准库和 `<sqlext.h>`（ODBC 过渡期）。
- `QueryInput`（定义在 `search_app.h`）是界面层与核心层的唯一数据契约。
- 边界层可包含 Win32 类型，但接口上暴露的数据结构（如 `ViewState`）保持干净。
- 入口层自由使用任意 Win32 API。

## 当前成熟度

- Win32 版：主界面、设置窗口、趋势窗口均已完成功能。DPI 感知、图标、防闪烁到位。
- 核心库：`search_core` 静态库已提取，Win32 和 Qt 共享。
- Qt 编译链路已通：CI 双绿 + Windows 实机构建脚本（`scripts/build_qt.ps1`）。
- Qt 入口占位窗口已就绪（`src_qt/main.cpp`），待填充真正界面组件。
- 详见 [QT_MIGRATION_GUIDE.md](QT_MIGRATION_GUIDE.md)。

## 演进方向

- ~~提取核心层为 `search_core` 静态库~~ ✅
- ~~搭建 Qt 5.15 编译链路~~ ✅
- **当前**：逐个实现 Qt 界面组件（设置对话框 → 主窗口 → 趋势窗）
- **中期**：Qt 版本功能对齐 Win32 版
- **长期**：Qt 版本稳定后，Win32 入口降级为可选回退构建
