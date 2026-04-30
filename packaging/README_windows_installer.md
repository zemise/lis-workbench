# cpp_search Windows 打包说明

本目录用于生成 `检验结果查询` 的 Windows 便携目录和安装包。

推荐从项目根目录执行：

```bash
scripts/build_windows_package.sh
```

输出：

```text
out/windows/portable/ResultSearch/result_search.exe
out/windows/installer/ResultSearch-Setup.exe
```

`result_search.exe` 使用 MinGW 静态链接 C++/GCC 运行时，避免在 Windows 上额外依赖 `libstdc++-6.dll`、`libgcc_s_seh-1.dll` 等运行库文件。

安装后程序会在安装目录生成并维护：

```text
result_search.ini
```

数据库设置页面填写的服务器、初始数据库、用户名、密码会保存到该文件。

运行数据库查询仍然需要目标 Windows 系统安装 SQL Server ODBC 驱动，例如：

- ODBC Driver 18 for SQL Server
- ODBC Driver 17 for SQL Server
- 系统自带 SQL Server ODBC 驱动
