# LIS Workbench 自动更新设计

## 目标

为 LIS Workbench 增加双通道自动更新能力：

- 外网更新：从 HTTP 服务器下载新版本，优先支持 GitHub Release，也兼容普通 HTTP 文件服务器。
- 内网/离线更新：从本地共享文件夹读取新版本，例如 `\\server\share\LISWorkbench\updates`。
- 两个通道共用同一套更新流程和文件格式。
- 更新替换由独立小型程序 `Updater.exe` 完成，主程序只负责检查、提示和启动更新器。

首阶段目标是稳定可靠的全量更新，不做差分补丁。

## 当前实现状态

当前分支已开始落地首版基础设施：

- 新增 `update_core` 静态库，包含 manifest 解析、版本比较、文件 SHA-256 校验和文件夹更新源骨架。
- `update_core` 已包含 `FolderUpdateSource` 和 `HttpUpdateSource` 两种更新源，两个通道共用 manifest 解析、包下载/复制、大小校验和 SHA-256 校验。
- 新增 `check_and_fetch_update` 统一流程：读取 manifest、比较当前版本、拉取更新包到缓存目录并返回结果。
- 新增独立 `Updater.exe` target。
- `build_main.ps1` 会同时构建 `lis_workbench.exe` 和 `Updater.exe`。
- NSIS 安装包会随主程序安装和卸载 `Updater.exe`。
- `Updater.exe` 支持 `--package-file` zip 包和 `--package-dir` 已展开目录两种输入；zip 包会先解压到缓存目录下的 `expanded`，再执行备份、替换、失败回滚和重启。
- 主程序系统设置页已新增更新源配置和 `检查更新` 入口，可按 `[Update]` 配置创建文件夹或 HTTP 更新源，在后台线程调用 `check_and_fetch_update`，把更新包缓存到 `ProgramData\LISWorkbench\UpdateCache`，并在用户确认后启动 `Updater.exe`、退出主程序。

尚未实现：

- GitHub Actions 自动生成 manifest 和更新 zip。
- manifest 签名。

## 当前项目基础

当前项目适合采用独立更新器方案：

- 主程序输出为 `lis_workbench.exe`。
- 版本号集中在 `src/version.h` 的 `search::kVersion`。
- GitHub Actions 已能生成 Windows 7-11 兼容安装包和便携 exe。
- NSIS 安装目录默认为 `$PROGRAMFILES64\LISWorkbench`。
- 配置文件为程序目录下的 `ClientConfig.ini`。

需要注意：

- 主程序运行时不能稳定替换自身 exe，必须交给独立 `Updater.exe`。
- Windows 7 对 TLS/证书链兼容性较弱，外网通道不能只依赖 GitHub。
- 安装目录权限虽然当前通过 NSIS 放宽，但更新包仍必须做完整性校验。

## 总体架构

```text
lis_workbench.exe
  |
  |-- 检查更新
  |     |
  |     |-- HttpUpdateSource
  |     |     - GitHub Release raw URL
  |     |     - 内网 HTTP 文件服务器
  |     |
  |     `-- FolderUpdateSource
  |           - 本地目录
  |           - 共享目录 UNC 路径
  |
  |-- 下载/复制更新包到 staging 目录
  |-- 校验 manifest + sha256
  |-- 启动 Updater.exe
  `-- 退出主程序

Updater.exe
  |
  |-- 等待 lis_workbench.exe 退出
  |-- 备份旧版本
  |-- 替换文件
  |-- 启动新版本
  `-- 失败时回滚
```

## 更新源抽象

定义统一更新源接口，主程序只依赖抽象，不关心来源是 HTTP 还是共享目录。

```cpp
struct UpdateManifest;

class IUpdateSource {
public:
    virtual ~IUpdateSource() = default;
    virtual bool fetch_manifest(UpdateManifest& manifest, std::string& error) = 0;
    virtual bool fetch_package(const UpdateManifest& manifest,
                               const std::wstring& targetPath,
                               std::string& error) = 0;
};
```

### HttpUpdateSource

用于外网或内网 HTTP：

- `manifestUrl`：例如 `https://example.com/lis-workbench/manifest.json`
- `baseUrl`：可选，用于解析相对包路径。
- GitHub Release 可以直接使用 release asset 的固定 URL，也可以后续扩展 GitHub API latest 查询。

首阶段不依赖 GitHub API，直接配置 manifest URL，降低认证、限流和 TLS 复杂度。当前实现基于 `URLDownloadToFileW` 下载 manifest 和更新包，兼容普通 HTTP 文件服务器和 GitHub Release 直链。

### FolderUpdateSource

用于内网或离线：

- `rootPath`：例如 `\\server\share\LISWorkbench\updates`
- 读取 `${rootPath}\manifest.json`
- 复制 `${rootPath}\packages\xxx.zip`

此通道对医院内网更稳，也便于人工把更新包放到共享目录。

## 统一文件格式

两个通道都使用同一份 manifest。

```json
{
  "appId": "lis-workbench",
  "version": "v2026.05.25",
  "channel": "stable",
  "minUpdaterVersion": "1.0.0",
  "publishedAt": "2026-05-25T10:00:00+08:00",
  "package": {
    "file": "packages/LISWorkbench-v2026.05.25-win7-win11.zip",
    "sha256": "0123456789abcdef...",
    "size": 12345678
  },
  "notes": [
    "优化常规报告条码打印",
    "更新 LabelPrint 依赖"
  ]
}
```

字段说明：

- `appId`：必须等于 `lis-workbench`，防止拿错包。
- `version`：目标版本号，和 `src/version.h` 保持同样格式。
- `channel`：首阶段固定 `stable`。
- `minUpdaterVersion`：如果将来更新器协议变化，可要求先升级更新器。
- `package.file`：相对 manifest 所在目录的包路径，HTTP 和 Folder 都适用。
- `package.sha256`：强制校验。
- `package.size`：用于提前校验和显示下载大小。
- `notes`：更新提示。

## 更新包格式

首阶段采用全量 zip 包。

```text
LISWorkbench-v2026.05.25-win7-win11.zip
  lis_workbench.exe
  Updater.exe
  resource files...
```

规则：

- 包内可以包含 `Updater.exe`，但运行中的更新器不能覆盖自己。
- `ClientConfig.ini` 不放入更新包，避免覆盖现场配置。
- 更新包只替换程序文件，不修改数据库，不修改用户配置。
- manifest 放在更新发布目录外层，例如 `updates\manifest.json`，不放入 zip 内；这样 manifest 中的 SHA-256 可以稳定校验整个 zip。
- 后续如果需要更新配置模板，只能新增默认值，不能覆盖已有配置。

## 主程序流程

### 手动检查更新

系统设置页已增加 `检查更新`：

1. 读取 `[Update]` 配置。
2. 根据配置创建 `IUpdateSource`。
3. 获取 manifest。
4. 比较 manifest 版本和当前版本。
5. 有新版本时下载或复制更新包到缓存目录。
6. 校验 size 和 sha256。
7. 展示当前版本、新版本和缓存路径，并询问是否立即安装并重启程序。
8. 用户确认后启动 `Updater.exe --package-file`。
9. 主程序退出，由 `Updater.exe` 等待进程结束后完成解压、备份、替换、失败回滚和重启。

### 自动检查更新

后续阶段再做：

- 启动后延迟检查。
- 每天最多检查一次。
- 自动检查只提示，不自动安装。

## Updater.exe 流程

命令行建议：

```text
Updater.exe
  --app-dir "C:\Program Files\LISWorkbench"
  --app-exe "lis_workbench.exe"
  --package-file "C:\ProgramData\LISWorkbench\UpdateCache\LISWorkbench-v2026.05.25-win7-win11.zip"
  --manifest "C:\ProgramData\LISWorkbench\UpdateCache\manifest.json"
  --pid 1234
```

兼容已展开目录：

```text
Updater.exe
  --app-dir "C:\Program Files\LISWorkbench"
  --app-exe "lis_workbench.exe"
  --package-dir "C:\ProgramData\LISWorkbench\UpdateCache\expanded"
  --pid 1234
```

执行流程：

1. 校验参数。
2. 等待 `--pid` 对应主程序退出。
3. 如果提供 manifest，则解析并校验基础字段。
4. 如果提供 `--package-file`，通过 Windows Shell zip 支持解压到缓存目录的 `expanded`。
5. 检查更新包目录内必须存在 `lis_workbench.exe`。
6. 备份当前程序文件到 `backup\时间戳\`。
7. 用更新包目录文件覆盖安装目录，跳过 `ClientConfig.ini`。
8. 如果替换失败，恢复备份。
9. 启动新的 `lis_workbench.exe`。
10. 写入更新日志。

## 目录规划

安装目录：

```text
C:\Program Files\LISWorkbench
  lis_workbench.exe
  Updater.exe
  ClientConfig.ini
```

更新缓存目录：

```text
C:\ProgramData\LISWorkbench\UpdateCache
  manifest.json
  LISWorkbench-v2026.05.25-win7-win11.zip
  expanded\
```

日志目录：

```text
C:\ProgramData\LISWorkbench\Logs
  updater.log
```

说明：

- 下载缓存和日志不建议放在安装目录，避免权限和清理问题。
- `ClientConfig.ini` 暂时保留在安装目录，符合当前项目现状。

## 配置设计

写入 `ClientConfig.ini`：

```ini
[Update]
SourceType=Folder
ManifestUrl=https://example.com/lis-workbench/manifest.json
FolderPath=\\server\share\LISWorkbench\updates
AutoCheck=0
LastCheckTime=
Channel=stable
```

字段说明：

- `SourceType`：`Http` 或 `Folder`。
- `ManifestUrl`：HTTP 更新源入口。
- `FolderPath`：共享文件夹更新源入口。
- `AutoCheck`：首阶段可以只保存，不启用自动检查。
- `Channel`：首阶段固定 `stable`。

## 打包与发布

GitHub Actions 已新增产物：

- 安装包：`LISWorkbench-Setup-vYYYY.MM.DD-win7-win11.exe`
- 便携更新包：`LISWorkbench-vYYYY.MM.DD-win7-win11.zip`
- manifest：`manifest.json`

发布目录示例：

```text
updates/
  manifest.json
  packages/
    LISWorkbench-v2026.05.25-win7-win11.zip
```

GitHub Release 可以上传 manifest 和 zip；内网共享目录直接复制同样结构。当前 CI 先把整个 `updates` 目录上传为 Actions artifact，后续再自动附加到 GitHub Release。

## 安全与校验

首阶段必须做：

- `appId` 校验。
- 版本号校验。
- 文件大小校验。
- SHA-256 校验。
- zip 解压目录穿越防护，禁止 `..\` 和绝对路径。
- 不覆盖 `ClientConfig.ini`。

后续建议做：

- manifest 签名。
- 包签名。
- 更新器自身版本校验。
- HTTPS 证书错误明确提示，不静默忽略。

## 权限策略

当前 NSIS 会给安装目录普通用户修改权限，这让普通用户更新可行。

可选策略：

1. 继续沿用当前权限策略，Updater 不要求管理员权限。
2. 收紧安装目录权限，Updater 请求管理员权限。

首阶段建议采用方案 1，原因是现场部署和更新体验更简单。但必须保留 sha256 校验，后续再引入签名校验。

## 风险点

- Windows 7 访问 GitHub HTTPS 可能失败。
- 医院内网共享目录权限可能不稳定。
- 杀毒软件可能拦截自更新行为。
- 更新器覆盖文件时如果主程序没有完全退出，会失败。
- 如果更新包损坏或被误放，必须能阻止安装。
- 如果安装目录权限被管理员收紧，普通用户更新会失败。

## 分阶段落地

### 阶段 1：设计和基础设施

- 新增本设计文档。
- 明确 manifest 和 zip 包格式。
- 明确配置项。

### 阶段 2：Updater.exe 最小实现

- 新增 `updater` CMake target。
- ~~支持命令行参数。~~
- ~~支持等待主程序退出。~~
- ~~支持 zip 解压、备份、替换、回滚。~~
- ~~写 `updater.log`。~~

### 阶段 3：主程序集成

- ~~新增更新源抽象。~~
- ~~实现 `FolderUpdateSource`。~~
- ~~实现 `HttpUpdateSource`。~~
- ~~新增统一检查和拉取流程。~~
- ~~系统设置页增加更新配置。~~
- ~~设置页增加 `检查更新`。~~
- ~~确认安装 -> 解压 -> 启动 Updater.exe -> 退出主程序。~~

### 阶段 4：CI 发布产物

- ~~GitHub Actions 生成 zip 更新包。~~
- ~~生成 manifest。~~
- ~~上传 artifact。~~
- 后续可自动附加到 GitHub Release。

### 阶段 5：增强

- 自动检查更新。
- manifest 签名。
- 更新器自更新。
- 多更新源优先级。
- 失败上报或日志导出。

## 推荐首版范围

首版只做：

- 手动检查更新。
- 一个启用的更新源：`Http` 或 `Folder` 二选一。
- 全量 zip 更新包。
- SHA-256 校验。
- 独立 `Updater.exe` 替换主程序。
- 保留旧版本备份和失败回滚。

不做：

- 差分更新。
- 静默强制更新。
- 多版本灰度。
- 自动发布到 GitHub Release。
- 数据库结构升级。

这样能先验证现场更新链路，把风险控制在程序文件替换范围内。
