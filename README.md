# LIS Workbench

LIS 工作台是基于 C++17、原生 Win32 和 SQL Server ODBC 的 Windows 桌面应用，面向 Windows 7–11。

## 功能

- 常规、微生物及检验结果查询，支持历史比较、追踪、趋势图和导出；常规报告支持精简置顶查看。
- 标本签收、条码查询与标签打印；支持按项目覆盖补关联多条码合并报告。
- 输血查询及备血、大量输血、HIV、急诊、免疫重复项目、TAT 统计。
- 质控分析、定时结果核查、门诊查询、常用电话和本机设置。
- 员工账号只读登录、采用已有登录、登录来源复核、自动切换账号和单实例运行。
- LIS、共享目录及 HTTP 更新源。

## 使用

需安装可用的 SQL Server ODBC 驱动，并在程序目录配置 `ClientConfig.ini`。现场打印模板与业务资料按需配置。

登录、精简置顶、查询规则、血库手工标签及 TAT 口径见[使用与配置](docs/USAGE.md)；部署与旧客户端升级见[安装与打包说明](packaging/README_windows_installer.md)。

## 构建

Windows 需要 CMake 和 Visual Studio C++ Build Tools，打包另需 NSIS：

```powershell
.\lis.ps1 build
.\lis.ps1 run
.\lis.ps1 package -LabelPrintSource github
```

macOS 安装 CMake、Make、MinGW-w64 及匹配的 LabelPrint 源码后：

```bash
make compile
make package
```

依赖、产物目录和发布流程见[构建与打包说明](packaging/README_windows_installer.md)。

## 测试与维护

```bash
cmake -S . -B build/native-tests -DCMAKE_BUILD_TYPE=Release -DBUILD_QT_GUI=OFF
cmake --build build/native-tests --target lis_tests -j
ctest --test-dir build/native-tests --output-on-failure --no-tests=error --timeout 120
```

- [开发与验证](docs/DEVELOPMENT.md)：平台限制、Windows发布门禁及回归测试。
- [变更日志](CHANGELOG.md)：版本与功能变化。
- 本地设计资料入口：`docs/private/README.md`；不随 Git 或发布包分发，需单独备份。

当前源码版本为 `v2026.11.13`，以 [src/version.h](src/version.h) 为准。构建产物、本机配置和私密排查资料由 `.gitignore` 排除。
