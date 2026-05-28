param(
    [Parameter(Position = 0)]
    [string]$Command = "build",

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$RemainingArgs = @()
)

$ErrorActionPreference = "Stop"
$script = Join-Path $PSScriptRoot "scripts\lis.ps1"
& $script $Command @RemainingArgs
if ($null -ne $LASTEXITCODE) {
    exit $LASTEXITCODE
}
exit 0
