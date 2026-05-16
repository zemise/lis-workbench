# Build and run Win32 main application
# Usage: .\scripts\build_main.ps1 [-Clean] [-Run] [-Config Release|Debug]

param(
    [switch]$Clean,
    [switch]$Run,
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$BuildDir = "build\main-app"
$Exe = "$BuildDir\$Config\lis_workbench.exe"

# Detect VS generator
$vsGen = (cmake -G --help 2>&1 | Select-String "Visual Studio \d+ 20\d+" | Select-Object -First 1).ToString().TrimStart("* ").Split("=")[0].Trim()
if (-not $vsGen) { $vsGen = "Visual Studio 17 2022" }
Write-Host "Generator: $vsGen"

# Clean
if ($Clean) {
    Write-Host "==> Cleaning..."
    Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
}

# Configure
if (-not (Test-Path "$BuildDir\CMakeCache.txt")) {
    Write-Host "==> Configuring..."
    cmake -S . -B $BuildDir -G $vsGen -A x64 -DCMAKE_BUILD_TYPE=$Config
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
}

# Build
Write-Host "==> Building ($Config)..."
cmake --build $BuildDir --target main_app --config $Config -j $env:NUMBER_OF_PROCESSORS
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host "==> Ready: $Exe"

# Run
if ($Run) {
    Write-Host "==> Running..."
    Start-Process -FilePath (Join-Path (Get-Location) $Exe)
}
