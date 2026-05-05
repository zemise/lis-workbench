# 主程序开发计划

## 目标

将当前 `检验结果查询` 从独立程序升级为主程序平台下的一个功能模块。主程序采用 Win32 原生界面，作为未来所有检验相关功能的统一入口。

## 主界面布局

```
┌─────────────────────────────────────────────────────────┐
│  检验管理    系统                              主程序标题 │  ← 菜单栏
├─────────────────────────────────────────────────────────┤
│                                                         │
│                    MDI 客户区                            │  ← 子窗口区域
│              （各功能模块作为子窗口打开）                   │
│                                                         │
│                                                         │
│                                                         │
├─────────────────────────────────────────────────────────┤
│  状态栏                                                 │
└─────────────────────────────────────────────────────────┘
```

- 默认打开全屏（`SW_MAXIMIZE`）
- 顶部菜单栏
- 中间为 MDI 客户区，各功能模块作为子窗口嵌入
- 底部状态栏

## 菜单结构

```
检验结果查询平台
├── 检验管理
│   ├── 检验结果查询    → 打开当前 cpp_search 窗口
│   └── 输血结果查询    → 预留（待开发）
│
└── 系统
    └── 参数设置         → 打开数据库/系统配置窗口
```

## 开发阶段

### 阶段 1：主窗口壳（Win32）

| 任务 | 说明 |
|------|------|
| 1.1 创建主窗口 | `main_frame.cpp` — 注册窗口类，全屏显示 |
| 1.2 菜单栏 | `main_menu.cpp` — 创建菜单资源，"检验管理"/"系统" |
| 1.3 MDI 客户区 | `main_mdi.cpp` — 创建 MDI 客户窗口，子窗口容器 |
| 1.4 状态栏 | `main_statusbar.cpp` — 底部状态栏 |
| 1.5 图标+标题 | 复用现有 `resource/app.ico`，标题 "检验结果查询平台" |

### 阶段 2：模块接入

| 任务 | 说明 |
|------|------|
| 2.1 检验结果查询接入 | 将现有 `main.cpp` 的窗口创建改为 MDI 子窗口 |
| 2.2 输血结果查询占位 | 空子窗口，显示"输血结果查询 — 待开发" |
| 2.3 参数设置接入 | 复用现有 `search_settings_dialog` 或创建系统配置页 |

### 阶段 3：统一配置

| 任务 | 说明 |
|------|------|
| 3.1 统一 INI | 所有模块共享同一个 `result_search.ini` 配置 |
| 3.2 统一数据库连接 | 主窗口持有 `DbSettings`，各模块共用 |
| 3.3 关于对话框 | 版本号、作者信息 |

### 阶段 4：Qt 主程序（后期）

主程序先以 Win32 实现，但架构上为 Qt 迁移预留接口。阶段 2 完成后，`feat/main-app-qt` 分支将：

| 任务 | 说明 |
|------|------|
| 4.1 Qt 主窗口 | `QMainWindow` + `QMenuBar` + `QMdiArea`，平移 Win32 菜单结构 |
| 4.2 Qt 模块接入 | 现有 `src_qt/` 下组件（MainWindow、TrendWindow）接入 QMdiArea |
| 4.3 Win32 共存 | CMake `BUILD_MAIN_APP` 选项切换 Win32/Qt 主程序入口 |
| 4.4 Qt 版 CI | 在现有 `build-qt` job 中增加主程序构建目标 |

### Qt 架构预留设计

当前 Win32 主程序的关键抽象，为 Qt 迁移做准备：

```
                    ┌─────────────────┐
                    │  search_core    │  ← 数据库层（双版共用）
                    └────────┬────────┘
                             │
              ┌──────────────┴──────────────┐
              │                             │
    ┌─────────┴─────────┐       ┌──────────┴──────────┐
    │  Win32 主窗口      │       │  Qt 主窗口（后期）    │
    │  main_frame.cpp    │       │  src_qt/main_app.cpp │
    │  main_menu.cpp     │       │  QMenuBar            │
    │  main_mdi.cpp      │       │  QMdiArea            │
    └─────────┬─────────┘       └──────────┬──────────┘
              │                             │
    ┌─────────┴─────────┐       ┌──────────┴──────────┐
    │  子模块（Win32）    │       │  子模块（Qt）        │
    │  main.cpp (查询)   │       │  main_window.cpp    │
    │  blood_query.cpp   │       │  blood_window.cpp   │
    └───────────────────┘       └─────────────────────┘
```

- 主窗口与子模块之间通过函数调用（Win32）或 signal/slot（Qt）通信
- 数据库连接由主窗口持有，通过 `DbSettings` 结构体传递给子模块
- 子模块不依赖主窗口的具体实现（Win32 或 Qt），只接收数据结构和回调

## 文件结构（计划）

```
src/
  main_frame.cpp          ← 新建：主窗口入口、消息循环
  main_menu.cpp           ← 新建：菜单创建与命令分发
  main_mdi.cpp            ← 新建：MDI 客户区管理
  main_statusbar.cpp      ← 新建：状态栏
  main_app.h              ← 新建：主程序上下文（共享状态）

  main.cpp                ← 修改：提取窗口创建逻辑，改为子窗口模式
  search_ui_layout.cpp    ← 现有：查询界面控件布局（不变）
  search_core.cpp         ← 现有：数据库查询（不变）
  ...                     ← 其余文件不变
```

## 当前现状与兼容

| 项目 | 状态 |
|------|------|
| Win32 检验结果查询 | 完整，独立运行 |
| Qt 检验结果查询 | 完整，独立运行 |
| 核心数据库层 | `search_core` 静态库，双版共用 |
| CI 构建 | Win32 + Qt 双绿 |

主程序开发期间：
- Win32 版查询仍可独立编译运行（`result_search.exe`）
- Qt 版不受影响
- 新增主程序入口编译为独立 exe（`main_app.exe`），链接 `search_core`

## 技术选型

| 维度 | 选择 | 理由 |
|------|------|------|
| 主窗口框架 | Win32 原生 | 与现有查询界面一致，零额外依赖 |
| 子窗口模式 | MDI | 多文档接口，各模块作为独立子窗口 |
| 配置共享 | `result_search.ini` | 延用现有格式 |
| 数据库连接 | 主窗口持有 `DbSettings` | 避免各模块重复配置 |

## 设计原则

### 模块接口标准化

每个功能模块对外暴露统一接口，主窗口不关心模块内部实现：

```cpp
// main_module.h — 所有模块遵循此约定
struct ModuleDef {
    const wchar_t* menuParent;   // 所属菜单，如 L"检验管理"
    const wchar_t* menuLabel;    // 菜单项文字，如 L"检验结果查询"
    HWND (*create)(HINSTANCE, HWND parent, const DbSettings&, HFONT);
    void (*destroy)(HWND);
};

// 各模块注册示例
ModuleDef g_queryModule = {
    L"检验管理", L"检验结果查询(&Q)...",
    create_query_window, destroy_query_window
};
```

新增模块只需编写 `ModuleDef` + 窗口过程，主窗口代码零修改。

### 构建隔离

```
CMake 目标：
  result_search.exe       ← 独立查询工具（现有，不受影响）
  main_app.exe            ← 主程序（新）
    ├── main_frame.cpp    ← 主窗口
    ├── main_menu.cpp     ← 菜单系统
    ├── main_mdi.cpp      ← MDI 管理
    ├── main.cpp          ← 查询模块（从现有改造）
    └── blood_query.cpp   ← 输血模块（新）
```

- `result_search.exe` 的 CMake 目标和源码不变
- 主程序作为独立的 `add_executable(main_app WIN32 ...)`
- 两个目标链接同一个 `search_core` 静态库
- 新增模块只改主程序的 CMake 文件，不影响查询工具

### 共享状态

```cpp
// main_app.h — 主程序全局上下文
struct AppContext {
    HINSTANCE instance;
    HWND mainWindow;
    HWND mdiClient;
    HFONT uiFont;
    search::DbSettings dbSettings;
    int fontSize;
};
```

- 主窗口创建时初始化 `AppContext`，通过 `GWLP_USERDATA` 或参数传递给子模块
- 子模块不直接访问全局变量，只通过上下文结构体获取所需资源
- 避免各模块各自读 INI、各自连数据库

### 向后兼容

| 保证项 | 措施 |
|--------|------|
| 查询工具独立运行 | `result_search.exe` 目标不变，不引入主程序依赖 |
| INI 格式不变 | 主程序沿用 `result_search.ini`，不新增必填字段 |
| 核心库不变 | `search_core` 只加功能不改接口 |
| Qt 版不受影响 | 主程序只做 Win32 入口，Qt 版保持原样 |
| CI 双版继续 | `build-win32` + `build-qt` job 不动，新加 `build-main-app` job |

### 测试策略

每个模块可脱离主程序独立测试：

```
独立模式：
  result_search.exe   ← 查询模块独立运行
  blood_query.exe     ← 输血模块独立运行（调试用）
  settings.exe        ← 参数设置独立运行

集成模式：
  main_app.exe        ← 所有模块在主窗口中运行
```

- 每个模块保留独立的 `WinMain` + 独立窗口入口
- 主程序模式下，模块的独立入口被替换为子窗口创建函数
- 用 `#ifdef STANDALONE` 或不同的 `.cpp` 入口文件控制

## 后续扩展

主程序平台建立后，新增功能只需：
1. 在菜单中加一项
2. 创建对应的子窗口 `.cpp`
3. 注册到 MDI 客户区
4. 共享主窗口的数据库连接
