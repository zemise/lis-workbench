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

1. 安装并启动 `lis_workbench.exe`，使用现有 `ClientConfig.ini` 中的数据库连接配置。登录窗口不提供连接设置入口。
2. 启动时可采用本机已核实的已有登录，无需再次输入密码；多个来源须选择。也可自行输入账号，显示匹配姓名后输入密码登录。手动登录优先，取消登录退出程序。账号和密码去除首尾空白，密码区分大小写且不能为空。
3. 系统菜单可退出登录或切换账号；切换先关闭旧页面并丢弃未保存编辑，再推荐当前已有登录或允许手动输入。采用来源的会话在来源锁屏或退出时暂停业务，同一主实例、同账号解锁并重新核实后自动恢复。源用户变化或源进程重启不自动换人，手动登录会话不受其影响。

认证与来源复核只执行查询，不写错误次数、在线状态、密码或数据库日志。采用已有登录仅支持当前已核实版本和同一 Windows 用户／会话；版本、权限或来源不符时使用手动登录。安装路径可通过本机配置 `[Authentication] SourceDirectory` 指定，登录窗口不提供连接设置。

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

查询失败显示固定错误码、安全 SQLSTATE／数字错误号和 `request_id`；排障时可用该 ID 关联日期日志中的查询阶段与耗时。查询诊断不输出原始 ODBC 消息、SQL、患者筛选值或连接凭据。

当前源码版本为 `v2026.10.15.1`，版本以 `src/version.h` 为准。构建产物、本机配置和私有设计资料由 `.gitignore` 排除。
