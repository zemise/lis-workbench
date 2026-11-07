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

正式 Windows CI 配置 `-DBUILD_TESTING=ON -DLIS_REQUIRE_RELEASE_TESTS=ON`，要求 Python、PowerShell 和 LabelPrint，构建 `lis_tests` 后先执行测试清单检查，再运行 Release CTest；缺少必需测试即阻断打包。登录相关测试覆盖只读数据库认证、来源匹配、采用前后核实、登录窗口、暂停恢复及后台任务隔离。真实来源探测与原软件联动验收需在现场 Windows 环境完成；默认测试不连接生产数据库。

清单检查命令：

```powershell
cmake -DTEST_DIR=build/main-app -DTEST_CONFIG=Release -P cmake/verify_release_tests.cmake
```

## 条码项目覆盖回归

`barcode_report_matching_tests` 包含身份、类型、仪器与专业组隔离、完整项目覆盖、结果缺失、审核时序、多候选及固定签收日期窗口测试；还验证补关联后的状态筛选、原条码保留及报告字段一致性。

Windows额外验证预先取消和取消签收历史视图不会发送补查SQL。该目标纳入 `lis_tests` 和正式发布测试清单，默认不连接真实数据库。

只读实测、调查材料及凭据只保存在本地 `docs/private/`，不得强制提交或打包。涉及真实数据的检查遵守本地只读排查入口，离线自动化测试通过不能替代正式工作站的界面、性能及数据关联验收。

## 文档维护

README保持项目概览、快速构建与链接入口；用户操作写入[使用与配置](USAGE.md)，构建发布约定写入安装说明，版本变化写入[CHANGELOG](../CHANGELOG.md)。本地实现细节的统一入口为 `docs/private/README.md`；本机设计资料不随Git分发，需单独备份。
