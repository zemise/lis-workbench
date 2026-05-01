# cpp_search 项目结构

## 目录约定

- `src/`
  - `main.cpp`
    - Win32 主窗口、设置窗口、列表渲染、交互事件。
  - `search_core.h/.cpp`
    - 数据库查询核心：字典下拉查询、报告列表查询、项目明细查询。
  - `search_app.h/.cpp`
    - 界面无关的应用层：查询输入组装、状态文案映射、报告/结果行语义状态。
  - `search_controller.h/.cpp`
    - 应用控制层：测试连接、加载字典下拉、执行主查询、加载右侧项目明细。
  - `search_input_view_model.h/.cpp`
    - 输入/view-model 层：主界面控件值读取、下拉框回填、查询输入组装。
  - `search_text.h/.cpp`
    - 公共文本工具：`trim`、UTF-8 与宽字符转换，避免各层重复实现。
  - `search_ui_context.h`
    - UI 上下文：主界面句柄集合、主窗口、字体对象。
  - `search_ui_columns.h`
    - 报告列表和项目明细列表的列号集中定义，避免新增列后多处硬编码错位。
  - `search_ui_events.h/.cpp`
    - 事件分发层：`WM_COMMAND / WM_NOTIFY` 对应的 Win32 事件桥接；控件 ID 通过 `MainUiIds` 注入。
  - `search_ui_layout.h/.cpp`
    - Win32 主界面控件创建、自适应布局、splitter 对应的布局层。
  - `search_ui_presenter.h/.cpp`
    - presenter 层：状态栏更新、报告列表填充、项目明细列表填充。
  - `search_settings_dialog.h/.cpp`
    - Win32 设置窗口模块，负责设置对话框生命周期与回调桥接。
  - `search_view_state.h/.cpp`
    - 应用状态聚合：配置、查询结果、字典下拉缓存、连接状态。
  - `app_settings.h/.cpp`
    - 本地配置读写、默认 `result_search.ini` 路径、数据库连接串生成。
  - `version.h`
    - 版本号与程序标题。
- `cmake/`
  - `toolchains/`
    - Windows 交叉编译工具链。
- `scripts/`
  - `build_windows_package.sh`
    - Windows 便携包/安装包构建脚本。
- `packaging/`
  - `ResultSearch.nsi`
    - NSIS 安装包脚本。
  - `README_windows_installer.md`
    - Windows 安装包构建说明。
- `resource/`
  - 预留资源目录，可放图标、默认配置、模板文件。
- `build/`
  - 本地和交叉编译中间产物，不纳入版本管理。
- `out/`
  - 打包输出目录，不纳入版本管理。

## 代码分层约定

- `main.cpp` 只负责界面与交互，不直接拼接 SQL，不直接维护业务显示词和状态语义。
- `main.cpp` 当前已经主要收敛为 Win32 入口、消息分发、少量事件胶水。
- `search_core.cpp` 只负责数据库访问，不直接依赖具体 Win32 控件。
- `search_app.cpp` 负责应用层规则，后续切 Qt 时可直接复用。
- `search_controller.cpp` 负责把界面输入连接到数据库查询与结果输出，后续 Win32/Qt 可共用。
- `search_input_view_model.cpp` 负责控件值读取与写回，后续 Qt 可替换成新的 view-model 实现。
- `search_text.cpp` 负责公共字符串处理，所有层统一使用同一套编码转换。
- `search_ui_context.h` 负责明确 UI 句柄和字体等上下文边界。
- `search_ui_columns.h` 负责主列表/明细列表列号常量，新增列时优先改这里和 presenter/layout。
- `search_ui_events.cpp` 负责 Win32 事件分发，减少入口文件中的分支逻辑，并避免事件层硬编码控件 ID。
- `search_ui_layout.cpp` 负责 Win32 主界面控件生命周期与布局收口。
- `search_ui_presenter.cpp` 负责列表与状态显示，减少入口文件直接操作 `ListView`。
- `search_settings_dialog.cpp` 负责 Win32 设置窗口，避免设置页逻辑继续堆积在入口文件。
- `search_view_state.cpp` 负责把分散状态收口成统一结构，便于后续 Qt 数据绑定。
- `app_settings.cpp` 只负责配置与连接串生成，不直接依赖业务查询逻辑。

## 当前成熟度

- 主界面布局、设置窗口、状态、输入、presenter、controller、数据库访问已经分层。
- 公共文本工具、列表列号、Win32 控件 ID 分发已经集中管理。
- `main.cpp` 已从超大业务文件收敛到中等规模入口文件。
- 当前结构已经适合作为 Win32 持续演进版本，也适合作为后续 Qt 平移前的稳定内核。

## 演进建议

- 下一步建议把 `main.cpp` 中 splitter 拖动、字体应用、窗口注册等剩余 Win32 胶水继续下沉，最终收口成更薄的入口文件。
- 如果后续引入导出、打印、预览，建议单独拆出 `report_actions.*`。
- 如果后续启动 Qt 试验版，优先复用 `search_core.*`、`app_settings.*`、`search_app.*`、`search_controller.*`、`search_view_state.*`。
