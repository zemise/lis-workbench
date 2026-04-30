# 检验结果查询复刻设计说明

本文档记录 `cpp_search` 当前查询逻辑和截图界面字段的对应关系。

## 当前查询链路

当前程序直接查询 LIS 数据库：

```text
LS_AS_REPORT  -- REP_NO -->  LS_AS_REPENTRY
```

流程：

1. 左侧输入查询条件。
2. 点击 `查询(&Q)`。
3. 程序按条件查询 `LS_AS_REPORT`，展示中间报告列表。
4. 选中某条报告后，用该行 `REP_NO` 查询 `LS_AS_REPENTRY`，展示右侧项目结果。

数据库连接信息通过主界面底部 `设置` 按钮维护，并保存到程序同目录 `result_search.ini`。

设置页当前只保留 `初始数据库`，它直接用于生成查询连接串里的 `Initial Catalog`。

连接串格式参照 `cpp_rapidp`：

```text
packet size=4096;user id=...;password=...;data source=...;persist security info=True;initial catalog=...
```

底层再自动尝试转换为以下 ODBC 驱动候选：

- `ODBC Driver 18 for SQL Server`
- `ODBC Driver 17 for SQL Server`
- `SQL Server`

## 报告列表字段

当前报告列表字段来自 `LS_AS_REPORT`：

| 界面列 | 数据库字段 |
| --- | --- |
| 样本号 | `OPER_NO` |
| 姓名 | `NAME` |
| 条码号 | `TXM_NO` |
| 上机时间 | `CHK_DATE` |
| 性别 | `SEX` |
| 年龄 | `AGE` |
| 床号 | `BED_CODE` |
| 病人类型 | `TYPE` |
| 检验者 | `REQ_DR` |
| 项目名称 | `GROUP_NO` |
| 审核 | `CONF` |
| 确认 | `CHK_FLAG` |

## 项目明细字段

当前项目明细字段来自 `LS_AS_REPENTRY`：

| 界面列 | 数据库字段 |
| --- | --- |
| 项目名称 | `ITEM_NAME` |
| 结果 | `RESULT` |
| 下限 | `DOWNBOUND` |
| 上限 | `UPBOUND` |
| 单位 | `ITEM_UNIT` |
| 英文名称 | `ITEM_ENG` |
| 偏 | `NORMAL` |

## 当前支持的筛选条件

| 界面输入 | 查询字段 |
| --- | --- |
| 诊疗卡号 / 病人号 | `REG_NO` |
| 条码号 | `TXM_NO` |
| 病人姓名 | `NAME LIKE '%输入%'` |
| 病床号 | `BED_CODE` |
| 样本号 | `OPER_NO` |
| 开始日期 | `CHK_DATE >= 开始日期` |
| 结束日期 | `CHK_DATE < 结束日期 + 1天` |
| 检验科室 | `ROOM_CODE` |
| 仪器 | `MACH_CODE` |
| 组合项目 | `GROUP_CODE` |
| 项目代码 | `EXISTS LS_AS_REPENTRY.ITEM_CODE` |

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
