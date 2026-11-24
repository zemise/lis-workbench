# 开发与验证

构建命令与依赖来源见[安装与打包说明](../packaging/README_windows_installer.md)。

## 核心测试与Windows发布门禁


macOS/Linux 原生 Release 核心测试（数据库访问仍仅支持 Windows）：

```bash
cmake -S . -B build/native-tests -DCMAKE_BUILD_TYPE=Release -DBUILD_QT_GUI=OFF
cmake --build build/native-tests --target lis_tests -j
ctest --test-dir build/native-tests --output-on-failure --no-tests=error --timeout 120
```

原生测试需要 Python；Linux 的密码兼容测试还需要 OpenSSL Crypto 开发库，macOS 使用系统 CommonCrypto，Windows 使用系统 BCrypt。没有 PowerShell 时明确排除离线上传脚本测试。Windows UI、WindowTask、ODBC 连接诊断和 LabelPrint 测试必须在 Windows 验证，交叉编译不能替代运行。

正式 Windows CI 配置 `-DBUILD_TESTING=ON -DLIS_REQUIRE_RELEASE_TESTS=ON`，要求 Python、PowerShell 和 LabelPrint，构建 `lis_tests` 后先执行测试清单检查，再运行 Release CTest；缺少必需测试即阻断打包。登录相关测试覆盖只读数据库认证、来源匹配、采用前后核实、登录窗口、来源复核宽限期、自动换人、超时退出及后台任务隔离。真实来源探测与原软件联动验收需在现场 Windows 环境完成；默认测试不连接生产数据库。

清单检查命令：

```powershell
cmake -DTEST_DIR=build/main-app -DTEST_CONFIG=Release -P cmake/verify_release_tests.cmake
```

## 条码项目覆盖回归

`barcode_report_matching_tests` 包含身份、类型、仪器与专业组隔离、完整项目覆盖、结果缺失、审核时序、多候选及固定签收日期窗口测试；还验证补关联后的状态筛选、原条码保留及报告字段一致性。

Windows额外验证预先取消和取消签收历史视图不会发送补查SQL。该目标纳入 `lis_tests` 和正式发布测试清单，默认不连接真实数据库。

只读实测、调查材料及凭据只保存在本地 `docs/private/`，不得强制提交或打包。涉及真实数据的检查遵守本地只读排查入口，离线自动化测试通过不能替代正式工作站的界面、性能及数据关联验收。

## 已签收条码查询性能

`v2026.11.16` 在同一台 Windows 虚拟机上与 `v2026.11.13` 的列表准备逻辑对照。每个条码约 3 条合成医嘱，每个场景取 11 次运行的中位数，单位为毫秒。

| 医嘱明细数 | 默认顺序，原版 → 新版 | 按医嘱内容排序，原版 → 新版 |
| ---: | ---: | ---: |
| 17,473 | 3.21 → 11.27 | 54.28 → 20.35 |
| 186,431 | 53.28 → 131.15 | 967.61 → 255.32 |

对照只测合并、归组和排序。数据库取数、Windows 控件首帧和端到端耗时待现场用相同筛选条件复测。诊断日志中，`main_query_and_fetch_ms` 记录主查询和取数，`coverage_ms` 记录报告补关联，`group_ms`、`sort_ms` 和 `listview_ms` 分别记录列表归组与合并、排序和绑定，`query_to_ready_ms` 记录从发起查询到列表绑定和状态更新完成。

## 常规报告置顶窗口验收

Windows 离线窗口回归使用 `regular_report_compact_tests`，与主程序共用 `LIS_REGULAR_REPORT_SOURCES` 中的生产实现，创建真实 MDI 和列表控件，使用合成数据，不连接数据库：

```powershell
cmake --build build/main-app --config Release --target regular_report_compact_tests
ctest --test-dir build/main-app -C Release -R "^regular_report_compact_tests$" --output-on-failure
```

回归覆盖窗口置顶、隐藏和恢复本程序其他窗口、关闭/Esc及直接销毁宿主、原页签与选择恢复、完整自定义列宽恢复、控件位置及窗口样式恢复、反复切换、左侧首列（行号）及项目名称/说明隐藏与恢复、默认视口恰好容纳组合项目/英文/结果三列、右侧默认视口容纳样本号/姓名两列、性别和年龄横向滚动查看、固定右侧宽度、缩小窗口、隐藏日期按钮与查询总数、仪器和日期摘要、字体广播、暂停阻止进入，以及关闭模块和会话结束后的窗口清理。该目标纳入 `lis_tests` 和正式 Windows 发布门禁；交叉编译仅验证构建，不能替代 Windows 实际运行。

窗口可见性和焦点相关测试须运行在已登录的用户桌面。Parallels CLI 使用 `prlctl exec <虚拟机名称> --current-user ...`，服务桌面会导致可见性断言失败。当前版已用 Windows 11 登录桌面验证默认的右侧两列布局、隐藏期间缩放主窗口、MDI 最大化和完整查询栏恢复。生产数据库联动仍需现场验收。

现场另需验证：查询中进入/退出置顶、右侧切换报告和自动刷新、失败提示位于置顶窗口上方、操作其他软件时保持可见且不抢焦点、精简窗口任务栏最小化/恢复、多屏不同缩放、反复切换后的滚动位置与列宽，来源恢复时保留页面，以及切换账号和退出时无残留窗口。

粪便常规标红按结果明细的 `LS_AS_REPENTRY.GROUP_CODE` 与列表使用的参考区间文字判断。完整页和精简置顶共用结果列表的绘制逻辑。上述离线测试也覆盖选中状态、临时编辑、参考区间不同的颜色和性状等项目、空白结果以及其他组合。

## 常规报告结果录入与保存

- `src/regular_report_result_editor.cpp` 管理结果编辑框、候选表格和编辑结束时的草稿处理。候选筛选文字仅在明确确认后进入保存流程。离开字典项目时丢弃筛选文字，避免刷新时草稿覆盖数据库结果。
- `src/regular_report_module.cpp` 管理验单选择、结果缓存和保存反馈。回车保存按结果记录 ID 提交当前项目。F2 汇总当前连接和验单下的草稿。保存后按记录 ID 恢复到对应的结果行，避免刷新排序后定位错行。
- `src/search_core.cpp` 按组合的 `LS_AS_GROUP.orderby` 和项目的 `LS_AS_ITEM.SEQ_NO` 提供常规结果的展示顺序。录入类型取自 `LS_AS_ITEM.TYPE`，候选来自当前项目在 `LS_AS_RESULT` 中的有效记录，并按数字编号排序。
- 单项保存与 F2 共用事务。写入前检查 `CONF`、验单号、结果记录 ID 和旧值，按配置执行参考范围判断、重算、历史记录及复检或危急值处理。初审完成标记由整单保存路径处理。项目删除在事务内复核范围，将相应明细标记为 `DELETE_BIT=1`。

`regular_report_compact_tests` 同时覆盖结果区三个按钮、数字字符限制、回车定位、候选两列、按名称和编号筛选、双击确认、点击外部后的草稿与重新打开。测试使用合成数据，运行方式同上。2026-11-24 的 MinGW 主程序构建、Parallels Windows 11 登录桌面回归和常规报告 5 项原生核心测试通过。

数据库联动验收使用测试验单，核对增删范围、回车单项保存和 F2 整单保存、并发修改拦截、审核状态切换，以及按现场配置触发的重算和提示。窗口离线回归不连接 LIS 数据库。故障可关联日志中的 `regular_report add items failed`、`regular_report delete items failed` 和 `regular_report save results failed`。

## 常规报告复查与取消审核

- `src/regular_report_module.cpp` 管理右键目标、取消前的报告快照、会话复核和操作后的列表刷新。复查标记使用 `LS_AS_REPORT.NeedInspect`，F3 的审核写入条件也检查该字段。
- `src/regular_report_cancel_audit_dialog.cpp` 管理原因字典项、预设原因回填和当前账号密码复核。原因类型与预设原因相同时合并显示，选项仍按字典项 ID 和类型保存。
- `src/regular_report_cancel_audit_sql.inc` 在同一个 SQL Server 事务内锁定并复核报告，检查权限、时限、原因和共享条码。提交时同步处理报告、关联条码、按配置启用的住院医嘱回退、危急值清理及日志。API 返回状态后才显示相应的操作结果。

`regular_report_compact_tests` 使用离线窗口和合成理由，覆盖右侧首列、复查行着色、原因类型文字和预设原因回填。隔离数据库回归使用以下命令，测试容器须标记为 `lis-workbench.cancel-audit-test=true`。

```sh
python3 tests/regular_report_cancel_audit_db.py <测试容器>
```

脚本创建兼容级别 100 的独立合成库，执行与工作台相同的 SQL 批次，覆盖已审核、已发送和危急值验单、复查标记保留、共享条码、并发发送及失败后整笔回滚。

现场验收仍需用测试验单走完 Windows ODBC 写回。危急值通知表缺少报告号索引，需按代表性数据量测量清理耗时和锁影响。原 Trasen 的发送写入仅以报告号为条件，发送前已通过界面检查的操作仍可覆盖先提交的取消。混用部署需验证发送端提交时也检查验单审核状态。启用工作台暂未接入的外部撤回接口时，使用原 Trasen 处理。

## LIS 启动更新验收

本地已通过 MinGW Windows 主程序、更新器及单实例测试交叉编译，19 项原生核心测试全部通过。单实例测试增加启动更新窗口的重复启动唤醒回归；Windows 测试由发布 CI 执行。上述验证不代表已完成真实 LIS 更新安装验收。Windows 测试环境还需验证：

- LIS 源发布高于本机版本的包，在手动登录或采用已有登录出现前自动下载、校验、安装重启，重启后进入登录；关闭“自动检查”或已有当天检查记录时仍执行。
- 相同或更低版本、未发布、数据库离线及首次未配置连接时正常进入登录；更新器缺失／不兼容、缓存或安装磁盘空间不足、包损坏时不安装，提示后进入登录。
- 检查或下载中关闭启动窗口，取消并清理本次下载目录后退出，不弹出登录；已校验的共享缓存保留供下次复用。
- 注销、切换账号、重复启动已有实例不重复更新；共享目录及 HTTP 源的自动检查和手动更新保持原有行为。

## 文档维护

README保持项目概览、快速构建与链接入口；用户操作写入[使用与配置](USAGE.md)，构建发布约定写入安装说明，版本变化写入[CHANGELOG](../CHANGELOG.md)。本地实现细节的统一入口为 `docs/private/README.md`；本机设计资料不随Git分发，需单独备份。
