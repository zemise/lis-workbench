"""Run the production SQL against a dedicated, disposable SQL Server container.

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
RAW = (ROOT / "src/regular_report_cancel_audit_sql.inc").read_text(encoding="utf-8").strip()
assert RAW.startswith('R"SQL(') and RAW.endswith(')SQL"')
BATCH = RAW[len('R"SQL('):-len(')SQL"')]

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


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"Usage: {sys.argv[0]} <dedicated SQL Server container>")
    main(sys.argv[1])
