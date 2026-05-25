# Unified LIS Workbench build helper.
# Usage:
#   .\lis.ps1 build
#   .\lis.ps1 clean
#   .\lis.ps1 run
#   .\lis.ps1 package
#   .\lis.ps1 rebuild-package

param(
    [Parameter(Position = 0)]
    [ValidateSet("build", "clean", "run", "package", "rebuild-package", "help")]
    [string]$Command = "build",

    [string]$Config = "Release",
    [string]$Generator = "",
    [string]$LabelPrintPackagePath = "",
    [string]$AppVersion = "",
    [string]$OutputName = "LISWorkbench-Setup.exe",
    [string]$NsisPath = "",
    [string[]]$CMakeArgs = @()
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $Root "build\main-app"
$InstallerDir = Join-Path $Root "out\windows\installer"

function Show-Help {
    Write-Host "LIS Workbench build helper"
    Write-Host ""
    Write-Host "Commands:"
    Write-Host "  build            Build lis_workbench.exe and Updater.exe"
    Write-Host "  clean            Remove build/main-app"
    Write-Host "  run              Build and run lis_workbench.exe"
    Write-Host "  package          Build Release, create NSIS installer, and create update package"
    Write-Host "  rebuild-package  Clean, build Release, create NSIS installer, and create update package"
    Write-Host ""
    Write-Host "Options:"
    Write-Host "  -LabelPrintPackagePath <path>  LabelPrint release root"
    Write-Host "  -Generator <name>              CMake generator"
    Write-Host "  -Config <Release|Debug>        Build config"
    Write-Host "  -AppVersion <version>          Installer version"
    Write-Host "  -OutputName <name>             Installer output exe name"
}

function Resolve-AppVersion {
    if ($AppVersion) {
        return $AppVersion
    }
    $line = Select-String -Path (Join-Path $Root "src\version.h") -Pattern 'kVersion\s*=' | Select-Object -First 1
    if (-not $line) {
        throw "Cannot read version from src\version.h"
    }
    $value = [regex]::Match($line.Line, '"([^"]+)"').Groups[1].Value
    if (-not $value) {
        throw "Invalid version line: $($line.Line)"
    }
    return $value
}

function Invoke-BuildMain([switch]$Clean, [switch]$Run, [string]$BuildConfig) {
    $buildParams = @{
        Config = $BuildConfig
    }
    if ($Clean) { $buildParams.Clean = $true }
    if ($Run) { $buildParams.Run = $true }
    if ($Generator) { $buildParams.Generator = $Generator }
    if ($LabelPrintPackagePath) { $buildParams.LabelPrintPackagePath = $LabelPrintPackagePath }
    if ($CMakeArgs.Count -gt 0) { $buildParams.CMakeArgs = $CMakeArgs }
    & (Join-Path $PSScriptRoot "build_main.ps1") @buildParams
}

function Resolve-Makensis {
    if ($NsisPath) {
        if (-not (Test-Path $NsisPath)) {
            throw "NSIS makensis.exe not found: $NsisPath"
        }
        return $NsisPath
    }

    $default = Join-Path ${env:ProgramFiles(x86)} "NSIS\makensis.exe"
    if (Test-Path $default) {
        return $default
    }

    $cmd = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    throw "makensis.exe not found. Install NSIS or pass -NsisPath."
}

function Invoke-Package([switch]$CleanFirst) {
    Invoke-BuildMain -Clean:$CleanFirst -BuildConfig "Release"

    $version = Resolve-AppVersion
    New-Item -ItemType Directory -Force $InstallerDir | Out-Null
    $makensis = Resolve-Makensis
    & $makensis `
        "/DAPP_VERSION=$version" `
        "/DAPP_EXE=lis_workbench.exe" `
        "/DBUILD_DIR=..\build\main-app\Release" `
        "/DOUTPUT_DIR=..\out\windows\installer" `
        "/DOUTPUT_NAME=$OutputName" `
        "packaging\LISWorkbench.nsi"

    if ($LASTEXITCODE -ne 0) {
        throw "NSIS packaging failed"
    }

    & (Join-Path $PSScriptRoot "create_update_package.ps1") `
        -Version $version `
        -BuildDir "build\main-app\Release" `
        -OutputRoot "out\windows\update"

    Write-Host "==> Installer: out\windows\installer\$OutputName"
    Write-Host "==> Update manifest: out\windows\update\updates\manifest.json"
    Write-Host "==> Update package: out\windows\update\updates\packages\LISWorkbench-$version-win7-win11.zip"
}

Push-Location $Root
try {
    switch ($Command) {
        "help" {
            Show-Help
        }
        "clean" {
            Write-Host "==> Cleaning..."
            Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
            Write-Host "==> Removed: build\main-app"
        }
        "build" {
            Invoke-BuildMain -BuildConfig $Config
        }
        "run" {
            Invoke-BuildMain -Run -BuildConfig $Config
        }
        "package" {
            Invoke-Package
        }
        "rebuild-package" {
            Invoke-Package -CleanFirst
        }
    }
} finally {
    Pop-Location
}
