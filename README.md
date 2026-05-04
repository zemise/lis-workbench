# cpp_search

`cpp_search` 是按截图复刻的 LIS 检验结果查询工具起步项目。

当前版本：`v2026.05.03`

项目已经整理为可长期演进的结构。
详见 [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) 和 [QT_MIGRATION_GUIDE.md](QT_MIGRATION_GUIDE.md)。

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

- `build/`、`out/`、`result_search.ini` 视为本地产物，不纳入版本管理

当前目标：

- 通过 SQL Server ODBC 连接 LIS 数据库。
- 从 `LS_AS_REPORT` 查询报告主记录。
- 通过 `REP_NO` 联查 `LS_AS_REPENTRY` 中的项目结果。
- 支持按姓名、诊疗卡号、病人号、条码号、样本号、仪器、组合项目、日期范围等条件过滤。

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
- 设置页面支持字号配置，保存后会持久化到 `result_search.ini` 并立即应用到主界面。
- 数据库配置持久化保存到程序同目录 `result_search.ini`。
- `设置`、`查询` 和 `退出` 按钮。

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
- [QUERY_DESIGN.md](QUERY_DESIGN.md)
- [QT_MIGRATION_GUIDE.md](QT_MIGRATION_GUIDE.md)
- [TREND_CHART_PLAN.md](TREND_CHART_PLAN.md)

## 使用说明

1. 在 Windows 上运行 `result_search.exe`。
2. 点击底部 `设置`。
3. 填写服务器、初始数据库、用户名、密码。
4. 可点击 `测试连接` 验证数据库连接。
5. 点击 `保存` 后，配置会写入程序同目录 `result_search.ini`。
6. 输入姓名、病人号、条码号、日期范围，或通过 `检验科室 / 病人类型 / 报告状态` 下拉筛选。
7. 点击 `查询`。
8. 在中间报告列表选择一行，右侧显示该报告的项目结果。
9. 如果本次查询使用了病人姓名或病人号，可点击 `趋势图` 查看该查询条件下的项目趋势。
10. 在趋势图窗口右侧点击项目行切换图表，勾选项目后可导出对应趋势明细 CSV。

程序内部会自动生成连接串，格式示例：

```text
Data Source=172.18.3.8\MSSQLSERVER1;Initial Catalog=trasen;User ID=sa;Password=your_password
```

程序会先生成和 `cpp_rapidp` 一致的 SQL Server 连接串格式，再在底层自动尝试：

- `ODBC Driver 18 for SQL Server`
- `ODBC Driver 17 for SQL Server`
- `SQL Server`

说明：

- `初始数据库` 直接作为实际查询连接里的 `Initial Catalog`。
- 前端不再手填 ODBC 驱动，驱动选择由底层自动尝试。
- 该字段会保存到 `result_search.ini`。
