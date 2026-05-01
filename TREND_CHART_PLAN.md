# 检验结果趋势图开发计划

## 目标

在当前检验结果查询工具中，支持查询某个患者在特定检验仪器下的历史结果，并将同一项目按时间生成趋势图，方便观察项目变化。

第一阶段优先保证数据正确、查询稳定、结果可核对，不追求复杂图表效果。

## 推荐范围

第一阶段只做：

- 单患者。
- 单检验仪器。
- 日期范围。
- 单项目趋势图。
- 趋势数据表格同步展示。
- 只绘制可转换为数值的结果。

暂不做：

- 多患者对比。
- 多仪器混合趋势。
- 多单位项目叠加在同一坐标轴。
- 非数值结果画图。
- 复杂统计分析。

## 数据关系

核心表关系：

```text
LS_AS_REPORT.REP_NO = LS_AS_REPENTRY.REP_NO
LS_AS_REPENTRY.ITEM_CODE = LS_AS_ITEM.ITEM_CODE
LS_AS_REPORT.MACH_CODE = LS_AS_MACHINE.MACH_CODE
```

患者定位沿用当前主界面已有条件：

```text
LS_AS_REPORT.REG_NO
LS_AS_REPORT.TXM_NO
LS_AS_REPORT.NAME
LS_AS_BARCODE.REG_NO
LS_AS_BARCODE.BARCODE = LS_AS_REPORT.TXM_NO
```

趋势时间优先使用：

```text
LS_AS_REPORT.REP_DATE
```

如果现场确认 `REP_DATE` 不稳定，再评估改为 `CHK_DATE`。当前主界面“上机时间”也已经按 `REP_DATE` 展示。

## 趋势数据字段

趋势查询至少返回：

| 字段 | 来源 |
| --- | --- |
| 报告号 | `LS_AS_REPORT.REP_NO` |
| 条码号 | `LS_AS_REPORT.TXM_NO` |
| 样本号 | `LS_AS_REPORT.OPER_NO` |
| 患者姓名 | `LS_AS_REPORT.NAME` |
| 仪器代码 | `LS_AS_REPORT.MACH_CODE` |
| 报告时间 | `LS_AS_REPORT.REP_DATE` |
| 项目代码 | `LS_AS_REPENTRY.ITEM_CODE` |
| 项目名称 | `LS_AS_ITEM.ITEM_NAME` |
| 英文名 | `LS_AS_ITEM.ENG_NAME` |
| 结果原文 | `LS_AS_REPENTRY.RESULT` |
| 数值结果 | 程序从 `RESULT` 转换 |
| 单位 | `LS_AS_ITEM.UNIT` |
| 下限 | `LS_AS_REPENTRY.UPBOUND` |
| 上限 | `LS_AS_REPENTRY.DOWNBOUND` |
| 异常标记 | `LS_AS_REPENTRY.NORMAL` |

注意：当前项目已确认“上限对应 `DOWNBOUND`，下限对应 `UPBOUND`”。

## 查询策略

第一阶段建议新增独立查询函数，不影响现有主查询：

```text
trend_core.h/.cpp
```

建议结构：

```cpp
struct TrendPoint {
    std::string rep_no;
    std::string txm_no;
    std::string oper_no;
    std::string patient_name;
    std::string report_time;
    std::string item_code;
    std::string item_name;
    std::string item_eng;
    std::string result_text;
    double result_value;
    bool has_numeric_value;
    std::string unit;
    std::string lower_bound;
    std::string upper_bound;
    std::string normal;
};
```

SQL 查询原则：

- 必须带日期范围，避免全库扫表。
- 必须带患者条件之一，避免误查大量患者。
- 如果指定仪器，则必须按 `LS_AS_REPORT.MACH_CODE` 过滤。
- 如果指定项目，则按 `LS_AS_REPENTRY.ITEM_CODE` 过滤。
- 排序按 `REP_DATE ASC, REP_NO ASC`。

## UI 方案

第一阶段新增一个独立窗口，不挤占当前主界面：

```text
trend_window.h/.cpp
```

入口建议：

- 当前主界面底部新增 `趋势图` 按钮。
- 默认带入当前左侧查询条件。
- 如果中间列表已选中某条报告，可默认带入该患者和仪器。

趋势窗口布局：

- 顶部：患者摘要、仪器、日期范围、项目选择。
- 左侧或上方：项目列表。
- 中间：折线图。
- 下方：趋势数据表格。

第一阶段可以先用 Win32 GDI 画简单折线：

- 横轴：时间。
- 纵轴：数值结果。
- 点：每次报告结果。
- 悬浮提示暂不做。
- 参考范围背景带可留到第二阶段。

## 图表规则

- 只画 `RESULT` 能安全转换成数字的点。
- 非数值结果仍显示在趋势表格，但不参与折线。
- 同一项目单位变化时，默认不合并画图，需要在表格中提示。
- 多项目趋势后续再做，第一阶段只允许一个项目一张图。
- 相同时间多条结果按 `REP_NO` 排序展示，不自动去重。

## 文件拆分建议

新增：

- `src/trend_core.h`
- `src/trend_core.cpp`
- `src/trend_window.h`
- `src/trend_window.cpp`

可能修改：

- `src/main.cpp`
  - 增加趋势图按钮入口。
- `src/search_ui_layout.*`
  - 增加按钮和布局。
- `src/search_ui_events.*`
  - 增加趋势图按钮事件。
- `src/search_controller.*`
  - 如果需要复用连接串或输入条件转换，可增加趋势查询桥接。
- `CMakeLists.txt`
  - 加入新增源文件。

不建议修改：

- `search_core.*` 现有主查询逻辑。
- `search_app.*` 现有报告/明细显示规则。

## 开发步骤

1. 新增趋势数据结构和 SQL 查询。
2. 先用日志或临时表格验证查询结果是否和主界面一致。
3. 新增趋势窗口，只显示趋势表格。
4. 增加单项目选择。
5. 增加 GDI 折线图。
6. 增加主界面 `趋势图` 按钮入口。
7. 实机验证同一患者、同一仪器、同一项目的历史趋势。
8. 根据现场反馈决定是否增加多项目、多单位、参考范围背景带。

## 验证重点

- 患者过滤是否准确。
- 仪器过滤是否准确。
- `REP_DATE` 时间排序是否符合现场观察。
- `RESULT` 数值转换是否稳定。
- 单位是否来自 `LS_AS_ITEM.UNIT`。
- 趋势表格和主界面右侧明细是否能互相核对。
- 大日期范围下查询是否会明显卡顿。

## 风险点

- 同名患者可能很多，必须尽量使用诊疗卡号、病人号或条码号缩小范围。
- `RESULT` 可能包含非数字、符号或文本，需要容错。
- 同一项目不同仪器的结果不一定能直接比较，因此第一阶段限定单仪器。
- 同一项目单位变化时，不能直接画在同一条折线上。
- 日期范围过大时可能对 LIS 数据库造成压力，需要保留明确日期过滤。

## 后续增强

- 支持多项目勾选。
- 支持同单位项目同图对比。
- 支持参考范围背景带。
- 支持导出 CSV。
- 支持打印趋势图。
- 如果后续引入 Qt，可用 Qt Charts 或 QCustomPlot 替换 Win32 GDI 图表层。
