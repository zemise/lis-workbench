R"SQL(
SET NOCOUNT ON;
SET XACT_ABORT ON;
SET LOCK_TIMEOUT 5000;
SET DEADLOCK_PRIORITY LOW;
BEGIN TRY
    BEGIN TRANSACTION;

    DECLARE @state varchar(1), @check varchar(1), @reportTime datetime, @tester bigint,
            @reviewer bigint, @barcode varchar(20), @machine smallint, @kind varchar(1),
            @patient nvarchar(80), @critical varchar(1), @status int = 0;
    SELECT @state=CONF, @check=CHK_FLAG, @reportTime=REP_TIME, @tester=OPER_CODE,
           @reviewer=REP_OPER, @barcode=RTRIM(TXM_NO), @machine=MACH_CODE, @kind=TYPE,
           @patient=NAME, @critical=assaypat_type
    FROM dbo.LS_AS_REPORT WITH (UPDLOCK,HOLDLOCK)
    WHERE REP_NO=@report AND DELETE_BIT=0;

    IF ISNULL(@state,'') NOT IN ('T','S') OR @state<>@expectedState
       OR ISNULL(@check,'')<>@expectedCheck
       OR @reportTime IS NULL OR @reportTime<>@expectedTime OR @reviewer<>@expectedReviewer
        SET @status=1;

    IF @status=0
    BEGIN
        DECLARE @dateMode varchar(5), @dayText nvarchar(4000), @days int,
                @role varchar(5), @director nvarchar(4000), @directorOnly varchar(5),
                @reasonMode varchar(5), @inpatient varchar(5), @oracle varchar(5),
                @examFirst char(1), @examSecond char(1);
        SELECT @dateMode=MAX(CASE WHEN ID=4010 THEN config_value END),
               @dayText=MAX(CASE WHEN ID=4011 THEN config_value END),
               @role=MAX(CASE WHEN ID=4013 THEN config_value END),
               @director=MAX(CASE WHEN ID=4019 THEN config_value END),
               @reasonMode=MAX(CASE WHEN ID=4158 THEN config_value END),
               @directorOnly=MAX(CASE WHEN ID=4323 THEN config_value END),
               @inpatient=MAX(CASE WHEN ID=4702 THEN config_value END),
               @oracle=MAX(CASE WHEN ID=4415 THEN config_value END),
               @examFirst=MAX(CASE WHEN ID=4439 THEN LEFT(config_value,1) END),
               @examSecond=MAX(CASE WHEN ID=4456 THEN LEFT(config_value,1) END)
        FROM (
            SELECT ID,LTRIM(RTRIM(CONVERT(nvarchar(4000),CONFIG))) AS config_value
            FROM dbo.JC_CONFIG WITH (HOLDLOCK)
            WHERE ID IN (4010,4011,4013,4019,4158,4323,4415,4439,4456,4702)
        ) AS settings;
        -- These conversions must work at the site's SQL Server compatibility level 100.
        SET @days=CASE WHEN LEN(@dayText) BETWEEN 1 AND 5
                            AND @dayText NOT LIKE '%[^0-9]%'
                       THEN CONVERT(int,@dayText) END;
        DECLARE @directorOverride bit=0;
        IF @dateMode='1' AND EXISTS (
            SELECT 1 FROM dbo.Pub_User u WITH (HOLDLOCK)
            JOIN dbo.Pub_Group_User gu WITH (HOLDLOCK) ON gu.user_id=u.id
            WHERE u.Employee_Id=@employee AND u.Code=@login AND
                CHARINDEX(','+CONVERT(varchar(20),gu.group_id)+',',
                          ','+REPLACE(@director,' ','')+',')>0
        )
            SET @directorOverride=1;

        IF ISNULL(@dateMode,'') NOT IN ('0','1') OR @days IS NULL OR @days NOT BETWEEN 0 AND 32767
           OR ISNULL(@role,'') NOT IN ('0','1','2') OR ISNULL(@reasonMode,'') NOT IN ('0','1')
           OR ISNULL(@director,'')='' OR ISNULL(@inpatient,'') NOT IN ('0','1')
           OR ISNULL(@oracle,'')<>'0' OR (@kind='3' AND
              (ISNULL(@examFirst,'')<>'0' OR ISNULL(@examSecond,'')<>'0'))
            SET @status=3;
        ELSE IF NOT EXISTS (
            SELECT 1 FROM dbo.Pub_User u WITH (HOLDLOCK)
            JOIN dbo.Pub_Group_User gu WITH (HOLDLOCK) ON gu.user_id=u.id
            JOIN dbo.LS_AS_GROUP_POWER p WITH (HOLDLOCK) ON p.group_id=gu.group_id
            WHERE u.Employee_Id=@employee AND u.Code=@login AND p.delete_bit=0 AND p.iscommit=1
        )
            SET @status=4;
        ELSE IF @dateMode='0' AND CONVERT(datetime,CONVERT(date,GETDATE())) >=
                DATEADD(day, @days+1, @reportTime)
            SET @status=5;
        ELSE IF @state='S' AND @directorOnly='1' AND @directorOverride=0
            SET @status=8;
        ELSE IF @directorOverride=0 AND NOT (
                  (@role IN ('0','2') AND @tester=@employee)
               OR (@role IN ('1','2') AND @reviewer=@employee))
            SET @status=4;
        ELSE IF (@reasonMode='1' AND (@reasonType IS NULL OR LEN(@reason)=0))
           OR (@reasonType IS NOT NULL AND NOT EXISTS (
             SELECT 1 FROM dbo.LS_AS_CANCEL_REPORT_REASON r WITH (HOLDLOCK)
             JOIN dbo.T_LIS_ReportCancelReasonType t WITH (HOLDLOCK) ON t.TYPE_CODE=r.TYPE_CODE
             WHERE r.TYPE_CODE=@reasonType AND r.ID=@reasonId))
            SET @status=6;
        ELSE IF ISNULL(@barcode,'')<>'' AND EXISTS (
            SELECT 1 FROM dbo.LS_AS_REPORT WITH (UPDLOCK,HOLDLOCK)
            WHERE TXM_NO=@barcode AND REP_NO<>@report AND DELETE_BIT=0 AND CONF IN ('T','S')
        )
            SET @status=7;

        IF @status=0
        BEGIN
            DECLARE @matches TABLE (id bigint NOT NULL PRIMARY KEY);
            IF ISNULL(@barcode,'')<>''
            BEGIN
                DECLARE @hasGroups bit = 0;
                IF EXISTS (SELECT 1 FROM dbo.LS_AS_REPENTRY WITH (HOLDLOCK)
                           WHERE REP_NO=@report AND DELETE_BIT=0)
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

                -- Trasen can mark the barcode as sent before it updates the report.
                IF @state='T' AND EXISTS (SELECT 1 FROM dbo.LS_AS_BARCODE b WITH (UPDLOCK,HOLDLOCK)
                           JOIN @matches m ON m.id=b.ID WHERE b.OPER_STATE='3')
                    SET @status=1;
            END;
        END;

        IF @status=0
        BEGIN
            UPDATE dbo.LS_AS_REPORT
            SET CONF='F', CHK_FLAG='F', IsUpdateData=1,
                UpdateTime=CONVERT(varchar(19),GETDATE(),120),
                UpdateMan=@operatorName, UpdateNote=N'取消审核'
            WHERE REP_NO=@report AND DELETE_BIT=0 AND CONF=@expectedState AND CHK_FLAG=@expectedCheck;
            IF @@ROWCOUNT<>1 THROW 51000, 'unexpected cancel audit row count', 1;

            IF ISNULL(@barcode,'')<>''
            BEGIN
                UPDATE b SET OPER_STATE='1'
                FROM dbo.LS_AS_BARCODE b JOIN @matches m ON m.id=b.ID
                WHERE ISNULL(b.OPER_STATE,'')<>'1';

                IF @inpatient='1'
                BEGIN
                    DECLARE @procedureResult int;
                    EXEC @procedureResult=dbo.SP_ZYYS_UPDATEMedicalResults
                        @TXM=@barcode, @ORDER_ID='', @LX=0, @MedicalResults=5;
                    IF @procedureResult<>0 THROW 51001, 'inpatient status rollback failed', 1;
                END;
            END;
            IF @critical='9'
            BEGIN
                UPDATE dbo.LS_AS_REPORT SET assaypat_type='1' WHERE REP_NO=@report;
                UPDATE dbo.LS_AS_REPENTRY SET NORMAL_WJ=0 WHERE REP_NO=@report;
                DELETE FROM dbo.LS_AS_READREPORT WHERE REP_NO=@report AND assaypat_type='9';
            END;

            INSERT dbo.LS_AS_CANCEL_LOG
                (REP_NO,CUR_NAME,OPERA_MAN,OPERA_MAN_ID,REASON,CANCEL_DATE,MACH_CODE,GROUP_NO,REASON_TYPE)
            VALUES (@report,@patient,@operatorName,@employee,@reason,GETDATE(),@machine,@machineName,
                    ISNULL(CONVERT(varchar(12),@reasonType),''));
            INSERT dbo.LS_LOG (oper_code,workstation,oper_time,exit_time,sys_code,func_code,note)
            VALUES (@employee,CONCAT(HOST_NAME(),'/',@machine),GETDATE(),GETDATE(),
                    N'检验报告',@report,N'取消审核');
            DELETE FROM dbo.T_Lis_As_PrintLog WHERE REP_NO=CONVERT(varchar(20),@report);
        END;
    END;
    IF @status=0 COMMIT TRANSACTION;
    ELSE ROLLBACK TRANSACTION;
    SELECT @status AS cancel_status;
END TRY
BEGIN CATCH
    IF XACT_STATE()<>0 ROLLBACK TRANSACTION;
    THROW;
END CATCH;
)SQL"
