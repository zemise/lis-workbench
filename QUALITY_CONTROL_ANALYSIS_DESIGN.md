# 质控分析页设计方案

本文档记录基于“常规报告”页中部分样本号对应质控品结果，新增“质控分析”页的设计方案与阶段性实现边界。当前阶段已根据多电脑局域网使用场景修正为“LIS 直查优先，本地 SQLite 仅作为可选缓存”。

## 背景

当前项目已经有较完整的常规报告工作台：

- 右侧报告列表按检验日期和检验仪器查询 `LS_AS_REPORT`。
- 选中报告后，通过 `REP_NO` 查询 `LS_AS_REPENTRY` 明细。
- 明细中包含项目代码、项目名称、结果、单位、参考范围和异常标记等信息。
- 常规报告页已有右键菜单、趋势图入口、批量打印、保留状态刷新、按报告号跳转定位等能力。
- 统计分析管理下已有 HIV 抗体检测统计和急诊样本统计两个独立统计页，均采用“独立模块 + 共享查询层 + 明细回跳常规报告”的模式。

现场观察到：常规报告左侧/右侧列表中的部分样本号对应质控品检测结果。这些质控品结果可以作为质控分析的数据来源。

## 设计目标

新增一个独立的“质控分析”页，用于按用户配置从 LIS 直接查询质控品检测点，完成质控趋势查看、失控规则判断和原始报告追溯。

质控结果来源是 LIS 中的 `LS_AS_REPORT + LS_AS_REPENTRY`。考虑到不同局域网电脑之间本地 SQLite 无法天然互通，质控结果的事实来源应保持为 LIS 数据库，避免每台电脑看到不同的本地缓存结果。

推荐主流程调整为：

```text
系统设置维护质控样本号 -> 质控分析页按日期范围直接查询 LIS -> 内存计算统计和规则 -> 展示/导出/回跳常规报告
```

SQLite 不再作为质控结果的强制主链路。它只保留为可选能力：

- 保存本机质控品配置。
- 缓存最近查询结果以提升单机大范围查询速度。
- 支持离线临时查看，但必须明确标识“缓存数据”。
- 提供手动刷新 LIS 的入口，缓存不得替代 LIS 事实来源。

第一阶段目标：

- 不写入 LIS 业务表。
- 在系统设置中维护“仪器 + 固定样本号 + 项目/水平”的质控品配置。
- 质控分析页打开后不自动查询；用户选择仪器并点击“查询 / 刷新 LIS”后，再按配置和日期范围查询 LIS。
- 查询结果在内存中聚合、排序、统计并展示。
- 展示质控明细表。
- 预留 Levey-Jennings 质控图入口，完整绘图放到阶段 2。
- 预留 Westgard 规则状态字段，完整规则判断放到阶段 3。
- 支持从明细双击回跳常规报告并定位原始报告。
- 本地 SQLite 只作为可选缓存，不作为阶段 1 必须链路。

非第一阶段目标：

- 不做 LIS 业务表写入。
- 不修改原始报告结果。
- 不做完整质控品批号生命周期管理。
- 不做自动审核、自动拦截或自动发送控制。
- 不做复杂月报、年报和监管格式报表。
- 不要求多台电脑共享同一份 SQLite 文件。

## 页面入口

建议新增菜单：

```text
统计分析管理 -> 质控分析(&3)
```

当前 `统计分析3` 仍是占位模块，可替换为质控分析模块。

## 分支与改动边界

质控分析后续实现应新开独立 Git 分支开发，避免影响其他并行开发分支。推荐分支名：

```text
feat/quality-control-analysis
```

当前设计文档分支：

```text
feat/quality-control-analysis-design
```

代码实现时应尽量保持改动边界清晰：

- 新增质控分析模块、LIS 查询层和图表渲染层。
- SQLite 存储层保留为可选缓存/配置能力，不作为质控结果主数据源。
- 只在必要位置接入菜单、系统设置页质控配置卡片、常规报告右键跳转入口。
- 非必要不修改常规报告、HIV 统计、急诊统计、输血查询等既有业务模块。
- 如必须修改共享结构或工具函数，应保持向后兼容，避免改变已有页面行为。
- 不做顺手重构，不做无关格式化，不批量改动现有文档和代码风格。
- 优先通过新增文件承载质控分析逻辑，让其他分支后续合并时冲突最小。

建议新增模块文件：

```text
src/quality_control_module.h
src/quality_control_module.cpp
src/quality_control_chart_renderer.h
src/quality_control_chart_renderer.cpp
src/quality_control_lis_query.h
src/quality_control_lis_query.cpp
src/quality_control_cache.h        # 可选缓存，非阶段 1 必须
src/quality_control_cache.cpp
```

建议新增两类接口：

1. LIS 查询接口
   负责按质控配置从 LIS 查询质控结果，返回内存行集。

2. 分析接口
   负责对内存行集做分组、统计、规则判断和图表数据准备。

```cpp
struct QualityControlQuery;
struct QualityControlRule;
struct QualityControlPoint;
struct QualityControlSummary;
struct QualityControlAnalysisResult;

bool query_quality_control_points_from_lis(
    const search::DbSettings& db,
    const QualityControlQuery& query,
    std::vector<QualityControlPoint>& rows,
    std::string& error,
    LogFn log = {});

bool analyze_quality_control_points(
    const QualityControlQuery& query,
    const std::vector<QualityControlPoint>& rows,
    QualityControlSummary& summary,
    QualityControlAnalysisResult& result,
    std::string& error);
```

## 总体布局

参考 `temp/参考1.png` 和 `temp/参考2.png` 后，质控分析页建议采用“两级视图”：

1. 今日质控概览  
   参考 `参考1.png` 的实时监控页，用卡片快速呈现当前仪器、当前批次或当前日期下各项目质控状态。

2. 项目质控详情  
   参考 `参考2.png` 的项目 L-J 图窗口，围绕某个项目展示 L-J 图、数据列表、规则判定和查询记录。

第一阶段先实现今日质控概览的筛选区、分组/明细列表和 LIS 直查。L-J 图可以预留入口，但完整绘图放到第二阶段。

推荐主页面结构：

```text
┌────────────────────────────────────────────────────────────┐
│ 日期范围  院区/检验科  仪器  质控品配置  项目  水平  查询 │
│ 查询/刷新LIS  最近查询时间  只看异常/失控                 │
├───────────────┬────────────────────────────────────────────┤
│ 项目/水平列表 │ 今日质控概览卡片区                         │
│               │ 无数据 / 在控 / 警告 / 失控未处理 / 已处理  │
├───────────────┴────────────────────────────────────────────┤
│ 质控明细表：时间、样本号、报告号、项目、结果、查询来源     │
└────────────────────────────────────────────────────────────┘
```

项目详情可使用独立弹窗或页内 Tab：

```text
┌────────────────────────────────────────────────────────────┐
│ L-J图 | 数据列表 | 规则判定 | 查询记录                     │
├────────────────────────────────────────────────────────────┤
│ 开始日期 结束日期  水平 L1 L2 L3  刷新LIS  使用缓存  选项  │
├────────────────────────────────────────────────────────────┤
│ 水平1 L-J图：均值 / ±1SD / ±2SD / ±3SD                     │
│ 水平2 L-J图：均值 / ±1SD / ±2SD / ±3SD                     │
│ 水平3 L-J图：均值 / ±1SD / ±2SD / ±3SD                     │
└────────────────────────────────────────────────────────────┘
```

## 顶部筛选区

建议控件：

| 控件 | 说明 |
| --- | --- |
| 开始日期 / 结束日期 | 默认最近 30 天，后续可按现场习惯改为当月 |
| 院区 / 检验科 | 全部、老院、新院或具体检验科 |
| 仪器 | 复用常规报告仪器字典逻辑，按 `LS_AS_ROOM / LS_AS_MACHINE` 加载；选中后显示仪器名称，内部保留仪器代码用于查询 |
| 质控品配置 | 从系统设置读取，如“全部质控品 / 血常规 L1 / 血常规 L2” |
| 项目 | 按当前规则和仪器可选，也允许全部 |
| 水平 | 全部、L1、L2、L3 |
| 只看失控 | 勾选后明细只显示命中失控规则的点 |
| 查询 / 刷新 LIS | 按当前配置和日期范围直接查询 LIS，后台执行，避免阻塞 UI |
| 使用缓存 | 可选；若启用本地缓存，可先显示缓存并允许手动刷新 LIS |
| 最近查询 | 显示本次 LIS 查询完成时间、返回点数和耗时 |
| 导出 CSV | 阶段 1 导出当前明细视图；如果已选左侧项目/水平分组，则只导出该分组明细 |

参考图中的“部门、仪器组、仪器、批”筛选链路可以简化为本项目的实际配置：`院区/检验科 -> 仪器 -> 质控品配置/样本号 -> 项目/水平`。第一阶段不强制实现“批”概念，除非现场能提供稳定批号来源。

## 左侧质控分组列表

左侧列表用于快速选择要看的质控项目或水平。参考图右侧项目树中 `(WBC) 白细胞`、`(RBC) 红细胞` 这类项目列表，本项目建议用原生 ListView 或 TreeView 展示，不做复杂皮肤化控件。

推荐分组层级：

```text
仪器 -> 项目 -> 质控品 / 水平
```

列表列建议：

| 列 | 说明 |
| --- | --- |
| 仪器 | 仪器名称或仪器代码 |
| 项目 | 项目名称 |
| 水平 | L1 / L2 / L3 或配置名称 |
| 点数 | 当前查询范围内有效质控点数量 |
| 均值 | 当前点集均值或配置靶值 |
| SD | 当前点集 SD 或配置 SD |
| CV% | `SD / 均值 * 100` |
| 警告 | 命中警告规则次数 |
| 失控 | 命中失控规则次数 |
| 最近失控 | 最近一次失控时间 |

点击分组后，右侧概览卡片和下方明细只展示该组数据。第二阶段进入 L-J 图时，也按当前分组打开。

## 今日质控概览卡片

参考 `参考1.png` 的卡片式状态区，主页面中部可展示当前筛选范围内的项目质控状态。

卡片建议字段：

| 字段 | 说明 |
| --- | --- |
| 质控名称 | 如“血常规质控 L1” |
| 项目 | 项目代码 + 项目名称 |
| 样本号 | 固定质控样本号 |
| 水平 | L1 / L2 / L3 |
| 最近结果 | 最近一次数值结果和单位 |
| 最近时间 | `effective_time` |
| 状态 | 无数据 / 在控 / 警告 / 失控未处理 / 失控已处理 |
| 数据来源 | LIS 实时查询 / 本机缓存 |

卡片动作：

- `L-J图`：打开项目质控详情。
- `数据`：定位下方明细。
- `刷新`：按该卡片对应仪器、样本号和项目重新查询 LIS。
- `处理`：后续用于失控备注，第一阶段可先预留不实现。

状态颜色建议：

| 状态 | 颜色建议 | 第一阶段含义 |
| --- | --- | --- |
| 无数据 | 灰色 | LIS 当前查询范围内没有该配置对应结果 |
| 在控 | 绿色 | 有 LIS 结果，未命中警告/失控 |
| 警告 | 黄色 | 预留；第一阶段可不计算完整规则 |
| 失控未处理 | 红色 | 预留；第一阶段可不计算完整规则 |
| 失控已处理 | 浅蓝色 | 预留；后续有处理记录后使用 |

第一阶段如果暂不做 Westgard 规则，可先按“是否查询到 LIS 结果”展示 `无数据 / 有数据`，并预留状态字段和颜色映射。

## 右侧 Levey-Jennings 图

该部分主要参考 `参考2.png`，建议作为第二阶段重点实现。

图表核心要求：

- 横轴按检测时间或结果顺序等距展示。
- 横轴标签显示真实检测时间。
- 纵轴显示结果值。
- 显示均值线。
- 显示 `+1SD / -1SD`、`+2SD / -2SD`、`+3SD / -3SD`。
- 正常点、警告点、失控点使用不同颜色。
- 鼠标悬停或选中点时显示报告号、样本号、时间、项目、结果、Z 值和命中规则。
- 点击图中点位时，下方明细表同步定位。
- 从明细表选择行时，图中点位同步高亮。

参考图中有 `L-J图 / 数据列表 / Youden图 / 测试信息 / 试剂信息` Tab。本项目第一版详情页建议先保留：

| Tab | 阶段 | 说明 |
| --- | --- | --- |
| L-J图 | 阶段 2 | 多水平纵向堆叠图 |
| 数据列表 | 阶段 1 | LIS 查询明细，缓存仅作可选来源 |
| 规则判定 | 阶段 3 | Westgard 命中详情 |
| 查询记录 | 后续 | 本机查询历史或缓存刷新记录 |
| Youden图 | 后续 | 暂不实现 |
| 试剂信息 | 后续 | 暂不实现，除非有稳定数据来源 |

阶段 2 首版使用 Win32 GDI 绘制，绘图逻辑集中到 `quality_control_chart_renderer.*`，避免窗口过程里混入大量绘图计算。首版从当前查询内存结果打开独立 L-J 图窗口，不重新访问 LIS。

## 下方质控明细表

阶段 1 明细表列建议：

| 列 | 来源 / 说明 |
| --- | --- |
| 时间 | 优先 `LS_AS_REPORT.REP_TIME`，为空时退到 `CHK_DATE`，再退到 `REP_DATE` |
| 样本号 | `LS_AS_REPORT.OPER_NO` |
| 报告号 | `LS_AS_REPORT.REP_NO` |
| 仪器 | 显示 `MACH_NAME`，内部保留 `MACH_CODE` 用于查询和回跳 |
| 检验者 | `JC_EMPLOYEE_PROPERTY.NAME`，由 `LS_AS_REPORT.OPER_CODE` 关联 |
| 项目 | `LS_AS_REPENTRY.ITEM_NAME` 或 `LS_AS_ITEM.ITEM_NAME` |
| 质控水平 | 本机配置派生，如 L1 / L2 / L3 |
| 结果 | `LS_AS_REPENTRY.RESULT` |
| 单位 | `LS_AS_ITEM.UNIT` |
| 来源 | 阶段 1 固定为 `LIS`，后续启用缓存时显示 `本机缓存` |

阶段 1 底部明细不展示 `条码号`、`审核`、`发送`。这些字段可作为后续追溯或规则扩展的内部数据，但不进入当前底部列表，避免质控分析页与常规报告页的报告流程字段混杂。

交互：

- 点击表头做内存排序，不重新访问 LIS。
- 双击明细行打开或激活常规报告页，并按 `REP_NO` 精确定位。
- 提供“导出 CSV”按钮，导出当前下方明细视图。
- 右键可提供“查看原始报告”“复制该点信息”等操作。

## 质控品识别规则

这是本功能最关键的边界。

质控品不通过样本号前缀、模糊匹配或报告字段自动推断，而是由用户在系统设置中明确指定：

```text
某个检验仪器 + 某几个固定样本号 = 质控结果
```

也就是说，质控品识别应使用精确配置。只有当前报告行同时满足以下条件，才视为质控点：

1. `LS_AS_REPORT.MACH_CODE` 匹配用户配置的检验仪器。
2. `LS_AS_REPORT.OPER_NO` 精确等于该仪器下配置的某个固定质控样本号。
3. 当前结果项目 `LS_AS_REPENTRY.ITEM_CODE` 在配置的质控项目范围内，或该样本号配置为“纳入全部项目”。

第一阶段建议将配置放在系统设置页，不直接写 LIS 业务数据库。配置可保存在本机配置文件或本机 SQLite 中；如果后续要求多台电脑共享同一套质控品配置，应优先考虑集中配置表或轻量服务，而不是共享 SQLite 文件。

推荐的配置粒度：

| 配置项 | 说明 |
| --- | --- |
| 仪器 | 必填，复用常规报告/系统设置已有的检验仪器选择器 |
| 样本号 | 必填，用户手动录入固定质控样本号，支持一个仪器配置多个样本号 |
| 质控名称 | 可选，如“血常规 L1”“血常规 L2” |
| 水平 | 可选，如 L1 / L2 / L3 |
| 项目代码 | 可选；为空表示该样本号下全部项目都纳入质控分析 |
| 靶值 | 可选；为空时可按当前查询结果计算本期均值 |
| SD | 可选；为空时可按当前查询结果计算本期 SD |
| 启用 | 是否启用该条配置 |

示例配置：

| 仪器 | 样本号 | 质控名称 | 水平 | 项目代码 | 靶值 | SD | 启用 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 4001 | 1001 | 血常规质控 L1 | L1 | 10001 | 5.20 | 0.15 | 是 |
| 4001 | 1002 | 血常规质控 L2 | L2 | 10001 | 12.80 | 0.30 | 是 |
| 5001 | 2001 | 生化质控 L1 | L1 | 20010 | 80.0 | 4.0 | 是 |

配置判定时必须使用精确样本号匹配，不使用 `LIKE`、前缀或包含匹配。这样可以避免患者样本号偶然符合文本规则而被误纳入质控分析。

如果同一台仪器的同一个固定样本号需要区分多个质控水平，应优先要求用户配置不同样本号；如果现场确实存在同一样本号跨水平复用，需要再增加“项目代码”或“质控名称”作为区分条件。

## 系统设置页设计

建议将“系统设置”页组织为 Tab 页，并增加独立的“质控品设置”Tab。由于系统设置页已经有常规报告条码打印机和快捷检验仪器配置，并且已具备检验仪器选择器，质控品设置应复用这套仪器选择能力。

推荐设置方式：

- 在系统设置页内提供“质控品设置”Tab，不再打开独立配置弹窗。
- Tab 左侧维护本机质控配置列表。
- Tab 右侧维护样本号对应的检验仪器、质控项目、水平、靶值和 SD。
- 仪器字段提供“选择”按钮，复用现有检验仪器选择器填充科室代码、仪器代码和仪器名称。
- 支持新增、删除、启用/停用、保存。
- 保存后广播设置变更，已打开的质控分析页下次查询时使用新配置。

列表列建议：

| 列 | 说明 |
| --- | --- |
| 启用 | 是否启用 |
| 仪器代码 | `MACH_CODE` |
| 仪器名称 | `MACH_NAME` |
| 样本号 | 固定质控样本号，精确匹配 `OPER_NO` |
| 质控名称 | 用户自定义显示名 |
| 水平 | L1 / L2 / L3 |
| 项目代码 | 可为空；为空表示该样本号全部项目 |
| TargetMean | 靶值，可为空 |
| TargetSd | 标准差，可为空 |

如果 `TargetMean / TargetSd` 为空，页面可以按当前查询周期自动计算均值和 SD，但必须在界面上标明“本期统计值”，不能当作正式靶值。

## 本地 SQLite 定位

修正后的方案中，本地 SQLite 不再是质控结果的主数据源。质控结果以 LIS 手动查询结果为准，SQLite 只作为可选的单机辅助能力。

SQLite 可以承担：

- 本机质控品配置保存。
- 最近一次或最近一段时间 LIS 查询结果缓存。
- 查询历史、耗时和错误记录。
- 离线临时查看。

SQLite 不应承担：

- 多台电脑共享的质控结果主库。
- 替代 LIS 作为事实来源。
- 共享文件夹上的多人并发写入数据库。

原因：

- 不同电脑各自本地 SQLite 无法天然互通，会导致质控分析结果不一致。
- Windows 局域网共享 SQLite 容易遇到锁、权限、延迟和损坏风险。
- LIS 已经是多电脑共同访问的数据源，直接查 LIS 更符合现有工作流。

建议数据库文件：

```text
data/quality_control.sqlite
```

如果安装目录不可写，可放到用户数据目录或与 `ClientConfig.ini` 同目录。具体路径通过 `ClientConfig.ini` 配置：

```ini
[QualityControl]
LocalCachePath=quality_control.sqlite
```

如果保留 SQLite，继续采用 SQLite amalgamation 源码静态编译进主程序，源码放在 `external/sqlite/`。这样 Windows/MinGW 构建不需要预装 SQLite 开发库，发布包也不需要额外携带 `sqlite3.dll`。但质控分析页不得强依赖“先导入 SQLite 才能查看”。

### 表结构

如果启用缓存，建议至少包含配置表和可选缓存表。阶段 1 只要求配置可维护；结果缓存可以推迟。

#### `qc_config`

保存系统设置页维护的质控品配置。也可以继续保存在 `ClientConfig.ini`，但放入 SQLite 更利于增删改和批量维护。该配置是本机配置；多电脑统一配置需要另行设计集中存储。

```sql
CREATE TABLE IF NOT EXISTS qc_config (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  enabled INTEGER NOT NULL DEFAULT 1,
  room_code TEXT,
  mach_code TEXT NOT NULL,
  mach_name TEXT,
  sample_no TEXT NOT NULL,
  qc_name TEXT,
  level TEXT,
  item_code TEXT,
  item_name TEXT,
  target_mean REAL,
  target_sd REAL,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  UNIQUE(mach_code, sample_no, item_code, level)
);
```

#### `qc_result_cache`（可选）

保存从 LIS 查询到的最近结果缓存。分析页在用户点击“查询 / 刷新 LIS”时应以 LIS 结果为准；只有用户启用缓存或 LIS 不可用时，才读取这张表，并在界面上标识为“本机缓存”。

```sql
CREATE TABLE IF NOT EXISTS qc_result_cache (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  source_rep_no TEXT NOT NULL,
  source_entry_key TEXT NOT NULL,
  room_code TEXT,
  mach_code TEXT NOT NULL,
  mach_name TEXT,
  sample_no TEXT NOT NULL,
  barcode_no TEXT,
  tester_name TEXT,
  report_date TEXT,
  inspect_date TEXT,
  report_time TEXT,
  effective_time TEXT NOT NULL,
  chk_flag TEXT,
  conf TEXT,
  item_code TEXT NOT NULL,
  item_name TEXT,
  result_text TEXT,
  result_value REAL,
  has_numeric_value INTEGER NOT NULL DEFAULT 0,
  unit TEXT,
  normal TEXT,
  qc_name TEXT,
  level TEXT,
  target_mean REAL,
  target_sd REAL,
  cached_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  UNIQUE(source_rep_no, source_entry_key)
);
```

`source_entry_key` 用于稳定标识同一报告中的同一条明细。当前质控专用 SQL 可使用 `LS_AS_REPENTRY.ID`；如果后续遇到特殊 LIS 版本无法取到 `ID`，才退化为 `REP_NO + ITEM_CODE + 行序号`。

#### `qc_query_log`（可选）

记录每次 LIS 查询或缓存刷新范围和结果，便于页面显示最近查询状态。

```sql
CREATE TABLE IF NOT EXISTS qc_query_log (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  started_at TEXT NOT NULL,
  finished_at TEXT,
  start_date TEXT NOT NULL,
  end_date TEXT NOT NULL,
  mach_code TEXT,
  result_count INTEGER NOT NULL DEFAULT 0,
  elapsed_ms INTEGER NOT NULL DEFAULT 0,
  source TEXT NOT NULL,
  status TEXT NOT NULL,
  error TEXT
);
```

### 索引

```sql
CREATE INDEX IF NOT EXISTS idx_qc_result_time
  ON qc_result_cache(effective_time);

CREATE INDEX IF NOT EXISTS idx_qc_result_group
  ON qc_result_cache(mach_code, sample_no, item_code, level, effective_time);

CREATE INDEX IF NOT EXISTS idx_qc_result_report
  ON qc_result_cache(source_rep_no);
```

## LIS 查询设计

质控分析页默认不自动访问 LIS。用户先选择仪器，再点击“查询 / 刷新 LIS”；查询应后台执行，完成后把结果放入内存模型，页面筛选、排序、绘图和导出优先基于当前内存结果完成。

推荐查询入口：

- 质控分析页顶部增加“查询 / 刷新 LIS”按钮。
- 页面打开后只初始化日期范围和列表，不自动查询 LIS。
- 未选择仪器时点击查询，应提示先选择检验仪器。
- 系统设置的“质控品设置”Tab 中可增加“测试查询最近 30 天”按钮，用于验证配置是否能命中 LIS 结果。
- 后续如果性能不足，可增加“使用本机缓存”选项，但第一阶段不应强制缓存。

查询流程：

1. 读取启用的质控品配置。
2. 按用户选择的日期范围、仪器和固定样本号，从 LIS 查询匹配结果。
3. 将结果逐条解析为内存中的 `QualityControlPoint`。
4. 按 `mach_code + sample_no + item_code + level` 聚合。
5. 计算均值、SD、CV%、Z 值和预留规则状态。
6. 刷新当前页面列表、概览卡片和明细。
7. 如果启用了本机缓存，再把查询结果写入 `qc_result_cache`。

内存结果必须去重：同一个 `REP_NO + REPENTRY.ID` 在一次查询结果中只保留一条。缓存写入也必须幂等。

### LIS 查询 SQL

查询基础表：

- `LS_AS_REPORT r`
- `LS_AS_REPENTRY e`
- `LS_AS_ITEM i`
- `LS_AS_MACHINE m`
- 必要时关联 `LS_AS_BARCODE b`

SQL 方向：

```sql
SELECT
  CAST(e.ID AS varchar(30)) AS ENTRY_ID,
  r.REP_NO,
  r.OPER_NO,
  r.TXM_NO,
  r.ROOM_CODE,
  r.MACH_CODE,
  m.MACH_NAME,
  r.REP_DATE,
  r.CHK_DATE,
  r.REP_TIME,
  r.CHK_FLAG,
  r.CONF,
  e.ITEM_CODE,
  ISNULL(i.ITEM_NAME, e.ITEM_NAME) AS ITEM_NAME,
  e.RESULT,
  i.UNIT,
  e.NORMAL
FROM LS_AS_REPORT r WITH (NOLOCK)
JOIN LS_AS_REPENTRY e WITH (NOLOCK)
  ON e.REP_NO = r.REP_NO
 AND ISNULL(e.DELETE_BIT, 0) = 0
LEFT JOIN LS_AS_ITEM i WITH (NOLOCK)
  ON i.ITEM_CODE = e.ITEM_CODE
 AND ISNULL(i.DELETE_BIT, 0) = 0
LEFT JOIN LS_AS_MACHINE m WITH (NOLOCK)
  ON m.MACH_CODE = r.MACH_CODE
 AND ISNULL(m.DELETE_BIT, 0) = 0
WHERE ISNULL(r.DELETE_BIT, 0) = 0
  AND r.CHK_DATE >= @start_date
  AND r.CHK_DATE < DATEADD(day, 1, @end_date)
  AND r.MACH_CODE = @mach_code
  AND r.OPER_NO IN (...)
  AND (
    e.ITEM_CODE IN (...)
    OR @include_all_items = 1
  )
ORDER BY e.ITEM_CODE, r.CHK_DATE, r.REP_NO;
```

查询实现时仍沿用项目现有 SQL 拼接工具风格，并注意：

- 所有文本条件必须 `trim` 和转义。
- 日期范围默认按 `CHK_DATE` 查询。
- 质控图显示时间优先使用 `REP_TIME`，其次 `CHK_DATE`，最后 `REP_DATE`。
- 质控样本号必须按用户配置的 `MACH_CODE + OPER_NO` 精确匹配。
- 结果必须保留原始文本，数值解析失败的记录显示在明细中，但不参与图表和规则判定。

## 缓存查询设计

质控分析页的主查询不再查询本地 SQLite，而是查询 LIS 后在内存中过滤和分析。可选缓存查询只作为降级路径：

```sql
SELECT *
FROM qc_result_cache
WHERE effective_time >= @start_time
  AND effective_time < @end_time
  AND (@mach_code IS NULL OR mach_code = @mach_code)
  AND (@item_code IS NULL OR item_code = @item_code)
  AND (@level IS NULL OR level = @level)
ORDER BY mach_code, item_code, level, effective_time, source_rep_no;
```

内存分析层负责：

- 按当前筛选条件返回质控点。
- 按 `mach_code + item_code + level` 聚合分组。
- 计算均值、SD、CV%、Z 值和 Westgard 规则。
- 标记每个点的正常、警告或失控状态。
- 标识数据来源：LIS 实时查询 / 本机缓存。

## 统计和质控计算

每个分组按以下键聚合：

```text
RoomCode + MachCode + ItemCode + Level
```

基础指标：

- 点数 N。
- 均值 Mean。
- 标准差 SD。
- 变异系数 CV%。
- 最小值、最大值。
- 警告点数。
- 失控点数。

Z 值：

```text
Z = (ResultValue - TargetMean) / TargetSd
```

如果配置了靶值和 SD，优先使用配置值。否则使用当前查询结果计算均值和 SD。

## Westgard 规则第一版

第一版建议实现以下规则：

| 规则 | 类型 | 说明 |
| --- | --- | --- |
| `1-2s` | 警告 | 单个质控点超过 ±2SD |
| `1-3s` | 失控 | 单个质控点超过 ±3SD |
| `2-2s` | 失控 | 连续两个点在同侧超过 ±2SD |
| `R-4s` | 失控 | 相邻两个点一高一低，跨度超过 4SD |
| `4-1s` | 警告或失控 | 连续四个点在同侧超过 ±1SD，具体口径可配置 |
| `10x` | 警告或失控 | 连续十个点在均值同侧，提示系统偏移 |

规则判断应保留命中详情，不只给最终状态。例如：

```text
状态：失控
命中规则：1-3s, 2-2s
```

## 与常规报告联动

建议实现两个方向的联动。

从常规报告到质控分析：

- 在常规报告右侧列表右键菜单增加“查看质控趋势”。
- 如果当前报告行的 `MACH_CODE + OPER_NO` 匹配系统设置中的质控品配置，则打开质控分析页并带入仪器、样本号、项目和日期范围。
- 如果不匹配质控品配置，提示“当前样本未在系统设置中配置为质控品”。

从质控分析回到常规报告：

- 明细表双击打开或激活常规报告。
- 通过 `REP_NO` 精确定位对应报告。
- 如果缺少 `REP_NO`，提示无法定位原始报告。

## 配置持久化设计

第一阶段优先放在系统设置页维护，不写 LIS 业务库。

如果只考虑本机使用，可以保存到本地 SQLite 的 `qc_config` 或 `ClientConfig.ini`。如果后续要求多台电脑共享配置，应新增集中配置方案，例如：

- 在 LIS 之外建立一张本软件专用配置表。
- 由一个轻量 HTTP 服务统一提供配置。
- 使用可人工分发的配置导入/导出文件。

不建议把 SQLite 文件放到共享文件夹作为多人共享配置库。

`ClientConfig.ini` 可保存质控模块默认行为：

```ini
[QualityControl]
DefaultDays=30
DefaultUseConfiguredTarget=1
FourOneSAsOutOfControl=0
TenXAsOutOfControl=0
UseLocalCache=0
LocalCachePath=quality_control.sqlite
```

质控品配置可以写入 `qc_config`。这样比把多条配置塞进 INI 更容易做新增、删除、启用、停用、排序和后续批量维护。但需要在界面上明确这是“本机配置”，除非后续实现集中配置。

CSV 只作为批量导入/导出配置的格式，不作为运行时主配置源：

```csv
MachCode,SampleNo,QcName,Level,ItemCode,TargetMean,TargetSd,Enabled
4001,1001,血常规质控 L1,L1,10001,5.20,0.15,1
4001,1002,血常规质控 L2,L2,10001,12.80,0.30,1
5001,2001,生化质控 L1,L1,20010,80.0,4.0,1
```

系统设置页负责读写配置，避免要求用户手工编辑 SQLite 或 CSV 文件。

## 导出设计

第一阶段建议先做 CSV 导出：

- 当前分组明细。
- 当前查询全部明细。
- 汇总列表。

后续再考虑：

- Excel 质控月报。
- Word/PDF 质控图报告。
- 按仪器和项目批量导出图片。

## 开发阶段建议

### 阶段 1：配置和 LIS 直查原型

阶段 1 目标是打通“配置 -> LIS 查询 -> 内存分析 -> 原始报告追溯”的最小闭环，不实现完整质控图和完整规则判断。

必须实现：

- 新增质控分析菜单和空页面。
- 在系统设置页增加“质控品设置”Tab，维护仪器、固定样本号、项目、水平、靶值和 SD。
- 增加“查询 / 刷新 LIS”动作，按配置从 LIS 查询质控结果。
- 仪器筛选显示仪器名称，内部保留仪器代码用于查询。
- 质控分析页显示项目/水平列表、概览卡片和明细表。
- 导出当前明细视图为 CSV，便于现场核对查询结果。
- 显示最近查询时间、返回点数和耗时。
- 双击明细回跳常规报告。
- 如保留 SQLite，仅用于配置或可选缓存，不作为质控结果必需链路。

阶段 1 暂不实现：

- Levey-Jennings 绘图。
- 完整 Westgard 规则判断。
- 失控处理记录闭环。
- 自动定时同步或定时缓存刷新。
- Youden 图、试剂信息、复杂批号管理。
- Excel/Word/PDF 正式质控报表。
- 多电脑共享配置服务。

阶段 1 验收标准：

- 用户可以在系统设置中为某台仪器配置固定质控样本号。
- 点击查询后，程序只读取匹配 `MACH_CODE + OPER_NO` 的 LIS 结果。
- 同一查询结果中不会重复显示同一个 `REP_NO + REPENTRY.ID` 质控点。
- 不同电脑在相同 LIS 数据、相同配置、相同日期范围下应得到一致结果。
- 明细行能通过 `REP_NO` 回跳常规报告。
- 除菜单、系统设置入口和必要回跳入口外，不改动其他页面行为。

### 阶段 2：质控图

- 新增 Levey-Jennings 图绘制。
- 支持分组切换。
- 支持图点和明细联动。
- 支持正常、警告、失控颜色区分。
- 支持参考图中的 L1/L2/L3 多水平纵向堆叠。
- 支持图表区垂直滚动，避免多项目或多水平挤在一个图中。

阶段 2 首版已实现：

- 在质控分析页增加 `L-J图` 入口。
- 按当前左侧项目/水平分组打开独立 L-J 图窗口；未选择分组时默认使用第一组。
- 使用当前内存查询结果绘制数值点、折线、均值线和 `±1SD / ±2SD / ±3SD` 参考线。
- 图表窗口内支持点位 Tooltip。
- 点击图中点位时同步选中下方数据列表。
- 选择下方数据列表时同步高亮图中点位。
- 同仪器同项目的多个水平可在同一个窗口内纵向堆叠显示；现场测试数据确认常见形态是一个固定样本号对应一个水平，如 `5001=L1`、`5002=L2`、`5003=L3`，因此 L-J 图聚合时应跨样本号收集同项目的多个水平。
- 多水平纵向图放在可滚动图表区内，每张水平图按图表区宽度动态计算高度，并限制在合理最小/最大值内，不因窗口缩小或水平数量增加而被压扁；窗口变大时可显示更多图，窗口变小时通过滚动查看完整图。
- 图片导出和正式报表作为后续阶段 2 增量。

### 阶段 3：规则判断

- 实现 `1-2s / 1-3s / 2-2s / R-4s / 4-1s / 10x`。
- 明细显示命中规则。
- 汇总显示警告和失控次数。
- 增加“只看失控”筛选。

阶段 3 当前已实现：

- 已覆盖单点规则 `1-2s`、`1-3s`，以及连续规则 `2-2s`、`R-4s`、`4-1s`、`10x`。
- `2-2s` 按同一质控分组内时间顺序判断；连续两个已判定点在均值同侧且均超过 `±2SD` 时，后一个点命中 `2-2s` 并按失控处理。
- `R-4s` 按同一质控分组内相邻两点判断；两个点一高一低且 Z 值跨度超过 4SD 时，后一个点命中 `R-4s` 并按失控处理。
- `4-1s` 按同一质控分组内时间顺序判断；连续四个点在均值同侧且均超过 `±1SD` 时，第 4 个点命中 `4-1s`。当前首版按警告处理；如果该点已因其他规则失控，则不降级。
- `10x` 按同一质控分组内时间顺序判断；连续十个点均在均值同侧时，第 10 个点命中 `10x`。当前首版按警告处理；如果该点已因其他规则失控，则不降级。
- 规则计算优先使用质控品设置中的靶值和 SD；如果未配置，则使用当前查询周期内同分组的均值和 SD。
- 主页面分组列表中的均值、SD、CV% 和 L-J 图参考线使用同一套规则基准，并在分组列表中显示基准来源为 `靶值` 或 `本期`。
- 每个质控点保留 Z 值、状态和命中规则；主页面明细表、CSV 导出、L-J 图下方数据列表和图点 Tooltip 均展示这些字段。
- 左侧分组列表和概览列表按点级规则汇总为 `在控 / 警告 / 失控 / 未判定`，并显示警告、失控点数。
- 已增加状态筛选：`全部 / 警告+失控 / 仅失控 / 仅警告`。筛选基于当前内存结果完成，不重新查询 LIS，并同步影响分组列表、概览列表、明细、CSV 导出和 L-J 图入口。
- 规则处理流程仍作为后续阶段 3 增量。

### 阶段 4：配置和导出

- 完善质控品设置 Tab，支持批量导入/导出配置。
- CSV 导出已实现，当前导出内容跟随页面状态筛选和当前明细视图。
- 如现场查询性能不足，再增加本机缓存策略，例如缓存最近 30 天 LIS 查询结果并提供手动刷新。
- 如果需要多电脑统一配置，设计集中配置方案。
- 后续按现场格式做 Excel/Word 报表。

## 风险和待确认问题

1. 固定质控样本号如何维护  
   当前确认由用户在系统设置中指定某个检验仪器的几个样本号为固定质控结果。需要进一步确认每台仪器通常配置几个样本号、是否分水平、是否需要按项目单独设置靶值和 SD。

2. 同一样本号是否会跨项目或跨水平复用  
   如果同一个固定样本号包含多个项目，默认可全部纳入；如果同一个样本号代表多个质控水平，需要增加项目或质控名称区分。

3. 靶值和 SD 来源  
   需要确认靶值、SD 是来自质控品说明书、仪器设置、LIS 配置，还是由本软件本地维护。

4. 时间口径  
   查询范围建议按 `CHK_DATE`，图表显示建议优先 `REP_TIME`。需要现场确认质控分析更关注上机时间、审核时间还是报告时间。

5. 同一样本多项目、多报告问题  
   如果同一个样本号存在多个报告或同一项目重复结果，需要明确取全部点、取最新点还是按 `REP_NO + ITEM_CODE` 去重。

6. 失控规则口径  
   `4-1s` 和 `10x` 在不同实验室可能作为警告或失控，需要配置化。

7. 多电脑配置一致性
   如果多台电脑都使用质控分析页，本机配置可能不一致。阶段 1 可以先接受“本机配置”，但需要在界面或文档中说明；如果现场要求统一配置，应优先做集中配置或导入/导出流程。

8. LIS 查询性能
   直接查 LIS 简化多电脑一致性，但大日期范围、多仪器、多项目查询可能较慢。阶段 1 应先限制默认日期范围并后台查询；只有确认性能不足时，再启用本机缓存。

## 推荐结论

质控分析页适合做成独立统计模块，入口放在“统计分析管理”下。第一版应坚持“不写 LIS、可追溯、设置页可维护、LIS 直查优先”。基于用户配置的“仪器 + 固定样本号”从 `LS_AS_REPORT + LS_AS_REPENTRY` 精确查询质控点，之后在内存中完成分组、统计、Levey-Jennings 图数据准备、基础 Westgard 规则和明细回跳常规报告。

不建议第一版写 LIS 业务表、做复杂批号管理、自动审核控制，或把 SQLite 作为多电脑共享主库。先把“能配置、能查 LIS、能分析、能画图、能判定、能追溯”做稳定，再根据现场性能和多电脑一致性需求，决定是否增加本机缓存、集中配置和正式报表能力。
