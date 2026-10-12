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
    IF LEN(@Version)<>11 OR @Version NOT LIKE 'v[0-9][0-9][0-9][0-9].[0-9][0-9].[0-9][0-9]'
        THROW 51005, 'Invalid target version.', 1;
    IF @Current IS NOT NULL
    BEGIN
        IF LEN(@Current)<>11 OR @Current NOT LIKE 'v[0-9][0-9][0-9][0-9].[0-9][0-9].[0-9][0-9]'
            THROW 51006, 'Invalid current version; manual inspection required.', 1;
        IF CONVERT(bigint, REPLACE(SUBSTRING(@Version,2,10),'.','')) <=
           CONVERT(bigint, REPLACE(SUBSTRING(@Current,2,10),'.',''))
            THROW 51007, 'Version downgrade is not allowed.', 1;
    END;
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
