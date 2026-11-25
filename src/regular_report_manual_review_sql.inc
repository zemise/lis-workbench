R"SQL(
SET NOCOUNT ON;
SET XACT_ABORT ON;
SET LOCK_TIMEOUT 5000;
BEGIN TRY
    BEGIN TRANSACTION;
    DECLARE @status int=0, @checked varchar(1), @current nvarchar(max);
    SELECT @checked=CHK_FLAG, @current=CONVERT(nvarchar(max),ISNULL(sgfc,''))
    FROM dbo.LS_AS_REPORT WITH (UPDLOCK,HOLDLOCK)
    WHERE REP_NO=@report AND DELETE_BIT=0;

    IF @checked IS NULL
        SET @status=1;
    ELSE IF @checked NOT IN ('F','C')
        SET @status=2;
    ELSE IF CONVERT(varbinary(max),@current)<>CONVERT(varbinary(max),@expectedText)
        SET @status=3;
    ELSE IF DATALENGTH(CONVERT(varchar(max),@reviewText COLLATE Chinese_PRC_CI_AS))>300
        SET @status=4;
    ELSE IF CONVERT(varbinary(max),CONVERT(nvarchar(max),
                CONVERT(varchar(max),@reviewText COLLATE Chinese_PRC_CI_AS)))<>CONVERT(varbinary(max),@reviewText)
        SET @status=5;
    ELSE
    BEGIN
        UPDATE dbo.LS_AS_REPORT SET sgfc=@reviewText
        WHERE REP_NO=@report AND DELETE_BIT=0 AND CHK_FLAG IN ('F','C');
        IF @@ROWCOUNT<>1 THROW 51030, 'unexpected manual review row count', 1;
    END;
    COMMIT TRANSACTION;
    SELECT @status AS manual_review_status;
END TRY
BEGIN CATCH
    IF XACT_STATE()<>0 ROLLBACK TRANSACTION;
    THROW;
END CATCH;
)SQL"
