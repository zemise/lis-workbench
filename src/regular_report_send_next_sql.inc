R"SQL(
SET NOCOUNT ON;
SET XACT_ABORT ON;
SET LOCK_TIMEOUT 5000;
SET DEADLOCK_PRIORITY LOW;
BEGIN TRY
    BEGIN TRANSACTION;

    DECLARE @today datetime=CONVERT(date,GETDATE()), @day datetime, @machine smallint,
            @group bigint, @kind varchar(1), @sample bigint, @nextSample decimal(20,0),
            @nextReport decimal(10,0), @status int=0;

    SELECT @day=CONVERT(date,CHK_DATE), @machine=MACH_CODE, @group=GROUP_CODE,
           @kind=TYPE, @sample=TRY_CAST(NULLIF(LTRIM(RTRIM(OPER_NO)),'') AS bigint)
    FROM dbo.LS_AS_REPORT WITH (UPDLOCK,HOLDLOCK)
    WHERE REP_NO=@report AND DELETE_BIT=0 AND CONF='S' AND CHK_FLAG='T';

    IF @sample IS NOT NULL AND @machine>0 AND @group>0 AND @day IS NOT NULL
    BEGIN
        DECLARE @samples TABLE(sample bigint NOT NULL, report decimal(10,0) NOT NULL);
        INSERT @samples(sample,report)
        SELECT TRY_CAST(NULLIF(LTRIM(RTRIM(OPER_NO)),'') AS bigint),REP_NO
        FROM dbo.LS_AS_REPORT WITH (UPDLOCK,HOLDLOCK)
        WHERE CHK_DATE>=@day AND CHK_DATE<DATEADD(day,1,@day) AND MACH_CODE=@machine
            AND GROUP_CODE=@group AND DELETE_BIT=0
            AND TRY_CAST(NULLIF(LTRIM(RTRIM(OPER_NO)),'') AS bigint)>=@sample;

        SET @nextSample=CONVERT(decimal(20,0),@sample)+1;
        SELECT @nextReport=MIN(report) FROM @samples WHERE sample=@nextSample;

        IF @nextReport IS NULL AND @day=@today
        BEGIN
            IF EXISTS (SELECT 1 FROM dbo.T_LIS_OPER_NO_SET WITH (HOLDLOCK)
                       WHERE MACH_CODE=@machine AND DELETE_BIT=0)
                SET @status=1;
            ELSE
            BEGIN
                SELECT @nextSample=MIN(CONVERT(decimal(20,0),s.sample)+1)
                FROM @samples s WHERE NOT EXISTS (
                    SELECT 1 FROM @samples n WHERE n.sample=CONVERT(decimal(20,0),s.sample)+1);
                DECLARE @room smallint, @specimen smallint, @label varchar(100),
                        @type char(1), @counter decimal(10,0), @useProcedure nvarchar(5);
                SELECT @room=MIN(ROOM_CODE),@specimen=MIN(SAMP_CODE)
                FROM dbo.LS_AS_GROUP WITH (HOLDLOCK)
                WHERE MACH_CODE=@machine AND GROUP_CODE=@group AND DELETE_BIT=0
                HAVING COUNT(*)=1;
                SELECT @label=ITEM_NAME FROM dbo.LS_CODE_ITEM WITH (HOLDLOCK) WHERE ITEM_CODE=@group;
                SELECT @type=CASE WHEN EXISTS (SELECT 1 FROM dbo.JC_CONFIG WITH (HOLDLOCK)
                                        WHERE ID=4234 AND RTRIM(CONVERT(varchar(5),CONFIG))='1')
                        THEN CASE WHEN @kind IN ('1','2','3','4','6','7','8') THEN @kind ELSE '5' END
                        ELSE @defaultType END;
                SELECT @useProcedure=RTRIM(CONVERT(nvarchar(5),CONFIG))
                FROM dbo.JC_CONFIG WITH (HOLDLOCK) WHERE ID=4500;

                IF @room IS NULL OR @specimen IS NULL OR @label IS NULL
                   OR @nextSample IS NULL OR @nextSample>9223372036854775807
                    SET @status=1;
                ELSE
                BEGIN
                    SELECT @counter=CURR_NO FROM dbo.LS_AS_GEN_NO WITH (XLOCK,HOLDLOCK) WHERE TYPE=2;
                    IF @counter IS NULL THROW 51020, 'missing report sequence', 1;
                    IF @useProcedure='1'
                    BEGIN
                        DECLARE @procedureResult int;
                        EXEC @procedureResult=dbo.Get_REP_NO @CURR_NO=@nextReport OUTPUT;
                        IF @procedureResult<>0 OR @nextReport<>@counter OR @nextReport IS NULL
                            THROW 51021, 'report sequence allocation failed', 1;
                    END
                    ELSE
                    BEGIN
                        SET @nextReport=@counter;
                        UPDATE dbo.LS_AS_GEN_NO SET CURR_NO=@counter+1 WHERE TYPE=2;
                        IF @@ROWCOUNT<>1 THROW 51021, 'report sequence allocation failed', 1;
                    END;
                    INSERT dbo.LS_AS_REPORT_KEY (REP_NO,MACH_CODE) VALUES (@nextReport,@machine);
                    INSERT dbo.LS_AS_REPORT (REP_NO,GROUP_NO,GROUP_CODE,NAME,SEX,TYPE,ASSAY_TYPE,
                        OPER_NO,ROOM_CODE,MACH_CODE,SAMP_CODE,CHK_DATE,OPER_CODE,REP_OPER,CHK_FLAG,
                        CONF,NORMAL,DELETE_BIT,assaypat_type)
                    VALUES (@nextReport,@label,@group,'','',@type,'1',CONVERT(varchar(20),@nextSample),
                        @room,@machine,@specimen,@day,@employee,0,'F','F','1',0,'1');
                END;
            END;
        END;
    END;
    COMMIT TRANSACTION;
    SELECT @status AS next_report_status, @nextReport AS next_report_number;
END TRY
BEGIN CATCH
    IF XACT_STATE()<>0 ROLLBACK TRANSACTION;
    THROW;
END CATCH;
)SQL"
