# 检验结果查询复刻设计说明

本文档记录 `lis-workbench` 当前查询逻辑和截图界面字段的对应关系。

## 当前查询链路

当前程序直接查询 LIS 数据库。报告主列表以 `LS_AS_REPORT` 为主表，右侧项目明细通过 `REP_NO` 联查 `LS_AS_REPENTRY`：

```text
LS_AS_REPORT  -- REP_NO -->  LS_AS_REPENTRY
```

截至目前已确认的辅助关系：

```text
LS_AS_REPORT.TYPE       = LS_AS_PATTYPE.TYPE
LS_AS_REPORT.SEX        = LS_AS_SEX.SEX_CODE
LS_AS_REPORT.OPER_CODE  = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID
LS_AS_REPORT.REP_OPER   = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID
LS_AS_REPORT.TXM_NO     = LS_AS_BARCODE.BARCODE
LS_AS_REPORT.ROOM_CODE  = LS_AS_ROOM.ROOM_CODE
LS_AS_REPORT.MACH_CODE  = LS_AS_MACHINE.MACH_CODE
LS_AS_REPENTRY.ITEM_CODE = LS_AS_ITEM.ITEM_CODE
```

说明：

- `LS_AS_REPORT` 是报告主表，负责中间报告列表。
- `LS_AS_REPENTRY` 是项目明细表，负责右侧项目结果。
- `JC_EMPLOYEE_PROPERTY` 是本项目当前确认的人员字典表，用于把 `OPER_CODE` 转为检验者姓名。
- `LS_AS_BARCODE` 用于按“病人号”筛选报告，关联键为 `BARCODE = TXM_NO`。
- `LS_AS_RESULTP.EditName` 覆盖率较低，当前不再作为“检验者”主来源。

流程：

1. 左侧输入查询条件。
2. 点击 `查询(&Q)`。
3. 程序按条件查询 `LS_AS_REPORT`，展示中间报告列表。
4. 选中某条报告后，用该行 `REP_NO` 查询 `LS_AS_REPENTRY`，展示右侧项目结果。

数据库连接信息通过主界面底部 `设置` 按钮维护，并保存到程序同目录 `ClientConfig.ini`。

设置页当前只保留 `初始数据库`，它直接用于生成查询连接串里的 `Initial Catalog`。

连接串格式：

```text
packet size=4096;user id=...;password=...;data source=...;persist security info=True;initial catalog=...
```

底层再自动尝试转换为以下 ODBC 驱动候选：

- `ODBC Driver 18 for SQL Server`
- `ODBC Driver 17 for SQL Server`
- `SQL Server`

## 报告列表字段

当前报告列表以 `LS_AS_REPORT` 为主表，部分列通过字典表显示名称：

| 界面列 | 数据库字段 |
| --- | --- |
| 样本号 | `OPER_NO` |
| 姓名 | `NAME` |
| 条码号 | `TXM_NO` |
| 上机时间 | `CHK_DATE` |
| 性别 | `LS_AS_REPORT.SEX = LS_AS_SEX.SEX_CODE`，显示 `LS_AS_SEX.SEX_NAME` |
| 年龄 | `AGE` |
| 床号 | `BED_CODE` |
| 病人类型 | `LS_AS_REPORT.TYPE = LS_AS_PATTYPE.TYPE`，显示 `LS_AS_PATTYPE.TYPE_NAME` |
| 检验者 | `LS_AS_REPORT.OPER_CODE = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID`，显示 `JC_EMPLOYEE_PROPERTY.NAME` |
| 审核者 | `LS_AS_REPORT.REP_OPER = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID`，显示 `JC_EMPLOYEE_PROPERTY.NAME` |
| 项目名称 | `GROUP_NO` |
| 审核 | `CONF` |
| 确认 | `CHK_FLAG` |

## 项目明细字段

当前项目明细以 `LS_AS_REPENTRY` 为主表，项目字典来自 `LS_AS_ITEM`：

| 界面列 | 数据库字段 |
| --- | --- |
| 项目名称 | `LS_AS_REPENTRY.ITEM_CODE = LS_AS_ITEM.ITEM_CODE`，优先显示 `LS_AS_ITEM.ITEM_NAME` |
| 结果 | `RESULT` |
| 下限 | `DOWNBOUND` |
| 上限 | `UPBOUND` |
| 单位 | `LS_AS_ITEM.UNIT` |
| 英文名称 | `LS_AS_ITEM.ENG_NAME` |

## 当前支持的筛选条件

| 界面输入 | 查询字段 |
| --- | --- |
| 诊疗卡号 | `LS_AS_REPORT.REG_NO` |
| 条码号 | `TXM_NO` |
| 病人姓名 | `NAME LIKE '%输入%'` |
| 病人号 | `LS_AS_BARCODE.REG_NO`，并通过 `LS_AS_BARCODE.BARCODE = LS_AS_REPORT.TXM_NO` 关联到报告 |
| 样本号 | `OPER_NO` |
| 开始日期 | `CHK_DATE >= 开始日期` |
| 结束日期 | `CHK_DATE < 结束日期 + 1天` |
| 检验科室 | `ROOM_CODE` |
| 仪器 | `MACH_CODE` |
| 组合项目 | `GROUP_CODE` |
| 项目代码 | `EXISTS LS_AS_REPENTRY.ITEM_CODE` |

## 已确认的数据表作用

| 表名 | 当前用途 | 关键字段 |
| --- | --- | --- |
| `LS_AS_REPORT` | 报告主表，中间报告列表主来源 | `REP_NO`, `TXM_NO`, `REG_NO`, `OPER_NO`, `NAME`, `SEX`, `TYPE`, `ROOM_CODE`, `MACH_CODE`, `GROUP_CODE`, `OPER_CODE`, `REP_OPER`, `REP_DATE`, `CHK_DATE`, `CONF`, `CHK_FLAG`, `ZYMZ_PRINT`, `ZZJ_PRINT` |
| `LS_AS_REPENTRY` | 报告项目明细，右侧结果列表主来源 | `REP_NO`, `ITEM_CODE`, `RESULT`, `UPBOUND`, `DOWNBOUND`, `NORMAL` |
| `LS_AS_ITEM` | 项目字典，用于右侧项目名称、单位、英文名 | `ITEM_CODE`, `ITEM_NAME`, `UNIT`, `ENG_NAME` |
| `LS_AS_PATTYPE` | 病人类型字典 | `TYPE`, `TYPE_NAME` |
| `LS_AS_SEX` | 性别字典 | `SEX_CODE`, `SEX_NAME` |
| `LS_AS_ROOM` | 检验科室字典 | `ROOM_CODE`, `ROOM_NAME` |
| `LS_AS_MACHINE` | 检验仪器字典，筛选 `RUL='启用'` | `MACH_CODE`, `MACH_NAME`, `ROOM_CODE`, `RUL` |
| `LS_AS_BARCODE` | 条码/病人号关联表，用于“病人号”和已签收条码查询 | `BARCODE`, `REG_NO`, `OPER_STATE`, `CANCEL_DATE`, `CANCEL_OPER` |
| `JC_EMPLOYEE_PROPERTY` | 人员字典，用于“检验者”和“审核者”显示 | `EMPLOYEE_ID`, `NAME`, `D_CODE`, `YS_CODE`, `TYPENAME` |
| `LS_AS_RESULTP` | 原候选检验者来源，但实测覆盖率低，当前不作为主来源 | `REP_NO`, `EditName`, `ChkNAME`, `TXM_NO` |

## 已签收条码查询

工具菜单中的 `已签收条码查询` 以 `LS_AS_BARCODE` 为主表做只读检索，不自动执行查询，等待用户点击 `查询` 或 `刷新`。当前仅开放查询能力，取消签收、取消医嘱签收、取消原因限制、导出 Excel 按钮保持禁用，不执行数据库修改。

### 查询条件

| 界面输入 | 查询字段 / 规则 |
| --- | --- |
| 日期类型：申请日期 | `LS_AS_BARCODE.REQ_TIME` |
| 日期类型：签收日期 | `LS_AS_BARCODE.IN_DATE` |
| 日期类型：上机日期 | `LS_AS_BARCODE.IN_DATE` |
| 条形码 | `LS_AS_BARCODE.BARCODE LIKE` |
| 姓名 | `LS_AS_BARCODE.NAME LIKE` |
| 病人号 | `LS_AS_BARCODE.REG_NO LIKE` |
| 专业组 | `LS_AS_BARCODE.ROOM_CODE`，下拉来源 `LS_AS_ROOM` |
| 未取消签收 | `LS_AS_BARCODE.CANCEL_DATE IS NULL` |
| 取消签收 | `LS_AS_BARCODE.CANCEL_DATE IS NOT NULL` |
| 已签收未上机 | `LS_AS_BARCODE.OPER_STATE = 0` |
| 已上机未审核 | `LS_AS_BARCODE.OPER_STATE = 1` |
| 审核完成 | `LS_AS_BARCODE.OPER_STATE = 2` |
| 发送完成 | `LS_AS_BARCODE.OPER_STATE = 3` |
| 已审核未发送 | 暂时空置，查询条件为 `1=0` |

上机状态直接读取 `LS_AS_BARCODE.OPER_STATE`，不再通过 `LS_AS_REPORT` 的审核/发送字段推导。

### 列表字段

| 界面列 | 数据库字段 / 规则 |
| --- | --- |
| 样本号 | `LS_AS_REPORT.OPER_NO`，通过 `TXM_NO = BARCODE` 取最近一条非空值 |
| 急诊 | `LS_AS_BARCODE.JZ_FLAG` |
| 条形码 | `LS_AS_BARCODE.BARCODE` |
| 病人号 | `LS_AS_BARCODE.REG_NO` |
| 类型 | `LS_AS_BARCODE.TYPENAME` |
| 姓名 | `LS_AS_BARCODE.NAME` |
| 性别 | `LS_AS_BARCODE.SEX` |
| 申请科室 | `LS_AS_BARCODE.DEPT_NAME` |
| 床号 | `LS_AS_BARCODE.BEDNO` |
| 签收人 | `LS_AS_BARCODE.OPER_CODE` |
| 签收时间 | `LS_AS_BARCODE.IN_DATE` |
| 医嘱内容 | `LS_AS_BARCODE.ORDER_TEXT` |
| 标本 | `LS_AS_BARCODE.SAMP_NAME` |
| 费用 | `LS_AS_BARCODE.FY` |
| 申请医生 | `LS_AS_BARCODE.REQ_DRN` |
| 状态 | `LS_AS_BARCODE.ZT_FLAG` |
| 备注 | `LS_AS_BARCODE.NOTE` |
| 原因 | `LS_AS_BARCODE.REASON` |
| 送检 | `LS_AS_BARCODE.sjyq_qsr` |
| 送检时间 | 优先 `LS_AS_BARCODE.COLLECTION_TIME`，为空时回退 `SUB_DATE` |
| 申请时间 | `LS_AS_BARCODE.REQ_TIME` |
| 取消时间 | `LS_AS_BARCODE.CANCEL_DATE` |
| 取消人 | `LS_AS_BARCODE.CANCEL_OPER` |
| HZID | `LS_AS_BARCODE.HZID` |
| 上机状态 | `LS_AS_BARCODE.OPER_STATE`，列表显示 `0=未上机`、`1=已上机`、`2=审核完成`、`3=发送完成`；筛选下拉仍保留 `已审核未发送`，但该项暂时不关联实际状态 |

列表不合并同一条形码的多条记录，保持 `LS_AS_BARCODE` 查询结果一行对应一行，避免因聚合造成现场查询变慢。

## 常规报告

工具菜单中的 `常规报告` 以三栏工作台形式复用检验结果查询的数据链路。页面打开时不自动查询；用户先选择左侧 `检验仪器`，再按左侧 `检验日期` 查询当天该仪器下的报告主记录。

### 查询入口

| 界面输入 | 查询字段 / 规则 |
| --- | --- |
| 检验日期 | `LS_AS_REPORT.CHK_DATE >= 日期` 且 `< 日期 + 1 天` |
| 检验仪器 | 弹窗来源 `LS_AS_ROOM` / `LS_AS_MACHINE`，选定后以 `LS_AS_REPORT.MACH_CODE` 过滤 |

系统设置页可配置常规报告底部 `1 / 2 / 3` 快捷检验仪器，保存到 `[RegularReport] QuickMachine*Code / QuickMachine*Name / QuickMachine*RoomCode`。配置弹窗复用 `LS_AS_ROOM / LS_AS_MACHINE` 数据源；点击快捷按钮后只更新当前页的 `检验仪器` 条件，并按当前 `检验日期` 重新查询右侧报告列表。

### 右侧信息列表

右侧信息列表以 `LS_AS_REPORT` 为主表，默认按 `OPER_NO` 升序展示。当前优先保存并使用 `LS_AS_REPORT.ID` 作为行唯一标识；界面选中行后再用该行 `REP_NO` 查询项目明细。

| 界面列 | 数据库字段 / 规则 |
| --- | --- |
| 标签 | `LS_AS_REPORT.TXM_NO = LS_AS_BARCODE.BARCODE` 后取 `JZ_FLAG`；`1` 显示 `急`，`0` 不显示 |
| 样本号 | `LS_AS_REPORT.OPER_NO` |
| 姓名 | `LS_AS_REPORT.NAME` |
| 性别 | `LS_AS_REPORT.SEX = LS_AS_SEX.SEX_CODE`，显示 `SEX_NAME` |
| 年龄 | `LS_AS_REPORT.AGE` |
| 医嘱内容 | `LS_AS_REPORT.TXM_NO = LS_AS_BARCODE.BARCODE` 后聚合 `ORDER_TEXT`，多行用 `/` 分隔 |
| 科室代码 | `LS_AS_REPORT.TXM_NO = LS_AS_BARCODE.BARCODE` 后取 `DEPT_NAME` |
| 床号 | `LS_AS_REPORT.BED_CODE` |
| 打印 | `LS_AS_REPORT.ZYMZ_PRINT` |
| 病人类型 | `LS_AS_REPORT.TYPE = LS_AS_PATTYPE.TYPE`，显示 `TYPE_NAME` |
| 检验者 | `LS_AS_REPORT.OPER_CODE = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID` |
| 项目名称 | `LS_AS_REPORT.GROUP_NO` |
| 验单号 | `LS_AS_REPORT.REP_NO` |
| 审核 / 确认 | `CHK_FLAG` / `CONF` |
| 条形码 | `LS_AS_REPORT.TXM_NO` |
| 检验仪器 | 当前显示 `GROUP_NO`，后续可按实际字段再调整 |
| 标本 | `LS_AS_REPORT.SAMP_CODE = LS_AS_SAMPLE.SAMP_CODE`，显示 `SAMP_NAME` |
| 备注 | `LS_AS_REPORT.NOTE` |
| 开单日期 | `LS_AS_REPORT.REP_DATE` |
| 签收时间 | `LS_AS_BARCODE.IN_DATE` |
| 检验日期 | `LS_AS_REPORT.CHK_DATE` |
| 报告时间 | `LS_AS_REPORT.REP_TIME` |
| 费用 | `LS_AS_REPORT.FY` |
| 医生代号 | `LS_AS_REPORT.REQ_DR = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID`，查不到姓名时显示为空 |
| 临床诊断 | `LS_AS_REPORT.DIAG_NAME` |
| 病人号 | `LS_AS_REPORT.REG_NO` |
| 上机时间 | `LS_AS_REPORT.CREATE_TIME` |
| 电话 | `LS_AS_REPORT.PAT_PHONE` |

行背景色规则：`CONF='S'` 优先显示深绿色；否则 `CHK_FLAG='T'` 显示蓝色；未审核保持默认背景色。若 `JZ_FLAG='1'`，该行文字显示为红色。

右侧顶部第一行摘要只统计当前已加载到列表中的内存数据，不额外访问数据库：`样本数` 统计 `REP_NO` 非空行，`上机数` 统计 `NAME` 非空行，`审核数` 统计 `CHK_FLAG='T'` 行，`发送数` 统计 `CONF='S'` 行。

点击右侧信息列表表头时，只对内存中的 `st->reportRows` 做 `std::stable_sort` 并重绘列表，不重新访问数据库。排序前会记录当前选中行的 `LS_AS_REPORT.ID` 和已勾选行的 ID，排序后再恢复选中和勾选状态。

右侧信息列表上方的 `⇧` / `⇩` 按钮只在当前内存列表内跳转，分别选中第一行和最后一行，不访问数据库。右侧信息列表下方的 `今天 / 前一天 / 后一天` 会先更新左侧 `检验日期`，再按当前 `检验仪器` 重新发起报告主列表查询；其后的 `自动刷新` 默认不启用，勾选后按秒数输入启动窗口定时器，默认 10 秒，输入值按 5-3600 秒夹取，且若上一轮报告主列表查询尚未完成则跳过本次定时触发，避免堆积数据库查询。页面底部 `刷新(F5)` 会读取当前左侧 `检验仪器` 和 `检验日期`，重新发起报告主列表查询并刷新右侧列表。

左侧 `样本号` 输入框按回车时，只在当前已加载的 `st->reportRows` 内按 `OPER_NO` 定位；匹配到后选中右侧对应行，并复用现有选中行联动逻辑刷新左侧信息和中间项目明细。

右侧信息列表第一列启用勾选框。右键某一行会弹出报告操作菜单：

- `打印条码`：打印当前右键行。
- `打印勾选条码`：按当前列表顺序打印所有勾选行；如果中途失败，会停止后续打印并提示已发送数量和失败记录。

打印会把对应行字段填入外部 `LabelPrint` 项目的 `MedicalLabelData`，再调用 `printMedicalLabel` 统一入口发送 RAW 打印任务。打印机名读取 `ClientConfig.ini` 的 `[RegularReport] BarcodePrinterName`，默认值为 `Xprinter XP-360B #2`，并以宽字符形式传给 LabelPrint，避免中文打印机名经过 ANSI 转换后失效。LabelPrint 内部会读取 Windows 打印机元数据，自动选择 XP-360B 的 TSPL 位图路径或 Zebra ZD888 的 ZPL 路径；无法识别时按 XP-360B 兼容路径兜底。打印数据中的样本号、条码号、姓名、标本、开单日期、科室代码、病人号来自右侧报告行；条码上的组合项目不使用右侧列表的项目名称，而是按该报告 `REP_NO` 读取中间项目明细 `ResultRow.group_name`，去重后用 `/` 拼接。若当前中间列表已经加载同一报告，则直接复用内存明细；否则打印前按 `REP_NO` 临时查询项目明细。

如果保存的打印机名因为 Windows 重命名、换电脑或驱动重装而失效，右键打印会提示失败原因和当前打印机名。用户需要到 `系统设置` 页重新选择常规报告条码打印机并保存。

构建时主项目优先通过 `find_package(LabelPrint 1.2 CONFIG QUIET)` 查找 `LabelPrint::labelprint`。正式打包建议使用 `scripts/build_main.ps1 -LabelPrintPackagePath` 指向 LabelPrint release zip 解压目录，使构建可复现且不依赖本机相邻源码目录。如果未找到已安装或已解压的 LabelPrint CMake package，或版本低于统一打印入口所需版本，则回退到 CMake 变量 `LIS_LABELPRINT_DIR` 指定的源码目录，默认路径为 `../../020 LabelPrint/LabelPrint`，并以 `add_subdirectory` 接入。两种方式都找不到时，常规报告仍可使用查询功能，但右键 `打印条码` 会提示打印功能未启用。

条码模板当前使用以下字段：

- 样本号
- 组合项目
- 条码号
- 姓名
- 标本
- 开单日期
- 科室代码
- 病人号

### 左侧和中间联动

右侧选中某一行后：

- 左侧 `标本信息 / 病人信息 / 验单信息` 从当前选中行回填。
- `申请日期 / 签收时间 / 上机时间 / 报告时间` 是只读展示日期控件，由程序写入，用户不能手动修改。
- `检验日期` 仍是查询条件；程序回填选中行检验日期时会屏蔽自动查询，避免选中行触发重复查询。
- 中间检验结果列表通过当前行 `REP_NO` 查询 `LS_AS_REPENTRY`，并复用 `LS_AS_ITEM` 字典、参考区间和 `NORMAL` 偏差显示规则。
- 中间列表的 `组合项目` 列不再直接使用右侧报告行项目名，而是按当前明细行 `LS_AS_REPENTRY.GROUP_CODE` 关联 `LS_AS_LABMATCH.GROUP_CODE`，再取 `LS_AS_LABMATCH.GROUP_NAME`。取名时优先使用 `DELETE_BIT=0 且 USE_FLAG=0` 的非空名称；若缺失，则取同一 `GROUP_CODE` 下任意非空名称作为兜底。
- 中间列表查询排序加入 `GROUP_CODE / ITEM_CODE / ID`，让相同组合项目尽量连续展示；界面展示时连续相同的组合项目名只显示第一行，其余行显示为空。
- 中间 `图象` 页签打开时，才通过当前行 `REP_NO` 查询 `LS_AS_ITEMPICTURE.PICTURE`。查询只取 `DELETE_BIT=0` 且图片非空的第一张记录，按 `PIC_NO, ID` 排序；若无图片则保持空白，有图片时用 GDI+ 在左上角固定大图层内按比例绘制，外层视口提供横向/纵向滚动条，不随拖条宽度变化而缩放。

## 当前未实现的截图功能

- 历史库 / 当前库切换。
- 取消发送。
- 导出、预览、打印。
- 科室、医生、病人类型、报告状态下拉字典。
- 行颜色规则。
- 勾选多条记录后的批量操作。
- 报告状态中文转换，例如 `已审核`、`已发送`。

## 后续建议

优先补齐：

1. 将 `CONF`、`CHK_FLAG`、`TYPE` 转换成现场软件一致的中文状态。
2. 按截图增加行颜色规则和选中行效果。
3. 导出当前报告列表为 CSV 或 Excel。
4. 若要做到完全复刻，再继续反查原系统里项目名称、医生、科室等字典表。
