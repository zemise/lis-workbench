# cpp_search Windows 打包说明

## CI 自动构建

每次 push 到 `main` 或 `feat/*` 分支，GitHub Actions 自动执行：

| Job | Runner | 产物 |
|-----|--------|------|
| `build-win32` | ubuntu-24.04 (MinGW-w64) | `result_search.exe` |
| `build-qt` | windows-2022 (MSVC + Qt 5.15) | `result_search_qt.exe` + `ResultSearch-Qt-Setup.exe` |

产物可从 Actions 页面下载（Artifacts）。

## 本地打包（macOS）

从项目根目录执行：

```bash
scripts/build_windows_package.sh
```

输出：

```text
out/windows/portable/ResultSearch/result_search.exe
out/windows/installer/ResultSearch-Setup.exe
```

## 实机开发（Windows）

前提：安装 Qt 5.15.2、CMake、Visual Studio 2022/2026。

```powershell
# 一键构建 Qt 版（自动检测 VS 版本）
.\scripts\build_qt.ps1 -Run

# 全新构建
.\scripts\build_qt.ps1 -Clean -Run
```

## 实机打包（Windows）

```powershell
# Win32 版（MinGW 交叉编译或 VS 原生）
cmake -S . -B build/windows-x64 -G "Visual Studio 18 2026" -A x64
cmake --build build/windows-x64 --config Release -j

# Qt 版
.\scripts\build_qt.ps1

# NSIS 安装包
makensis /DAPP_VERSION=v2026.05.03 /DBUILD_DIR=build\windows-x64\Release ^
  /DOUTPUT_DIR=..\out\windows\installer packaging\ResultSearch.nsi
```

## Visual Studio 版本

| VS 版本 | CMake Generator |
|---------|----------------|
| VS 2022 (17.x) | `"Visual Studio 17 2022"` |
| VS 2026 (18.x) | `"Visual Studio 18 2026"` |

## 运行时依赖

- 目标 Windows 需安装 SQL Server ODBC 驱动（ODBC Driver 17/18 for SQL Server 或系统自带）
- Qt 版需附带 Qt5Widgets.dll、Qt5Core.dll、Qt5Gui.dll 等（或用 `windeployqt` 自动收集）
- **gnuplot**（趋势图渲染）：`gnuplot.exe` 放在程序目录 `gnuplot\` 子目录下，或系统 PATH 中
- Win32 版用 MinGW 静态链接，无需额外 GCC 运行时

## 捆绑 gnuplot

```powershell
# 下载 gnuplot（开发机一次性）
winget install gnuplot.gnuplot

# 复制到构建输出目录（每次打包前）
mkdir build\windows-qt\Release\gnuplot
copy "C:\Program Files\gnuplot\bin\gnuplot.exe" build\windows-qt\Release\gnuplot\

# NSIS 打包（/nonfatal 标志 — 无 gnuplot 时跳过不报错）
makensis ... packaging\ResultSearch.nsi
```
