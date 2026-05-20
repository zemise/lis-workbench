# lis-workbench

`lis-workbench`（LIS 工作台）是面向 LIS 检验结果、输血申请和相关检验摘要查询的 Windows 工作台。

当前版本：`v2026.05.20`

项目已经整理为可长期演进的结构。
详见 [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) 和 [QT_MIGRATION_GUIDE.md](QT_MIGRATION_GUIDE.md)。

命名说明：

- 对外项目名和仓库名使用 `lis-workbench`。
- 用户可见程序名使用 `LIS 工作台`。
- 配置文件使用 `ClientConfig.ini`；升级时如果只存在旧 `result_search.ini`，程序会自动复制迁移。
- 主程序输出文件名使用 `lis_workbench.exe`。
- 为兼容既有部署，独立检验查询工具 `result_search.exe` 和 `search_*` 内部模块名暂时保留。

### 架构概要

```
┌─────────────────────┐
│  main.cpp (Win32)   │  ← 入口层，Qt 迁移后由 src_qt/main.cpp 替代
│  search_ui_*        │
├─────────────────────┤
│  search_input_*     │  ← 边界层：控件读写、事件分发、列表呈现
│  search_settings_*  │
│  trend_window.*     │
├─────────────────────┤
│  search_controller  │  ← 控制层：桥接查询请求与数据层
├─────────────────────┤
│  search_core        │  ← 核心层：ODBC 查询、数据映射
│  search_app         │      Qt 迁移后原样复用
│  app_settings       │      (ODBC → Qt SQL, INI → QSettings)
│  search_text        │
│  search_view_state  │
│  trend_core         │
└─────────────────────┘
```

- `build/`、`out/`、`ClientConfig.ini` 视为本地产物，不纳入版本管理

当前目标：

- 通过 SQL Server ODBC 连接 LIS 数据库。
- 从 `LS_AS_REPORT` 查询报告主记录。
- 通过 `REP_NO` 联查 `LS_AS_REPENTRY` 中的项目结果。
- 支持按姓名、诊疗卡号、病人号、条码号、样本号、仪器、组合项目、日期范围等条件过滤。
- 在主程序中接入输血结果查询模块，从 `LS_XK_BloodRequestApply` 查询输血申请，并通过 `LS_XK_BloodRequestApplySon.ApplyFormNO` 聚合申请成分。

## 当前实现范围

已实现：

- 原生 Win32 查询界面。
- 左侧查询条件区。
- `检验科室 / 病人类型 / 报告状态` 下拉筛选。
- `检验科室` 下拉来源于 `LS_AS_ROOM.ROOM_NAME`，查询时回写对应 `ROOM_CODE` 过滤。
- `病人类型` 下拉来源于 `LS_AS_PATTYPE`，显示格式为 `TYPE-TYPE_NAME`，查询时回写对应 `TYPE` 过滤。
- `检验仪器` 下拉来源于 `LS_AS_MACHINE.MACH_NAME`，仅显示 `RUL='启用'` 的记录，查询时回写对应 `MACH_CODE` 过滤。
- `检验科室 -> 检验仪器` 联动筛选，选定科室后，仪器列表自动缩小到该科室下启用的仪器。
- 主列表补充 `打印`、`自助机` 两列，分别对应 `ZYMZ_PRINT`、`ZZJ_PRINT`。
- 主列表补充 `检验者`、`审核者` 两列：
  - `检验者`：`LS_AS_REPORT.OPER_CODE = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID`
  - `审核者`：`LS_AS_REPORT.REP_OPER = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID`
- 主列表支持按审核/打印状态着色：
  - 已打印整行白底
  - 已审核整行浅青底
  - 未审核整行橙底
- 中间报告列表。
- 右侧项目明细列表。
- 点击报告行后自动加载对应项目结果。
- 右侧项目明细支持按 `NORMAL` 着色。
- 底部 `趋势图` 按钮可按上一次成功查询条件打开趋势窗口：
  - 上一次查询必须使用病人姓名或病人号，任意一个即可。
  - 如果查询成功后又清空输入框，趋势图仍使用上一次成功查询条件。
  - 按上一次患者/仪器/日期/项目条件查询历史项目点。
  - 趋势数据采用后台加载，窗口会先打开并显示加载状态，避免长日期范围查询时卡住主界面。
  - 右侧平铺显示项目列表，点击项目行切换图表。
  - 项目列表每行带勾选框。
  - 中间显示折线图，横轴按有效结果点顺序等距排列，日期和时间分两行显示。
  - 下方显示趋势明细表，便于和主界面核对。
  - 右侧底部 `导出勾选项目` 可将勾选项目的趋势明细导出为 CSV。
  - 右侧底部 `导出勾选图片` 可选择一个文件夹，并将勾选项目分别导出为多张 PNG。
  - 导出默认文件名为 `病人姓名-病人号-查询日期.csv`，病人姓名或病人号为空时自动跳过对应部分。
- 数据库设置页面，支持服务器、初始数据库、用户名、密码配置。
- 设置页面支持字号配置，保存后会持久化到 `ClientConfig.ini` 并立即应用到菜单栏及子菜单、主界面、输血模块和 LIS 检验信息弹窗；底部状态栏保持系统默认字体。
- 系统设置支持配置 LIS 摘要项目代码，ABO、RhD、Hb、PLT 均以分号分隔保存到 `ClientConfig.ini` 的 `[LisSummary]`。
- 系统设置支持选择常规报告条码打印机，保存到 `ClientConfig.ini` 的 `[RegularReport] BarcodePrinterName`。
- 系统设置支持配置常规报告底部 `1 / 2 / 3` 快捷检验仪器，选择器使用 `LS_AS_ROOM / LS_AS_MACHINE` 数据源，保存到 `ClientConfig.ini` 的 `[RegularReport] QuickMachine*`。
- 数据库配置持久化保存到程序同目录 `ClientConfig.ini`；中文打印机名、快捷检验仪器名等模块配置会以 ASCII 安全编码保存，程序读取时自动还原，避免受系统 ANSI 代码页影响后乱码。
- `设置`、`查询` 和 `退出` 按钮。
- 主程序中的 `检验结果查询`、`输血结果查询`、`系统设置` 均为单实例 MDI 窗口，重复点击菜单会激活已打开窗口。
- 主程序工具栏提供 `常规报告` 快捷入口，复用菜单栏 `工具 -> 常规报告` 的打开逻辑；右侧 `关闭` 按钮仍用于关闭当前 MDI 子窗口。
- 工具菜单中的 `已签收条码查询` 已接入 `LS_AS_BARCODE` 只读查询：
  - 默认不自动查询，日期默认当天申请日期，支持切换签收日期、上机日期。
  - 支持按条形码、姓名、病人号、上机状态、专业组、取消签收状态筛选；取消签收状态按 `CANCEL_DATE` 是否为空判断，上机状态直接对应 `LS_AS_BARCODE.OPER_STATE`，列表中 `0/1/2` 简化显示为 `未上机 / 已上机 / 审核完成`，下拉中的 `已审核未发送` 暂时不关联实际状态。
  - 下方列表展示样本号、急诊、条形码、病人号、类型、姓名、性别、申请科室、床号、签收人、签收时间、医嘱内容、标本、费用、申请医生、状态、备注、原因、送检、送检时间、申请时间、取消时间、取消人、HZID、上机状态。
  - 当前仅实现查询和刷新；取消签收、取消医嘱签收、取消原因限制、导出 Excel 保持禁用，不执行数据库修改。
- 工具菜单中的 `常规报告` 已替换原 `工具2` 占位页，按 `temp/模版2.png` 基本完成三栏报告工作台界面：
  - 构建时优先通过 `find_package(LabelPrint 1.2 CONFIG QUIET)` 链接外部 `LabelPrint` package；正式打包建议用 `scripts/build_main.ps1 -LabelPrintPackagePath` 指向 LabelPrint release zip 解压目录。找不到符合版本的 package 时，再按 `LIS_LABELPRINT_DIR` 回退到源码 `add_subdirectory`，默认路径为 `../../020 LabelPrint/LabelPrint`。两者都找不到时仍可构建，但条码打印不可用。
  - 通过左侧 `检验日期` 和 `检验仪器` 查询 `LS_AS_REPORT`，右侧信息列表按样本号升序展示报告主记录。
  - 右侧选中行会回填左侧标本、病人和验单信息，并通过 `REP_NO` 查询中间检验结果列表。
  - 左侧 `检验者 / 审核 / 申请日期 / 签收时间 / 上机时间 / 报告时间` 为只读展示控件，不允许用户手动修改。
  - 左侧可输入控件使用独立 Tab 顺序，按页面视觉从上到下跳转焦点，Shift+Tab 反向跳转。
  - 右侧信息列表新增 `标签` 列，按 `LS_AS_BARCODE.JZ_FLAG` 显示急诊标记：`1` 显示 `急` 且整行文字为红色，`0` 不显示；行背景按 `CONF`、`CHK_FLAG` 着色，`CONF='S'` 优先显示深绿色，`CHK_FLAG='T'` 显示蓝色。
  - 中间和右侧 ListView 失去焦点时仍保持选中行高亮；自绘时清理 `CDIS_SELECTED/CDIS_FOCUS` 并重绘当前选中行，避免系统非活动选中态覆盖业务行色。
  - 右侧顶部 `样本数 / 上机数 / 审核数 / 发送数` 按当前列表内存数据动态统计，分别对应 `REP_NO` 非空、`NAME` 非空、`CHK_FLAG='T'`、`CONF='S'`。
  - 右侧信息列表支持点击表头进行本地内存排序；排序不会重新查询数据库，并会保留当前选中行和勾选状态。
  - 右侧信息列表上方的上下箭头用于在当前列表内快速选中第一行和最后一行；底部 `刷新(F5)` 会按当前 `检验仪器` 和 `检验日期` 刷新右侧信息列表，但不会在查询开始前清空现有列表。
  - `刷新(F5)` 和 `自动刷新` 查询完成后按 `LS_AS_REPORT.ID` 恢复当前选中行、勾选行和滚动位置；如果刷新前后行顺序未变，只更新内容变化的单元格，减少闪烁和不必要的界面重建。
  - 右侧已有选中行时，重复选择当前 `检验日期` 或点击 `今天` 按钮，也会复用保留状态刷新，避免因为日期控件触发查询而清空当前页面信息。
  - 右侧信息列表下方提供 `今天 / 前一天 / 后一天` 快捷按钮，用于切换左侧 `检验日期` 并按当前检验仪器刷新列表；其后 `自动刷新` 默认未启用，勾选后按秒数输入定时刷新，默认 10 秒，最小 5 秒，且不会在上一次查询未完成时叠加发起。
  - 页面底部 `1 / 2 / 3` 按钮会读取系统设置中的快捷检验仪器，快速切换左侧 `检验仪器` 并按当前 `检验日期` 刷新右侧列表；如果当前页面已经是该快捷仪器，则复用保留状态刷新。
  - 左侧 `样本号` 输入框支持输入样本号后按回车，在当前右侧信息列表中定位并选中对应行，同时触发左侧信息和中间结果列表联动更新。
  - 右侧信息列表第一列支持勾选；行右键菜单提供 `打印条码` 和 `打印勾选条码`，会把对应行样本号、组合项目、条码号、姓名、标本、开单日期、科室代码、病人号填入 LabelPrint 的 `MedicalLabelData`，再调用 LabelPrint `printMedicalLabel` 统一入口发送到条码打印机；其中条码上的组合项目取自该报告 `REP_NO` 对应的中间项目明细 `组合项目` 列，而不是右侧列表的项目名称。
  - 条码打印机名读取 `ClientConfig.ini` 的 `[RegularReport] BarcodePrinterName`，默认 `Xprinter XP-360B #2`；打印机型号和 TSPL/ZPL 后端由 LabelPrint 根据 Windows 打印机元数据自动选择，识别不出时按 XP-360B 兼容路径兜底。如果打印机名失效，需要在系统设置页重新选择条码打印机。
  - 中间结果列表复用检验结果查询的项目明细查询和 `NORMAL` 偏差展示逻辑；其中 `组合项目` 按 `LS_AS_REPENTRY.GROUP_CODE -> LS_AS_LABMATCH.GROUP_NAME` 显示，优先取未删除启用记录，缺失时取同组任意非空名称兜底；连续相同组合项目只在第一行显示；`结果` 列保持白底，其他列使用浅灰底以突出结果值。
  - 中间结果列表的 `结果` 单元格支持界面内临时编辑：单击结果单元格开始编辑，回车提交到当前内存行并跳到下一行继续编辑，失焦或 Esc 取消且不抢回焦点；当前不执行数据库写入。
  - 中间 `图象` 页签打开时，才按当前选中报告的 `REP_NO` 查询 `LS_AS_ITEMPICTURE.PICTURE`；无图像时保持空白，有图像时在左上角固定大图层内按比例绘制，并通过外层滚动视口查看超出窗口的部分。
  - 底部 `图形(T)` 按钮会按当前选中报告打开独立结果图窗口，复用同一张 `LS_AS_ITEMPICTURE.PICTURE` 图片；若中间 `图象` 页签已经加载同一报告，则直接复用已加载图像，否则在弹窗内后台加载后显示。弹窗打开后会跟随右侧信息列表当前选中报告自动刷新，图片采用离屏双缓冲绘制，减少窗口缩放时的图片残影；窗口使用项目图标，关闭或完成拖拽缩放时保存尺寸，下次打开沿用。
  - 中间结果区与右侧信息区之间的拖条可调整宽度，位置保存到 `ClientConfig.ini` 的 `[RegularReport] SplitterX`。
  - 左侧滚动表单不再使用真实 `GROUPBOX` 子窗口，改由内容容器自绘分组边框和标题，并给内部控件启用兄弟裁剪以减少拖动残影。
  - 右侧顶部摘要文字改由父面板自绘并按宽度自动换行，避免拖条调整宽度时透明 `STATIC` 控件残影。
- 主程序 `输血结果查询` 模块：
  - 默认按最近 7 天申请日期自动查询。
  - 支持按病人编号、病人姓名、申请单号、申请状态、申请日期筛选。
  - `申请状态` 直接对应 `LS_XK_BloodRequestApply.ApplyForm_Statue` 的中文值，支持“未审核 / 已审核 / 已完结”过滤。
  - 下方列表按 `ApplyForm_Statue` 着色，并显示申请 ABO/RHD、申请成分、病人号、申请单号、审核人、审核时间等字段。
  - `查询检验结果` 窗口可按当前病人号或姓名查询 LIS 结果，并根据可配置项目代码显示最近一次血型鉴定、血红蛋白和血小板摘要。
  - `查询检验结果` 窗口的组合项目列表和摘要信息分别走独立后台查询，组合项目列表不等待摘要查询完成。
- GitHub Actions 会在 `windows-2022` runner 上使用 VS2022 和 LabelPrint `v1.2.0` Win7 兼容 release 包生成 `LISWorkbench-Setup-<version>-win7-win11.exe` 安装包。该包按 Windows 7 兼容目标构建，目标覆盖 Windows 7 到 Windows 11；实际运行验证仍需在对应系统或虚拟机中完成。

暂未实现：

- 历史库 / 当前库切换。
- 导出、预览、打印、取消发送。
- 医嘱项目、临床科室、临床医生等字典下拉。
- 权限、审核、发送状态业务动作。

## 查询表关系

核心关系：

```text
LS_AS_REPORT.REP_NO = LS_AS_REPENTRY.REP_NO
LS_AS_REPENTRY.ITEM_CODE = LS_AS_ITEM.ITEM_CODE
LS_AS_REPORT.OPER_CODE = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID
LS_AS_REPORT.REP_OPER = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID
LS_AS_REPORT.TXM_NO = LS_AS_BARCODE.BARCODE
LS_AS_REPORT.TYPE = LS_AS_PATTYPE.TYPE
LS_AS_REPORT.SEX = LS_AS_SEX.SEX_CODE
LS_AS_REPORT.ROOM_CODE = LS_AS_ROOM.ROOM_CODE
LS_AS_REPORT.MACH_CODE = LS_AS_MACHINE.MACH_CODE
```

主表 `LS_AS_REPORT` 用于按病人和报告维度定位报告：

- `NAME`
- `REG_NO`
- `TXM_NO`
- `OPER_NO`
- `BED_CODE`
- `ROOM_CODE`
- `MACH_CODE`
- `GROUP_CODE`
- `CHK_DATE`

明细表 `LS_AS_REPENTRY` 用于查询具体项目结果：

- `ITEM_CODE`
- `ITEM_NAME`
- `RESULT`
- `DOWNBOUND`
- `UPBOUND`
- `ITEM_UNIT`
- `ITEM_ENG`
- `NORMAL`

字典和辅助表：

- `LS_AS_ITEM`：项目名称、单位、英文名。
- `JC_EMPLOYEE_PROPERTY`：人员字典，当前用于“检验者”和“审核者”列；`审核者` 使用 `LS_AS_REPORT.REP_OPER` 关联。
- `LS_AS_BARCODE`：条码/病人号关联，用于“病人号”筛选。
- `LS_AS_PATTYPE`：病人类型字典。
- `LS_AS_SEX`：性别字典。
- `LS_AS_ROOM`：检验科室字典。
- `LS_AS_MACHINE`：检验仪器字典。

更完整的字段对应和表关系见 [QUERY_DESIGN.md](QUERY_DESIGN.md)。

## NORMAL 码值说明

当前项目基于实际运行数据，右侧项目明细颜色按 `LS_AS_REPENTRY.NORMAL` 解释为：

- `1`：整行红色
- `5`：整行蓝色
- `3`：不处理，保持默认颜色
- `NULL` / 空：不处理，保持默认颜色

说明：

- 这是一条基于现场实测得到的显示规则。
- 如果后续现场验证结论变化，同步修改 `main.cpp` 中的 `result_row_color()`。

## Windows 交叉编译

```bash
cmake -S . -B build/windows-x64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build/windows-x64 -j
```

输出：`build/windows-x64/result_search.exe`

## 项目文件

- [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md)
- [CHANGELOG.md](CHANGELOG.md)
- [packaging/README_windows_installer.md](packaging/README_windows_installer.md)
- [QUERY_DESIGN.md](QUERY_DESIGN.md)
- [QT_MIGRATION_GUIDE.md](QT_MIGRATION_GUIDE.md)
- [TREND_CHART_PLAN.md](TREND_CHART_PLAN.md)

## Windows 安装包

主程序安装包使用 NSIS 生成，详见 `packaging/README_windows_installer.md`。

VS 原生构建的 `lis_workbench.exe` 默认静态链接 MSVC runtime，Release 安装包通常不需要再携带 `MSVCP140.dll`、`VCRUNTIME140.dll`、`VCRUNTIME140_1.dll`。

项目按 Windows 7 兼容目标编译，CMake 会为 Win32 目标统一设置 `WINVER/_WIN32_WINNT=0x0601`。代码中不能直接导入 Windows 8/10 才有的 API；需要使用时应通过 `GetProcAddress` 动态探测并提供 Win7 回退，避免在 Win7 上出现 `CreateFile2`、`GetDpiForWindow` 等入口点缺失错误。

面向 Windows 7 打包时需要安装 VS 2022 Build Tools，并优先使用 LabelPrint 的 `windows-x64-vs2022-win7` release 包，例如 `.\scripts\build_main.ps1 -Clean -Config Release -LabelPrintPackagePath "C:\Deps\LabelPrint\labelprint-v1.2.0-windows-x64-vs2022-win7"`。不要把 VS 2026 的 CRT DLL 打入安装目录，否则可能出现 `GetSystemTimePreciseAsFileTime` 等 Win8+ 入口点缺失。只面向 Windows 10/11 时可以显式使用 `-Generator "Visual Studio 18 2026"` 和对应的 LabelPrint VS2026 release 包。

## 使用说明

1. 在 Windows 上运行主程序 `lis_workbench.exe`；独立检验查询工具仍可运行 `result_search.exe`。
2. 点击底部 `设置`。
3. 填写服务器、初始数据库、用户名、密码。
4. 可点击 `测试连接` 验证数据库连接。
5. 点击 `保存` 后，配置会写入程序同目录 `ClientConfig.ini`。LIS 摘要项目代码可在系统设置里按分号维护，默认值来自当前现场排查结果。
6. 输入姓名、病人号、条码号、日期范围，或通过 `检验科室 / 病人类型 / 报告状态` 下拉筛选。
7. 点击 `查询`。
8. 在中间报告列表选择一行，右侧显示该报告的项目结果。
9. 如果本次查询使用了病人姓名或病人号，可点击 `趋势图` 查看该查询条件下的项目趋势。
10. 在趋势图窗口右侧点击项目行切换图表，勾选项目后可导出对应趋势明细 CSV。

程序内部会自动生成连接串，格式示例：

```text
Data Source=172.18.3.8\MSSQLSERVER1;Initial Catalog=trasen;User ID=sa;Password=your_password
```

程序会生成 SQL Server 连接串，并在底层自动尝试：

- `ODBC Driver 18 for SQL Server`
- `ODBC Driver 17 for SQL Server`
- `SQL Server`

说明：

- `初始数据库` 直接作为实际查询连接里的 `Initial Catalog`。
- 前端不再手填 ODBC 驱动，驱动选择由底层自动尝试。
- 该字段会保存到 `ClientConfig.ini`。
