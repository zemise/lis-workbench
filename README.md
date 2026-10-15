# LIS Workbench

LIS 工作台是基于 C++17 和原生 Win32 的 Windows 桌面应用，用于检验报告查询、标本管理、输血统计与检验业务辅助分析，通过 SQL Server ODBC 连接 LIS 数据库。

## 主要功能

- 检验结果、常规报告与微生物报告查询，支持历史结果比较、追踪、趋势图和导出。
- 标本签收、已签收条码查询及标签打印。
- 输血查询、备血与大量输血统计，以及 HIV、急诊、免疫重复项目和 TAT 统计。
- 质控分析、定时结果核查、门诊查询与常用电话。
- 本机配置管理，以及 LIS、共享目录和 HTTP 更新源。
- 员工账号只读登录，支持采用已有登录、来源选择、退出登录、切换账号和员工姓名显示。
- 双栏登录窗口，支持高对比度、DPI 缩放及紧凑来源布局；小屏幕下收起品牌栏并允许表单滚动，键盘焦点自动保持可见。

## 运行与配置

目标平台为 Windows 7–11，需安装可用的 SQL Server ODBC 驱动，并具备 LIS 数据库访问权限。

工作台在同一 Windows 会话内只运行一个实例，限制覆盖登录窗口、业务窗口、切换账号和暂停界面。重复启动会恢复并唤起已有窗口；从不同目录启动也不会多开，正常退出或异常结束后可重新启动。

1. 安装并启动 `lis_workbench.exe`，使用现有 `ClientConfig.ini` 中的数据库连接配置。登录窗口不提供连接设置入口。
2. 启动时可采用本机已核实的已有登录，无需再次输入密码；多个来源须选择。也可自行输入账号，显示匹配姓名后输入密码登录。手动登录优先，取消登录退出程序。账号和密码去除首尾空白，密码区分大小写且不能为空。
3. 系统菜单可退出登录或切换账号；切换在菜单回调结束后关闭当前主窗口，再进入登录窗口，关闭当前会话不向线程发送退出消息。切换先关闭旧页面并丢弃未保存编辑，再推荐当前已有登录或允许手动输入。采用来源的会话在来源锁屏或退出时暂停业务，同一主实例、同账号解锁并重新核实后自动恢复。源用户变化或源进程重启不自动换人，手动登录会话不受其影响。

认证与来源复核只执行查询，不写错误次数、在线状态、密码或数据库日志。采用已有登录仅支持当前已核实版本和同一 Windows 用户／会话；版本、权限或来源不符时使用手动登录。本机已核实的进程会话作为身份来源，数据库只复核账号、员工和锁定／有效期。账号级 `login_bit`、`login_port`、`login_pcname` 不参与登录采用和会话恢复判断，同一账号在其他电脑登录或退出不会因这些共享字段变化阻断本机工作台。默认从运行中的 来源程序 自动发现安装目录，支持任意盘符和中文路径。`[Authentication] SourceDirectory` 留空或不配置时自动发现；已有非空配置仍作为安装目录限制，如需自动发现应删除该项或留空。来源按组件角色、文件大小和 SHA-256 指纹发现，文件真实性由内置版本清单验证，未知版本不会自动信任。登录窗口显示路径、版本、辅助进程、WMI、锁屏或账号核实失败原因，详细文件哈希和系统错误写入应用日志，不记录命令行或密码。辅助进程启动期间最多等待一秒后重试一次；多个实例或存在未核实实例时不自动采用账号。

Windows 现场可运行 `login_source_runtime_tests.exe --scan` 进行自动路径扫描，或用 `--scan "实际安装目录"` 限制范围。输出扫描完成状态、主进程／来源数量及失败原因；详细哈希和文件错误保存在 `%TEMP%\LIS-login-source-diagnostics`。使用 `--scan` 时退出码 0 表示识别到来源，仍需主程序完成数据库账号核实。使用 `--verify-config "程序目录\ClientConfig.ini"` 可在同一 Windows 用户会话下只读核实数据库，配置先复制到临时文件再读取，不修改原配置；退出码 0 表示至少一个来源账号核实成功。账号诊断区分连接、查询、字段和账号状态失败，只输出安全错误码，不输出密码或原始驱动消息。扩展支持版本时，在 `src/login_source_versions.h` 添加经核实的完整文件组合与协议；当前只收录原先核实的一组版本，不支持未经核实的附加参数或中间启动进程。

配置保存在程序目录的 `ClientConfig.ini`。现场使用的打印机、模板及其他业务资料按需配置，详见 [安装与打包说明](packaging/README_windows_installer.md)。

旧客户端迁移至当前日期版本格式时，首次需使用完整安装包，同时升级主程序与 `Updater.exe`。LIS 更新沿用普通查询的驱动和连接参数，使用独立只读连接，不额外要求现代驱动或强制加密，详见安装说明。

## 构建

### Windows

需要 CMake、Visual Studio C++ Build Tools；生成安装包还需 NSIS。面向 Windows 7 时使用 VS 2022 和对应的 LabelPrint Win7 兼容包。

```powershell
.\lis.ps1 build
.\lis.ps1 run
.\lis.ps1 package -LabelPrintSource github
```

默认本地构建优先使用 LabelPrint 源码；打包可从 GitHub 获取匹配的依赖包。依赖来源、生成器选择与完整命令见 [构建和打包说明](packaging/README_windows_installer.md)。

### macOS 交叉编译

需要 CMake、Make、MinGW-w64、本地 LabelPrint 源码；打包还需 NSIS。

```bash
make compile
make package
```

编译输出位于 `build/windows-x64/`，安装和更新产物位于 `out/windows/`。其他构建目标可通过 `make help` 查看。

## 测试

macOS/Linux 原生 Release 核心测试（数据库访问仍仅支持 Windows）：

```bash
cmake -S . -B build/native-tests -DCMAKE_BUILD_TYPE=Release -DBUILD_QT_GUI=OFF
cmake --build build/native-tests --target lis_tests -j
ctest --test-dir build/native-tests --output-on-failure --no-tests=error --timeout 120
```

原生测试需要 Python；Linux 的密码兼容测试还需要 OpenSSL Crypto 开发库，macOS 使用系统 CommonCrypto，Windows 使用系统 BCrypt。没有 PowerShell 时明确排除离线上传脚本测试。Windows UI、WindowTask、ODBC 连接诊断和 LabelPrint 测试必须在 Windows 验证，交叉编译不能替代运行。

正式 Windows CI 配置 `-DBUILD_TESTING=ON -DLIS_REQUIRE_RELEASE_TESTS=ON`，要求 Python、PowerShell 和 LabelPrint，构建 `lis_tests` 后先执行测试清单检查，再运行 Release CTest；缺少必需测试即阻断打包。登录相关测试覆盖只读数据库认证、来源匹配、采用前后核实、登录窗口、暂停恢复及后台任务隔离。真实来源探测与原软件联动验收需在现场 Windows 环境完成；默认测试不连接生产数据库。

清单检查命令：

```powershell
cmake -DTEST_DIR=build/main-app -DTEST_CONFIG=Release -P cmake/verify_release_tests.cmake
```

## 项目结构

| 路径 | 内容 |
| --- | --- |
| `src/` | Win32 主程序、业务模块、查询核心与更新器 |
| `src_qt/` | 可选 Qt 界面代码 |
| `tests/` | 核心逻辑与工具测试 |
| `scripts/` | 构建、更新包和数据库发布工具 |
| `packaging/` | NSIS 安装脚本与打包说明 |
| `cmake/` | 构建工具链配置 |

## 文档与维护

- [变更日志](CHANGELOG.md)：版本与功能变更。
- [安装与打包说明](packaging/README_windows_installer.md)：环境、依赖、发布与部署。
- 本地设计资料：`docs/private/README.md` 为统一索引，`docs/private/PROJECT_REFERENCE.md` 保存原 README 的完整实现细节及主题导航，供后续开发和 AI 编码查阅。

开发时先读本地文档索引和项目参考，再查对应模块设计，并核对源码与测试。设计资料仅本地保存，不随 Git 克隆或发布包分发，需单独备份。

查询失败显示固定错误码、安全 SQLSTATE／数字错误号和 `request_id`；排障时可用该 ID 关联日期日志中的查询阶段与耗时。查询诊断不输出原始 ODBC 消息、SQL、患者筛选值或连接凭据。TAT 模块及其筛选列表查询不写查询日志、文件日志或调试日志。

当前源码版本为 `v2026.10.15.2`，版本以 `src/version.h` 为准。构建产物、本机配置和私有设计资料由 `.gitignore` 排除。

## 检验周转时间统计

按签收日期查询已有有效报告的明细，采集时间来自申请/执行记录，签收与发送时间优先使用已有 TAT 日志。页面显示时间来源、多报告和数据缺失提示；刷新重新查询数据库，导出当前筛选和排序的明细。

- 默认“标准兼容”口径：保留原多行明细、NULL筛选及条码总数/超时明细数口径，零超时率显示“-”；重复明细可能使兼容超时率超过100%。
- “改良统计”口径：每个条码只计一次，按已有明确超时/正常判定汇总，缺少时间、缺少判定标记和异常单列；分母为可判定条码，阶段率各用该阶段可判定条码。无超时但有有效样本时显示0.00%。仅有分析时长不作为正常判定。
- 科室、签收科室、专业组和医嘱组从只读列表选择；医嘱名称按完整值匹配，多项用顿号分隔。
- 本功能只执行参数化SELECT，不写TAT日志、不修改数据库或规则配置，也不写本机诊断日志。不会根据全局默认阈值或当前时间重新生成超时标记。
