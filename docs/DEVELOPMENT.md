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

## 常规报告置顶窗口验收

Windows 离线窗口回归使用 `regular_report_compact_tests`，与主程序共用 `LIS_REGULAR_REPORT_SOURCES` 中的生产实现，创建真实 MDI 和列表控件，使用合成数据，不连接数据库：

```powershell
cmake --build build/main-app --config Release --target regular_report_compact_tests
ctest --test-dir build/main-app -C Release -R "^regular_report_compact_tests$" --output-on-failure
```

回归覆盖窗口置顶、隐藏和恢复本程序其他窗口、关闭/Esc及直接销毁宿主、原页签与选择恢复、完整自定义列宽恢复、控件位置及窗口样式恢复、反复切换、五列显示、固定右侧宽度、缩小窗口、隐藏查询总数、按钮位置、字体广播、暂停阻止进入，以及关闭模块和会话结束后的窗口清理。该目标纳入 `lis_tests` 和正式 Windows 发布门禁；交叉编译仅验证构建，不能替代 Windows 实际运行。

本次本地验证：MinGW 主程序及窗口回归目标交叉编译通过，常规报告比较、追踪、仪器映射和条码范围 4 项原生核心测试通过。Windows 窗口回归与现场界面验收尚未运行。

现场另需验证：查询中进入/退出置顶、右侧切换报告和自动刷新、失败提示位于置顶窗口上方、操作其他软件时保持可见且不抢焦点、精简窗口任务栏最小化/恢复、多屏不同缩放、反复切换后的滚动位置与列宽，来源恢复时保留页面，以及切换账号和退出时无残留窗口。

## LIS 启动更新验收

本地已通过 MinGW Windows 主程序、更新器及单实例测试交叉编译，19 项原生核心测试全部通过。单实例测试增加启动更新窗口的重复启动唤醒回归；Windows 测试由发布 CI 执行。上述验证不代表已完成真实 LIS 更新安装验收。Windows 测试环境还需验证：

- LIS 源发布高于本机版本的包，在手动登录或采用已有登录出现前自动下载、校验、安装重启，重启后进入登录；关闭“自动检查”或已有当天检查记录时仍执行。
- 相同或更低版本、未发布、数据库离线及首次未配置连接时正常进入登录；更新器缺失／不兼容、缓存或安装磁盘空间不足、包损坏时不安装，提示后进入登录。
- 检查或下载中关闭启动窗口，取消并清理本次下载目录后退出，不弹出登录；已校验的共享缓存保留供下次复用。
- 注销、切换账号、重复启动已有实例不重复更新；共享目录及 HTTP 源的自动检查和手动更新保持原有行为。

## 文档维护

README保持项目概览、快速构建与链接入口；用户操作写入[使用与配置](USAGE.md)，构建发布约定写入安装说明，版本变化写入[CHANGELOG](../CHANGELOG.md)。本地实现细节的统一入口为 `docs/private/README.md`；本机设计资料不随Git分发，需单独备份。
