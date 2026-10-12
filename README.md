# LIS Workbench

LIS 工作台是基于 C++17 和原生 Win32 的 Windows 桌面应用，用于检验报告查询、标本管理、输血统计与检验业务辅助分析，通过 SQL Server ODBC 连接 LIS 数据库。

## 主要功能

- 检验结果、常规报告与微生物报告查询，支持历史结果比较、追踪、趋势图和导出。
- 标本签收、已签收条码查询及标签打印。
- 输血查询、备血与大量输血统计，以及 HIV、急诊、免疫重复项目和 TAT 统计。
- 质控分析、定时结果核查、门诊查询与常用电话。
- 本机配置管理，以及 LIS、共享目录和 HTTP 更新源。

## 运行与配置

目标平台为 Windows 7–11，需安装可用的 SQL Server ODBC 驱动，并具备 LIS 数据库访问权限。

1. 安装并启动 `lis_workbench.exe`。
2. 在系统设置中填写数据库服务器、数据库名称和连接凭据，测试连接后保存。
3. 打开所需业务模块，设置查询条件并查询。

配置保存在程序目录的 `ClientConfig.ini`。现场使用的打印机、模板及其他业务资料按需配置，详见 [安装与打包说明](packaging/README_windows_installer.md)。

旧客户端迁移至当前日期版本格式时，首次需使用完整安装包，同时升级主程序与 `Updater.exe`。

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

版本以 `src/version.h` 为准。构建产物、本机配置和私有设计资料由 `.gitignore` 排除。
