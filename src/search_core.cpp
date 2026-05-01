#include "search_core.h"
#include "search_text.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <sqlext.h>
#endif

namespace search {
namespace {

std::string sql_escape(std::string value) {
    size_t pos = 0;
    while ((pos = value.find('\'', pos)) != std::string::npos) {
        value.insert(pos, "'");
        pos += 2;
    }
    return value;
}

std::vector<std::string> split(const std::string& text, char delimiter) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string part;
    while (std::getline(ss, part, delimiter)) {
        out.push_back(part);
    }
    return out;
}

std::string upper_ascii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return text;
}

std::map<std::string, std::string> parse_connection_kv(const std::string& text) {
    std::map<std::string, std::string> values;
    for (const auto& part : split(text, ';')) {
        const auto pos = part.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        values[upper_ascii(trim(part.substr(0, pos)))] = trim(part.substr(pos + 1));
    }
    return values;
}

std::string candidate_driver_name(const std::string& candidate) {
    const auto upper = upper_ascii(candidate);
    const auto pos = upper.find("DRIVER={");
    if (pos == std::string::npos) {
        return "manual";
    }
    const auto start = pos + 8;
    const auto end = candidate.find('}', start);
    if (end == std::string::npos) {
        return "manual";
    }
    return candidate.substr(start, end - start);
}

std::vector<std::string> odbc_candidates(const std::string& input) {
    if (input.find("DRIVER=") != std::string::npos || input.find("Driver=") != std::string::npos) {
        return {input};
    }
    const auto kv = parse_connection_kv(input);
    const auto server_it = kv.find("DATA SOURCE");
    const auto db_it = kv.find("INITIAL CATALOG");
    const auto user_it = kv.find("USER ID");
    const auto pass_it = kv.find("PASSWORD");
    if (server_it == kv.end() || db_it == kv.end() || user_it == kv.end() || pass_it == kv.end()) {
        return {input};
    }

    return {
        "DRIVER={ODBC Driver 18 for SQL Server};SERVER=" + server_it->second +
            ";DATABASE=" + db_it->second +
            ";UID=" + user_it->second +
            ";PWD=" + pass_it->second +
            ";TrustServerCertificate=Yes;",
        "DRIVER={ODBC Driver 17 for SQL Server};SERVER=" + server_it->second +
            ";DATABASE=" + db_it->second +
            ";UID=" + user_it->second +
            ";PWD=" + pass_it->second +
            ";TrustServerCertificate=Yes;",
        "DRIVER={SQL Server};SERVER=" + server_it->second +
            ";DATABASE=" + db_it->second +
            ";UID=" + user_it->second +
            ";PWD=" + pass_it->second + ";",
    };
}

#ifdef _WIN32
struct DbContext {
    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC dbc = SQL_NULL_HDBC;
};

std::string fetch_column(SQLHSTMT stmt, SQLUSMALLINT col) {
    std::wstring buffer(2048, L'\0');
    SQLLEN indicator = 0;
    const SQLRETURN rc = SQLGetData(stmt, col, SQL_C_WCHAR, buffer.data(),
                                    static_cast<SQLLEN>(buffer.size() * sizeof(wchar_t)), &indicator);
    if (!(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) || indicator == SQL_NULL_DATA) {
        return "";
    }
    buffer.resize(wcslen(buffer.c_str()));
    return trim(wide_to_utf8(buffer));
}

std::string collect_diag(SQLSMALLINT handle_type, SQLHANDLE handle) {
    std::ostringstream oss;
    SQLSMALLINT rec = 1;
    while (true) {
        SQLWCHAR state[16] = {};
        SQLWCHAR message[1024] = {};
        SQLINTEGER native_error = 0;
        SQLSMALLINT text_len = 0;
        const SQLRETURN rc = SQLGetDiagRecW(handle_type, handle, rec, state, &native_error,
                                            message, 1024, &text_len);
        if (!(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO)) {
            break;
        }
        if (rec > 1) {
            oss << " | ";
        }
        oss << wide_to_utf8(state) << ":" << native_error << ":" << wide_to_utf8(message);
        ++rec;
    }
    return oss.str();
}

void disconnect(DbContext& db) {
    if (db.dbc != SQL_NULL_HDBC) {
        SQLDisconnect(db.dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, db.dbc);
        db.dbc = SQL_NULL_HDBC;
    }
    if (db.env != SQL_NULL_HENV) {
        SQLFreeHandle(SQL_HANDLE_ENV, db.env);
        db.env = SQL_NULL_HENV;
    }
}

bool connect(const std::string& connection_string, DbContext& db, std::string& error, LogFn log) {
    if (connection_string.empty()) {
        error = "missing connection string";
        return false;
    }
    if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &db.env) != SQL_SUCCESS) {
        error = "SQLAllocHandle ENV failed";
        return false;
    }
    SQLSetEnvAttr(db.env, SQL_ATTR_ODBC_VERSION, reinterpret_cast<void*>(SQL_OV_ODBC3), 0);
    if (SQLAllocHandle(SQL_HANDLE_DBC, db.env, &db.dbc) != SQL_SUCCESS) {
        error = "SQLAllocHandle DBC failed";
        disconnect(db);
        return false;
    }

    for (const auto& candidate : odbc_candidates(connection_string)) {
        const auto driver_name = candidate_driver_name(candidate);
        if (log) {
            log("db try driver=" + driver_name + "\n");
        }
        const auto wide = utf8_to_wide(candidate);
        SQLWCHAR out_conn[2048] = {};
        SQLSMALLINT out_len = 0;
        const SQLRETURN rc = SQLDriverConnectW(
            db.dbc, nullptr,
            reinterpret_cast<SQLWCHAR*>(const_cast<wchar_t*>(wide.c_str())), SQL_NTS,
            out_conn, 2048, &out_len, SQL_DRIVER_NOPROMPT);
        if (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) {
            if (log) {
                log("db connect ok driver=" + driver_name + "\n");
            }
            return true;
        }
        if (log) {
            log("db connect failed driver=" + driver_name + " diag=" + collect_diag(SQL_HANDLE_DBC, db.dbc) + "\n");
        }
    }

    error = "SQLDriverConnect failed: " + collect_diag(SQL_HANDLE_DBC, db.dbc);
    disconnect(db);
    return false;
}

bool exec_query(SQLHDBC dbc, const std::string& sql, SQLHSTMT& stmt, std::string& error) {
    stmt = SQL_NULL_HSTMT;
    if (SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt) != SQL_SUCCESS) {
        error = "SQLAllocHandle STMT failed";
        return false;
    }
    const auto wide = utf8_to_wide(sql);
    const SQLRETURN rc = SQLExecDirectW(stmt, reinterpret_cast<SQLWCHAR*>(const_cast<wchar_t*>(wide.c_str())), SQL_NTS);
    if (!(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO)) {
        error = "SQLExecDirect failed: " + collect_diag(SQL_HANDLE_STMT, stmt);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        stmt = SQL_NULL_HSTMT;
        return false;
    }
    return true;
}

void add_eq(std::ostringstream& sql, const char* col, const std::string& value) {
    if (!trim(value).empty()) {
        sql << " AND " << col << "='" << sql_escape(trim(value)) << "'";
    }
}

void add_like(std::ostringstream& sql, const char* col, const std::string& value) {
    if (!trim(value).empty()) {
        sql << " AND " << col << " LIKE '%" << sql_escape(trim(value)) << "%'";
    }
}

void add_report_status(std::ostringstream& sql, const std::string& value) {
    const auto status = trim(value);
    if (status.empty() || status == "全部") {
        return;
    }
    if (status == "已审核") {
        sql << " AND r.CONF='T'";
        return;
    }
    if (status == "未审核") {
        sql << " AND isnull(r.CONF,'')<>'T' AND isnull(r.CONF,'')<>'S'";
        return;
    }
    if (status == "已发送") {
        sql << " AND r.CHK_FLAG='T'";
        return;
    }
    if (status == "未发送") {
        sql << " AND isnull(r.CHK_FLAG,'')<>'T'";
    }
}
#endif

}  // namespace

bool query_rooms(const std::string& connection_string, std::vector<RoomOption>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)connection_string;
    (void)log;
    error = "query_rooms is only available on Windows";
    return false;
#else
    DbContext db;
    if (!connect(connection_string, db, error, log)) {
        return false;
    }

    const std::string sql =
        "SELECT CAST(ROOM_CODE AS varchar(20)), isnull(RTRIM(ROOM_NAME),'')"
        " FROM LS_AS_ROOM WHERE DELETE_BIT=0 ORDER BY ROOM_CODE";
    if (log) {
        log("exec sql: " + sql + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql, stmt, error)) {
        disconnect(db);
        return false;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        RoomOption row;
        row.room_code = fetch_column(stmt, 1);
        row.room_name = fetch_column(stmt, 2);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    disconnect(db);
    error.clear();
    return true;
#endif
}

bool query_patient_types(const std::string& connection_string, std::vector<PatientTypeOption>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)connection_string;
    (void)log;
    error = "query_patient_types is only available on Windows";
    return false;
#else
    DbContext db;
    if (!connect(connection_string, db, error, log)) {
        return false;
    }

    const std::string sql =
        "SELECT isnull(TYPE,''), isnull(RTRIM(TYPE_NAME),'')"
        " FROM LS_AS_PATTYPE WHERE DELETE_BIT=0 ORDER BY TYPE";
    if (log) {
        log("exec sql: " + sql + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql, stmt, error)) {
        disconnect(db);
        return false;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        PatientTypeOption row;
        row.type_code = fetch_column(stmt, 1);
        row.type_name = fetch_column(stmt, 2);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    disconnect(db);
    error.clear();
    return true;
#endif
}

bool query_machines(const std::string& connection_string, const std::string& room_code, std::vector<MachineOption>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)connection_string;
    (void)room_code;
    (void)log;
    error = "query_machines is only available on Windows";
    return false;
#else
    DbContext db;
    if (!connect(connection_string, db, error, log)) {
        return false;
    }

    std::ostringstream sql;
    sql << "SELECT CAST(MACH_CODE AS varchar(20)), isnull(RTRIM(MACH_NAME),'')"
        << " FROM LS_AS_MACHINE WHERE DELETE_BIT=0 AND isnull(RTRIM(RUL),'')='启用'";
    add_eq(sql, "ROOM_CODE", room_code);
    sql << " ORDER BY MACH_CODE";
    if (log) {
        log("exec sql: " + sql.str() + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) {
        disconnect(db);
        return false;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        MachineOption row;
        row.mach_code = fetch_column(stmt, 1);
        row.mach_name = fetch_column(stmt, 2);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    disconnect(db);
    error.clear();
    return true;
#endif
}

bool query_reports(const QueryFilters& filters, std::vector<ReportRow>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)filters;
    (void)log;
    error = "query_reports is only available on Windows";
    return false;
#else
    DbContext db;
    if (!connect(filters.connection_string, db, error, log)) {
        return false;
    }

    std::ostringstream sql;
    sql << "SELECT ";
    if (filters.limit > 0) {
        sql << "TOP " << filters.limit << " ";
    }
    sql << "CAST(r.REP_NO AS varchar(20)),"
        << " isnull(r.OPER_NO,''),isnull(r.NAME,''),isnull(r.TXM_NO,''),"
        << " isnull(CONVERT(varchar(19),r.REP_DATE,120),''),isnull(RTRIM(sx.SEX_NAME),''),isnull(r.AGE,''),"
        << " isnull(r.BED_CODE,''),isnull(RTRIM(p.TYPE_NAME),''),isnull(RTRIM(emp_oper.NAME),''),"
        << " isnull(RTRIM(emp_rep.NAME),''),isnull(r.GROUP_NO,''),"
        << " isnull(r.CONF,''),isnull(r.CHK_FLAG,''),cast(isnull(r.ZYMZ_PRINT,0) as varchar(20)),"
        << " cast(isnull(r.ZZJ_PRINT,0) as varchar(20)),isnull(r.REG_NO,'')"
        << " FROM LS_AS_REPORT r"
        << " LEFT JOIN LS_AS_PATTYPE p ON r.TYPE = p.TYPE AND p.DELETE_BIT=0"
        << " LEFT JOIN LS_AS_SEX sx ON sx.SEX_CODE = r.SEX"
        << " LEFT JOIN JC_EMPLOYEE_PROPERTY emp_oper ON emp_oper.EMPLOYEE_ID = r.OPER_CODE"
        << " LEFT JOIN JC_EMPLOYEE_PROPERTY emp_rep ON emp_rep.EMPLOYEE_ID = r.REP_OPER"
        << " WHERE r.DELETE_BIT=0";

    add_eq(sql, "r.REG_NO", filters.patient_id);
    add_eq(sql, "r.TXM_NO", filters.barcode);
    add_like(sql, "r.NAME", filters.patient_name);
    if (!trim(filters.patient_no).empty()) {
        sql << " AND EXISTS (SELECT 1 FROM LS_AS_BARCODE b"
            << " WHERE isnull(b.DELETE_BIT,0)=0"
            << " AND b.REG_NO='" << sql_escape(trim(filters.patient_no)) << "'"
            << " AND b.BARCODE = r.TXM_NO)";
    }
    add_eq(sql, "r.OPER_NO", filters.oper_no);
    add_eq(sql, "r.ROOM_CODE", filters.room_code);
    add_eq(sql, "r.TYPE", filters.patient_type);
    add_report_status(sql, filters.report_status);
    add_eq(sql, "r.MACH_CODE", filters.mach_code);
    add_eq(sql, "r.GROUP_CODE", filters.group_code);
    if (!trim(filters.start_date).empty()) {
        sql << " AND r.CHK_DATE >= '" << sql_escape(trim(filters.start_date)) << "'";
    }
    if (!trim(filters.end_date).empty()) {
        sql << " AND r.CHK_DATE < DATEADD(day,1,'" << sql_escape(trim(filters.end_date)) << "')";
    }
    if (!trim(filters.item_code).empty()) {
        sql << " AND EXISTS (SELECT 1 FROM LS_AS_REPENTRY e WHERE e.REP_NO=r.REP_NO"
            << " AND e.DELETE_BIT=0 AND e.ITEM_CODE=" << sql_escape(trim(filters.item_code)) << ")";
    }
    sql << " ORDER BY r.CHK_DATE DESC, r.REP_NO DESC";

    if (log) {
        log("exec sql: " + sql.str() + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) {
        disconnect(db);
        return false;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        ReportRow row;
        row.rep_no = fetch_column(stmt, 1);
        row.oper_no = fetch_column(stmt, 2);
        row.name = fetch_column(stmt, 3);
        row.txm_no = fetch_column(stmt, 4);
        row.chk_date = fetch_column(stmt, 5);
        row.sex = fetch_column(stmt, 6);
        row.age = fetch_column(stmt, 7);
        row.bed_code = fetch_column(stmt, 8);
        row.patient_type = fetch_column(stmt, 9);
        row.requester = fetch_column(stmt, 10);
        row.reviewer = fetch_column(stmt, 11);
        row.group_name = fetch_column(stmt, 12);
        row.conf = fetch_column(stmt, 13);
        row.chk_flag = fetch_column(stmt, 14);
        row.zymz_print = fetch_column(stmt, 15);
        row.zzj_print = fetch_column(stmt, 16);
        row.reg_no = fetch_column(stmt, 17);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    disconnect(db);
    error.clear();
    return true;
#endif
}

bool query_results(const std::string& connection_string, const std::string& rep_no, std::vector<ResultRow>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)connection_string;
    (void)rep_no;
    (void)log;
    error = "query_results is only available on Windows";
    return false;
#else
    DbContext db;
    if (!connect(connection_string, db, error, log)) {
        return false;
    }

    std::ostringstream sql;
    sql << "SELECT isnull(i.ITEM_NAME,e.ITEM_NAME),isnull(e.RESULT,''),isnull(e.UPBOUND,''),"
        << " isnull(e.DOWNBOUND,''),isnull(RTRIM(i.UNIT),''),isnull(i.ENG_NAME,''),"
        << " isnull(e.NORMAL,''),CAST(e.ITEM_CODE AS varchar(20))"
        << " FROM LS_AS_REPENTRY e"
        << " LEFT JOIN LS_AS_ITEM i ON e.ITEM_CODE = i.ITEM_CODE AND i.DELETE_BIT=0"
        << " WHERE e.DELETE_BIT=0 AND e.REP_NO=" << sql_escape(rep_no)
        << " ORDER BY e.ITEM_CODE ASC";

    if (log) {
        log("exec sql: " + sql.str() + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) {
        disconnect(db);
        return false;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        ResultRow row;
        row.item_name = fetch_column(stmt, 1);
        row.result = fetch_column(stmt, 2);
        row.downbound = fetch_column(stmt, 3);
        row.upbound = fetch_column(stmt, 4);
        row.unit = fetch_column(stmt, 5);
        row.item_eng = fetch_column(stmt, 6);
        row.normal = fetch_column(stmt, 7);
        row.item_code = fetch_column(stmt, 8);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    disconnect(db);
    error.clear();
    return true;
#endif
}

}  // namespace search
