param(
    [Parameter(Position = 0)]
    [string]$Command = "build",

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$RemainingArgs = @()
)

$ErrorActionPreference = "Stop"
$script = Join-Path $PSScriptRoot "scripts\lis.ps1"
if ($Command -like "-*") {
    $RemainingArgs = @($Command) + $RemainingArgs
    $Command = "build"
}
& $script $Command @RemainingArgs
if ($null -ne $LASTEXITCODE) {
    exit $LASTEXITCODE
}
exit 0
