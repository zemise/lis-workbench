# Offline test only. Extracts a pure validator; never executes the upload entry point.
#Requires -Version 5.1
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$tokens=$null; $errors=$null
$source=Join-Path (Split-Path $PSScriptRoot -Parent) 'scripts/db_update/upload.ps1'
$ast=[System.Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
$function=$ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Assert-IndexDefinitions'},$false)
if ($null -eq $function) { throw 'Index validator missing.' }
Invoke-Expression $function.Extent.Text

foreach ($name in @('Test-PublishConfirmation','Get-DatabaseFailureMessage','Get-UploadFailureMessage','Resolve-CertificateTrust','Test-ReleaseVersion')) {
    $helper=$ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$false)
    if ($null -eq $helper) { throw "Missing helper: $name" }
    Invoke-Expression $helper.Extent.Text
}
foreach ($version in @('v2026.10.12','v2026.10.12.1','v2026.10.12.10','v2024.02.29','v2026.10.12.999999999')) {
    if (-not (Test-ReleaseVersion $version)) { throw "Valid release rejected: $version" }
}
foreach ($version in @('v2026.02.29','v0000.01.01','v2026.10.12.0','v2026.10.12.01','v2026.10.12.1000000000','v2026.10.12.1.1',"v2026.10.12.1`n")) {
    if (Test-ReleaseVersion $version) { throw "Invalid release accepted: $version" }
}
# Validate a real local archive through the upload validator, never opening SQL.
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($name in @('Get-LocalHash','Assert-Package')) {
    $helper=$ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$false)
    Invoke-Expression $helper.Extent.Text
}
$fixturePath=Join-Path ([IO.Path]::GetTempPath()) (([guid]::NewGuid().ToString())+'.zip')
try {
    $archive=[IO.Compression.ZipFile]::Open($fixturePath,[IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in @('lis_workbench.exe','Updater.exe')) {
            $entry=$archive.CreateEntry($file)
            $stream=$entry.Open()
            try { $stream.WriteByte(1) } finally { $stream.Dispose() }
        }
    } finally { $archive.Dispose() }
    $manifest=[pscustomobject]@{
        appId='lis-workbench'; channel='stable'; version='v2026.10.12.4'; minUpdaterVersion='1.1.0'
        package=[pscustomobject]@{file='LISWorkbench-v2026.10.12.4-win7-win11.zip'; size=(Get-Item $fixturePath).Length; sha256=(Get-LocalHash $fixturePath)}
    }
    Assert-Package $manifest $fixturePath
    $manifest.package.sha256='0'*64
    $rejected=$false
    try { Assert-Package $manifest $fixturePath } catch { $rejected=$true }
    if (-not $rejected) { throw 'Tampered package accepted.' }
} finally { Remove-Item -LiteralPath $fixturePath -Force -ErrorAction SilentlyContinue }

$parameter=$ast.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq 'TrustServerCertificate' }
if ($null -eq $parameter -or $parameter.DefaultValue.Extent.Text -cne '$true') { throw 'Uploader must default to trusted LAN certificates.' }
if (-not (Resolve-CertificateTrust $true $false) -or
    (Resolve-CertificateTrust $true $true) -or (Resolve-CertificateTrust $false $false)) {
    throw 'Certificate validation opt-out is inconsistent.'
}
foreach ($answer in @('YES','yes','Yes','  yEs  ',"`tYES`r`n")) {
    if (-not (Test-PublishConfirmation $answer)) { throw 'Explicit confirmation rejected.' }
}
foreach ($answer in @('',' ','no','y','YES please','yes;other',$null)) {
    if (Test-PublishConfirmation $answer) { throw 'Unconfirmed publication accepted.' }
}
$message=Get-DatabaseFailureMessage 'publication' 195
foreach ($expected in @('SQL error 195','compatibility level','WHOLE upload directory','Do not change database','keep the staged package','same version and hash')) {
    if (-not $message.Contains($expected)) { throw "Missing SQL compatibility guidance: $expected" }
}
if ($message.Contains('check update-table permissions')) { throw 'Compatibility error misclassified as permissions.' }
$message=Get-DatabaseFailureMessage 'upload' 229
if (-not $message.Contains('SQL error 229') -or -not $message.Contains('permissions')) {
    throw 'Other SQL errors lost their diagnostic code.'
}

$wrapped=[Reflection.TargetInvocationException]::new('synthetic wrapper', [Exception]::new('PRIVATE_CONNECTION_DETAILS'))
foreach ($trust in @($false,$true)) {
    $message=Get-UploadFailureMessage $wrapped 'connect' $trust
    if ($message -match 'PRIVATE_CONNECTION_DETAILS' -or $message -notmatch 'No SQL was executed') {
        throw 'Connection failure leaked provider details or obscured the safe boundary.'
    }
    if (-not $trust -and $message -notmatch '\-TrustServerCertificate') { throw 'Certificate guidance missing.' }
    if ($trust -and $message -match 'rerun upload.cmd') { throw 'Certificate override suggested repeatedly.' }
}

# Exercise the real SqlClient builder without creating/opening a SQL connection.
Add-Type -AssemblyName System.Data
$function=$ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'New-UploadConnectionString'},$false)
if ($null -eq $function) { throw 'Connection-string builder missing.' }
Invoke-Expression $function.Extent.Text
foreach ($trust in @($false,$true)) {
    foreach ($target in @('synthetic-server','synthetic-server;User ID=unexpected')) {
        $text=New-UploadConnectionString $target 'synthetic-db;Password=unexpected' $trust
        $builder=New-Object System.Data.SqlClient.SqlConnectionStringBuilder($text)
        if ($builder.get_DataSource() -cne $target -or
            $builder.get_InitialCatalog() -cne 'synthetic-db;Password=unexpected' -or
            -not $builder.get_Encrypt() -or $builder.get_TrustServerCertificate() -ne $trust -or
            $builder.get_ConnectTimeout() -ne 5 -or $builder.get_PersistSecurityInfo() -or
            $builder.get_UserID() -ne '' -or $builder.get_Password() -ne '' -or
            $builder.get_IntegratedSecurity()) { throw 'Incorrect or unsafe connection string.' }
    }
}

function New-IndexRows([string]$Name,[int]$Type,[bool]$Primary,[bool]$Constraint,[string[]]$Keys,[string[]]$Included,[string]$Filter) {
    for ($i=0; $i -lt $Keys.Count+$Included.Count; $i++) {
        $isKey=$i -lt $Keys.Count
        [pscustomobject]@{ Name=$Name; Type=$Type; Primary=$Primary; Constraint=$Constraint
            Unique=$true; Disabled=$false; Hypothetical=$false; IgnoreDuplicates=$false
            Filter=$Filter; HasFilter=($Filter.Length -gt 0); Descending=$false
            Column=$(if ($isKey) { $Keys[$i] } else { $Included[$i-$Keys.Count] })
            Ordinal=$(if ($isKey) {$i+1} else {0}); Included=(-not $isKey) }
    }
}
function New-ValidFixture {
    New-IndexRows 'PK_LS_AS_LISWORK' 1 $true $false @('PackageId') @() ''
    New-IndexRows 'UQ_LS_AS_LISWORK_Version' 2 $false $true @('AppId','Channel','Version') @() ''
    New-IndexRows 'UX_LS_AS_LISWORK_Current' 2 $false $false @('AppId','Channel') @('Version','MinUpdaterVersion','PackageFileName','PackageSize','PackageSha256','PublishedAtUtc') '([IsCurrent]=(1))'
}
function Assert-Rejected($Rows,[string]$Scenario) {
    $rejected=$false
    try { Assert-IndexDefinitions $Rows } catch { $rejected=$true }
    if (-not $rejected) { throw "Invalid index accepted: $Scenario" }
}
Assert-IndexDefinitions @(New-ValidFixture)
$rows=@(New-ValidFixture); $rows[0].Disabled=$true; Assert-Rejected $rows 'disabled primary key'
$rows=@(New-ValidFixture); $rows[0].Unique=$false; Assert-Rejected $rows 'nonunique primary key'
$rows=@(New-ValidFixture); $rows[0].Column='Version'; Assert-Rejected $rows 'wrong primary key'
$rows=@(New-ValidFixture); $rows[1].Ordinal=2; $rows[2].Ordinal=1; Assert-Rejected $rows 'wrong key ordering'
$rows=@(New-ValidFixture); $rows[4].Filter='([IsCurrent]=(0))'; Assert-Rejected $rows 'wrong filter'
$rows=@(New-ValidFixture); $rows[4].HasFilter=$false; Assert-Rejected $rows 'filter absent'
$rows=@(New-ValidFixture); $rows[4].IgnoreDuplicates=$true; Assert-Rejected $rows 'ignore duplicate key'
$rows=@(New-ValidFixture); $rows[4].Descending=$true; Assert-Rejected $rows 'descending key'
$rows=@(New-ValidFixture); $rows[4].Hypothetical=$true; Assert-Rejected $rows 'hypothetical index'
$rows=@(New-ValidFixture); $rows[1].Constraint=$false; Assert-Rejected $rows 'wrong constraint type'
$rows=@(New-ValidFixture); $rows[6].Column='PackageData'; Assert-Rejected $rows 'LOB included in metadata index'
Assert-Rejected @((New-ValidFixture) | Where-Object Name -ne 'UX_LS_AS_LISWORK_Current') 'missing filtered index'
Assert-Rejected @((New-ValidFixture) | Select-Object -SkipLast 1) 'missing included column'
$rows=@(New-ValidFixture); $rows[0].Name='UnexpectedIndex'; Assert-Rejected $rows 'unexpected index'
Write-Output 'PowerShell syntax, confirmation/error guidance, 4 real SqlClient connection-string scenarios and 15 index-schema scenarios passed; no database used.'
