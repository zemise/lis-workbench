# 急诊样本统计设计文档

本文档记录 `统计分析管理 -> 急诊样本统计` 的当前实现口径。页面主体功能已经完成，后续需求变更以小修小改为主，涉及口径变化时继续同步本文档。

## 当前状态

- 页面入口：`统计分析管理 -> 急诊样本统计(&2)`。
- 页面性质：只读统计页，不写入 LIS 业务表。
- 核心代码：
  - `src/emergency_statistics_module.h`
  - `src/emergency_statistics_module.cpp`
  - `src/search_core.h`
  - `src/search_core.cpp`
- 查询入口：`search::query_emergency_statistics`。
- 统计对象：以唯一 `LS_AS_BARCODE.BARCODE` 为主，不按医嘱行或报告行计数。
- 默认时间：按签收时间统计，控件精确到分钟。
- 默认筛选：勾选 `只看未完成`。

## 功能目标

急诊样本统计的目标是让用户快速关注急诊条码当前是否已经完成检验流程：

- 指定时间段内有多少急诊条码。
- 每个急诊条码目前处于未上机、已上机未审核、审核完成未发送、审核完成已发送或医生已查看等状态。
- 急诊条码从签收到审核已经耗时多久。
- 未审核条码持续动态显示从签收到当前的等待时长。
- 用户可从明细行双击跳转到 `常规报告` 查看对应报告。

## 页面结构

### 筛选区

顶部筛选区包含：

- `签收时间` 开始时间，格式 `yyyy-MM-dd HH:mm`。
- `签收时间` 结束时间，格式 `yyyy-MM-dd HH:mm`。
- `院区`：`全部 / 老院 / 新院`。
- `只看未完成`。
- `查询` 按钮。
- 查询状态文本。

默认时间范围为当天 `00:00` 到 `23:59`。查询结束条件按 `< DATEADD(minute, 1, 结束时间)` 处理，使用户选择的结束分钟被纳入统计。

### 汇总区

上方汇总区使用横向 `ListView`：

- 第一行作为表头，显示各统计指标。
- 第二行显示对应数量。
- 汇总结果基于当前过滤后的明细内存计算。

当前汇总指标：

| 指标 | 口径 |
| --- | --- |
| 急诊条码总数 | 当前筛选结果中唯一急诊条码数 |
| 未完成数 | 报告未发送，即 `LS_AS_REPORT.CONF <> 'S'` 或为空 |
| 未上机数 | 派生状态为 `未上机` |
| 已上机未审核数 | 派生状态为 `已上机未审核` |
| 审核完成数 | 派生状态为审核完成，不含医生已查看 |
| 医生已查看数 | 已审核且条码状态为医生已查看 |
| 报告已发送数 | `LS_AS_REPORT.CONF = 'S'` |
| 条码急诊数 | 同一条码任一有效行 `JZ_FLAG = 1` |
| 报告急诊补充数 | 报告急诊但非双重命中的数量 |
| 双重命中数 | 同时命中条码急诊和报告急诊 |

### 明细区

下方明细区使用 `ListView` 展示急诊条码明细，支持点击表头本地排序。当前列顺序：

| 列名 | 来源/口径 |
| --- | --- |
| 院区 | C++ 派生 |
| 样本号 | `LS_AS_REPORT.OPER_NO` |
| 条码号 | `LS_AS_BARCODE.BARCODE` |
| 急诊来源 | `条码急诊 / 报告急诊 / 报告+条码` |
| 当前状态 | 由报告号、`CHK_FLAG`、`CONF`、`OPER_STATE` 派生 |
| 签收-审核用时 | 软件计算 |
| 签收人 | `LS_AS_BARCODE.OPER_CODE` |
| 病人号 | `LS_AS_BARCODE.REG_NO` |
| 类型 | `LS_AS_BARCODE.TYPENAME` |
| 姓名 | `LS_AS_BARCODE.NAME` |
| 性别 | `LS_AS_BARCODE.SEX` |
| 年龄 | `LS_AS_BARCODE.AGE` |
| 申请科室 | `LS_AS_BARCODE.DEPT_NAME` |
| 床号 | `LS_AS_BARCODE.BEDNO` |
| 医嘱内容 | 同条码多行 `LS_AS_BARCODE.ORDER_TEXT` 去重后拼接 |
| 标本 | `LS_AS_BARCODE.SAMP_NAME` |
| 报告号 | `LS_AS_REPORT.REP_NO` |
| 仪器 | `MACH_CODE` 经 `LS_AS_MACHINE` 字典映射为 `MACH_NAME` |
| 申请时间 | `LS_AS_REPORT.REP_DATE` |
| 签收时间 | 聚合后最早 `LS_AS_BARCODE.IN_DATE` |
| 上机时间 | `LS_AS_REPORT.CREATE_TIME` |
| 报告时间 | `LS_AS_REPORT.REP_TIME` |
| 审核 | `LS_AS_REPORT.CHK_FLAG` |
| 发送 | `LS_AS_REPORT.CONF` |
| 空白列 | 预留显示空间 |

## 急诊口径

急诊样本统计复用 `常规报告` 页右侧红色行的急诊逻辑：

```text
LS_AS_REPORT.assaypat_type = '0'
OR
LS_AS_BARCODE.JZ_FLAG = 1
```

条码级急诊来源：

| 来源 | 判断 |
| --- | --- |
| 条码急诊 | 同一 `BARCODE` 任一有效行 `JZ_FLAG = 1` |
| 报告急诊 | 关联报告 `assaypat_type = '0'` |
| 报告+条码 | 同时满足条码急诊和报告急诊 |

统计数量按唯一条码计算。同一条码存在多条医嘱时，仍只统计为一个急诊条码。

## 时间口径

当前页面默认按签收时间统计：

```text
LS_AS_BARCODE.IN_DATE
```

时间列含义：

| 页面列 | 数据库字段 |
| --- | --- |
| 申请时间 | `LS_AS_REPORT.REP_DATE` |
| 签收时间 | `LS_AS_BARCODE.IN_DATE` |
| 上机时间 | `LS_AS_REPORT.CREATE_TIME` |
| 报告时间 | `LS_AS_REPORT.REP_TIME` |

`签收-审核用时` 不由数据库计算，而由软件根据原始时间字段计算。

## 院区口径

院区筛选不直接下推到 SQL。主查询先按时间范围和急诊口径取数，C++ 聚合后再派生院区并过滤，避免 `sign_dept` 为空时漏掉数据，也避免主 SQL 引入 `OR + LIKE '%滨水%'`。

派生规则：

| 条件 | 院区 |
| --- | --- |
| `LS_AS_BARCODE.sign_dept = 102` | 老院 |
| `LS_AS_BARCODE.sign_dept = 401` | 新院 |
| `sign_dept` 为空或异常，且 `LS_AS_BARCODE.DEPT_NAME` 包含 `滨水` | 新院 |
| 其他情况 | 老院 |

汇总指标只统计院区过滤后的明细。

## 数据字段

### LS_AS_BARCODE

| 字段 | 用途 |
| --- | --- |
| `BARCODE` | 条码号，主统计键 |
| `JZ_FLAG` | 条码级急诊标记 |
| `IN_DATE` | 签收时间 |
| `REQ_TIME` | 申请时间备用口径 |
| `OPER_STATE` | 条码流程状态 |
| `OPER_CODE` | 签收人 |
| `sign_dept` | 院区优先判断字段 |
| `REG_NO` | 病人号 |
| `TYPENAME` | 类型 |
| `NAME` | 姓名 |
| `SEX` | 性别 |
| `AGE` | 年龄 |
| `DEPT_NAME` | 申请科室，同时用于院区兜底判断 |
| `BEDNO` | 床号 |
| `SAMP_NAME` | 标本 |
| `ORDER_TEXT` | 医嘱内容 |
| `DELETE_BIT` | 删除标记，统计要求为 `0` |
| `CANCEL_DATE` | 取消签收时间，当前默认排除非空记录 |

### LS_AS_REPORT

| 字段 | 用途 |
| --- | --- |
| `TXM_NO` | 关联条码号 |
| `REP_NO` | 报告号，也用于判断是否已上机的优先依据 |
| `OPER_NO` | 样本号 |
| `MACH_CODE` | 仪器代码 |
| `ROOM_CODE` | 检验科室代码，跳转常规报告时携带 |
| `CHK_DATE` | 检验日期，跳转常规报告时携带 |
| `REP_DATE` | 申请时间 |
| `CREATE_TIME` | 上机时间 |
| `REP_TIME` | 报告时间/审核时间 |
| `CHK_FLAG` | 审核状态，`T=已审核`、`F=未审核` |
| `CONF` | 发送状态，`S=已发送`、`F=未发送` |
| `assaypat_type` | 报告级急诊标记，`0=急诊` |
| `DELETE_BIT` | 删除标记，统计要求为 `0` |

### LS_AS_MACHINE

主查询不再 `JOIN LS_AS_MACHINE`。查询前先执行轻量字典查询：

```sql
SELECT MACH_CODE, MACH_NAME
FROM LS_AS_MACHINE
WHERE ISNULL(DELETE_BIT,0)=0
```

C++ 端保存为 `std::map<MACH_CODE, MACH_NAME>`，明细填充时用 `mach_code` 查找仪器名称。查不到或名称为空时回退显示 `MACH_CODE`。

## 查询与聚合实现

### 主查询原则

- 以 `LS_AS_BARCODE` 为主表。
- 按签收时间范围缩小数据集。
- 排除删除条码：`ISNULL(b.DELETE_BIT,0)=0`。
- 排除取消签收：`b.CANCEL_DATE IS NULL`。
- 左连接未删除报告：`r.TXM_NO = b.BARCODE AND ISNULL(r.DELETE_BIT,0)=0`。
- 急诊条件命中条码急诊或报告急诊任一来源。
- 不在 SQL 端按 `BARCODE` 聚合。
- 不使用 `MAX(b.ORDER_TEXT)`。
- 不使用 SQL 字符串聚合医嘱。

当前主查询返回 `LS_AS_BARCODE` 行级数据，再由 C++ 按条码聚合。这样可以拿到同条码多医嘱的全部 `ORDER_TEXT`，同时避免数据库端复杂聚合拖慢主查询。

### SQL 形态

核心形态如下，实际代码会根据时间口径拼接日期字段：

```sql
SELECT
    b.BARCODE,
    CASE WHEN ISNULL(b.JZ_FLAG,0)=1 THEN 1 ELSE 0 END AS BARCODE_EMERGENCY,
    CASE WHEN r.assaypat_type='0' THEN 1 ELSE 0 END AS REPORT_EMERGENCY,
    b.IN_DATE,
    r.REP_DATE,
    b.OPER_STATE,
    b.CANCEL_DATE,
    b.REG_NO,
    b.TYPENAME,
    b.NAME,
    b.SEX,
    b.AGE,
    b.DEPT_NAME,
    b.BEDNO,
    b.OPER_CODE,
    b.sign_dept,
    b.SAMP_NAME,
    b.ORDER_TEXT,
    r.REP_NO,
    r.OPER_NO,
    r.MACH_CODE,
    r.CHK_DATE,
    r.ROOM_CODE,
    r.CHK_FLAG,
    r.CONF,
    r.CREATE_TIME,
    r.REP_TIME
FROM LS_AS_BARCODE b WITH (NOLOCK)
LEFT JOIN LS_AS_REPORT r WITH (NOLOCK)
       ON r.TXM_NO = b.BARCODE
      AND ISNULL(r.DELETE_BIT,0)=0
WHERE ISNULL(b.DELETE_BIT,0)=0
  AND NULLIF(LTRIM(RTRIM(b.BARCODE)),'') IS NOT NULL
  AND b.IN_DATE >= @start_time
  AND b.IN_DATE < DATEADD(minute, 1, @end_time)
  AND b.CANCEL_DATE IS NULL
  AND (
      r.assaypat_type='0'
      OR EXISTS (
          SELECT 1
          FROM LS_AS_BARCODE be WITH (NOLOCK)
          WHERE ISNULL(be.DELETE_BIT,0)=0
            AND be.BARCODE=b.BARCODE
            AND ISNULL(be.JZ_FLAG,0)=1
      )
  )
ORDER BY LTRIM(RTRIM(b.BARCODE)), b.ID;
```

### C++ 聚合规则

C++ 端按 `BARCODE` 聚合为唯一条码记录：

| 字段/状态 | 聚合规则 |
| --- | --- |
| 急诊来源 | 条码急诊、报告急诊分别 OR，最终派生中文来源 |
| 签收时间 | 取最早非空 `IN_DATE` |
| 条码原始状态 | 取最低 `OPER_STATE`，用于反映最需要关注的进度 |
| 是否已上机 | 优先看是否取到 `REP_NO`，再参考 `OPER_STATE >= 1` |
| 是否已审核 | 优先看 `CHK_FLAG = 'T'` |
| 是否已发送 | 看 `CONF = 'S'` |
| 医生已查看 | `CHK_FLAG = 'T'` 且 `OPER_STATE = 3` |
| 医嘱内容 | 非空 `ORDER_TEXT` 去重，按查询顺序用 `/` 拼接 |
| 其他展示字段 | 优先保留第一个非空值 |
| 院区 | 聚合后按 `sign_dept` 和 `DEPT_NAME` 派生 |

当前现场判断为：`LS_AS_BARCODE` 同条码可能多行，通常对应 `LS_AS_REPORT` 一行。因此 C++ 端聚合能取得同条码全部医嘱，同时不会显著放大报告字段。

## 状态规则

`LS_AS_BARCODE.OPER_STATE` 已确认含义：

| 值 | 含义 |
| --- | --- |
| `0` | 未上机 |
| `1` | 已上机未审核 |
| `2` | 审核完成 |
| `3` | 医生已查看 |

由于 `OPER_STATE` 可能异步更新，当前页面使用报告链路优先校正状态：

| 派生判断 | 当前状态 |
| --- | --- |
| `CHK_FLAG='T'` 且 `OPER_STATE=3` | 医生已查看 |
| `CHK_FLAG='T'` 且 `CONF='S'` | 审核完成已发送 |
| `CHK_FLAG='T'` 且 `CONF<>'S'` 或为空 | 审核完成未发送 |
| 有 `REP_NO` 或 `OPER_STATE>=1`，且未审核 | 已上机未审核 |
| `OPER_STATE=0` 且无报告号 | 未上机 |
| 取消签收 | 取消签收 |
| 无法判断 | 未知 |

未完成主判断：

```text
LS_AS_REPORT.CONF <> 'S' OR LS_AS_REPORT.CONF IS NULL
```

也就是报告未发送即视为未完成。审核状态主要看 `CHK_FLAG`，发送状态主要看 `CONF`。

## 签收-审核用时

该列由软件计算，不由数据库计算。

| 场景 | 显示规则 |
| --- | --- |
| 已审核 | `LS_AS_REPORT.REP_TIME - LS_AS_BARCODE.IN_DATE`，固定显示 |
| 未审核 | `当前软件时间 - LS_AS_BARCODE.IN_DATE`，每秒动态刷新 |
| 未签收或时间缺失 | `-` |

显示格式：

```text
35 秒
12 分 08 秒
1 小时 12 分 08 秒
```

未审核条码继续动态增长，用于保留紧迫感；页面不再单独设置 `超时` 列。

## 明细行视觉提示

明细行使用状态色辅助识别：

- 未上机、已上机未审核：待处理背景色。
- 审核完成未发送：审核完成提示色。
- 审核完成已发送、报告已发送：完成提示色。
- 文字保留急诊红色提示。
- 选中行优先使用系统选中高亮。

颜色只作为视觉提示，统计口径仍以字段判断为准。

## 双击跳转常规报告

明细行支持双击打开 `常规报告` 并定位对应报告。

跳转参数：

| 参数 | 来源 |
| --- | --- |
| 报告号 | `REP_NO` |
| 样本号 | `OPER_NO` |
| 检验日期 | `CHK_DATE` |
| 仪器代码 | `MACH_CODE` |
| 仪器名称 | 预取字典映射后的 `MACH_NAME` |
| 检验科室 | `ROOM_CODE` |

实现方式：

- 构造 `RegularReportOpenTarget`。
- 打开或激活 `常规报告` 模块。
- 发送 `WM_REGULAR_OPEN_REPORT`。
- 常规报告按目标报告号定位。

如果缺少报告号、仪器或检验日期，说明该条码还不能定位到常规报告，提示：

```text
该条码未上机
```

## 性能设计

当前性能取舍：

- 主查询优先使用签收时间范围缩小 `LS_AS_BARCODE` 数据集。
- 院区不下推 SQL，避免 `sign_dept` 空值漏数，也避免 `OR + LIKE '%滨水%'` 影响主查询。
- 医嘱内容不做 SQL 聚合，改由 C++ 端按条码去重拼接。
- 仪器名称不在主查询中 JOIN，改为一次轻量预查询字典后内存映射。
- 明细填充前调用 `WM_SETREDRAW(FALSE)` 暂停 ListView 重绘，填充完成后 `WM_SETREDRAW(TRUE)` 并统一刷新。
- 排序在内存中完成，不重新查询数据库。
- 未审核等待时长由定时器刷新，不重新访问数据库。

这些设计保证了页面以条码监控为主，同时尽量减少数据库端复杂聚合和多表 JOIN 压力。

## 只读边界

当前页面不提供以下操作：

- 不修改条码状态。
- 不修改报告状态。
- 不补写审核、发送或上机信息。
- 不取消或恢复签收。
- 不写入统计缓存表。

所有状态只从数据库读取并在客户端派生展示。

## 后续小修方向

后续如需继续完善，建议优先考虑低风险功能：

- 增加复制选中行或导出 CSV。
- 增加仪器、申请科室、病人类型筛选。
- 增加“只看未上机”“只看已上机未审核”等快捷筛选。
- 如现场确认一条码多报告，需要补充报告字段的去重拼接或代表报告选择规则。
- 如现场需要纳入取消签收条码，可增加 `包含取消签收` 筛选，而不是改变默认统计口径。
- 如需要固定时限管理，可在现有 `签收-审核用时` 基础上增加阈值筛选或颜色分级。

## 当前结论

`急诊样本统计` 已可基于现有数据库和代码结构实现，不需要修改业务数据库。当前实现以唯一条码为主，按签收时间查询急诊条码，结合 `REP_NO`、`CHK_FLAG`、`CONF` 和 `OPER_STATE` 派生当前状态，以 `CONF='S'` 作为完成主判断，并通过动态 `签收-审核用时` 提醒用户关注尚未完成的急诊条码。
