# lis-workbench 项目结构

## 目录约定

- `src/`
  - Win32 界面 + 跨平台核心层源码
- `src_qt/`（新建，待实现）
  - Qt 5.15 界面层，与 Win32 共存
- `cmake/`
  - `toolchains/` — Windows 交叉编译工具链
- `scripts/`
  - `lis.ps1` — Windows 常用构建/运行/打包统一入口
  - `build_main.ps1` — Win32 主程序和 `Updater.exe` 构建脚本
  - `create_update_package.ps1` — 生成自动更新 zip 和 `manifest.json`
  - `build_windows_package.sh` — Windows 便携包/安装包构建脚本
- `packaging/`
  - `LISWorkbench.nsi` — NSIS 安装包脚本
  - `README_windows_installer.md` — 安装包构建说明
- `resource/`
  - `app.ico` — 应用图标（16+32px）
  - `app.rc` — Windows 资源脚本
  - `app.manifest` — DPI 感知 + Common Controls 清单
  - `resource.h` — 资源 ID 定义
- `build/` — 编译中间产物，不入版本管理
- `out/` — 打包输出，不入版本管理
- `WIN32_NATIVE_UI_DESIGN.md` — 原生 Win32 界面设计约定，记录复选框背景、宿主窗口、字体和布局取舍

## 命名约定

- 对外项目名：`lis-workbench`
- 用户可见程序名：`LIS 工作台`
- CMake project 名：`lis_workbench`
- 配置文件：`ClientConfig.ini`；旧 `result_search.ini` 仅作为升级迁移来源。
- 主程序输出文件：`lis_workbench.exe`
- 自动更新器输出文件：`Updater.exe`
- 兼容保留：独立检验查询工具 `result_search.exe`、`search_core` 和 `search_*` 源文件名暂不重命名，避免影响既有构建脚本和代码引用。

## 文件层级 — 按迁移状态

### 核心层（新 UI 可直接复用）

| 文件 | 职责 | Qt 复用方式 |
|------|------|-----------|
| `search_core.*` | ODBC 数据库查询 | 切换到 Qt SQL 后端，接口不变 |
| `search_app.*` | `QueryInput` 结构、筛选器组装、状态文案映射 | 原样复用 |
| `search_controller.*` | 测试连接、加载字典、执行查询 | 原样复用（ODBC 切换后） |
| `search_text.*` | `trim`、UTF-8 ↔ 宽字符转换 | Qt 下替换为 `QString`，过渡期保持兼容 |
| `app_settings.*` | `ClientConfig.ini` 读写、连接串生成、LIS 摘要项目代码默认配置 | 切换到 `QSettings`，接口保留 |
| `search_view_state.*` | `ViewState` 聚合运行时状态 | 原样复用 |
| `version.h` | 版本号与标题 | 原样复用 |
| `search_ui_columns.h` | 列号常量 | Qt 中列语义由 model 管理，参考此文件 |
| `trend_core.*` | 趋势数据查询 | 原样复用（ODBC 切换后） |
| `update_config.h` | 自动更新配置键、更新源取值和 GitHub latest manifest 默认地址 | Win32 主程序和设置页共用 |
| `update_manifest.*` | 自动更新 manifest 解析、版本比较、SHA-256 校验 | 可作为后续 Qt/Win32 共享更新核心 |
| `update_source.*` | 自动更新源抽象、文件夹更新源、HTTP 更新源和统一检查拉取流程 | 主程序菜单 `系统 -> 检查更新` 入口已复用 |

### 边界层（接口保留，实现替换）

| 文件 | Win32 实现 | Qt 替换 |
|------|-----------|---------|
| `search_input_view_model.*` | HWND 控件读写、下拉填充 | QLineEdit/QComboBox/QDateEdit 读写 |
| `search_ui_events.*` | Win32 消息分发 → 回调 | Qt signal/slot |
| `search_ui_presenter.*` | ListView 列定义与行填充 | QTableView + QStandardItemModel |
| `search_ui_layout.*` | 硬编码像素布局 + splitter | QLayout + QSplitter |
| `search_splitter.*` | Win32 通用拖条控件，向父窗口发送拖动/释放消息 | QSplitter |
| `search_settings_dialog.*` | Win32 模式对话框 | QDialog |
| `trend_window.*` | GDI+ 图表 + ListView | QwtPlot + QTableView |
| `trend_chart_renderer.*` | GDI+ 离屏位图 | QPainter + QCustomPlot |

### 入口层（Win32 独有，Qt 新建等价物）

| 文件 | 职责 |
|------|------|
| `main.cpp` | Win32 入口、消息循环、窗口过程、全局状态（独立查询工具） |
| `main_frame.cpp` | 主程序入口、g_modules[] 注册表、自动菜单/分发、主工具栏快捷入口、MDI 活动页与工具栏 active/关闭状态同步，以及 `系统 -> 检查更新` |
| `main_app.h` | 主程序全局上下文 |
| `module_registry.h` | ModuleContext + ModuleDef 统一模块接口；MDI 子窗口按标题激活的单实例 helper |
| `menu_toolbar.cpp/h` | 原生 Win32 自定义工具栏组件；GDI 双缓冲绘制浅色 command bar，支持 hover/pressed/active/disabled、右侧文字型关闭按钮和拉伸占位 |
| `barcode_label_printing.cpp/h` | Win32 主程序条码标签打印共享 helper，统一读取 `[RegularReport] BarcodePrinterName`，封装 LabelPrint `printMedicalLabel` 调用；常规报告和标本签收中心共用 |
| `query_module.cpp/h` | 检验结果查询单实例 MDI 子窗口 |
| `barcode_module.cpp/h` | 已签收条码查询单实例 MDI 子窗口，按 `LS_AS_BARCODE` 只读检索 |
| `regular_report_module.cpp/h` | 常规报告单实例 MDI 子窗口，按 `temp/模版2.png` 基本完成报告工作台；按检验日期和检验仪器查询 `LS_AS_REPORT`，右侧选中行回填左侧信息并联动中间项目明细；中间组合项目按 `LS_AS_REPENTRY.GROUP_CODE -> LS_AS_LABMATCH.GROUP_NAME` 显示，连续相同项只显示首行，图象页按 `REP_NO` 按需查询 `LS_AS_ITEMPICTURE.PICTURE` 并用滚动视口显示大图；底部 `图形(T)` 打开独立结果图窗口，复用同一图片查询逻辑，跟随右侧选中行刷新，双缓冲绘制并按 `[RegularReport] PicturePopupWidth/Height` 保存尺寸；右侧列表支持本地排序、首末行跳转、今天/前一天/后一天快捷切换检验日期、样本号回车定位、顶部动态统计、保留状态刷新、勾选批量打印；中间/右侧自绘 ListView 会在失焦后保持选中行高亮；底部 `1/2/3` 快捷检验仪器读取 `[RegularReport] QuickMachine*`；右键菜单对接 `LabelPrint` 执行 `打印条码`，条码组合项目来自中间明细 `ResultRow.group_name` 去重拼接，打印机型号和 TSPL/ZPL 后端由 LabelPrint `printMedicalLabel` 统一入口负责；构建时优先 `find_package(LabelPrint 1.2)`，找不到再回退 `LIS_LABELPRINT_DIR` 源码接入；中间/右侧拖条位置按 `[RegularReport] SplitterX` 持久化；左侧长表单用自绘分组框替代真实 `GROUPBOX`，右侧顶部摘要由父面板自绘并自动换行，配合 `WS_CLIPSIBLINGS` 降低拖动/滚动残影 |
| `specimen_sign_module.cpp/h` | 标本签收中心单实例 MDI 子窗口，替换原 `工具3`；当前按 `temp/模版.png` 完成界面骨架，日期筛选默认当天起止时间并在跨日后自动切换；条码输入框和查询按钮已接入第一版只读查询，有条码时精确查询 `LS_AS_BARCODE / LS_AS_REPORT` 并可选补查 `V_lis_mzinfo_txm / YJ_MZSQ / YJ_ZYSQ` 回填页面，左侧条码为空时按签收日期 `IN_DATE` 和/或申请日期 `REQ_TIME` 查询已签收条码列表；下方列表补充签收时间、送检时间、年龄、签收人、标本类型和检验室，检验室/仪器分别通过 `LS_AS_ROOM`、`LS_AS_MACHINE` 转名称；`补打条码` 复用常规报告共用的 LabelPrint helper，样本号固定 `补`，组合项目取当前选中行 `检验室`；暂不执行签收、拒签、导出等数据库写入业务操作 |
| `hiv_statistics_module.cpp/h` | HIV 抗体检测统计单实例 MDI 子窗口，替换 `统计分析管理 -> 统计分析1`；第一版按年份/月度只读查询 `LS_AS_REPORT.REP_TIME` 范围内的三组 HIV 初筛候选项目，只纳入已审核、已发送且姓名非空的报告；查询先按月份、审核、发送、仪器和可选 `DEPT_CODE IN (...)` 从 `LS_AS_REPORT` 筛出候选 `REP_NO`，再按候选 `REP_NO` 分批查询 `LS_AS_REPENTRY` 的目标 `ITEM_CODE`，并在 C++ 中按报告仪器匹配目标项目；仪器、病人类型和科室名称通过 `LS_AS_MACHINE`、`LS_AS_PATTYPE`、`JC_DEPT_PROPERTY` 小字典缓存后在 C++ 内存映射，方法学按 `MACH_CODE` 派生为 `4005/914 -> 化学发光法`、`4008 -> 酶免法`；顶部 `新院 / 老院` 筛选会先从 `JC_DEPT_PROPERTY` 分出 `DEPT_ID` 集合并下推为 `r.DEPT_CODE IN (...)`；按 `REP_NO + MACH_CODE + ITEM_CODE` 去重统计合计行的初筛检测数和初筛阳性数，并展示明细核对列表；`性病门诊`、`其他就诊检测`、`孕产期检查` 行暂按明细科室名称文字规则统计，后续再考虑将这三类改为 `DEPT_ID` 精确口径；`受血（制品）前检测` 行按明细 `已完结输血单申请号` 非空统计；`术前检测` 行按总数扣除受血、性病门诊、其他就诊和孕产期检查后的剩余量统计；病人号显示 `LS_AS_REPORT.REG_NO`，已完结输血单申请号按统计月份预取 `LS_XK_BloodRequestApply.Apply_Time` 当月 `ApplyForm_Statue='已完结'` 记录后，再按 `REG_NO` 匹配 `Patient_NO` 的 `ApplyFormNO`，阳性行以红色背景提示；明细表支持点击任意表头做本地内存排序，不额外查询数据库；`导出统计表` 基于安装目录 `templates/HIVStatisticsTemplate.docx` 的 `{}` 占位符生成 `.docx`，模版不随项目发布，需通过页面 `上传模版` 指定并复制到安装目录；未检测到匹配模版时导出按钮不可用；只填写当前汇总结果，不导出明细列表，也不额外查询数据库；非合计行数字为 `0` 时导出为空，合计行保留 `0`；其他样本来源分类、复检数和本年度累计暂未接入 |
| `blood_module.cpp/h` | 输血结果查询单实例 MDI 子窗口，按 `LS_XK_BloodRequestApply` 只读检索，已确认申请主表 `ApplyFormNO / Apply_Time / Plan_Date / ApplyForm_Statue / Patient_NO / Patient_NOType / Patient_Name` 等字段含义，`ApplyForm_Statue` 状态值为 `已审核 / 未审核 / 已完结`；通过 `LS_XK_BloodRequestApplySon.ApplyFormNO` 聚合申请成分；交叉配血记录可按 `LS_XK_BloodCrossMatch.ApplyFormNO` 关联申请单，已确认 `Patient_NO / Patient_NOType / Patient_Name / VerifyState / Match_Date` 等字段含义；LIS 结果弹窗列表与摘要分离后台查询，报告列表显示 `LS_AS_REPORT.OPER_NO` 样本号，弹窗按当前显示器工作区限制最大尺寸并居中，默认查询最近 14 天 LIS 结果 |
| `settings_module.cpp/h` | 系统设置单实例 MDI 子窗口，维护数据库、字号、LIS 摘要代码、常规报告条码打印机、底部快捷检验仪器和自动更新源配置；页面采用原生 Win32 分组卡片布局，字号下拉限制为 `9 / 11 / 12 / 13`，自动检查更新复选框使用纯原生 `BUTTON + BS_AUTOCHECKBOX` 并通过白色宿主窗口统一背景，保存后页面不关闭且在按钮附近显示状态 |
| `search_ui_context.h` | Win32 句柄集合、字体上下文 |
| `updater_main.cpp` | 独立 `Updater.exe` 入口，支持 zip 包/已展开目录，负责等待主程序退出、解压、备份、替换、失败回滚、重启，并自动写入 `log\updater.log` |

## 代码分层约定

- 核心层禁止包含 `<windows.h>`、`HWND`、`HDC` 等 Win32 类型。
- 核心层只使用 C++17 标准库和 `<sqlext.h>`（ODBC 过渡期）。
- `QueryInput`（定义在 `search_app.h`）是界面层与核心层的唯一数据契约。
- 边界层可包含 Win32 类型，但接口上暴露的数据结构（如 `ViewState`）保持干净。
- 入口层自由使用任意 Win32 API。

## 当前成熟度

- Win32 版：主界面、设置窗口、趋势窗口均已完成功能。DPI 感知、图标、防闪烁到位。
- 核心库：`search_core` 静态库已提取，Win32 和 Qt 共享。
- 主程序壳：`lis_workbench.exe` 已接入检验结果查询、输血结果查询、已签收条码查询和系统设置等 MDI 子窗口。
- Qt 编译链路已通：CI 双绿 + Windows 实机构建脚本（`scripts/build_qt.ps1`）。
- Qt 四大组件已实现：设置对话框、主窗口（查询+双列表）、趋势窗口（QwtPlot 折线图）。
- 详见 [QT_MIGRATION_GUIDE.md](QT_MIGRATION_GUIDE.md)。

## 演进方向

- ~~提取核心层为 `search_core` 静态库~~ ✅
- ~~搭建 Qt 5.15 编译链路~~ ✅
- ~~主程序阶段 1：MDI 窗口壳~~ ✅
- ~~主程序阶段 2.3：系统设置接入~~ ✅
- ~~主程序阶段 2.1：检验结果查询接入~~ ✅
- ~~主程序阶段 2.2：输血结果查询接入~~ ✅
- ~~工具模块：已签收条码查询接入~~ ✅
- ~~工具模块：常规报告界面接入~~ ✅
- ~~模块系统改造（5.1~5.7）~~ ✅
- **当前**：自动更新基础设施、主程序模块细节打磨与现场验证
- **中期**：Qt 版本功能对齐 Win32 版
- **长期**：Qt 版本稳定后，Win32 入口降级为可选回退构建
