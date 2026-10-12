#Requires -Version 5.1
param([string]$Server, [string]$Database, [string]$User,
      [switch]$TrustServerCertificate)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Data
Add-Type -AssemblyName System.IO.Compression.FileSystem
$connection = $null
$stage = 'local_validation'

function Test-PublishConfirmation([string]$Answer) {
    return -not [string]::IsNullOrWhiteSpace($Answer) -and
        $Answer.Trim().Equals('YES', [StringComparison]::OrdinalIgnoreCase)
}
function Get-UploadFailureMessage([Exception]$Failure, [string]$Phase, [bool]$TrustCertificate) {
    # PowerShell wraps .NET method errors. Inspect the chain without printing provider text.
    $sqlFailure = $null
    for ($current=$Failure; $null -ne $current; $current=$current.InnerException) {
        if ($current -is [Data.SqlClient.SqlException]) { $sqlFailure=$current; break }
    }
    if ($Phase -eq 'connect') {
        $code = if ($null -ne $sqlFailure) { " (SQL error $($sqlFailure.Number))" } else { '' }
        $hint = if ($TrustCertificate) {
            'Certificate trust override was requested; check server address, SQL login and TLS compatibility.'
        } else {
            'Check server address, SQL login and server certificate. If this is your verified LAN/test server with an untrusted certificate, rerun upload.cmd with -TrustServerCertificate. Encryption remains enabled.'
        }
        return "Connection/login failed$code. No SQL was executed. $hint"
    }
    if ($null -ne $sqlFailure) {
        return "Database operation failed during $Phase (SQL error $($sqlFailure.Number)); check update-table permissions and state. No automatic repair."
    }
    return "Failed during ${Phase}: $($Failure.Message)"
}

function New-Command([string]$Sql) {
    $cmd = $connection.CreateCommand()
    $cmd.CommandText = $Sql
    $cmd.CommandTimeout = 120
    return $cmd
}
function Add-Value($Cmd, [string]$Name, [System.Data.SqlDbType]$Type, [int]$Size, $Value) {
    $p = $Cmd.Parameters.Add($Name, $Type, $Size)
    $p.Value = $Value
}
function New-UploadConnectionString([string]$TargetServer, [string]$TargetDatabase, [bool]$TrustCertificate) {
    $builder = New-Object System.Data.SqlClient.SqlConnectionStringBuilder
    # PowerShell adapts this IDictionary: dot assignment treats DataSource as a SQL
    # keyword rather than the CLR property. Use explicit CLR setters for every field.
    $builder.set_DataSource($TargetServer)
    $builder.set_InitialCatalog($TargetDatabase)
    $builder.set_Encrypt($true)
    $builder.set_TrustServerCertificate($TrustCertificate)
    $builder.set_ConnectTimeout(5)
    $builder.set_PersistSecurityInfo($false)
    return $builder.get_ConnectionString()
}
# Pure validator, also exercised with synthetic metadata without a database connection.
function Assert-IndexDefinitions($Rows) {
    $expected = @{
        PK_LS_AS_LISWORK = @{ Type=1; Primary=$true; Constraint=$false; Keys='PackageId'; Included=''; Filter='' }
        UQ_LS_AS_LISWORK_Version = @{ Type=2; Primary=$false; Constraint=$true; Keys='AppId,Channel,Version'; Included=''; Filter='' }
        UX_LS_AS_LISWORK_Current = @{ Type=2; Primary=$false; Constraint=$false; Keys='AppId,Channel'; Included='MinUpdaterVersion,PackageFileName,PackageSha256,PackageSize,PublishedAtUtc,Version'; Filter='iscurrent=1' }
    }
    $found = @{}
    foreach ($row in $Rows) {
        if (-not $expected.ContainsKey($row.Name)) { throw 'Unexpected update-table index; stop without repair.' }
        $e = $expected[$row.Name]
        $filter = ($row.Filter -replace '[\s\[\]()]', '').ToLowerInvariant()
        if ($row.Type -ne $e.Type -or -not $row.Unique -or $row.Disabled -or $row.Hypothetical -or
            $row.IgnoreDuplicates -or $row.Primary -ne $e.Primary -or $row.Constraint -ne $e.Constraint -or
            $filter -cne $e.Filter -or $row.HasFilter -ne ($e.Filter.Length -gt 0)) {
            throw 'Incompatible update-table index definition; stop without repair.'
        }
        if (-not $found.ContainsKey($row.Name)) { $found[$row.Name] = @{ Keys=@(); Included=@() } }
        if ($row.Descending) { throw 'Unexpected descending index key; stop without repair.' }
        if ($row.Ordinal -gt 0) { $found[$row.Name].Keys += "$($row.Ordinal):$($row.Column)" }
        elseif ($row.Included) { $found[$row.Name].Included += $row.Column }
    }
    foreach ($name in $expected.Keys) {
        if (-not $found.ContainsKey($name)) { throw 'Required update-table index missing; stop without repair.' }
        $keys = @($found[$name].Keys | Sort-Object | ForEach-Object { ($_ -split ':',2)[1] }) -join ','
        $included = @($found[$name].Included | Sort-Object) -join ','
        if ($keys -cne $expected[$name].Keys -or $included -cne $expected[$name].Included) {
            throw 'Incompatible update-table index columns; stop without repair.'
        }
    }
}
function Get-LocalHash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Assert-Package($Manifest, [string]$Path) {
    if ($Manifest.appId -cne 'lis-workbench' -or $Manifest.channel -cne 'stable' -or
        $Manifest.version -cnotmatch '^v\d{4}\.\d{2}\.\d{2}$' -or
        $Manifest.minUpdaterVersion -cne '1.0.0' -or
        $Manifest.package.file -cne "LISWorkbench-$($Manifest.version)-win7-win11.zip" -or
        $Manifest.package.sha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Invalid update manifest.' }
    $length = (Get-Item -LiteralPath $Path).Length
    if ($length -le 0 -or $length -gt 104857600 -or $length -ne $Manifest.package.size -or
        (Get-LocalHash $Path) -cne $Manifest.package.sha256) { throw 'Local package integrity check failed.' }
    $zip = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $names = @{}
        $expanded = [long]0
        foreach ($entry in $zip.Entries) {
            $name = $entry.FullName
            if ($name -notmatch '^[A-Za-z0-9_.-]+\.(exe|dll)$' -or $name -match '\.\.' -or
                $name -match '^(con|prn|aux|nul|com[1-9]|lpt[1-9])\.' -or $names.ContainsKey($name.ToLowerInvariant())) {
                throw 'Unsafe, protected or duplicate ZIP entry.'
            }
            $names[$name.ToLowerInvariant()] = $true
            $expanded += $entry.Length
            if ($expanded -gt 536870912) { throw 'Expanded ZIP exceeds limit.' }
        }
        if (-not $names.ContainsKey('lis_workbench.exe') -or -not $names.ContainsKey('updater.exe')) { throw 'Incomplete ZIP.' }
    } finally { $zip.Dispose() }
}
function Verify-Stored([long]$Id, $Manifest) {
    $cmd = New-Command 'SELECT PackageData FROM dbo.LS_AS_LISWORK WHERE PackageId=@Id AND AppId=@App AND Channel=@Channel AND Version=@Version AND PackageSha256=@Hash AND PackageSize=@Size;'
    Add-Value $cmd '@Id' BigInt 0 $Id
    Add-Value $cmd '@App' VarChar 64 'lis-workbench'
    Add-Value $cmd '@Channel' VarChar 32 'stable'
    Add-Value $cmd '@Version' VarChar 64 $Manifest.version
    Add-Value $cmd '@Hash' Char 64 $Manifest.package.sha256
    Add-Value $cmd '@Size' BigInt 0 ([long]$Manifest.package.size)
    $reader = $null
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $reader = $cmd.ExecuteReader([Data.CommandBehavior]::SequentialAccess)
        if (-not $reader.Read()) { throw 'Stored package not found or changed.' }
        $buffer = New-Object byte[] 65536
        $offset = [long]0
        $deadline = [DateTime]::UtcNow.AddSeconds(180)
        while (($read = $reader.GetBytes(0, $offset, $buffer, 0, $buffer.Length)) -gt 0) {
            if ([DateTime]::UtcNow -gt $deadline) { throw 'Read-back verification timed out.' }
            $offset += $read
            if ($offset -gt [long]$Manifest.package.size) { throw 'Stored package exceeds expected size.' }
            [void]$sha.TransformBlock($buffer, 0, [int]$read, $buffer, 0)
        }
        [void]$sha.TransformFinalBlock((New-Object byte[] 0), 0, 0)
        $hash = ([BitConverter]::ToString($sha.Hash)).Replace('-', '').ToLowerInvariant()
        if ($offset -ne [long]$Manifest.package.size -or $hash -cne $Manifest.package.sha256) { throw 'Stored package SHA-256 mismatch.' }
    } finally {
        if ($null -ne $reader) { $reader.Dispose() }
        $sha.Dispose(); $cmd.Dispose()
    }
}

try {
    if ([Environment]::OSVersion.Version.Major -lt 10) { throw 'Upload requires Windows 10/11.' }
    $manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $info = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'release-info.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($item in $info.files.PSObject.Properties) {
        if ($item.Name -match '[/\\:]|\.\.' -or (Get-LocalHash (Join-Path $PSScriptRoot $item.Name)) -cne $item.Value) {
            throw 'Offline materials changed; generate a new directory from the Release.'
        }
    }
    foreach ($required in @('manifest.json','upload.ps1','upload.cmd','02-publish.sql','03-cleanup.sql')) {
        if ($info.files.PSObject.Properties.Match($required).Count -ne 1) { throw 'Incomplete offline material checksums.' }
    }
    $path = Join-Path $PSScriptRoot $manifest.package.file
    Assert-Package $manifest $path
    $notes = if ($manifest.PSObject.Properties.Match('notes').Count -gt 0) { @($manifest.notes) -join "`n" } else { '' }
    if ($notes.Length -gt 2000) { throw 'Release notes exceed 2000 UTF-16 units.' }
    if (-not $Server) { $Server = Read-Host 'SQL Server' }
    if (-not $Database) { $Database = Read-Host 'LIS database' }
    if (-not $User) { $User = Read-Host 'Upload SQL username' }
    if (-not $Server -or -not $Database -or -not $User -or $Database -in @('master','model','msdb','tempdb')) { throw 'Invalid target.' }
    $password = Read-Host 'Password (not saved)' -AsSecureString
    $password.MakeReadOnly()
    $connectionString = New-UploadConnectionString $Server $Database ([bool]$TrustServerCertificate)
    $connection = New-Object System.Data.SqlClient.SqlConnection($connectionString)
    $connection.Credential = New-Object System.Data.SqlClient.SqlCredential($User, $password)
    $stage = 'connect'
    $connection.Open()
    $stage = 'table_preflight'
    $cmd = New-Command "SELECT CONVERT(varchar(128),SERVERPROPERTY('ProductVersion'));"
    try {
        $serverVersion = [string]$cmd.ExecuteScalar()
        if ([int]($serverVersion.Split('.')[0]) -lt 11) { throw 'SQL Server 2012 or newer is required.' }
    } finally { $cmd.Dispose() }
    $cmd = New-Command "SELECT HAS_PERMS_BY_NAME(N'dbo.LS_AS_LISWORK',N'OBJECT',N'SELECT'), HAS_PERMS_BY_NAME(N'dbo.LS_AS_LISWORK',N'OBJECT',N'INSERT'), HAS_PERMS_BY_NAME(N'dbo.LS_AS_LISWORK',N'OBJECT',N'UPDATE'), HAS_PERMS_BY_NAME(N'dbo.LS_AS_LISWORK',N'OBJECT',N'DELETE');"
    $reader = $null
    try {
        $reader = $cmd.ExecuteReader()
        if (-not $reader.Read()) { throw 'Cannot inspect update-table permissions.' }
        for ($i=0; $i -lt 4; $i++) {
            if ($reader.IsDBNull($i) -or $reader.GetInt32($i) -ne 1) { throw 'Update table missing or required table permissions unavailable.' }
        }
    } finally { if ($null -ne $reader) { $reader.Dispose() }; $cmd.Dispose() }
    # Read-only metadata checks are restricted to the fixed target table.
    $expected = @{
        PackageId=@(127,8,0); AppId=@(167,64,0); Channel=@(167,32,0); Version=@(167,64,0)
        MinUpdaterVersion=@(167,32,0); PackageFileName=@(231,520,0); PackageSize=@(127,8,0)
        PackageSha256=@(175,64,0); PackageData=@(165,-1,0); ReleaseNotes=@(231,4000,1)
        State=@(167,16,0); IsCurrent=@(104,1,0); CreatedAtUtc=@(42,0,0)
        VerifiedAtUtc=@(42,0,1); PublishedAtUtc=@(42,0,1); RetiredAtUtc=@(42,0,1)
    }
    $cmd = New-Command "SELECT name,system_type_id,max_length,is_nullable,is_computed,is_identity,scale,user_type_id FROM sys.columns WHERE object_id=OBJECT_ID(N'dbo.LS_AS_LISWORK',N'U');"
    $reader = $null; $columnCount = 0
    try {
        $reader = $cmd.ExecuteReader()
        while ($reader.Read()) {
            $name = $reader.GetString(0)
            if (-not $expected.ContainsKey($name)) { throw 'Unexpected update-table column; stop without repair.' }
            $shape = $expected[$name]
            if ($reader.GetByte(1) -ne $shape[0] -or ($shape[1] -ne 0 -and $reader.GetInt16(2) -ne $shape[1]) -or
                [int]$reader.GetBoolean(3) -ne $shape[2] -or $reader.GetBoolean(4) -or
                $reader.GetBoolean(5) -ne ($name -ceq 'PackageId') -or
                ($shape[0] -eq 42 -and $reader.GetByte(6) -ne 0) -or $reader.GetInt32(7) -ne $shape[0]) { throw 'Incompatible update-table structure; stop without repair.' }
            $columnCount++
        }
        if ($columnCount -ne 16) { throw 'Update table missing or incompatible. Create it manually.' }
    } finally { if ($null -ne $reader) { $reader.Dispose() }; $cmd.Dispose() }
    $cmd = New-Command "SELECT COUNT(*) FROM sys.identity_columns WHERE object_id=OBJECT_ID(N'dbo.LS_AS_LISWORK',N'U') AND name='PackageId' AND CONVERT(bigint,seed_value)=1 AND CONVERT(bigint,increment_value)=1 AND is_not_for_replication=0;"
    try { if ([int]$cmd.ExecuteScalar() -ne 1) { throw 'Incompatible package identity; stop without repair.' } } finally { $cmd.Dispose() }
    $cmd = New-Command "SELECT i.name,i.type,i.is_unique,i.is_disabled,i.is_hypothetical,i.ignore_dup_key,i.is_primary_key,i.is_unique_constraint,i.has_filter,COALESCE(i.filter_definition,N''),c.name,ic.key_ordinal,ic.is_included_column,ic.is_descending_key FROM sys.indexes i JOIN sys.index_columns ic ON ic.object_id=i.object_id AND ic.index_id=i.index_id JOIN sys.columns c ON c.object_id=ic.object_id AND c.column_id=ic.column_id WHERE i.object_id=OBJECT_ID(N'dbo.LS_AS_LISWORK',N'U');"
    $reader = $null; $indexRows = @()
    try {
        $reader = $cmd.ExecuteReader()
        while ($reader.Read()) {
            $indexRows += [pscustomobject]@{ Name=$reader.GetString(0); Type=$reader.GetByte(1); Unique=$reader.GetBoolean(2)
                Disabled=$reader.GetBoolean(3); Hypothetical=$reader.GetBoolean(4); IgnoreDuplicates=$reader.GetBoolean(5)
                Primary=$reader.GetBoolean(6); Constraint=$reader.GetBoolean(7); HasFilter=$reader.GetBoolean(8)
                Filter=$reader.GetString(9); Column=$reader.GetString(10); Ordinal=$reader.GetByte(11)
                Included=$reader.GetBoolean(12); Descending=$reader.GetBoolean(13) }
        }
        Assert-IndexDefinitions $indexRows
    } finally { if ($null -ne $reader) { $reader.Dispose() }; $cmd.Dispose() }
    $defaults = @{ State="'staged'"; IsCurrent='0'; CreatedAtUtc='sysutcdatetime' }
    $cmd = New-Command "SELECT c.name,d.definition FROM sys.default_constraints d JOIN sys.columns c ON c.object_id=d.parent_object_id AND c.column_id=d.parent_column_id WHERE d.parent_object_id=OBJECT_ID(N'dbo.LS_AS_LISWORK',N'U');"
    $reader = $null; $defaultCount = 0
    try {
        $reader = $cmd.ExecuteReader()
        while ($reader.Read()) {
            $name = $reader.GetString(0)
            $definition = ($reader.GetString(1) -replace '[\s\[\]()]','').ToLowerInvariant()
            if (-not $defaults.ContainsKey($name) -or $definition -cne $defaults[$name]) { throw 'Incompatible update-table default; stop without repair.' }
            $defaultCount++
        }
        if ($defaultCount -ne 3) { throw 'Required update-table default missing; stop without repair.' }
    } finally { if ($null -ne $reader) { $reader.Dispose() }; $cmd.Dispose() }
    $checks = @('CK_LS_AS_LISWORK_AppId','CK_LS_AS_LISWORK_Channel','CK_LS_AS_LISWORK_Version',
                'CK_LS_AS_LISWORK_MinUpdaterVersion','CK_LS_AS_LISWORK_FileName','CK_LS_AS_LISWORK_PackageSize',
                'CK_LS_AS_LISWORK_Sha256','CK_LS_AS_LISWORK_Lifecycle')
    $cmd = New-Command "SELECT name,is_disabled,is_not_trusted FROM sys.check_constraints WHERE parent_object_id=OBJECT_ID(N'dbo.LS_AS_LISWORK',N'U');"
    $reader = $null; $checkCount = 0
    try {
        $reader = $cmd.ExecuteReader()
        while ($reader.Read()) {
            if ($reader.GetString(0) -cnotin $checks -or $reader.GetBoolean(1) -or $reader.GetBoolean(2)) { throw 'Update-table constraints missing, disabled or untrusted; stop without repair.' }
            $checkCount++
        }
        if ($checkCount -ne $checks.Count) { throw 'Required update-table constraint missing; stop without repair.' }
    } finally { if ($null -ne $reader) { $reader.Dispose() }; $cmd.Dispose() }
    $cmd = New-Command "SELECT (SELECT COUNT(*) FROM sys.triggers WHERE parent_id=OBJECT_ID(N'dbo.LS_AS_LISWORK') AND is_disabled=0) + (SELECT COUNT(*) FROM sys.foreign_keys WHERE parent_object_id=OBJECT_ID(N'dbo.LS_AS_LISWORK') OR referenced_object_id=OBJECT_ID(N'dbo.LS_AS_LISWORK'));"
    try { if ([int]$cmd.ExecuteScalar() -ne 0) { throw 'Update table has triggers or foreign keys; refuse possible cross-table side effects.' } } finally { $cmd.Dispose() }
    $cmd = New-Command 'SELECT SYSUTCDATETIME();'
    try { $started = [datetime]$cmd.ExecuteScalar() } finally { $cmd.Dispose() }
    Write-Host "Target: $($connection.DataSource) / $($connection.Database) / dbo.LS_AS_LISWORK"
    Write-Host "SQL Server: $serverVersion"
    Write-Host "Version: $($manifest.version); bytes: $($manifest.package.size); SHA-256: $($manifest.package.sha256)"
    Write-Host "Encrypted connection; trust server certificate: $([bool]$TrustServerCertificate)"
    Write-Host 'Will upload, verify, publish, then clean previously retired packages older than 24 hours.'
    if (-not (Test-PublishConfirmation (Read-Host 'Proceed? Type YES or yes; Enter cancels'))) { Write-Host 'Cancelled; no table data changed.'; exit 0 }
    $stage = 'upload'
    $cmd = New-Command 'SELECT PackageId,State,PackageSize,PackageSha256,MinUpdaterVersion,PackageFileName FROM dbo.LS_AS_LISWORK WHERE AppId=@App AND Channel=@Channel AND Version=@Version;'
    Add-Value $cmd '@App' VarChar 64 'lis-workbench'; Add-Value $cmd '@Channel' VarChar 32 'stable'
    Add-Value $cmd '@Version' VarChar 64 $manifest.version
    $reader = $null; $id = [long]0; $state = ''
    try {
        $reader = $cmd.ExecuteReader()
        if ($reader.Read()) {
            $id = $reader.GetInt64(0); $state = $reader.GetString(1)
            if ($reader.GetInt64(2) -ne [long]$manifest.package.size -or $reader.GetString(3) -cne $manifest.package.sha256 -or
                $reader.GetString(4) -cne $manifest.minUpdaterVersion -or $reader.GetString(5) -cne $manifest.package.file) { throw 'Same version already exists with different content or metadata.' }
            if ($state -eq 'retired') { throw 'Retired version cannot be republished.' }
        }
    } finally { if ($null -ne $reader) { $reader.Dispose() }; $cmd.Dispose() }
    if ($id -eq 0) {
        Write-Host 'Uploading binary package...'
        $cmd = New-Command "INSERT INTO dbo.LS_AS_LISWORK (AppId,Channel,Version,MinUpdaterVersion,PackageFileName,PackageSize,PackageSha256,PackageData,ReleaseNotes,State,IsCurrent) OUTPUT inserted.PackageId VALUES (@App,@Channel,@Version,@Min,@File,@Size,@Hash,@Data,@Notes,'staged',0);"
        Add-Value $cmd '@App' VarChar 64 'lis-workbench'; Add-Value $cmd '@Channel' VarChar 32 'stable'
        Add-Value $cmd '@Version' VarChar 64 $manifest.version; Add-Value $cmd '@Min' VarChar 32 $manifest.minUpdaterVersion
        Add-Value $cmd '@File' NVarChar 260 $manifest.package.file; Add-Value $cmd '@Size' BigInt 0 ([long]$manifest.package.size)
        Add-Value $cmd '@Hash' Char 64 $manifest.package.sha256
        Add-Value $cmd '@Data' VarBinary -1 ([IO.File]::ReadAllBytes($path))
        Add-Value $cmd '@Notes' NVarChar 2000 $notes
        try { $id = [long]$cmd.ExecuteScalar() } finally { $cmd.Dispose() }
    }
    Write-Host "Verifying stored package $id..."
    $stage = 'readback_verification'
    Verify-Stored $id $manifest
    if ($state -ne 'published') {
        $cmd = New-Command "UPDATE dbo.LS_AS_LISWORK SET VerifiedAtUtc=SYSUTCDATETIME() WHERE PackageId=@Id AND State='staged' AND AppId='lis-workbench' AND Channel='stable' AND PackageSha256=@Hash AND PackageSize=@Size;"
        Add-Value $cmd '@Id' BigInt 0 $id; Add-Value $cmd '@Hash' Char 64 $manifest.package.sha256
        Add-Value $cmd '@Size' BigInt 0 ([long]$manifest.package.size)
        try { if ($cmd.ExecuteNonQuery() -ne 1) { throw 'Package changed; verification cannot be recorded.' } } finally { $cmd.Dispose() }
    }
    $stage = 'publication'
    $cmd = New-Command (Get-Content -LiteralPath (Join-Path $PSScriptRoot '02-publish.sql') -Raw -Encoding UTF8)
    try { $isNew = [bool]$cmd.ExecuteScalar() } finally { $cmd.Dispose() }
    Write-Host "Publication successful: $($manifest.version)"
    if ($isNew) {
        try {
            $cmd = New-Command "DELETE TOP (5) FROM dbo.LS_AS_LISWORK WHERE AppId='lis-workbench' AND Channel='stable' AND State='retired' AND IsCurrent=0 AND RetiredAtUtc<@Started AND RetiredAtUtc<=DATEADD(hour,-24,@Started);"
            Add-Value $cmd '@Started' DateTime2 0 $started
            try { Write-Host "Old retired packages cleaned: $($cmd.ExecuteNonQuery())" } finally { $cmd.Dispose() }
        } catch { Write-Warning 'Publication succeeded; cleanup failed. Use 03-cleanup.sql to retry.' }
    }
    Write-Host 'Done. The verified current version is available for manual LIS update checks.'
} catch {
    Write-Host (Get-UploadFailureMessage $_.Exception $stage ([bool]$TrustServerCertificate))
    exit 1
} finally {
    if ($null -ne $connection) { $connection.Dispose() }
}
