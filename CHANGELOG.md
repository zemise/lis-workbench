# Changelog

## v2026.06.13

- **系统设置页原生 Win32 体验优化**：设置页调整为两列分组卡片布局，整理数据库、界面、LIS 摘要、常规报告、快捷检验仪器和自动更新区域；`自动检查更新` 保持纯原生 `BUTTON + BS_AUTOCHECKBOX`，通过白色宿主窗口统一背景，不使用 owner-draw。
- **系统设置交互优化**：字号选择改为原生下拉框，仅提供 `9 / 11 / 12 / 13` 四档并兼容旧配置就近归一；LIS 摘要标签简化为 `ABO 代码 / RhD 代码 / Hb 代码 / PLT 代码`；点击保存后页面保持打开，并在保存按钮附近显示状态提示；字号未变化时避免全窗口字体重建，减少保存闪烁。
- **Win32 原生设计文档**：新增 `WIN32_NATIVE_UI_DESIGN.md`，记录坚持原生控件、复选框背景处理、白色宿主窗口、字体层级和后续页面设计检查清单。
- **输血结果查询修复**：恢复输血结果查询页浅蓝背景；`查询检验结果` 弹窗按当前显示器工作区限制最大尺寸并居中，避免出现在屏幕边缘导致显示不全；LIS 检验结果弹窗默认时间范围从 7 天调整为 14 天。
- **Windows 默认构建修复**：`lis.ps1 build/run` 在 `auto` 策略下优先使用本地 LabelPrint 源码，并显式禁用外部 `find_package(LabelPrint)`，避免旧 CMake 缓存或 `/MD` package 与主程序默认 `/MT` 静态运行库混用导致 LNK2038/LNK2001。
- 版本号 v2026.06.13。

## v2026.06.12

- **代码重构：常规报告模块拆分**：将 `regular_report_module.cpp`（原 4232 行单体文件）拆分为 3 个实现文件 + 1 个共享头文件：`regular_report_state.h`（结构体/常量/前向声明）、`regular_report_utils.cpp`（工具函数）、`regular_report_picture.cpp`（图片/GDI+ 处理）、`regular_report_module.cpp`（2213 行，面板/布局/查询/入口）。
- **CMake 构建优化**：提取 8 个重复编译的 `.cpp` 文件为 `search_ui_core` STATIC 库，消除 `result_search` 和 `main_app` 之间的双重编译；添加 `/MP` 多进程编译和 `search_ui_core` 预编译头；添加 Release 构建 `/GL` + `/LTCG` 全程序优化。
- **代码去重**：将 `clampFontSize`（3 份副本）和 `createUiFont`（4 份副本）移入共享的 `search::clamp_font_size` / `search::create_ui_font`；创建 `quick_machine_keys.h`，消除 `QUICK_MACHINE_CODE_KEYS`/`NAME_KEYS`/`ROOM_KEYS` 双份副本；`search_ui_layout` 新增 `search::apply_font_to_children`，消除 4 份 `applyFont`/`applyFontToChildren` 重复实现。
- **WNDCLASSEXW 样板消除**：在 `search_ui_layout.h` 中添加 `REGISTER_MDI_CHILD_CLASS` 宏，统一替换 9 个 MDI 子窗口类注册站点（~100 行缩减）。
- **g_pending 全局状态消除**：用 `lpCreateParams` 替代 8 个 `g_pending`/`g_pendingLis` 全局裸指针，消除线程安全隐患。
- **DbContext RAII**：为 `search_core.cpp` 的 `DbContext` 添加析构函数（自动 `SQLDisconnect` + `SQLFreeHandle`），删除 28 处冗余手动 `disconnect()` 调用。
- **query_lis_summary 查询优化**：将 ABO/RhD 和 HGB/PLT 两次独立 SQL 往返合并为单次 `OUTER APPLY` 查询，减少一次网络往返。
- **编译警告与清理**：启用 `/W3` 编译警告，添加 `/wd4996` 抑制已知安全的 `sscanf` 警告；修复 `hiv_statistics_module.cpp` 使用过时 `WNDCLASSW`/`RegisterClassW` 的问题；修复 2 处 GDI 画刷泄漏（`blood_module.cpp`）；移除 `search_text.cpp` 死 `#else` 分支；移除未使用的 `app::ModuleDef` 结构体；修复 `query_module.cpp` 设置按钮先创建后销毁的浪费。
- **日志与崩溃诊断系统**：新增 `log.h/cpp` 线程安全文件日志（按天滚动 `log/YYYY-MM-DD.log` + `OutputDebugString` 同步输出）；新增 `crash_handler.cpp` 崩溃转储（`crash_YYYYMMDD_HHMMSS.dmp`）；激活 `search_core` 休眠的 `LogFn` 基础设施；为查询线程添加 `try/catch` 保护（`PostMessageW` 失败记录日志、`WM_CREATE` lpCreateParams null 守卫）。
- **打包文件更新**：NSIS 安装脚本 `LISWorkbench.nsi` 和打包文档 `packaging/README_windows_installer.md` 与当前构建产物保持一致；`lis.ps1 build` 现在会在本地构建后直接生成 `out\windows\installer\LISWorkbench-Setup.exe`，且默认不下载远程 LabelPrint；`lis.ps1 package` 会优先复用现有 CMake 缓存生成器，没有缓存时再解析实际 Visual Studio 生成器，并选择匹配的 LabelPrint release 包，避免 VS2026 构建误用 VS2022/Win7 包。
- **macOS 交叉编译 Makefile**：新增根目录 `Makefile`，支持 macOS 上通过 MinGW-w64 toolchain 交叉编译 Windows 版本；提供 Maven 风格目标（clean / compile / test / package / verify / install），生命周期链 compile → test → package → verify → install；package 目标生成 NSIS 安装包、自动更新 zip 和 `manifest.json`，产物与 Windows `lis.ps1 package` 保持一致。
- **交叉编译兼容修复**：`/GL` + `/LTCG` 标志用 `if(MSVC)` 包裹，MinGW 交叉编译正常；`regular_report_utils.cpp` 和 `regular_report_picture.cpp` 补全 `<cstring>` include，消除 MinGW GCC `std::strlen`/`std::memcpy` 缺失错误。
- 版本号 v2026.06.12。

## v2026.06.06

- **数据库关系补充**：补充 `JC_DEPT_PROPERTY` 和 `JC_dept_mz_zy` 临床申请科室字典说明；`LS_AS_BARCODE.DEPT_CODE / DEPT_NAME` 属于申请科室，后续需要从代码补全名称时，优先使用覆盖率更高的 `JC_DEPT_PROPERTY.DEPT_ID -> NAME`，`JC_dept_mz_zy.mzksid/mzksmc` 或 `zyksid/zyksmc` 作为门诊/住院辅助关系，不与 `SIGN_DEPT`、检验科室或仪器科室混用。
- **输血表结构补充**：补充 `LS_XK_BloodRequestApply` 输血申请主表说明，确认 `ApplyFormNO` 为输血单申请号，`Apply_Time` 为输血申请时间，`Plan_Date` 为输血计划时间，`ApplyForm_Statue` 为审核状态且状态值为 `已审核 / 未审核 / 已完结`，`Patient_NO` 为病人号，`Patient_NOType` 为病人类型，`Patient_Name` 为病人姓名；补充 `LS_XK_BloodCrossMatch` 交叉配血记录表说明，确认 `ApplyFormNO` 对应输血申请号，`Patient_NO` 为病人号，`Patient_NOType` 为患者类型，`Patient_Name` 为病人姓名，`VerifyState` 为审核状态，`Match_Date` 为配血时间。
- **统计分析管理**：新增 `统计分析管理` 顶级菜单，预留 5 个统计分析入口；`统计分析1` 替换为 `HIV 抗体检测统计` 页面，第一版按 `LS_AS_REPORT.REP_TIME` 月份只读统计三组已确认 HIV 初筛候选项目，只纳入已审核 `CHK_FLAG='T'`、已发送 `CONF='S'` 且姓名非空的报告；HIV 查询先按月份、审核、发送、仪器和可选 `DEPT_CODE IN (...)` 从 `LS_AS_REPORT` 筛出候选 `REP_NO`，再按候选 `REP_NO` 分批查询 `LS_AS_REPENTRY` 的目标 `ITEM_CODE`，避免月度范围直接联查大明细表；仪器、病人类型和科室名称字典移到 C++ 内存映射，减少多字典 JOIN；明细新增 `方法学` 列，按 `MACH_CODE` 派生为 `4005/914 -> 化学发光法`、`4008 -> 酶免法`，不额外查库；`新院 / 老院` 筛选先从 `JC_DEPT_PROPERTY` 分出 `DEPT_ID` 集合并下推为 `r.DEPT_CODE IN (...)`，避免主 SQL 使用 `LIKE '%滨水新城%'`；按 `REP_NO + MACH_CODE + ITEM_CODE` 去重生成合计行的初筛检测数和初筛阳性数；`性病门诊` 行暂按明细 `科室` 包含 `皮肤科门诊` 的文字口径生成，`其他就诊检测` 行暂按明细 `科室` 包含 `体检`、`儿童保健`、`健康管理`、`GCP` 或科室为空/`0` 的文字口径生成，`孕产期检查` 行暂按明细 `科室` 包含 `产科门诊` 或 `早孕关爱门诊` 的文字口径生成，后续再考虑将这三类改为 `DEPT_ID` 精确口径；`受血（制品）前检测` 行按明细 `已完结输血单申请号` 非空生成初筛检测数和初筛阳性数；`术前检测` 行按总数扣除受血、性病门诊、其他就诊和孕产期检查后的剩余量生成初筛检测数和初筛阳性数；阳性按结果文本判断，`待确认` 优先，其次为 `阳性` 或 `+`，不做结果值与上下限的数值比较；顶部新增 `全部 / 新院 / 老院` 检验科来源筛选；新增 `病人号` 列显示 `LS_AS_REPORT.REG_NO`，新增 `已完结输血单申请号` 列，先按统计月份预取 `LS_XK_BloodRequestApply.Apply_Time` 当月 `ApplyForm_Statue='已完结'` 申请记录，再按当前明细 `REG_NO` 匹配 `Patient_NO` 的 `ApplyFormNO`，报告时间按 `LS_AS_REPORT.REP_TIME` 显示，阳性行用红色背景提示；下方明细列表支持点击任意表头进行本地内存排序，不额外查询数据库；`导出统计表` 支持基于安装目录 `templates/HIVStatisticsTemplate.docx` 的 `{}` 占位符生成 Word `.docx` 统计表，模版不随项目发布，需通过页面 `上传模版` 指定并复制到安装目录；未检测到匹配模版时导出按钮不可用；只导出当前汇总结果，不导出下方明细列表，也不额外查询数据库；非合计行数字为 `0` 时导出为空，合计行保留 `0`；WB 检测数、复检和本年度累计暂未接入。
- **标本签收中心界面**：工具菜单 `标本签收中心` 替换原 `工具3` 占位页，新增 `specimen_sign_module.cpp/h`，按 `temp/模版.png` 完成顶部扫码区、病人信息、医嘱明细、左侧筛选区和右侧已签收条码列表的界面骨架；病人类型下拉框复用 `LS_AS_PATTYPE` 数据源并只显示 `TYPE_NAME`；顶部操作按钮和医嘱明细列表右侧 `- / +` 按钮会随窗口右侧边界移动；左上扫码框增加 `标本签收工作台` 标签，签收日期和申请日期使用日期时间选择器并默认当天起止时间，页面跨过凌晨后自动切换到新一天；按钮完全复用检验结果查询页的公共 `search::create_button` 原生按钮创建逻辑，不额外 owner-draw；当前已接入第一版只读条码查询，按单个条码精确查询 `LS_AS_BARCODE / LS_AS_REPORT` 并可选补查 `V_lis_mzinfo_txm / YJ_MZSQ / YJ_ZYSQ` 回填页面；当左侧条码为空时，支持按签收日期 `IN_DATE` 和/或申请日期 `REQ_TIME` 查询已签收条码列表，不执行签收、拒签、打印或导出操作。
- **标本签收中心查询细节**：下方已签收列表补充签收时间、送检时间、年龄、签收人、标本类型和检验室，检验室通过 `LS_AS_BARCODE.ROOM_CODE -> LS_AS_ROOM.ROOM_NAME` 转换，字典查不到时回退显示代码；`此条码已存在...` 提示拆分为顶部红色专用标签，普通查询状态移至左下按钮区；已签收提示中的仪器通过 `LS_AS_REPORT.MACH_CODE -> LS_AS_MACHINE.MACH_NAME` 显示，并在任一条码明细 `OPER_STATE=0` 时追加 `未上机检验!`。
- **标本签收中心补打条码**：新增 `barcode_label_printing.cpp/h` 共享条码打印 helper，常规报告和标本签收中心共用 LabelPrint `printMedicalLabel` 入口；标本签收中心 `补打条码` 以右侧已签收列表当前选中行为输入，样本号固定为 `补`，组合项目固定取当前行 `检验室` 内容。
- **标本签收中心管理跳转**：`管理` 按钮会打开或激活 `已签收条码查询` 页面，复用该页面已有单实例 MDI 入口。
- **主工具栏入口**：菜单栏下方自定义工具栏在 `常规报告` 前新增 `标本签收中心` 快捷入口，复用菜单栏 `工具 -> 标本签收中心` 的打开逻辑。
- **标本签收中心文档**：新增 `SPECIMEN_SIGN_MODULE.md`，将标本签收中心的界面结构、复用约定、日期行为和当前边界从 README 中拆出独立维护。
- **常规报告危急值提示**：中间结果列表优先采用 `LS_AS_REPENTRY.NORMAL_WJ` 的 LIS 判定结果，并保留客户端按 `LS_AS_DEF_ITEMSCOPE.DNBOUND1 / UPBOUND1` 计算危急值的能力，用于当前展示兜底和后续仪器输入后的实时判断。
- **常规报告报告类型校正**：右侧信息列表的 `急` 标签继续按 `LS_AS_BARCODE.JZ_FLAG=1` 显示；行文字红色由 `LS_AS_REPORT.assaypat_type=0` 或 `JZ_FLAG=1` 触发，报告级危急状态按 `assaypat_type=9` 判断。
- **常规报告急诊展示**：左侧标本信息区的 `急诊` 标签改为红色，并在前方新增勾选框；当当前右侧选中报告为 `assaypat_type=0` 或 `JZ_FLAG=1` 时自动勾选。
- **常规报告报告统计**：右侧顶部第二行显示 `危急报告数`、`危急报告已审`、`急诊报告数` 和 `急诊报告已审`；危急报告按 `assaypat_type=9` 统计，急诊报告按 `assaypat_type=0` 或 `JZ_FLAG=1` 合并统计，已审均要求已审核且已发送。
- **LabelPrint 来源选择**：`lis.ps1` 新增 `-LabelPrintSource github|local|package`，正式打包可默认从 GitHub Release 下载 LabelPrint，本地联调可显式使用本地源码，已有解压包可继续通过 `-LabelPrintPackagePath` 使用。
- **LabelPrint 跟进**：GitHub Actions 和打包文档改为引用 LabelPrint `v1.2.9` release 包。
- 版本号 v2026.06.06。

## v2026.05.25.2

- **自动更新基础设施**：新增自动更新设计文档、`update_core` 基础库和独立 `Updater.exe` target；构建脚本会同时构建主程序和更新器，NSIS 安装包会随主程序安装/卸载 `Updater.exe`。首版更新器支持 zip 包和已展开目录两种输入，可解压、备份、替换、失败回滚和重启主程序。
- **自动更新源**：`update_core` 新增 `FolderUpdateSource`、`HttpUpdateSource` 和统一 `check_and_fetch_update` 流程，两个通道共用 manifest 解析、版本比较、更新包拉取和 SHA-256 校验。
- **自动更新主程序集成**：系统设置页新增 `[Update]` 更新源配置，菜单栏 `系统 -> 检查更新` 支持后台按共享文件夹或 HTTP manifest 检查新版本、缓存更新包并校验 size / SHA-256；发现新版本后可确认安装，主程序会启动 `Updater.exe` 并退出。
- **自动更新发布产物**：GitHub Actions 新增更新 zip 和 `manifest.json` artifact，更新 zip 与 manifest 采用共享目录、HTTP 和 GitHub Release 共用的同目录结构；推送与版本号一致的 `v*` 标签时会自动创建/更新 GitHub Release。
- **自动更新配置体验**：系统设置页更新源默认选择共享文件夹，切换更新源时只显示共享目录或 HTTP 地址其中一项；HTTP manifest 地址为空时默认使用 GitHub `releases/latest/download/manifest.json`，客户端不再需要随版本号修改更新地址。
- **自动检查更新**：系统设置页新增自动检查开关；开启后主程序启动会延迟检查 manifest，每天最多一次，只提示新版本，用户确认后再进入下载和安装流程。
- **更新日志落盘**：`Updater.exe` 每次运行都会自动写入安装目录 `log\updater.log`，方便现场更新失败后回传分析。
- **构建脚本整理**：新增根目录 `lis.ps1` 和 `scripts/lis.ps1`，封装 `build / clean / run / package / rebuild-package` 常用命令。
- **常规报告布局优化**：左侧区域宽度当前限制为 360 逻辑像素，内容区始终预留垂直滚动条宽度，避免有无滚动条时宽度跳变。
- **常规报告年龄显示**：左侧年龄回填会拆分年龄数字和单位，单位下拉固定提供 `岁 / 月 / 天 / 小时 / 分` 并按查询结果自动匹配。
- **常规报告条码打印**：右键 `打印条码` 和 `打印勾选条码` 的组合项目内容改为取右侧报告行 `检验仪器` 列，不再为了条码打印额外查询中间项目明细；开单日期改为 `yyyy/M/d` 仅日期格式。
- **LabelPrint 跟进**：GitHub Actions 和打包文档改为引用 LabelPrint release 包，继续跟进 XP-360B 条码布局和文本居中修正。
- 版本号 v2026.05.25.2。

## v2026.05.20

- **项目改名**：对外项目名切换为 `lis-workbench`，用户可见程序名切换为 `LIS 工作台`；CMake project 改为 `lis_workbench`，主程序输出改为 `lis_workbench.exe`，NSIS 安装包脚本改为 `LISWorkbench.nsi`，默认安装目录和安装包名改为 `LISWorkbench` / `LISWorkbench-Setup.exe`。为兼容既有部署，独立检验查询工具 `result_search.exe` 和 `search_*` 内部命名暂时保留。
- **配置文件改名**：主配置文件改为 `ClientConfig.ini`；Win32 和 Qt 入口都会在新文件不存在且旧 `result_search.ini` 存在时复制迁移，安装包也会优先从旧配置复制生成新配置。Win32 模块配置中的非 ASCII 值会以 ASCII 安全编码保存并在读取时自动还原，避免中文打印机名、快捷检验仪器名受系统 ANSI 代码页影响后乱码。
- **模块系统**：`ModuleContext` + `ModuleDef` 统一接口；`g_modules[]` 注册表自动菜单+自动分发；`save/load_module_int` 分区配置；设置表单提取为 `settings_module.cpp`；占位模块预留扩展点。新增模块只需写一个 `.cpp` + 注册一行。
- **MDI 单实例窗口**：`module_registry.h` 提供 `activate_existing_mdi_child_by_title`，检验结果查询、输血结果查询、系统设置均复用该入口；重复点击菜单时激活已打开窗口。
- **检验结果查询接入**：`query_module.cpp` — 每实例独立 `QueryState`，复用全部查询/趋势逻辑，复用通用拖条控件并持久化分割器位置到 INI。
- **输血结果查询接入**：`blood_module.cpp` 接入 `LS_XK_BloodRequestApply` / `LS_XK_BloodRequestApplySon` 只读查询；支持默认最近 7 天自动查询、病人编号/姓名/申请单号/申请状态/申请日期筛选，`申请状态` 按 `ApplyForm_Statue` 中文值过滤。
- **已签收条码查询接入**：工具菜单 `已签收条码查询` 替换原工具占位页，新增 `barcode_module.cpp` 和 `query_barcodes` 只读查询，按 `LS_AS_BARCODE` 支持日期、条形码、姓名、病人号、上机状态、专业组和取消签收状态筛选；上机状态筛选和列表展示均基于 `OPER_STATE`。
- **常规报告接入**：工具菜单 `常规报告` 替换原 `工具2` 占位页，新增 `regular_report_module.cpp`，按 `temp/模版2.png` 基本完成左侧标本/病人/验单信息、中间检验结果列表、右侧信息列表和底部功能按钮区；支持按检验日期和检验仪器查询 `LS_AS_REPORT`，右侧选中报告后回填左侧信息并通过 `REP_NO` 查询中间项目明细，左侧可输入控件支持按视觉上下顺序 Tab 跳转；中间 `组合项目` 按 `LS_AS_REPENTRY.GROUP_CODE -> LS_AS_LABMATCH.GROUP_NAME` 显示，优先未删除启用名称并用同组非空名称兜底，连续相同组合项目只显示首行，中间 `结果` 列保持白底、其他列浅灰底，结果单元格支持界面内临时编辑且不写数据库，中间 `图象` 页签按 `REP_NO` 显示 `LS_AS_ITEMPICTURE.PICTURE`；底部 `图形(T)` 可打开独立结果图窗口，跟随右侧选中报告刷新，使用项目图标、双缓冲绘制并保存窗口尺寸；右侧信息列表新增 `标签` 列并按 `LS_AS_BARCODE.JZ_FLAG=1` 显示 `急`，支持表头本地排序并保留选中/勾选状态，上方箭头可快速选中第一行/最后一行，底部 `上一个 / 下一个` 可切换当前选中报告的相邻行，下方 `今天 / 前一天 / 后一天` 可快捷切换检验日期，`自动刷新` 可按默认 10 秒间隔定时刷新且避免查询叠加，左侧样本号回车可定位右侧对应报告行，中间/右侧列表失焦后仍保持当前选中行高亮；底部 `1/2/3` 可按系统设置快速切换检验仪器，并在当前页面匹配对应快捷仪器时用 `[1] / [2] / [3]` 标记按钮文字，检验仪器弹窗失焦时延迟关闭以保持主窗口点击激活体验，底部 `刷新(F5)`、自动刷新、重复选择当前检验日期、点击 `今天` 和重复点击当前快捷仪器会保留现有列表、选中行、勾选行和滚动位置，行顺序未变时只更新变化单元格，无可选中报告时会清空左侧详情、中间结果和图像状态；右侧顶部样本数/上机数/审核数/发送数按当前列表动态统计；主工具栏新增 `常规报告` 快捷入口；右侧报告行新增右键菜单，`打印条码` / `打印勾选条码` 对接外部 `LabelPrint` 项目，使用中间项目明细的组合项目内容填充条码标签，并通过 LabelPrint `printMedicalLabel` 统一入口自动选择 XP-360B TSPL 位图或 Zebra ZD888 ZPL 路径；构建时优先 `find_package(LabelPrint 1.2)`，找不到再回退 `LIS_LABELPRINT_DIR` 源码接入；系统设置页新增常规报告条码打印机和快捷检验仪器选择入口；中间/右侧拖条复用 `search_splitter` 并保存到 `[RegularReport] SplitterX`；左侧滚动区域用自绘分组边框/标题替代真实 `GROUPBOX`，内部控件启用 `WS_CLIPSIBLINGS`；右侧顶部摘要改为父面板自绘并按宽度换行，以减少快速拖动和滚动时的残影。
- **LIS 摘要项目代码配置化**：系统设置新增 ABO、RhD、Hb、PLT 项目代码配置，保存到 `ClientConfig.ini` 的 `[LisSummary]`，输血检验结果窗口据此显示最近血型鉴定和血常规摘要。
- **LIS 查询体验优化**：输血检验结果窗口中，组合项目列表查询和最近血型/血常规摘要查询拆分为两个独立后台任务；列表先返回先展示，摘要完成后再单独刷新，避免慢摘要阻塞列表显示。
- **字体设置联动**：主程序按系统设置字号创建模块字体；菜单栏及子菜单、输血结果查询窗口和 LIS 检验信息弹窗会随设置页字号保存后同步刷新，底部状态栏保持系统默认字体。
- **安装包运行库与 Win7 兼容**：Win32 目标固定 `WINVER/_WIN32_WINNT=0x0601`，Win10 DPI API 改为动态探测并回退；配置文件迁移改用 Win32 文件 API，减少 `std::filesystem` 带来的新系统入口依赖。`scripts/build_main.ps1` 默认优先已安装的 VS 2022，未安装时自动退到 VS 2026，并开启 `LIS_STATIC_MSVC_RUNTIME=ON`；NSIS 安装时会清理旧包残留的 CRT/UCRT DLL。面向 Windows 7 打包时仍需安装 VS 2022 Build Tools，且不建议携带 VS 2026 CRT DLL，避免 `CreateFile2` / `GetSystemTimePreciseAsFileTime` 等入口点缺失。
- **GitHub Actions 安装包**：CI 改为当前 `LISWorkbench` 主程序专用构建，下载 LabelPrint `v1.2.0` Win7 兼容 release 包，使用 VS2022 构建并通过 NSIS 输出 `LISWorkbench-Setup-<version>-win7-win11.exe`，弥补本地缺少完整 Windows 7-11 打包环境的问题。
- 版本号 v2026.05.20。

## v2026.05.06

- **主程序壳**：新建 `lis_workbench.exe`（Win32），全屏平台窗口。
- 菜单栏：检验管理 + 工具 + 系统（含关于对话框）。
- **MenuToolbar 组件**：自绘菜单风格工具栏，键盘导航、分隔线、禁用态、拉伸占位、图标支持。
- 状态栏：系统字体 + 百分比宽度，本机 IP + 实时时钟。
- `result_search.exe` 构建隔离；`scripts/build_main.ps1` 一键构建。
- `MAIN_APP_PLAN.md`：开发计划含模块接口、Qt 迁移预留。
- **MDI 客户区**：完整 MDI 子窗口容器，窗口菜单（层叠/平铺/排列/关闭）+ 子窗口默认最大化 + Ctrl+F6/F4 键盘导航。
- `DefFrameProcW` 替代 `DefWindowProcW`，`TranslateMDISysAccel` 处理 MDI 快捷键。
- 工具栏"关闭"按钮接入 MDI，关闭活动子窗口。
- 所有菜单项创建 MDI 占位子窗口（阶段 2 将替换为真实模块）。

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
- QSettings 使用绝对 exe 目录路径，与 Win32 版共享 `ClientConfig.ini`。

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
