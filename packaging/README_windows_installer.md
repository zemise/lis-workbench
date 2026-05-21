# lis-workbench Windows 打包说明

## GitHub Actions 自动打包

每次 push 到 `main` 或 `feat/*` 分支，或手动执行 `workflow_dispatch`，GitHub Actions 会在 `windows-2022` runner 上构建 Win32 主程序并生成 NSIS 安装包。

当前 CI 产物面向 Windows 7 到 Windows 11 的兼容安装包：

| Job | Runner | Toolchain | LabelPrint | 产物 |
|-----|--------|-----------|------------|------|
| `windows-installer` | `windows-2022` | Visual Studio 2022 x64 | `labelprint-v1.2.2-windows-x64-vs2022-win7.zip` | `LISWorkbench-Setup-<version>-win7-win11.exe` |

CI 会从 LabelPrint GitHub Release 下载 `v1.2.2` 的 Win7 兼容包，通过 `build_main.ps1 -LabelPrintPackagePath` 传给 CMake，再用 `packaging/LISWorkbench.nsi` 打包。产物可从 Actions 页面下载 Artifacts。

说明：GitHub Actions 不能真正提供 Windows 7 runner 做运行验证；这里的 “Win7-Win11” 表示使用 VS2022、静态 MSVC runtime、Win7 兼容宏和 LabelPrint Win7 兼容包构建出的安装包，目标是覆盖 Windows 7 到 Windows 11。

## 本地打包（macOS）

从项目根目录执行：

```bash
scripts/build_windows_package.sh
```

输出：

```text
out/windows/portable/LISWorkbench/result_search.exe
out/windows/installer/LISWorkbench-Setup.exe
```

## 实机开发（Windows）

前提：安装 CMake、Visual Studio 2022/2026。

```powershell
# 一键构建主程序（自动检测 VS 版本，默认优先 VS 2022）
.\scripts\build_main.ps1 -Run

# 全新构建
.\scripts\build_main.ps1 -Clean -Run
```

## 实机打包（Windows）

```powershell
# Win32 主程序。默认优先使用 VS 2022，并静态链接 MSVC runtime，便于兼容 Windows 7。
# 正式打包建议使用 LabelPrint release zip 解压目录，而不是依赖相邻源码目录。
.\scripts\build_main.ps1 -Clean -Config Release -LabelPrintPackagePath "C:\Deps\LabelPrint\labelprint-v1.2.2-windows-x64-vs2022-win7"

# NSIS 安装包
New-Item -ItemType Directory -Force out\windows\installer
& "C:\Program Files (x86)\NSIS\makensis.exe" /DAPP_VERSION=v2026.05.21 /DAPP_EXE=lis_workbench.exe /DBUILD_DIR=..\build\main-app\Release /DOUTPUT_DIR=..\out\windows\installer /DOUTPUT_NAME=LISWorkbench-Setup.exe packaging\LISWorkbench.nsi
```

如果只面向 Windows 10/11，可以使用 VS 2026 对应的 LabelPrint release 包：

```powershell
.\scripts\build_main.ps1 -Clean -Config Release -Generator "Visual Studio 18 2026" -LabelPrintPackagePath "C:\Deps\LabelPrint\labelprint-v1.2.2-windows-x64-vs2026"
```

`-LabelPrintPackagePath` 会传给 CMake 的 `CMAKE_PREFIX_PATH`，使主项目优先通过 `find_package(LabelPrint 1.2 CONFIG)` 使用 release 包。该路径必须是 release zip 解压后的根目录，且包含 `cmake\LabelPrintConfig.cmake`；路径无效时脚本会直接停止，避免正式包误回退到本机源码。若不传该参数，构建仍会回退到 `LIS_LABELPRINT_DIR` 指向的源码目录，默认是 `..\..\020 LabelPrint\LabelPrint`，适合本机联调。

需要传入其他 CMake 变量时，可使用 `-CMakeArgs`：

```powershell
.\scripts\build_main.ps1 -Clean -Config Release -CMakeArgs "-DCMAKE_PREFIX_PATH=C:\Deps\LabelPrint\labelprint-v1.2.2-windows-x64-vs2022-win7","-DLIS_LABELPRINT_DIR=C:\src\LabelPrint"
```

`build_main.ps1` 会优先选择已安装的 VS 2022；如果本机没有 VS 2022，会自动退到已安装的 VS 2026。Windows 7 包必须安装 VS 2022 Build Tools 后再构建，也不要把 VS 2026 的 `Microsoft.VC*.CRT` 目录打进安装包。较新的运行库可能依赖 Win8+ 入口，例如 `CreateFile2`、`GetSystemTimePreciseAsFileTime`，在 Win7 上会启动失败。只面向 Windows 10/11 时可以显式使用：

```powershell
.\scripts\build_main.ps1 -Clean -Config Release -Generator "Visual Studio 18 2026"
```

安装脚本会在写入新程序前清理安装目录中旧包残留的 `MSVCP*.dll`、`VCRUNTIME*.dll`、`ucrtbase.dll` 和 `api-ms-win-crt-*.dll`。如果手动复制过运行库，建议先卸载旧版本或删除安装目录后再安装。

## Visual Studio 版本

| VS 版本 | CMake Generator |
|---------|----------------|
| VS 2022 (17.x) | `"Visual Studio 17 2022"` |
| VS 2026 (18.x) | `"Visual Studio 18 2026"` |

## 运行时依赖

- 目标 Windows 需安装 SQL Server ODBC 驱动（ODBC Driver 17/18 for SQL Server 或系统自带）
- Win32 主程序默认 `LIS_STATIC_MSVC_RUNTIME=ON`，VS 原生 Release 包不再需要额外携带 `MSVCP140.dll` / `VCRUNTIME140.dll`。
- 当前 Win32 目标按 Windows 7 兼容编译，CMake 固定 `WINVER/_WIN32_WINNT=0x0601`，并避免直接导入 Win8/Win10 专用入口。若目标机仍提示 `CreateFile2`、`GetSystemTimePreciseAsFileTime` 等入口点缺失，优先检查是否使用了 VS 2026 构建产物，或安装目录中是否混入了较新且不兼容 Win7 的第三方 DLL / MSVC 运行库。
- Qt 版需附带 Qt5Widgets.dll、Qt5Core.dll、Qt5Gui.dll 等（或用 `windeployqt` 自动收集）
- **Qwt**（趋势图渲染）：`qwt.dll` + `Qt5OpenGL.dll` 由 CMake post-build 自动复制到 exe 目录。
- MinGW 静态链接的 Win32 版无需额外 GCC 运行时

## 构建 Qwt

```powershell
# 1. 克隆源码
git clone https://git.code.sf.net/p/qwt/git C:\qwt-src
cd C:\qwt-src && git checkout qwt-6.3.0

# 2. 在 x64 Native Tools 终端编译
qmake qwt.pro
nmake

# 3. CMake 自动检测 C:\qwt-src 下的库和头文件
```
