-- Preview by default. Only retired update packages can be deleted.
SET NOCOUNT ON;
IF EXISTS (SELECT 1 FROM sys.triggers WHERE parent_id=OBJECT_ID(N'dbo.LS_AS_LISWORK') AND is_disabled=0)
    THROW 51010, 'Update table has a trigger; refuse operations with possible side effects.', 1;
IF EXISTS (SELECT 1 FROM sys.foreign_keys WHERE parent_object_id=OBJECT_ID(N'dbo.LS_AS_LISWORK') OR referenced_object_id=OBJECT_ID(N'dbo.LS_AS_LISWORK'))
    THROW 51011, 'Update table has foreign-key dependencies; stop without repair.', 1;
DECLARE @Execute bit = 0;
DECLARE @Before datetime2(0) = SYSUTCDATETIME();
IF @Execute=0
BEGIN
    SELECT PackageId, Version, PackageSize, RetiredAtUtc FROM dbo.LS_AS_LISWORK
        WHERE AppId='lis-workbench' AND Channel='stable' AND State='retired'
        AND IsCurrent=0 AND RetiredAtUtc<@Before
        AND RetiredAtUtc<=DATEADD(hour,-24,@Before);
    RETURN;
END;
IF @@TRANCOUNT<>0 THROW 51000, 'Use a connection without an existing transaction.', 1;
SET XACT_ABORT ON;
BEGIN TRY
    BEGIN TRANSACTION;
    DELETE TOP (5) FROM dbo.LS_AS_LISWORK
        WHERE AppId='lis-workbench' AND Channel='stable' AND State='retired'
        AND IsCurrent=0 AND RetiredAtUtc<@Before
        AND RetiredAtUtc<=DATEADD(hour,-24,@Before);
    COMMIT TRANSACTION;
END TRY
BEGIN CATCH
    IF XACT_STATE()<>0 ROLLBACK TRANSACTION;
    THROW;
END CATCH;
