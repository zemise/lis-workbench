-- Recovery only. Select the correct LIS database first. No DDL or other-table access.
SET NOCOUNT ON;
IF EXISTS (SELECT 1 FROM sys.triggers WHERE parent_id=OBJECT_ID(N'dbo.LS_AS_LISWORK') AND is_disabled=0)
    THROW 51010, 'Update table has a trigger; refuse operations with possible side effects.', 1;
IF EXISTS (SELECT 1 FROM sys.foreign_keys WHERE parent_object_id=OBJECT_ID(N'dbo.LS_AS_LISWORK') OR referenced_object_id=OBJECT_ID(N'dbo.LS_AS_LISWORK'))
    THROW 51011, 'Update table has foreign-key dependencies; stop without repair.', 1;
IF @@TRANCOUNT <> 0 THROW 51000, 'Use a connection without an existing transaction.', 1;
SET XACT_ABORT ON;
DECLARE @Version varchar(64) = '__VERSION__';
DECLARE @Hash char(64) = '__SHA256__';
DECLARE @Size bigint = __SIZE__;
DECLARE @Target bigint, @State varchar(16), @Current varchar(64), @Lock int;
BEGIN TRY
    BEGIN TRANSACTION;
    EXEC @Lock = sys.sp_getapplock @Resource=N'LISWorkbench.Update.Publish',
        @LockMode='Exclusive', @LockOwner='Transaction', @LockTimeout=5000;
    IF @Lock < 0 THROW 51001, 'Another update publication is running.', 1;
    SELECT @Target=PackageId, @State=State FROM dbo.LS_AS_LISWORK WITH (UPDLOCK, HOLDLOCK)
        WHERE AppId='lis-workbench' AND Channel='stable' AND Version=@Version
        AND PackageSha256=@Hash AND PackageSize=@Size
        AND VerifiedAtUtc IS NOT NULL AND DATALENGTH(PackageData)=@Size;
    IF @Target IS NULL THROW 51002, 'No matching verified package. Upload and verify first.', 1;
    IF @State='published'
    BEGIN
        IF NOT EXISTS (SELECT 1 FROM dbo.LS_AS_LISWORK WHERE PackageId=@Target AND IsCurrent=1)
            THROW 51003, 'Invalid publication state.', 1;
        COMMIT TRANSACTION;
        SELECT CAST(0 AS bit) AS NewlyPublished;
        RETURN;
    END;
    IF @State<>'staged' THROW 51004, 'Retired packages cannot be republished by this tool.', 1;
    SELECT @Current=Version FROM dbo.LS_AS_LISWORK WITH (UPDLOCK, HOLDLOCK)
        WHERE AppId='lis-workbench' AND Channel='stable' AND IsCurrent=1;
    -- SQL Server 2012-compatible validation and numeric comparison. No schema changes.
    DECLARE @Versions TABLE (RoleName varchar(8), Version varchar(64), DateKey int, Revision int);
    INSERT INTO @Versions (RoleName, Version) VALUES ('target', @Version);
    IF @Current IS NOT NULL INSERT INTO @Versions (RoleName, Version) VALUES ('current', @Current);
    IF EXISTS (SELECT 1 FROM @Versions WHERE
        DATALENGTH(Version) NOT BETWEEN 11 AND 21 OR
        LEFT(Version,11) COLLATE Latin1_General_BIN2 NOT LIKE 'v[0-9][0-9][0-9][0-9].[0-9][0-9].[0-9][0-9]' OR
        TRY_CONVERT(date, REPLACE(SUBSTRING(Version,2,10),'.','-'),23) IS NULL OR
        (DATALENGTH(Version)>11 AND
            (SUBSTRING(Version,12,1)<>'.' OR DATALENGTH(Version)<13 OR
             SUBSTRING(Version,13,1) COLLATE Latin1_General_BIN2 NOT LIKE '[1-9]' OR
             SUBSTRING(Version,13,9) COLLATE Latin1_General_BIN2 LIKE '%[^0-9]%')))
        THROW 51005, 'Invalid release version; expected vYYYY.MM.DD or vYYYY.MM.DD.N.', 1;
    UPDATE @Versions SET
        DateKey=CASE WHEN Version IN ('v2026.10.13','v2026.10.14','v2026.10.15') THEN 20261012
            ELSE CONVERT(int,REPLACE(SUBSTRING(Version,2,10),'.','')) END,
        Revision=CASE Version WHEN 'v2026.10.13' THEN 1 WHEN 'v2026.10.14' THEN 2 WHEN 'v2026.10.15' THEN 3
            ELSE CASE WHEN DATALENGTH(Version)=11 THEN 0 ELSE CONVERT(int,SUBSTRING(Version,13,9)) END END;
    IF EXISTS (SELECT 1 FROM @Versions AS target CROSS JOIN @Versions AS currentVersion
        WHERE target.RoleName='target' AND currentVersion.RoleName='current' AND
        (target.DateKey<currentVersion.DateKey OR
         (target.DateKey=currentVersion.DateKey AND target.Revision<=currentVersion.Revision)))
        THROW 51007, 'Version downgrade is not allowed.', 1;
    UPDATE dbo.LS_AS_LISWORK SET State='retired', IsCurrent=0, RetiredAtUtc=SYSUTCDATETIME()
        WHERE AppId='lis-workbench' AND Channel='stable' AND IsCurrent=1;
    IF @@ROWCOUNT > 1 THROW 51008, 'Unexpected current-package count.', 1;
    UPDATE dbo.LS_AS_LISWORK SET State='published', IsCurrent=1, PublishedAtUtc=SYSUTCDATETIME()
        WHERE PackageId=@Target AND State='staged' AND VerifiedAtUtc IS NOT NULL;
    IF @@ROWCOUNT<>1 THROW 51009, 'Package changed during publication.', 1;
    COMMIT TRANSACTION;
    SELECT CAST(1 AS bit) AS NewlyPublished;
END TRY
BEGIN CATCH
    IF XACT_STATE()<>0 ROLLBACK TRANSACTION;
    THROW;
END CATCH;
