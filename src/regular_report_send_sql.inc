R"SQL(
SET NOCOUNT ON;
SET XACT_ABORT ON;
SET LOCK_TIMEOUT 5000;
SET DEADLOCK_PRIORITY LOW;
BEGIN TRY
    BEGIN TRANSACTION;

    DECLARE @state char(1), @checked char(1), @reviewTime datetime, @reviewer bigint,
            @needsReview int, @barcode varchar(20), @machine smallint, @kind varchar(1),
            @status int=0;
    SELECT @state=CONF, @checked=CHK_FLAG, @reviewTime=REP_TIME, @reviewer=REP_OPER,
           @needsReview=NeedInspect, @barcode=RTRIM(TXM_NO), @machine=MACH_CODE, @kind=TYPE
    FROM dbo.LS_AS_REPORT WITH (UPDLOCK,HOLDLOCK)
    WHERE REP_NO=@report AND DELETE_BIT=0;

    IF @state IS NULL OR @state<>'T' OR ISNULL(@checked,'')<>'T'
       OR @reviewTime<>@expectedTime
       OR (@reviewTime IS NULL AND @expectedTime IS NOT NULL)
       OR (@reviewTime IS NOT NULL AND @expectedTime IS NULL)
       OR ISNULL(@reviewer,0)<>@expectedReviewer
        SET @status=1;
    ELSE IF @needsReview=1
        SET @status=2;
    ELSE IF NOT EXISTS (
        SELECT 1 FROM dbo.Pub_User u WITH (HOLDLOCK)
        JOIN dbo.Pub_Group_User gu WITH (HOLDLOCK) ON gu.user_id=u.id
        JOIN dbo.LS_AS_GROUP_POWER p WITH (HOLDLOCK) ON p.group_id=gu.group_id
        WHERE u.Employee_Id=@employee AND u.Code=@login AND p.delete_bit=0 AND p.issend=1
    )
        SET @status=3;

    IF @status=0
    BEGIN
        DECLARE @examGate nvarchar(5), @examWk nvarchar(20), @examRoo2 nvarchar(5),
                @examRoo18 nvarchar(5), @examRoo5 nvarchar(5), @ditu nvarchar(5),
                @inpatient nvarchar(5), @tat nvarchar(5);
        SELECT @examGate=MAX(CASE WHEN ID=4009 THEN v END),
               @examWk=MAX(CASE WHEN ID=4456 THEN LEFT(v,CHARINDEX('|',v+'|')-1) END),
               @examRoo2=MAX(CASE WHEN ID=4652 THEN v END),
               @examRoo18=MAX(CASE WHEN ID=4782 THEN v END),
               @examRoo5=MAX(CASE WHEN ID=4696 THEN v END),
               @ditu=MAX(CASE WHEN ID=4818 THEN v END),
               @inpatient=MAX(CASE WHEN ID=4702 THEN v END),
               @tat=MAX(CASE WHEN ID=4147 THEN v END)
        FROM (SELECT ID,LTRIM(RTRIM(CONVERT(nvarchar(2000),CONFIG))) AS v
              FROM dbo.JC_CONFIG WITH (HOLDLOCK)
              WHERE ID IN (4009,4147,4456,4652,4696,4702,4782,4818)) c;

        IF (@kind='3' AND (
                @examGate='1' OR @examWk='1'
                OR (@examRoo2='1' AND LEFT(@barcode,1)='2')
                OR (@examRoo18='1' AND LEFT(@barcode,1) IN ('0','2') AND LEN(@barcode)=18)
                OR (@examRoo5='1' AND LEFT(@barcode,1)='5')))
           OR (@ditu='1' AND LEFT(@barcode,2)='55')
            SET @status=4;
        ELSE IF ISNULL(@barcode,'')<>'' AND EXISTS (
            SELECT 1 FROM dbo.LS_AS_REPORT WITH (UPDLOCK,HOLDLOCK)
            WHERE TXM_NO=@barcode AND REP_NO<>@report AND DELETE_BIT=0
              AND (CONF IN ('T','S') OR (@kind='2' AND @inpatient='1'))
        )
            SET @status=5;

        IF @status=0
        BEGIN
            DECLARE @matches TABLE (id bigint NOT NULL PRIMARY KEY);
            IF ISNULL(@barcode,'')<>''
            BEGIN
                DECLARE @hasGroups bit=0;
                IF EXISTS (SELECT 1 FROM dbo.LS_AS_REPENTRY WITH (HOLDLOCK)
                           WHERE REP_NO=@report AND DELETE_BIT=0 AND ISNULL(GROUP_CODE,0)<>0)
                    SET @hasGroups=1;

                INSERT @matches(id)
                SELECT b.ID FROM dbo.LS_AS_BARCODE b WITH (UPDLOCK,HOLDLOCK)
                WHERE b.BARCODE=@barcode AND b.DELETE_BIT=0 AND (
                    @machine IS NULL OR (@kind='3' AND ISNULL(b.HOITEM_ID,'')='')
                    OR EXISTS (
                        SELECT 1 FROM dbo.LS_AS_LABMATCH m WITH (HOLDLOCK)
                        JOIN dbo.LS_AS_GROUP g WITH (HOLDLOCK) ON g.GROUP_CODE=m.GROUP_CODE
                        WHERE m.DELETE_BIT=0 AND g.DELETE_BIT=0 AND g.MACH_CODE=@machine
                          AND CASE WHEN LEN(LTRIM(RTRIM(b.HOITEM_ID))) BETWEEN 1 AND 38
                                         AND LTRIM(RTRIM(b.HOITEM_ID)) NOT LIKE '%[^0-9]%'
                                    THEN CONVERT(decimal(38,0),LTRIM(RTRIM(b.HOITEM_ID))) END =
                              CASE WHEN @kind='3' THEN m.GROUP_CODE ELSE m.ORDER_CODE END
                          AND (@hasGroups=0 OR EXISTS (
                              SELECT 1 FROM dbo.LS_AS_REPENTRY e WITH (HOLDLOCK)
                              WHERE e.REP_NO=@report AND e.DELETE_BIT=0 AND e.GROUP_CODE=g.GROUP_CODE
                          ))
                    )
                );
                IF @kind='3' AND NOT EXISTS (SELECT 1 FROM @matches)
                    INSERT @matches(id)
                    SELECT ID FROM dbo.LS_AS_BARCODE WITH (UPDLOCK,HOLDLOCK)
                    WHERE BARCODE=@barcode AND DELETE_BIT=0;
            END;

            UPDATE dbo.LS_AS_REPORT SET CONF='S', CHK_FLAG='T'
            WHERE REP_NO=@report AND DELETE_BIT=0 AND CONF='T' AND CHK_FLAG='T'
              AND ISNULL(NeedInspect,0)<>1;
            IF @@ROWCOUNT<>1 THROW 51010, 'unexpected report send row count', 1;

            IF ISNULL(@barcode,'')<>''
            BEGIN
                IF @kind='2' AND @inpatient='1'
                BEGIN
                    DECLARE @procedureResult int;
                    EXEC @procedureResult=dbo.SP_ZYYS_UPDATEMedicalResults
                        @TXM=@barcode, @ORDER_ID='', @LX=0, @MedicalResults=6;
                    IF @procedureResult<>0 THROW 51011, 'inpatient report send failed', 1;
                END;

                UPDATE b SET OPER_STATE='3'
                FROM dbo.LS_AS_BARCODE b JOIN @matches m ON m.id=b.ID
                WHERE ISNULL(b.OPER_STATE,'')<>'3';

                IF @kind IN ('1','2') AND @tat='1'
                   AND (SELECT COUNT_BIG(*) FROM dbo.T_Lis_As_TATlog WITH (UPDLOCK,HOLDLOCK)
                        WHERE TXM_NO=@barcode)=1
                    UPDATE dbo.T_Lis_As_TATlog SET Send_Time=GETDATE(), State=N'发送'
                    WHERE TXM_NO=@barcode;
            END;
        END;
    END;
    IF @status=0 COMMIT TRANSACTION;
    ELSE ROLLBACK TRANSACTION;
    SELECT @status AS report_send_status;
END TRY
BEGIN CATCH
    IF XACT_STATE()<>0 ROLLBACK TRANSACTION;
    THROW;
END CATCH;
)SQL"
