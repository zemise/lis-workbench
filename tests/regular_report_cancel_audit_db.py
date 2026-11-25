"""Test regular-report cancellation, sending and manual review in a disposable SQL Server container.

The container must carry the label lis-workbench.cancel-audit-test=true.
The test creates RegularReportCancelAuditTest and refuses an existing catalog.
No database server or credentials are accepted from the command line.
"""

import json
from pathlib import Path
import subprocess
import sys


DATABASE = "RegularReportCancelAuditTest"
ROOT = Path(__file__).resolve().parents[1]


def sql_batch(name):
    raw = (ROOT / "src" / name).read_text(encoding="utf-8").strip()
    assert raw.startswith('R"SQL(') and raw.endswith(')SQL"')
    return raw[len('R"SQL('):-len(')SQL"')]


BATCH = sql_batch("regular_report_cancel_audit_sql.inc")
SEND_BATCH = sql_batch("regular_report_send_sql.inc")
NEXT_REPORT_BATCH = sql_batch("regular_report_send_next_sql.inc")
MANUAL_REVIEW_BATCH = sql_batch("regular_report_manual_review_sql.inc")

SCHEMA = """
CREATE TABLE dbo.LS_AS_REPORT (
 REP_NO decimal(10,0) PRIMARY KEY, CONF char(1), CHK_FLAG char(1), REP_TIME datetime,
 OPER_CODE bigint, REP_OPER bigint, TXM_NO char(20), MACH_CODE smallint, TYPE char(1),
 NAME nvarchar(80), assaypat_type char(1), DELETE_BIT smallint, IsUpdateData int,
 UpdateTime varchar(50), UpdateMan nvarchar(10), UpdateNote nvarchar(500), NeedInspect int);
CREATE INDEX report_barcode ON dbo.LS_AS_REPORT(TXM_NO);
CREATE TABLE dbo.JC_CONFIG (ID int PRIMARY KEY, CONFIG nvarchar(4000));
CREATE TABLE dbo.Pub_User (id bigint PRIMARY KEY, Employee_Id bigint, Code varchar(30));
CREATE TABLE dbo.Pub_Group_User (user_id bigint, group_id int);
CREATE TABLE dbo.LS_AS_GROUP_POWER (group_id int, delete_bit smallint, iscommit bit);
CREATE TABLE dbo.LS_AS_CANCEL_REPORT_REASON (ID int PRIMARY KEY, TYPE_CODE int);
CREATE TABLE dbo.T_LIS_ReportCancelReasonType (TYPE_CODE int PRIMARY KEY);
CREATE TABLE dbo.LS_AS_REPENTRY (REP_NO decimal(10,0), DELETE_BIT smallint, GROUP_CODE int, NORMAL_WJ int);
CREATE TABLE dbo.LS_AS_READREPORT (REP_NO decimal(10,0), assaypat_type varchar(1));
CREATE TABLE dbo.LS_AS_BARCODE (
 ID bigint IDENTITY PRIMARY KEY, BARCODE varchar(20), DELETE_BIT smallint,
 HOITEM_ID varchar(50), OPER_STATE varchar(1));
CREATE INDEX barcode_lookup ON dbo.LS_AS_BARCODE(BARCODE);
CREATE TABLE dbo.LS_AS_GROUP (GROUP_CODE int, MACH_CODE smallint, DELETE_BIT smallint);
CREATE TABLE dbo.LS_AS_LABMATCH (GROUP_CODE bigint, ORDER_CODE bigint, DELETE_BIT smallint);
CREATE TABLE dbo.YJ_ZYSQ (TXM varchar(30), YZID varchar(36));
CREATE TABLE dbo.ZY_ORDERRECORD (ORDER_ID varchar(36) PRIMARY KEY, MedicalResults int);
CREATE TABLE dbo.LS_AS_CANCEL_LOG (
 REP_NO nvarchar(80), CUR_NAME nvarchar(80), OPERA_MAN nvarchar(80), OPERA_MAN_ID nvarchar(80),
 REASON nvarchar(300), CANCEL_DATE datetime, MACH_CODE nvarchar(50),
 GROUP_NO nvarchar(50), REASON_TYPE nvarchar(10));
CREATE TABLE dbo.LS_LOG (
 oper_code bigint, workstation nvarchar(100), oper_time datetime, exit_time datetime,
 sys_code nvarchar(40), func_code nvarchar(40), note nvarchar(40));
CREATE TABLE dbo.T_Lis_As_PrintLog (REP_NO decimal(10,0) PRIMARY KEY);
GO
CREATE PROCEDURE dbo.SP_ZYYS_UPDATEMedicalResults
 @TXM varchar(30), @ORDER_ID varchar(36), @LX int, @MedicalResults int
AS
 IF @LX=0
    UPDATE dbo.ZY_ORDERRECORD SET MedicalResults=@MedicalResults
    WHERE ORDER_ID IN (SELECT YZID FROM dbo.YJ_ZYSQ WHERE TXM=@TXM);
 ELSE IF @LX=1
    UPDATE dbo.ZY_ORDERRECORD SET MedicalResults=@MedicalResults WHERE ORDER_ID=@ORDER_ID;
GO
CREATE TRIGGER dbo.reject_print_delete ON dbo.T_Lis_As_PrintLog AFTER DELETE
AS
 IF EXISTS (SELECT 1 FROM deleted WHERE REP_NO IN (41,53))
     THROW 51100, 'fixture failure after the report, barcode, inpatient status and logs were written', 1;
GO
INSERT dbo.JC_CONFIG VALUES
 (4010,'0'),(4011,'2000'),(4013,'0'),(4019,'900'),(4158,'1'),
 (4323,'0'),(4415,'0'),(4439,'0'),(4456,'0'),(4702,'1');
INSERT dbo.Pub_User VALUES (1,100,'fixture');
INSERT dbo.Pub_Group_User VALUES (1,10);
INSERT dbo.LS_AS_GROUP_POWER VALUES (10,0,1);
INSERT dbo.LS_AS_CANCEL_REPORT_REASON VALUES (1,2);
INSERT dbo.T_LIS_ReportCancelReasonType VALUES (2);
INSERT dbo.LS_AS_GROUP VALUES (600,7,0);
INSERT dbo.LS_AS_LABMATCH VALUES (600,700,0);
INSERT dbo.LS_AS_REPORT
 (REP_NO,CONF,CHK_FLAG,REP_TIME,OPER_CODE,REP_OPER,TXM_NO,MACH_CODE,TYPE,NAME,assaypat_type,DELETE_BIT,NeedInspect)
SELECT n, state, 'T', DATEADD(day, -1, CONVERT(date, GETDATE())), 100, 200, barcode, 7, '2',
       N'Fixture', critical, 0, CASE WHEN n=11 THEN 1 ELSE 0 END
FROM (VALUES (11,'T','B11','1'),(21,'T','B21','1'),(22,'S','B21','1'),
             (31,'T','B31','1'),(41,'T','B41','1'),(51,'T','B51','9'),
             (53,'T',NULL,'9'),(61,'T','B61','1'),(71,'T',NULL,'1'),(81,'S','B81','1'),
             (82,'S',NULL,'1')) f(n,state,barcode,critical);
INSERT dbo.LS_AS_REPENTRY(REP_NO,DELETE_BIT,GROUP_CODE)
 VALUES (11,0,600),(21,0,600),(22,0,600),(31,0,600),(41,0,600),(61,0,600),(81,0,600);
INSERT dbo.LS_AS_REPENTRY VALUES (51,0,600,9),(52,0,600,9),(53,0,600,9);
INSERT dbo.LS_AS_READREPORT VALUES (51,'9'),(51,'1'),(52,'9'),(53,'9');
INSERT dbo.LS_AS_BARCODE(BARCODE,DELETE_BIT,HOITEM_ID,OPER_STATE)
 VALUES ('B11',0,'000700','2'),('B11',0,'701','2'),
        ('B21',0,'700','2'),('B31',0,'700','2'),('B41',0,'700','2'),('B61',0,'700','3'),
        ('B81',0,'000700','3'),('B81',0,'not-a-code','3');
INSERT dbo.YJ_ZYSQ VALUES ('B11','O11'),('B21','O21'),('B31','O31'),('B41','O41'),('B61','O61'),('B81','O81');
INSERT dbo.ZY_ORDERRECORD VALUES ('O11',6),('O21',6),('O31',6),('O41',6),('O61',6),('O81',6);
INSERT dbo.T_Lis_As_PrintLog VALUES (11),(21),(31),(41),(53),(61),(81);
"""


def client(container, database):
    return [
        "docker", "exec", "-i", container, "sh", "-c",
        'SQLCMDPASSWORD="$MSSQL_SA_PASSWORD" exec /opt/mssql-tools18/bin/sqlcmd '
        '-S tcp:127.0.0.1,1433 -U sa -C -b -r 1 -h -1 -f 65001 -l 8 -t 15 -d "$1"',
        "sqlcmd-test", database,
    ]


def run(container, sql, database=DATABASE, expected_error=None):
    result = subprocess.run(client(container, database), input=sql + "\nGO\n", text=True,
                            capture_output=True, timeout=25)
    if expected_error:
        if result.returncode == 0 or expected_error not in result.stderr:
            raise AssertionError(f"Expected fixture error {expected_error}: {result.stderr}")
    elif result.returncode != 0:
        raise AssertionError(result.stdout + result.stderr)
    return result.stdout.strip()


def cancel_sql(report, reason="N'Fixture correction'", reviewer=200, expected_state="T"):
    return f"""
DECLARE @report decimal(10,0)={report}, @expectedTime datetime=DATEADD(day,-1,CONVERT(date,GETDATE())),
 @expectedState varchar(1)='{expected_state}', @expectedCheck varchar(1)='T',
 @expectedReviewer bigint={reviewer}, @employee bigint=100, @login nvarchar(128)=N'fixture',
 @operatorName nvarchar(80)=N'Operator', @machineName nvarchar(50)=N'Fixture instrument',
 @reason nvarchar(300)={reason}, @reasonId int=1,@reasonType int=2;
""" + BATCH


def state_sql(report, barcode, order, report_state, barcode_state="2"):
    return f"""
IF NOT EXISTS(SELECT 1 FROM dbo.LS_AS_REPORT WHERE REP_NO={report} AND CONF='{report_state}'
              AND CHK_FLAG='T' AND IsUpdateData IS NULL AND UpdateNote IS NULL)
 OR NOT EXISTS(SELECT 1 FROM dbo.LS_AS_BARCODE WHERE BARCODE='{barcode}' AND OPER_STATE='{barcode_state}')
 OR NOT EXISTS(SELECT 1 FROM dbo.ZY_ORDERRECORD WHERE ORDER_ID='{order}' AND MedicalResults=6)
 OR NOT EXISTS(SELECT 1 FROM dbo.T_Lis_As_PrintLog WHERE REP_NO={report})
 OR EXISTS(SELECT 1 FROM dbo.LS_AS_CANCEL_LOG WHERE REP_NO='{report}')
 OR EXISTS(SELECT 1 FROM dbo.LS_LOG WHERE func_code='{report}')
 THROW 51102, 'Report or linked status changed after a denied/failed cancellation', 1;
"""


def send_sql(report, reviewer=200, expected_time="DATEADD(day,-1,CONVERT(date,GETDATE()))"):
    return f"""
DECLARE @report decimal(10,0)={report}, @expectedTime datetime={expected_time},
 @expectedReviewer bigint={reviewer}, @employee bigint=100, @login nvarchar(128)=N'fixture';
""" + SEND_BATCH


def next_report_sql(report):
    return f"""
DECLARE @report decimal(10,0)={report}, @employee bigint=100, @defaultType char(1)='2';
""" + NEXT_REPORT_BATCH


def check_send(container):
    run(container, """
ALTER TABLE dbo.LS_AS_GROUP_POWER ADD issend int NOT NULL DEFAULT 1;
ALTER TABLE dbo.LS_AS_REPORT ADD OPER_NO varchar(20),GROUP_NO varchar(100),GROUP_CODE bigint,SEX varchar(1),
 ASSAY_TYPE varchar(1),ROOM_CODE smallint,SAMP_CODE smallint,CHK_DATE datetime,NORMAL varchar(1);
ALTER TABLE dbo.LS_AS_GROUP ADD ROOM_CODE smallint,SAMP_CODE smallint;
GO
UPDATE dbo.LS_AS_GROUP SET ROOM_CODE=3,SAMP_CODE=4 WHERE GROUP_CODE=600;
CREATE TABLE dbo.LS_CODE_ITEM (ITEM_CODE bigint PRIMARY KEY,ITEM_NAME varchar(100));
INSERT dbo.LS_CODE_ITEM VALUES (600,'Fixture');
CREATE TABLE dbo.T_Lis_As_TATlog (TXM_NO varchar(20),Send_Time datetime,[State] nvarchar(20));
CREATE TABLE dbo.T_LIS_OPER_NO_SET (MACH_CODE smallint,DELETE_BIT smallint);
CREATE TABLE dbo.LS_AS_REPORT_KEY (REP_NO decimal(10,0) PRIMARY KEY,MACH_CODE smallint);
CREATE TABLE dbo.LS_AS_GEN_NO ([TYPE] smallint PRIMARY KEY,CURR_NO decimal(10,0));
INSERT dbo.LS_AS_GEN_NO VALUES (2,500);
GO
CREATE PROCEDURE dbo.Get_REP_NO @CURR_NO decimal(10,0) OUTPUT
AS
BEGIN
 BEGIN TRANSACTION;
 SELECT @CURR_NO=CURR_NO FROM dbo.LS_AS_GEN_NO WHERE [TYPE]=2;
 UPDATE dbo.LS_AS_GEN_NO SET CURR_NO=@CURR_NO+1 WHERE [TYPE]=2;
 COMMIT TRANSACTION;
END;
GO
CREATE TRIGGER dbo.reject_barcode_send ON dbo.LS_AS_BARCODE AFTER UPDATE
AS
 IF EXISTS (SELECT 1 FROM inserted WHERE BARCODE='B94' AND OPER_STATE='3')
     THROW 51110, 'fixture rejects barcode send', 1;
GO
INSERT dbo.JC_CONFIG VALUES (4147,'1'),(4500,'1'),(4234,'0');
INSERT dbo.LS_AS_REPORT (REP_NO,CONF,CHK_FLAG,REP_TIME,OPER_CODE,REP_OPER,TXM_NO,MACH_CODE,TYPE,
 NAME,assaypat_type,DELETE_BIT,NeedInspect,CHK_DATE,GROUP_CODE,OPER_NO)
SELECT n,'T','T',DATEADD(day,-1,CONVERT(date,GETDATE())),100,200,barcode,7,kind,
 N'Fixture','1',0,inspect,CONVERT(date,GETDATE()),600,CONVERT(varchar(20),n)
FROM (VALUES (91,'B91','2',0),(93,'B93','2',1),(94,'B94','2',0),
             (96,'B96','3',0),(97,NULL,'1',0),(98,NULL,'1',0),(99,NULL,'1',0)) f(n,barcode,kind,inspect);
UPDATE dbo.LS_AS_REPORT SET REP_TIME=NULL,CHK_DATE=DATEADD(day,-1,CONVERT(date,GETDATE())) WHERE REP_NO=97;
INSERT dbo.LS_AS_REPENTRY (REP_NO,DELETE_BIT,GROUP_CODE) VALUES (91,0,600),(94,0,600);
INSERT dbo.LS_AS_BARCODE (BARCODE,DELETE_BIT,HOITEM_ID,OPER_STATE)
 VALUES ('B91',0,'000700','2'),('B91',0,'701','2'),('B91',1,'700','2'),
        ('B94',0,'700','2'),('B96',0,'999','2');
INSERT dbo.YJ_ZYSQ VALUES ('B91','O91'),('B94','O94');
INSERT dbo.ZY_ORDERRECORD VALUES ('O91',5),('O94',5);
INSERT dbo.T_Lis_As_TATlog VALUES ('B91',NULL,N'审核');
""")
    assert run(container, send_sql(93)) == "2"
    assert run(container, send_sql(21)) == "5"
    assert run(container, send_sql(91, reviewer=201)) == "1"
    run(container, "UPDATE dbo.LS_AS_GROUP_POWER SET issend=0;")
    assert run(container, send_sql(91)) == "3"
    run(container, "UPDATE dbo.LS_AS_GROUP_POWER SET issend=1;")
    run(container, "UPDATE dbo.JC_CONFIG SET CONFIG='1|fixture' WHERE ID=4456;")
    assert run(container, send_sql(96)) == "4"
    run(container, "UPDATE dbo.JC_CONFIG SET CONFIG='0' WHERE ID=4456;")
    assert run(container, send_sql(96)) == "0"
    assert run(container, send_sql(91)) == "0"
    assert run(container, send_sql(91)) == "1"
    run(container, """
IF NOT EXISTS(SELECT 1 FROM dbo.LS_AS_REPORT WHERE REP_NO=91 AND CONF='S' AND CHK_FLAG='T'
              AND REP_OPER=200 AND REP_TIME=DATEADD(day,-1,CONVERT(date,GETDATE())))
 OR NOT EXISTS(SELECT 1 FROM dbo.LS_AS_BARCODE WHERE BARCODE='B91' AND HOITEM_ID='000700' AND OPER_STATE='3')
 OR EXISTS(SELECT 1 FROM dbo.LS_AS_BARCODE WHERE BARCODE='B91' AND HOITEM_ID IN ('701','700') AND OPER_STATE='3')
 OR NOT EXISTS(SELECT 1 FROM dbo.LS_AS_BARCODE WHERE BARCODE='B96' AND OPER_STATE='3')
 OR NOT EXISTS(SELECT 1 FROM dbo.ZY_ORDERRECORD WHERE ORDER_ID='O91' AND MedicalResults=6)
 OR NOT EXISTS(SELECT 1 FROM dbo.T_Lis_As_TATlog WHERE TXM_NO='B91' AND Send_Time IS NOT NULL AND [State]=N'发送')
 THROW 51111, 'Report send did not write exactly the expected state', 1;
""")
    run(container, send_sql(94), expected_error="51110")
    run(container, """
IF NOT EXISTS(SELECT 1 FROM dbo.LS_AS_REPORT WHERE REP_NO=94 AND CONF='T')
 OR NOT EXISTS(SELECT 1 FROM dbo.LS_AS_BARCODE WHERE BARCODE='B94' AND OPER_STATE='2')
 OR NOT EXISTS(SELECT 1 FROM dbo.ZY_ORDERRECORD WHERE ORDER_ID='O94' AND MedicalResults=5)
 THROW 51112, 'Failed barcode write did not roll back send and medical order', 1;
""")
    assert run(container, send_sql(97)) == "1"
    assert run(container, send_sql(97, expected_time="NULL")) == "0"
    assert run(container, next_report_sql(97)).split() == ["0", "NULL"]
    assert run(container, send_sql(98)) == "0"
    assert run(container, next_report_sql(98)).split() == ["0", "99"]
    assert run(container, next_report_sql(91)).split() == ["0", "500"]
    assert run(container, next_report_sql(91)).split() == ["0", "500"]
    run(container, """
IF NOT EXISTS(SELECT 1 FROM dbo.LS_AS_REPORT WHERE REP_NO=500 AND OPER_NO='92' AND CONF='F'
    AND CHK_FLAG='F' AND MACH_CODE=7 AND GROUP_CODE=600 AND ROOM_CODE=3 AND SAMP_CODE=4 AND TYPE='2')
 OR NOT EXISTS(SELECT 1 FROM dbo.LS_AS_REPORT_KEY WHERE REP_NO=500 AND MACH_CODE=7)
 OR NOT EXISTS(SELECT 1 FROM dbo.LS_AS_GEN_NO WHERE [TYPE]=2 AND CURR_NO=501)
 THROW 51113, 'Next report did not use the verified allocation and numbering rules', 1;
""")
    assert run(container, cancel_sql(91, expected_state="S")) == "0"
    assert run(container, send_sql(91)) == "1"
    print("Send and next-sample regression passed at compatibility 100.")


def review_sql(report, expected, text):
    return f"""
DECLARE @report decimal(10,0)={report}, @expectedText nvarchar(max)={expected},
 @reviewText nvarchar(max)={text};
""" + MANUAL_REVIEW_BATCH


def check_manual_review(container):
    run(container, """
ALTER TABLE dbo.LS_AS_REPORT ADD sgfc varchar(300) COLLATE Chinese_PRC_CI_AS NULL;
GO
INSERT dbo.LS_AS_REPORT (REP_NO,CONF,CHK_FLAG,DELETE_BIT,NeedInspect)
 VALUES (110,'F','F',0,1),(111,'F','C',0,0),(112,'T','T',0,0),(113,'S','T',0,0),(114,'F','F',1,0);
UPDATE dbo.LS_AS_REPORT SET sgfc=N'已审核说明' WHERE REP_NO IN (112,113,114);
""")
    multiline = "N'手工复查''引号'+NCHAR(13)+NCHAR(10)+N'第二行'"
    assert run(container, review_sql(110, "N''", multiline)) == "0"
    assert run(container, review_sql(110, "N''", "N'并发修改'")) == "3"
    assert run(container, review_sql(111, "N''", "REPLICATE(N'复',151)")) == "4"
    assert run(container, review_sql(111, "N''", "N'不支持😀'")) == "5"
    assert run(container, review_sql(111, "N''", "REPLICATE(N'复',150)")) == "0"
    for report in (112, 113):
        assert run(container, review_sql(report, "N'已审核说明'", "N'修改'")) == "2"
    assert run(container, review_sql(114, "N'已审核说明'", "N'修改'")) == "1"
    run(container, f"""
IF NOT EXISTS(SELECT 1 FROM dbo.LS_AS_REPORT WHERE REP_NO=110 AND NeedInspect=1
              AND CONVERT(nvarchar(max),sgfc)=({multiline}))
 OR NOT EXISTS(SELECT 1 FROM dbo.LS_AS_REPORT WHERE REP_NO=111 AND DATALENGTH(sgfc)=300)
 OR (SELECT COUNT(*) FROM dbo.LS_AS_REPORT WHERE REP_NO IN (112,113,114) AND sgfc=N'已审核说明')<>3
 THROW 51114, 'Manual review changed a protected report or corrupted text', 1;
""")
    assert run(container, review_sql(110, multiline, "N''")) == "0"
    run(container, """
IF NOT EXISTS (SELECT 1 FROM dbo.LS_AS_REPORT WHERE REP_NO=110 AND DATALENGTH(sgfc)=0 AND NeedInspect=1)
 THROW 51115, 'Manual review did not clear the note independently of NeedInspect', 1;
""")
    print("Manual-review note regression passed at compatibility 100.")


def main(container):
    inspected = subprocess.check_output(
        ["docker", "inspect", "--format", "{{json .Config.Labels}}", container], text=True)
    labels = json.loads(inspected)
    if not labels or labels.get("lis-workbench.cancel-audit-test") != "true":
        raise RuntimeError("The container is not labeled as the dedicated cancellation test fixture")
    run(container, f"""
IF DB_ID('{DATABASE}') IS NOT NULL THROW 51101, 'Fixture database already exists', 1;
GO
CREATE DATABASE {DATABASE};
GO
ALTER DATABASE {DATABASE} SET COMPATIBILITY_LEVEL=100;
""", database="master")
    run(container, SCHEMA)

    assert run(container, cancel_sql(11)) == "0"
    run(container, """
IF NOT EXISTS(SELECT 1 FROM dbo.LS_AS_REPORT WHERE REP_NO=11 AND CONF='F' AND CHK_FLAG='F'
              AND IsUpdateData=1 AND REP_OPER=200 AND UpdateNote=N'取消审核' AND NeedInspect=1)
 OR NOT EXISTS(SELECT 1 FROM dbo.LS_AS_BARCODE WHERE BARCODE='B11' AND HOITEM_ID='000700' AND OPER_STATE='1')
 OR NOT EXISTS(SELECT 1 FROM dbo.LS_AS_BARCODE WHERE BARCODE='B11' AND HOITEM_ID='701' AND OPER_STATE='2')
 OR NOT EXISTS(SELECT 1 FROM dbo.ZY_ORDERRECORD WHERE ORDER_ID='O11' AND MedicalResults=5)
 OR EXISTS(SELECT 1 FROM dbo.T_Lis_As_PrintLog WHERE REP_NO=11)
 OR NOT EXISTS(SELECT 1 FROM dbo.LS_AS_CANCEL_LOG WHERE REP_NO='11' AND REASON_TYPE='2')
 OR NOT EXISTS(SELECT 1 FROM dbo.LS_LOG WHERE func_code='11')
 THROW 51103, 'Cancellation did not update all linked state', 1;
""")
    assert run(container, cancel_sql(11)) == "1"
    assert run(container, cancel_sql(21)) == "7"
    run(container, state_sql(21, "B21", "O21", "T"))
    assert run(container, cancel_sql(41, reason="N''")) == "6"
    assert run(container, cancel_sql(41, reviewer=201)) == "1"
    assert run(container, cancel_sql(51)) == "0"
    run(container, """
IF NOT EXISTS(SELECT 1 FROM dbo.LS_AS_REPORT WHERE REP_NO=51 AND CONF='F' AND assaypat_type='1')
 OR NOT EXISTS(SELECT 1 FROM dbo.LS_AS_REPENTRY WHERE REP_NO=51 AND NORMAL_WJ=0)
 OR EXISTS(SELECT 1 FROM dbo.LS_AS_READREPORT WHERE REP_NO=51 AND assaypat_type='9')
 OR NOT EXISTS(SELECT 1 FROM dbo.LS_AS_READREPORT WHERE REP_NO=51 AND assaypat_type='1')
 OR NOT EXISTS(SELECT 1 FROM dbo.LS_AS_READREPORT WHERE REP_NO=52 AND assaypat_type='9')
 THROW 51107, 'Critical cancellation did not clear exactly the selected report', 1;
""")
    run(container, cancel_sql(53), expected_error="51100")
    run(container, """
IF NOT EXISTS(SELECT 1 FROM dbo.LS_AS_REPORT WHERE REP_NO=53 AND CONF='T' AND assaypat_type='9')
 OR NOT EXISTS(SELECT 1 FROM dbo.LS_AS_REPENTRY WHERE REP_NO=53 AND NORMAL_WJ=9)
 OR NOT EXISTS(SELECT 1 FROM dbo.LS_AS_READREPORT WHERE REP_NO=53 AND assaypat_type='9')
 THROW 51109, 'Critical cancellation was not rolled back after a failed write', 1;
""")
    assert run(container, cancel_sql(82, expected_state="S")) == "0"
    run(container, "UPDATE dbo.JC_CONFIG SET CONFIG='1' WHERE ID=4323;")
    assert run(container, cancel_sql(81, expected_state="S")) == "8"
    run(container, state_sql(81, "B81", "O81", "S", barcode_state="3"))
    run(container, "UPDATE dbo.JC_CONFIG SET CONFIG='1' WHERE ID=4010; "
                   "UPDATE dbo.JC_CONFIG SET CONFIG='10' WHERE ID=4019;")
    assert run(container, cancel_sql(81, expected_state="S")) == "0"
    run(container, """
IF NOT EXISTS(SELECT 1 FROM dbo.LS_AS_REPORT WHERE REP_NO=81 AND CONF='F' AND CHK_FLAG='F')
 OR NOT EXISTS(SELECT 1 FROM dbo.LS_AS_BARCODE WHERE BARCODE='B81' AND HOITEM_ID='000700' AND OPER_STATE='1')
 OR NOT EXISTS(SELECT 1 FROM dbo.LS_AS_BARCODE WHERE BARCODE='B81' AND HOITEM_ID='not-a-code' AND OPER_STATE='3')
 OR NOT EXISTS(SELECT 1 FROM dbo.ZY_ORDERRECORD WHERE ORDER_ID='O81' AND MedicalResults=5)
 THROW 51108, 'Sent cancellation did not clear the selected mapped state', 1;
""")
    run(container, "UPDATE dbo.JC_CONFIG SET CONFIG='0' WHERE ID IN (4010,4323);")
    assert run(container, cancel_sql(61)) == "1"
    run(container, state_sql(61, "B61", "O61", "T", barcode_state="3"))

    with subprocess.Popen(client(container, DATABASE), stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) as sender:
        sender.stdin.write("""
BEGIN TRANSACTION;
UPDATE dbo.LS_AS_REPORT SET CONF='S' WHERE REP_NO=31;
RAISERROR('sender-holds-lock',10,1) WITH NOWAIT;
WAITFOR DELAY '00:00:02';
COMMIT TRANSACTION;
GO
""")
        sender.stdin.close()
        if not any("sender-holds-lock" in line for line in sender.stdout):
            raise AssertionError("The sending connection did not acquire the report lock")
        assert run(container, cancel_sql(31)) == "1"
        assert sender.wait(timeout=15) == 0
    run(container, state_sql(31, "B31", "O31", "S"))
    run(container, cancel_sql(41), expected_error="51100")
    run(container, state_sql(41, "B41", "O41", "T"))
    print("SQL Server compatibility 100 regression passed: ordinary, sent and critical cancellation, "
          "director permission, shared barcode, stale review, concurrent send and failed write rollback.")
    assert run(container, cancel_sql(71)) == "0"
    run(container, """
UPDATE dbo.LS_AS_REPORT SET CONF='S',CHK_FLAG='T' WHERE REP_NO=71 AND CONF='T' AND CHK_FLAG='T';
IF @@ROWCOUNT<>0 THROW 51105, 'Guarded sender overwrote a completed cancellation', 1;
-- Exercise the field client's unguarded final send update after its earlier precheck.
UPDATE dbo.LS_AS_REPORT SET CONF='S',CHK_FLAG='T' WHERE REP_NO=71;
IF @@ROWCOUNT<>1 THROW 51106, 'Legacy sender race reproduction did not reach the report', 1;
""")
    print("Legacy interleaving reproduced: the original unguarded sender can overwrite a prior "
          "cancellation; an atomic send-state guard rejects that write.")
    check_send(container)
    check_manual_review(container)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"Usage: {sys.argv[0]} <dedicated SQL Server container>")
    main(sys.argv[1])
