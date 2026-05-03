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

## 实机打包（Windows）

前提：安装 Qt 5.15.2、CMake、NSIS。

```cmd
:: Win32 版
cmake -S . -B build/windows-x64 -G "Visual Studio 17 2022" -A x64
cmake --build build/windows-x64 --config Release -j

:: Qt 版
cmake -S . -B build/windows-qt -G "Visual Studio 17 2022" -A x64 ^
  -DBUILD_QT_GUI=ON ^
  -DCMAKE_PREFIX_PATH=C:\Qt\5.15.2\msvc2019_64
cmake --build build/windows-qt --config Release -j

:: NSIS 安装包
makensis /DAPP_VERSION=v2026.05.03 /DBUILD_DIR=build\windows-x64 ^
  /DOUTPUT_DIR=out\windows\installer packaging\ResultSearch.nsi
```

## 运行时依赖

- 目标 Windows 需安装 SQL Server ODBC 驱动（ODBC Driver 17/18 for SQL Server 或系统自带）
- Qt 版需附带 Qt5Widgets.dll、Qt5Core.dll、Qt5Gui.dll 等（或用 `windeployqt` 自动收集）
- Win32 版用 MinGW 静态链接，无需额外 GCC 运行时
