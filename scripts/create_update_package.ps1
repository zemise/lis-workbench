param(
    [string]$Version,
    [string]$BuildDir = "build\main-app\Release",
    [string]$OutputRoot = "out\windows\update",
    [string]$Channel = "stable"
)

$ErrorActionPreference = "Stop"

if (-not $Version) {
    $line = Select-String -Path "src\version.h" -Pattern 'kVersion\s*=' | Select-Object -First 1
    if (-not $line) {
        throw "Cannot read version from src\version.h"
    }
    $Version = [regex]::Match($line.Line, '"([^"]+)"').Groups[1].Value
    if (-not $Version) {
        throw "Invalid version line: $($line.Line)"
    }
}

$appExe = Join-Path $BuildDir "lis_workbench.exe"
$updaterExe = Join-Path $BuildDir "Updater.exe"
if (-not (Test-Path $appExe)) {
    throw "Missing $appExe"
}
if (-not (Test-Path $updaterExe)) {
    throw "Missing $updaterExe"
}

$updatesRoot = Join-Path $OutputRoot "updates"
$packagesDir = Join-Path $updatesRoot "packages"
$stageDir = Join-Path $OutputRoot "package-root"
$packageName = "LISWorkbench-$Version-win7-win11.zip"
$packagePath = Join-Path $packagesDir $packageName
$manifestPath = Join-Path $updatesRoot "manifest.json"

Remove-Item -LiteralPath $stageDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stageDir, $packagesDir | Out-Null

Copy-Item -LiteralPath $appExe -Destination $stageDir
Copy-Item -LiteralPath $updaterExe -Destination $stageDir
Get-ChildItem -Path $BuildDir -Filter *.dll -File -ErrorAction SilentlyContinue |
    Copy-Item -Destination $stageDir

Remove-Item -LiteralPath $packagePath -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $packagePath -Force

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $packagePath).Hash.ToLowerInvariant()
$size = (Get-Item -LiteralPath $packagePath).Length
$publishedAt = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")

$manifest = [ordered]@{
    appId = "lis-workbench"
    version = $Version
    channel = $Channel
    minUpdaterVersion = "1.0.0"
    publishedAt = $publishedAt
    package = [ordered]@{
        file = "packages/$packageName"
        sha256 = $hash
        size = $size
    }
    notes = @()
}

$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host "Update package: $packagePath"
Write-Host "Manifest: $manifestPath"
