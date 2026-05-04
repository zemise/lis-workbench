# Build and run Qt 5.15 frontend on Windows
# Usage: .\scripts\build_qt.ps1 [-Clean] [-Run] [-Config Release|Debug]

param(
    [switch]$Clean,
    [switch]$Run,
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$QtDir = "C:\Qt\5.15.2\msvc2019_64"
$BuildDir = "build\windows-qt"
$Exe = "$BuildDir\$Config\result_search_qt.exe"

# Detect VS generator
$vsGen = (cmake -G --help 2>&1 | Select-String "Visual Studio \d+ 20\d+" | Select-Object -First 1).ToString().TrimStart("* ").Split("=")[0].Trim()
if (-not $vsGen) { $vsGen = "Visual Studio 17 2022" }
Write-Host "Generator: $vsGen"

# Clean
if ($Clean) {
    Write-Host "==> Cleaning build dir..."
    Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
}

# Configure
if (-not (Test-Path "$BuildDir\CMakeCache.txt")) {
    Write-Host "==> Configuring..."
    cmake -S . -B $BuildDir -G $vsGen -A x64 `
        -DCMAKE_PREFIX_PATH=$QtDir `
        -DBUILD_QT_GUI=ON
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
}

# Build
Write-Host "==> Building ($Config)..."
cmake --build $BuildDir --target result_search_qt --config $Config -j $env:NUMBER_OF_PROCESSORS
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

# Deploy Qt DLLs
Write-Host "==> Deploying Qt DLLs..."
$env:Path = "$QtDir\bin;$env:Path"
windeployqt $Exe --no-translations --no-compiler-runtime 2>&1 | Out-Null
Write-Host "==> Ready: $Exe"

# Run
if ($Run) {
    Write-Host "==> Running..."
    & $Exe
}
