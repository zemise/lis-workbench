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
