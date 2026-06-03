#include "search_core.h"
#include "search_text.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <utility>

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

bool contains_text(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

void add_count(int& screening_count, int& positive_count, bool is_positive) {
    ++screening_count;
    if (is_positive) {
        ++positive_count;
    }
}

bool is_hiv_sti_clinic_dept(const std::string& dept_name) {
    return contains_text(dept_name, "皮肤科门诊");
}

bool is_hiv_other_visit_dept(const std::string& dept_name) {
    const std::string normalized = trim(dept_name);
    return normalized.empty() || normalized == "0" ||
           contains_text(normalized, "体检") ||
           contains_text(normalized, "儿童保健") ||
           contains_text(normalized, "健康管理") ||
           contains_text(normalized, "GCP");
}

bool is_hiv_prenatal_dept(const std::string& dept_name) {
    return contains_text(dept_name, "产科门诊") ||
           contains_text(dept_name, "早孕关爱门诊");
}

std::string sql_item_code_list(const std::string& text, const char* fallback) {
    const std::string source = trim(text).empty() ? fallback : text;
    std::vector<std::string> codes;
    std::set<std::string> seen;
    std::string token;
    auto flush = [&]() {
        token = trim(token);
        if (!token.empty() &&
            std::all_of(token.begin(), token.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; }) &&
            seen.insert(token).second) {
            codes.push_back(token);
        }
        token.clear();
    };

    for (unsigned char ch : source) {
        if (std::isdigit(ch)) {
            token.push_back(static_cast<char>(ch));
            continue;
        }
        flush();
    }
    flush();

    if (codes.empty() && source != fallback) {
        return sql_item_code_list(fallback, fallback);
    }

    std::ostringstream out;
    for (size_t i = 0; i < codes.size(); ++i) {
        if (i > 0) out << ",";
        out << codes[i];
    }
    return out.str();
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

std::string collect_diag(SQLSMALLINT handle_type, SQLHANDLE handle);

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

bool fetch_binary_column(SQLHSTMT stmt, SQLUSMALLINT col, std::vector<unsigned char>& out, std::string& error) {
    out.clear();
    unsigned char buffer[8192]{};
    while (true) {
        SQLLEN indicator = 0;
        const SQLRETURN rc = SQLGetData(stmt, col, SQL_C_BINARY, buffer, sizeof(buffer), &indicator);
        if (rc == SQL_NO_DATA) {
            return true;
        }
        if (indicator == SQL_NULL_DATA) {
            out.clear();
            return true;
        }
        if (!(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO)) {
            error = "SQLGetData binary failed: " + collect_diag(SQL_HANDLE_STMT, stmt);
            out.clear();
            return false;
        }

        SQLLEN bytes_read = 0;
        if (indicator == SQL_NO_TOTAL) {
            bytes_read = rc == SQL_SUCCESS_WITH_INFO ? static_cast<SQLLEN>(sizeof(buffer)) : 0;
        } else if (rc == SQL_SUCCESS_WITH_INFO && indicator > static_cast<SQLLEN>(sizeof(buffer))) {
            bytes_read = static_cast<SQLLEN>(sizeof(buffer));
        } else {
            bytes_read = std::min<SQLLEN>(indicator, static_cast<SQLLEN>(sizeof(buffer)));
        }
        if (bytes_read > 0) {
            out.insert(out.end(), buffer, buffer + bytes_read);
        }
        if (rc == SQL_SUCCESS) {
            return true;
        }
    }
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

void add_lis_patient_filters(std::ostringstream& sql, const QueryFilters& filters, const char* report_alias) {
    if (!trim(filters.patient_name).empty()) {
        sql << " AND " << report_alias << ".NAME LIKE '%" << sql_escape(trim(filters.patient_name)) << "%'";
    }
    if (!trim(filters.patient_no).empty()) {
        sql << " AND EXISTS (SELECT 1 FROM LS_AS_BARCODE b WITH (NOLOCK)"
            << " WHERE isnull(b.DELETE_BIT,0)=0"
            << " AND b.REG_NO='" << sql_escape(trim(filters.patient_no)) << "'"
            << " AND b.BARCODE = " << report_alias << ".TXM_NO)";
    }
    if (!trim(filters.start_date).empty()) {
        sql << " AND " << report_alias << ".CHK_DATE >= '" << sql_escape(trim(filters.start_date)) << "'";
    }
    if (!trim(filters.end_date).empty()) {
        sql << " AND " << report_alias << ".CHK_DATE < DATEADD(day,1,'" << sql_escape(trim(filters.end_date)) << "')";
    }
}

void add_blood_apply_status(std::ostringstream& sql, const std::string& value) {
    const auto status = trim(value);
    if (status.empty() || status == "全部") {
        return;
    }

    sql << " AND LTRIM(RTRIM(a.ApplyForm_Statue))='" << sql_escape(status) << "'";
}

void add_barcode_machine_status(std::ostringstream& sql, const std::string& value) {
    const auto status = trim(value);
    if (status.empty() || status == "全部") {
        return;
    }
    if (status == "已签收未上机") {
        sql << " AND b.OPER_STATE=0";
    } else if (status == "已上机未审核") {
        sql << " AND b.OPER_STATE=1";
    } else if (status == "审核完成") {
        sql << " AND b.OPER_STATE=2";
    } else if (status == "已审核未发送") {
        sql << " AND 1=0";
    } else if (status == "发送完成") {
        sql << " AND b.OPER_STATE=3";
    }
}

const char* barcode_machine_status_sql() {
    return "CASE b.OPER_STATE"
           " WHEN 0 THEN '未上机'"
           " WHEN 1 THEN '已上机'"
           " WHEN 2 THEN '审核完成'"
           " WHEN 3 THEN '发送完成'"
           " ELSE '' END";
}

void fill_if_empty(std::string& target, const std::string& value) {
    if (trim(target).empty() && !trim(value).empty()) {
        target = value;
    }
}

bool exec_optional_query(SQLHDBC dbc, const std::string& sql, SQLHSTMT& stmt, LogFn log) {
    std::string ignored_error;
    if (log) log("exec optional sql: " + sql + "\n");
    return exec_query(dbc, sql, stmt, ignored_error);
}

std::string specimen_order_key(const SpecimenOrderRow& row) {
    return trim(row.barcode) + "\n" + trim(row.room_code) + "\n" + trim(row.order_text) + "\n" +
           trim(row.sample_name) + "\n" + trim(row.fee) + "\n" +
           trim(row.request_time);
}

void add_unique_order(std::vector<SpecimenOrderRow>& rows, const SpecimenOrderRow& row) {
    const auto key = specimen_order_key(row);
    for (const auto& existing : rows) {
        if (specimen_order_key(existing) == key) {
            return;
        }
    }
    rows.push_back(row);
}

bool should_add_supplemental_orders(const SpecimenBarcodeResult& result) {
    return !result.has_barcode_rows || result.orders.empty();
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
    sql << "CAST(r.ID AS varchar(20)),CAST(r.REP_NO AS varchar(20)),"
        << " isnull(r.OPER_NO,''),isnull(r.NAME,''),isnull(r.TXM_NO,''),"
        << " isnull(CONVERT(varchar(19),r.REP_DATE,120),''),isnull(RTRIM(sx.SEX_NAME),''),isnull(r.AGE,''),"
        << " isnull(r.BED_CODE,''),isnull(RTRIM(p.TYPE_NAME),''),isnull(RTRIM(emp_oper.NAME),''),"
        << " isnull(RTRIM(emp_rep.NAME),''),isnull(r.GROUP_NO,''),"
        << " isnull(r.CONF,''),isnull(r.CHK_FLAG,''),cast(isnull(r.ZYMZ_PRINT,0) as varchar(20)),"
        << " cast(isnull(r.ZZJ_PRINT,0) as varchar(20)),isnull(r.REG_NO,''),"
        << " isnull(LTRIM(RTRIM(bar.DEPT_NAME)),''),isnull(ord.ORDER_TEXT,''),"
        << " isnull(LTRIM(RTRIM(samp.SAMP_NAME)),''),isnull(r.NOTE,''),"
        << " isnull(cast(r.OPER_CODE as varchar(20)),''),isnull(CONVERT(varchar(19),bar.IN_DATE,120),''),"
        << " isnull(CONVERT(varchar(19),r.CHK_DATE,120),''),isnull(CONVERT(varchar(19),r.REP_TIME,120),''),"
        << " isnull(cast(r.FY as varchar(32)),''),"
        << " isnull(NULLIF(LTRIM(RTRIM(emp_dean.NAME)),''),isnull(cast(r.DEAN_OPER as varchar(20)),'')),"
        << " isnull(LTRIM(RTRIM(emp_req.NAME)),''),"
        << " isnull(r.DIAG_NAME,''),isnull(r.CREATE_TIME,''),isnull(r.PAT_PHONE,''),"
        << " isnull(cast(r.assaypat_type as varchar(20)),''),"
        << " isnull(cast(bar.JZ_FLAG as varchar(20)),'')"
        << " FROM LS_AS_REPORT r"
        << " LEFT JOIN LS_AS_PATTYPE p ON r.TYPE = p.TYPE AND p.DELETE_BIT=0"
        << " LEFT JOIN LS_AS_SEX sx ON sx.SEX_CODE = r.SEX"
        << " LEFT JOIN LS_AS_SAMPLE samp ON samp.SAMP_CODE=r.SAMP_CODE AND samp.DELETE_BIT=0"
        << " LEFT JOIN JC_EMPLOYEE_PROPERTY emp_oper ON emp_oper.EMPLOYEE_ID = r.OPER_CODE"
        << " LEFT JOIN JC_EMPLOYEE_PROPERTY emp_rep ON emp_rep.EMPLOYEE_ID = r.REP_OPER"
        << " LEFT JOIN JC_EMPLOYEE_PROPERTY emp_dean ON emp_dean.EMPLOYEE_ID = r.DEAN_OPER"
        << " LEFT JOIN JC_EMPLOYEE_PROPERTY emp_req ON emp_req.EMPLOYEE_ID = r.REQ_DR"
        << " OUTER APPLY (SELECT TOP 1 b.DEPT_NAME,b.IN_DATE,b.JZ_FLAG FROM LS_AS_BARCODE b WITH (NOLOCK)"
        << " WHERE isnull(b.DELETE_BIT,0)=0 AND b.BARCODE=r.TXM_NO ORDER BY b.ID DESC) bar"
        << " OUTER APPLY (SELECT STUFF(("
        << " SELECT '/' + LTRIM(RTRIM(b2.ORDER_TEXT))"
        << " FROM LS_AS_BARCODE b2 WITH (NOLOCK)"
        << " WHERE isnull(b2.DELETE_BIT,0)=0 AND b2.BARCODE=r.TXM_NO"
        << " AND NULLIF(LTRIM(RTRIM(b2.ORDER_TEXT)),'') IS NOT NULL"
        << " ORDER BY b2.ID"
        << " FOR XML PATH(''),TYPE).value('.','varchar(max)'),1,1,'') AS ORDER_TEXT) ord"
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
        row.id = fetch_column(stmt, 1);
        row.rep_no = fetch_column(stmt, 2);
        row.oper_no = fetch_column(stmt, 3);
        row.name = fetch_column(stmt, 4);
        row.txm_no = fetch_column(stmt, 5);
        row.chk_date = fetch_column(stmt, 6);
        row.sex = fetch_column(stmt, 7);
        row.age = fetch_column(stmt, 8);
        row.bed_code = fetch_column(stmt, 9);
        row.patient_type = fetch_column(stmt, 10);
        row.requester = fetch_column(stmt, 11);
        row.reviewer = fetch_column(stmt, 12);
        row.group_name = fetch_column(stmt, 13);
        row.conf = fetch_column(stmt, 14);
        row.chk_flag = fetch_column(stmt, 15);
        row.zymz_print = fetch_column(stmt, 16);
        row.zzj_print = fetch_column(stmt, 17);
        row.reg_no = fetch_column(stmt, 18);
        row.dept_name = fetch_column(stmt, 19);
        row.order_text = fetch_column(stmt, 20);
        row.sample_name = fetch_column(stmt, 21);
        row.note = fetch_column(stmt, 22);
        row.oper_code = fetch_column(stmt, 23);
        row.collection_time = fetch_column(stmt, 24);
        row.inspect_date = fetch_column(stmt, 25);
        row.rep_time = fetch_column(stmt, 26);
        row.fee = fetch_column(stmt, 27);
        row.dean_oper = fetch_column(stmt, 28);
        row.req_doctor = fetch_column(stmt, 29);
        row.diag_name = fetch_column(stmt, 30);
        row.create_time = fetch_column(stmt, 31);
        row.patient_phone = fetch_column(stmt, 32);
        row.report_type = fetch_column(stmt, 33);
        row.barcode_jz_flag = fetch_column(stmt, 34);
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
    sql << "SELECT isnull(lm.GROUP_NAME,''),isnull(i.ITEM_NAME,e.ITEM_NAME),isnull(e.RESULT,''),isnull(e.UPBOUND,''),"
        << " isnull(e.DOWNBOUND,''),isnull(RTRIM(i.UNIT),''),isnull(i.ENG_NAME,''),"
        << " isnull(e.NORMAL,''),CAST(e.ITEM_CODE AS varchar(20)),"
        << " isnull(CAST(e.NORMAL_WJ AS varchar(20)),''),"
        << " isnull(scope.UPBOUND1,''),isnull(scope.DNBOUND1,'')"
        << " FROM LS_AS_REPENTRY e"
        << " LEFT JOIN LS_AS_ITEM i ON e.ITEM_CODE = i.ITEM_CODE AND i.DELETE_BIT=0"
        << " OUTER APPLY ("
        << " SELECT TOP 1 m.GROUP_NAME"
        << " FROM LS_AS_LABMATCH m"
        << " WHERE m.GROUP_CODE=e.GROUP_CODE"
        << " AND NULLIF(LTRIM(RTRIM(isnull(m.GROUP_NAME,''))),'') IS NOT NULL"
        << " ORDER BY CASE WHEN isnull(m.DELETE_BIT,0)=0 AND isnull(m.USE_FLAG,0)=0 THEN 0 ELSE 1 END,m.ID"
        << " ) lm"
        << " OUTER APPLY ("
        << " SELECT TOP 1 LTRIM(RTRIM(isnull(sx.SEX_NAME,''))) AS SEX_NAME"
        << " FROM LS_AS_REPORT rr WITH (NOLOCK)"
        << " LEFT JOIN LS_AS_SEX sx WITH (NOLOCK) ON sx.SEX_CODE=rr.SEX"
        << " WHERE rr.REP_NO=e.REP_NO ORDER BY rr.ID"
        << " ) report_sex"
        << " OUTER APPLY ("
        << " SELECT TOP 1 s.UPBOUND1,s.DNBOUND1"
        << " FROM LS_AS_DEF_ITEMSCOPE s WITH (NOLOCK)"
        << " WHERE s.ITEM_CODE=e.ITEM_CODE"
        << " AND (LTRIM(RTRIM(isnull(s.SEX,'')))='通用'"
        << " OR (NULLIF(report_sex.SEX_NAME,'') IS NOT NULL"
        << " AND LTRIM(RTRIM(isnull(s.SEX,'')))=report_sex.SEX_NAME))"
        << " ORDER BY CASE"
        << " WHEN NULLIF(report_sex.SEX_NAME,'') IS NOT NULL"
        << " AND LTRIM(RTRIM(isnull(s.SEX,'')))=report_sex.SEX_NAME THEN 0"
        << " WHEN LTRIM(RTRIM(isnull(s.SEX,'')))='通用' THEN 1"
        << " ELSE 2 END,"
        << " CASE WHEN isnull(s.DEF_FLAG,'')='1' THEN 0 ELSE 1 END,s.ID"
        << " ) scope"
        << " WHERE e.DELETE_BIT=0 AND e.REP_NO=" << sql_escape(rep_no)
        << " ORDER BY e.GROUP_CODE ASC,e.ITEM_CODE ASC,e.ID ASC";

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
        row.group_name = fetch_column(stmt, 1);
        row.item_name = fetch_column(stmt, 2);
        row.result = fetch_column(stmt, 3);
        row.downbound = fetch_column(stmt, 4);
        row.upbound = fetch_column(stmt, 5);
        row.unit = fetch_column(stmt, 6);
        row.item_eng = fetch_column(stmt, 7);
        row.normal = fetch_column(stmt, 8);
        row.item_code = fetch_column(stmt, 9);
        row.normal_wj = fetch_column(stmt, 10);
        row.critical_low_bound = fetch_column(stmt, 11);
        row.critical_high_bound = fetch_column(stmt, 12);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    disconnect(db);
    error.clear();
    return true;
#endif
}

bool query_report_picture(const std::string& connection_string, const std::string& rep_no,
                          std::vector<unsigned char>& picture, std::string& error, LogFn log) {
    picture.clear();
#ifndef _WIN32
    (void)connection_string;
    (void)rep_no;
    (void)log;
    error = "query_report_picture is only available on Windows";
    return false;
#else
    const std::string trimmed_rep_no = trim(rep_no);
    if (trimmed_rep_no.empty()) {
        error.clear();
        return true;
    }

    DbContext db;
    if (!connect(connection_string, db, error, log)) {
        return false;
    }

    std::ostringstream sql;
    sql << "SELECT TOP 1 PICTURE"
        << " FROM LS_AS_ITEMPICTURE WITH (NOLOCK)"
        << " WHERE isnull(DELETE_BIT,0)=0"
        << " AND REP_NO='" << sql_escape(trimmed_rep_no) << "'"
        << " AND PICTURE IS NOT NULL"
        << " AND DATALENGTH(PICTURE)>0"
        << " ORDER BY PIC_NO,ID";

    if (log) {
        log("exec sql: " + sql.str() + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) {
        disconnect(db);
        return false;
    }

    if (SQLFetch(stmt) == SQL_SUCCESS) {
        if (!fetch_binary_column(stmt, 1, picture, error)) {
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
            disconnect(db);
            return false;
        }
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    disconnect(db);
    error.clear();
    return true;
#endif
}

bool query_lis_summary(const QueryFilters& filters, LisSummary& summary, std::string& error, LogFn log) {
    summary = {};
#ifndef _WIN32
    (void)filters;
    (void)log;
    error = "query_lis_summary is only available on Windows";
    return false;
#else
    DbContext db;
    if (!connect(filters.connection_string, db, error, log)) {
        return false;
    }

    auto exec_one = [&](const std::ostringstream& sql, std::vector<std::string>& cols) -> bool {
        if (log) {
            log("exec sql: " + sql.str() + "\n");
        }
        SQLHSTMT stmt = SQL_NULL_HSTMT;
        if (!exec_query(db.dbc, sql.str(), stmt, error)) {
            return false;
        }
        if (SQLFetch(stmt) == SQL_SUCCESS) {
            for (SQLUSMALLINT i = 1; i <= cols.size(); ++i) {
                cols[static_cast<size_t>(i - 1)] = fetch_column(stmt, i);
            }
        }
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return true;
    };

    const std::string abo_codes = sql_item_code_list(filters.lis_abo_codes, "91962;11101;91963;11102");
    const std::string rhd_codes = sql_item_code_list(filters.lis_rhd_codes, "91964;11103");
    const std::string hgb_codes = sql_item_code_list(filters.lis_hgb_codes, "91672;90891;1013;92563;90943;89786");
    const std::string plt_codes = sql_item_code_list(filters.lis_plt_codes, "91678;90897;1019;92569;90949");
    const std::string blood_codes = abo_codes + "," + rhd_codes;
    const std::string cbc_codes = hgb_codes + "," + plt_codes;

    std::ostringstream blood_sql;
    blood_sql
        << "SELECT TOP 1"
        << " isnull(MAX(CASE WHEN e.ITEM_CODE IN (" << abo_codes << ") THEN nullif(LTRIM(RTRIM(e.RESULT)),'') END),''),"
        << " isnull(MAX(CASE WHEN e.ITEM_CODE IN (" << rhd_codes << ") THEN nullif(LTRIM(RTRIM(e.RESULT)),'') END),''),"
        << " isnull(CONVERT(varchar(10),r.CHK_DATE,120),'')"
        << " FROM LS_AS_REPORT r WITH (NOLOCK)"
        << " INNER JOIN LS_AS_REPENTRY e WITH (NOLOCK) ON e.REP_NO=r.REP_NO AND isnull(e.DELETE_BIT,0)=0"
        << " WHERE isnull(r.DELETE_BIT,0)=0"
        << " AND e.ITEM_CODE IN (" << blood_codes << ")";
    add_lis_patient_filters(blood_sql, filters, "r");
    blood_sql
        << " GROUP BY r.REP_NO,r.CHK_DATE"
        << " HAVING MAX(CASE WHEN e.ITEM_CODE IN (" << abo_codes << ")"
        << " AND nullif(LTRIM(RTRIM(e.RESULT)),'') IS NOT NULL THEN 1 ELSE 0 END)=1"
        << " AND MAX(CASE WHEN e.ITEM_CODE IN (" << rhd_codes << ")"
        << " AND nullif(LTRIM(RTRIM(e.RESULT)),'') IS NOT NULL THEN 1 ELSE 0 END)=1"
        << " ORDER BY r.CHK_DATE DESC,r.REP_NO DESC";

    std::vector<std::string> blood_cols(3);
    if (!exec_one(blood_sql, blood_cols)) {
        disconnect(db);
        return false;
    }
    summary.abo = blood_cols[0];
    summary.rhd = blood_cols[1];
    summary.blood_type_date = blood_cols[2];

    std::ostringstream cbc_sql;
    cbc_sql
        << "SELECT TOP 1"
        << " isnull(MAX(CASE WHEN e.ITEM_CODE IN (" << hgb_codes << ") THEN nullif(LTRIM(RTRIM(e.RESULT)),'') END),''),"
        << " isnull(MAX(CASE WHEN e.ITEM_CODE IN (" << plt_codes << ") THEN nullif(LTRIM(RTRIM(e.RESULT)),'') END),''),"
        << " isnull(CONVERT(varchar(10),r.CHK_DATE,120),'')"
        << " FROM LS_AS_REPORT r WITH (NOLOCK)"
        << " INNER JOIN LS_AS_REPENTRY e WITH (NOLOCK) ON e.REP_NO=r.REP_NO AND isnull(e.DELETE_BIT,0)=0"
        << " WHERE isnull(r.DELETE_BIT,0)=0"
        << " AND e.ITEM_CODE IN (" << cbc_codes << ")";
    add_lis_patient_filters(cbc_sql, filters, "r");
    cbc_sql
        << " GROUP BY r.REP_NO,r.CHK_DATE"
        << " HAVING MAX(CASE WHEN e.ITEM_CODE IN (" << hgb_codes << ")"
        << " AND nullif(LTRIM(RTRIM(e.RESULT)),'') IS NOT NULL THEN 1 ELSE 0 END)=1"
        << " AND MAX(CASE WHEN e.ITEM_CODE IN (" << plt_codes << ")"
        << " AND nullif(LTRIM(RTRIM(e.RESULT)),'') IS NOT NULL THEN 1 ELSE 0 END)=1"
        << " ORDER BY r.CHK_DATE DESC,r.REP_NO DESC";

    std::vector<std::string> cbc_cols(3);
    if (!exec_one(cbc_sql, cbc_cols)) {
        disconnect(db);
        return false;
    }
    summary.hgb = cbc_cols[0];
    summary.plt = cbc_cols[1];
    summary.cbc_date = cbc_cols[2];

    disconnect(db);
    error.clear();
    return true;
#endif
}

bool query_blood_requests(const BloodQueryFilters& filters, std::vector<BloodRequestRow>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)filters; (void)log;
    error = "query_blood_requests is only available on Windows";
    return false;
#else
    DbContext db;
    if (!connect(filters.connection_string, db, error, log)) return false;

    std::ostringstream sql;
    sql << "SELECT ";
    if (filters.limit > 0) {
        sql << "TOP " << filters.limit << " ";
    }
    sql << "isnull(LTRIM(RTRIM(a.TranProperty)),''),"
        << "isnull(LTRIM(RTRIM(a.Patient_Name)),''),"
        << "isnull(LTRIM(RTRIM(a.Apply_Dept)),''),isnull(LTRIM(RTRIM(a.Apply_BedNo)),''),"
        << "isnull(LTRIM(RTRIM(a.BloodType)),''),isnull(LTRIM(RTRIM(a.RHD)),''),"
        << "isnull(("
        << "SELECT "
        << "isnull(LTRIM(RTRIM(s.ApplyComposition)),'') + "
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(32),s.ApplyNum))),'') + "
        << "isnull(LTRIM(RTRIM(s.ApplyUnit)),'') + ';'"
        << " FROM LS_XK_BloodRequestApplySon s"
        << " WHERE s.ApplyFormNO=a.ApplyFormNO"
        << " ORDER BY s.ID"
        << " FOR XML PATH(''),TYPE).value('.','varchar(max)'),'')"
        << ",isnull(LTRIM(RTRIM(a.Patient_NO)),''),isnull(LTRIM(RTRIM(a.ApplyFormNO)),''),"
        << "isnull(LTRIM(RTRIM(a.Check_Doctor)),''),isnull(CONVERT(varchar(19),a.Check_Date,120),''),"
        << "isnull(LTRIM(RTRIM(a.ApplyForm_Statue)),''),isnull(CONVERT(varchar(19),a.Apply_Time,120),''),"
        << "isnull(LTRIM(RTRIM(a.UrgencyLevel)),''),isnull(LTRIM(RTRIM(a.reactionHistory)),''),"
        << "isnull(LTRIM(RTRIM(a.Patient_NOType)),''),isnull(LTRIM(RTRIM(a.Patient_Sex)),''),"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(20),a.Patient_Age))),'') + isnull(LTRIM(RTRIM(a.Patient_AgeUnit)),''),"
        << "isnull(LTRIM(RTRIM(a.FYS)),'')"
        << " FROM LS_XK_BloodRequestApply a"
        << " WHERE a.Delete_Bit=0";

    if (!trim(filters.patient_no).empty()) {
        sql << " AND a.Patient_NO='" << sql_escape(trim(filters.patient_no)) << "'";
    }
    if (!trim(filters.patient_name).empty()) {
        sql << " AND a.Patient_Name LIKE '%" << sql_escape(trim(filters.patient_name)) << "%'";
    }
    if (!trim(filters.apply_form_no).empty()) {
        sql << " AND a.ApplyFormNO='" << sql_escape(trim(filters.apply_form_no)) << "'";
    }
    add_blood_apply_status(sql, filters.apply_status);
    if (!trim(filters.start_date).empty()) {
        sql << " AND a.Apply_Time >= '" << sql_escape(trim(filters.start_date)) << "'";
    }
    if (!trim(filters.end_date).empty()) {
        sql << " AND a.Apply_Time < DATEADD(day,1,'" << sql_escape(trim(filters.end_date)) << "')";
    }

    sql << " ORDER BY a.Apply_Time DESC";

    if (log) log("exec sql: " + sql.str() + "\n");

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) { disconnect(db); return false; }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        BloodRequestRow row;
        row.tran_property     = fetch_column(stmt, 1);
        row.patient_name      = fetch_column(stmt, 2);
        row.apply_dept        = fetch_column(stmt, 3);
        row.apply_bed_no      = fetch_column(stmt, 4);
        row.apply_abo         = fetch_column(stmt, 5);
        row.apply_rhd         = fetch_column(stmt, 6);
        row.apply_composition = fetch_column(stmt, 7);
        row.patient_no        = fetch_column(stmt, 8);
        row.apply_form_no     = fetch_column(stmt, 9);
        row.check_doctor      = fetch_column(stmt, 10);
        row.check_date        = fetch_column(stmt, 11);
        row.apply_status      = fetch_column(stmt, 12);
        row.apply_time        = fetch_column(stmt, 13);
        row.urgency_level     = fetch_column(stmt, 14);
        row.transfusion_history = fetch_column(stmt, 15);
        row.patient_no_type   = fetch_column(stmt, 16);
        row.patient_sex       = fetch_column(stmt, 17);
        row.patient_age       = fetch_column(stmt, 18);
        row.reaction_history  = fetch_column(stmt, 19);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    disconnect(db);
    error.clear();
    return true;
#endif
}

bool query_barcodes(const BarcodeQueryFilters& filters, std::vector<BarcodeQueryRow>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)filters; (void)log;
    error = "query_barcodes is only available on Windows";
    return false;
#else
    DbContext db;
    if (!connect(filters.connection_string, db, error, log)) return false;

    const std::string date_col =
        trim(filters.date_field) == "Receive" ? "b.IN_DATE" :
        trim(filters.date_field) == "Machine" ? "b.IN_DATE" :
        "b.REQ_TIME";

    std::ostringstream where;
    where << " WHERE b.CANCEL_DATE IS " << (filters.canceled ? "NOT NULL" : "NULL");

    if (!trim(filters.start_date).empty()) {
        where << " AND " << date_col << " >= '" << sql_escape(trim(filters.start_date)) << "'";
    }
    if (!trim(filters.end_date).empty()) {
        where << " AND " << date_col << " < DATEADD(day,1,'" << sql_escape(trim(filters.end_date)) << "')";
    }
    add_like(where, "b.BARCODE", filters.barcode);
    add_like(where, "b.NAME", filters.patient_name);
    add_like(where, "b.REG_NO", filters.reg_no);
    add_eq(where, "CONVERT(varchar(20),b.ROOM_CODE)", filters.room_code);

    add_barcode_machine_status(where, filters.machine_status);

    const auto sort = trim(filters.sort_order);
    std::string order_expr;
    if (sort == "request") {
        order_expr = "b.REQ_TIME DESC,b.ID DESC";
    } else if (sort == "barcode") {
        order_expr = "b.BARCODE DESC,b.ID DESC";
    } else if (sort == "receive_desc") {
        order_expr = "b.IN_DATE DESC,b.ID DESC";
    } else {
        order_expr = "b.IN_DATE ASC,b.ID ASC";
    }

    std::ostringstream sql;
    sql << "SELECT "
        << "isnull((SELECT TOP 1 LTRIM(RTRIM(r.OPER_NO))"
        << " FROM LS_AS_REPORT r WITH (NOLOCK)"
        << " WHERE isnull(r.DELETE_BIT,0)=0"
        << " AND r.TXM_NO=b.BARCODE"
        << " AND nullif(LTRIM(RTRIM(r.OPER_NO)),'') IS NOT NULL"
        << " ORDER BY r.CHK_DATE DESC,r.REP_NO DESC),'')"
        << ",isnull(CONVERT(varchar(10),b.JZ_FLAG),'') AS emergency,"
        << "isnull(LTRIM(RTRIM(b.BARCODE)),'') AS barcode,"
        << "isnull(LTRIM(RTRIM(b.REG_NO)),'') AS reg_no,"
        << "isnull(LTRIM(RTRIM(b.TYPENAME)),'') AS type_name,"
        << "isnull(LTRIM(RTRIM(b.NAME)),'') AS patient_name,"
        << "isnull(LTRIM(RTRIM(b.SEX)),'') AS sex,"
        << "isnull(LTRIM(RTRIM(b.DEPT_NAME)),'') AS dept_name,"
        << "isnull(LTRIM(RTRIM(b.BEDNO)),'') AS bed_no,"
        << "isnull(LTRIM(RTRIM(b.OPER_CODE)),'') AS receiver,"
        << "isnull(CONVERT(varchar(19),b.IN_DATE,120),'') AS receive_time,"
        << "isnull(LTRIM(RTRIM(b.ORDER_TEXT)),'') AS order_text,"
        << "isnull(LTRIM(RTRIM(b.SAMP_NAME)),'') AS sample_name,"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(32),b.FY))),'') AS fee,"
        << "isnull(LTRIM(RTRIM(b.REQ_DRN)),'') AS request_doctor,"
        << "isnull(CONVERT(varchar(10),b.ZT_FLAG),'') AS status,"
        << "isnull(LTRIM(RTRIM(b.NOTE)),'') AS note,"
        << "isnull(LTRIM(RTRIM(b.REASON)),'') AS reason,"
        << "isnull(LTRIM(RTRIM(b.sjyq_qsr)),'') AS submitter,"
        << "isnull(nullif(LTRIM(RTRIM(b.COLLECTION_TIME)),''),isnull(CONVERT(varchar(19),b.SUB_DATE,120),'')) AS submit_time,"
        << "isnull(CONVERT(varchar(19),b.REQ_TIME,120),'') AS request_time,"
        << "isnull(CONVERT(varchar(19),b.CANCEL_DATE,120),'') AS cancel_time,"
        << "isnull(LTRIM(RTRIM(b.CANCEL_OPER)),'') AS cancel_operator,"
        << "isnull(CONVERT(varchar(30),b.HZID),'') AS hzid,"
        << barcode_machine_status_sql() << " AS machine_status"
        << " FROM LS_AS_BARCODE b WITH (NOLOCK)"
        << where.str()
        << " ORDER BY " << order_expr;

    if (log) log("exec sql: " + sql.str() + "\n");

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) { disconnect(db); return false; }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        BarcodeQueryRow row;
        row.sample_no      = fetch_column(stmt, 1);
        row.emergency      = fetch_column(stmt, 2);
        row.barcode        = fetch_column(stmt, 3);
        row.reg_no         = fetch_column(stmt, 4);
        row.type_name      = fetch_column(stmt, 5);
        row.name           = fetch_column(stmt, 6);
        row.sex            = fetch_column(stmt, 7);
        row.dept_name      = fetch_column(stmt, 8);
        row.bed_no         = fetch_column(stmt, 9);
        row.receiver       = fetch_column(stmt, 10);
        row.receive_time   = fetch_column(stmt, 11);
        row.order_text     = fetch_column(stmt, 12);
        row.sample_name    = fetch_column(stmt, 13);
        row.fee            = fetch_column(stmt, 14);
        row.request_doctor = fetch_column(stmt, 15);
        row.status         = fetch_column(stmt, 16);
        row.note           = fetch_column(stmt, 17);
        row.reason         = fetch_column(stmt, 18);
        row.submitter      = fetch_column(stmt, 19);
        row.submit_time    = fetch_column(stmt, 20);
        row.request_time   = fetch_column(stmt, 21);
        row.cancel_time    = fetch_column(stmt, 22);
        row.cancel_operator = fetch_column(stmt, 23);
        row.hzid           = fetch_column(stmt, 24);
        row.machine_status = fetch_column(stmt, 25);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    disconnect(db);
    error.clear();
    return true;
#endif
}

bool query_specimen_signed_list(const SpecimenSignedListQuery& query, std::vector<SpecimenSignedListRow>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)query; (void)log;
    error = "query_specimen_signed_list is only available on Windows";
    return false;
#else
    if (!query.use_sign_time && !query.use_apply_time) {
        error = "missing date filter";
        return false;
    }

    DbContext db;
    if (!connect(query.connection_string, db, error, log)) return false;

    std::ostringstream where;
    where << " WHERE b.IN_DATE IS NOT NULL"
          << " AND (b.DELETE_BIT IS NULL OR b.DELETE_BIT=0)"
          << " AND (b.ZT_FLAG IS NULL OR b.ZT_FLAG<>9)";

    if (query.use_sign_time) {
        if (!trim(query.sign_start).empty()) {
            where << " AND b.IN_DATE >= '" << sql_escape(trim(query.sign_start)) << "'";
        }
        if (!trim(query.sign_end).empty()) {
            where << " AND b.IN_DATE < DATEADD(minute,1,'" << sql_escape(trim(query.sign_end)) << "')";
        }
    }
    if (query.use_apply_time) {
        if (!trim(query.apply_start).empty()) {
            where << " AND b.REQ_TIME >= '" << sql_escape(trim(query.apply_start)) << "'";
        }
        if (!trim(query.apply_end).empty()) {
            where << " AND b.REQ_TIME < DATEADD(minute,1,'" << sql_escape(trim(query.apply_end)) << "')";
        }
    }
    add_eq(where, "CONVERT(varchar(20),b.ROOM_CODE)", query.room_code);
    add_like(where, "b.NAME", query.patient_name);

    std::ostringstream sql;
    sql << "SELECT "
        << "isnull(LTRIM(RTRIM(b.BARCODE)),''),"
        << "isnull(LTRIM(RTRIM(b.REG_NO)),''),"
        << "isnull(LTRIM(RTRIM(b.TYPENAME)),''),"
        << "isnull(LTRIM(RTRIM(b.NAME)),''),"
        << "isnull(LTRIM(RTRIM(b.SEX)),''),"
        << "isnull(LTRIM(RTRIM(b.DEPT_NAME)),''),"
        << "isnull(LTRIM(RTRIM(b.ORDER_TEXT)),''),"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(32),b.FY))),''),"
        << "isnull(CONVERT(varchar(19),b.REQ_TIME,120),''),"
        << "isnull(nullif(LTRIM(RTRIM(b.COLLECTION_TIME)),''),isnull(CONVERT(varchar(19),b.SUB_DATE,120),'')),"
        << "isnull(CONVERT(varchar(19),b.IN_DATE,120),''),"
        << "isnull(CONVERT(varchar(19),b.SUB_DATE,120),''),"
        << "isnull(LTRIM(RTRIM(b.AGE)),''),"
        << "isnull(LTRIM(RTRIM(b.OPER_CODE)),''),"
        << "isnull(LTRIM(RTRIM(b.SAMP_NAME)),''),"
        << "isnull(nullif(LTRIM(RTRIM(room.ROOM_NAME)),''),isnull(LTRIM(RTRIM(CONVERT(varchar(20),b.ROOM_CODE))),''))"
        << " FROM LS_AS_BARCODE b WITH (NOLOCK)"
        << " LEFT JOIN LS_AS_ROOM room WITH (NOLOCK) ON b.ROOM_CODE=room.ROOM_CODE AND room.DELETE_BIT=0"
        << where.str()
        << " ORDER BY b.IN_DATE ASC,b.BARCODE ASC,b.ID ASC";

    if (log) log("exec sql: " + sql.str() + "\n");

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) { disconnect(db); return false; }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        SpecimenSignedListRow row;
        row.barcode = fetch_column(stmt, 1);
        row.reg_no = fetch_column(stmt, 2);
        row.type_name = fetch_column(stmt, 3);
        row.name = fetch_column(stmt, 4);
        row.sex = fetch_column(stmt, 5);
        row.dept_name = fetch_column(stmt, 6);
        row.order_text = fetch_column(stmt, 7);
        row.fee = fetch_column(stmt, 8);
        row.request_time = fetch_column(stmt, 9);
        row.collection_time = fetch_column(stmt, 10);
        row.signed_time = fetch_column(stmt, 11);
        row.submit_time = fetch_column(stmt, 12);
        row.age = fetch_column(stmt, 13);
        row.receiver = fetch_column(stmt, 14);
        row.sample_name = fetch_column(stmt, 15);
        row.room_code = fetch_column(stmt, 16);
        rows.push_back(std::move(row));
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    disconnect(db);
    error.clear();
    return true;
#endif
}

bool query_specimen_barcode(const SpecimenBarcodeQuery& query, SpecimenBarcodeResult& result, std::string& error, LogFn log) {
    result = SpecimenBarcodeResult{};
#ifndef _WIN32
    (void)query; (void)log;
    error = "query_specimen_barcode is only available on Windows";
    return false;
#else
    const auto barcode = trim(query.barcode);
    if (barcode.empty()) {
        error = "empty barcode";
        return false;
    }

    DbContext db;
    if (!connect(query.connection_string, db, error, log)) return false;

    result.barcode = barcode;
    const auto escaped = sql_escape(barcode);

    {
        std::ostringstream sql;
        sql << "SELECT TOP 200 "
            << "isnull(LTRIM(RTRIM(b.BARCODE)),''),"
            << "isnull(LTRIM(RTRIM(b.REG_NO)),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(20),b.TYPE))),''),"
            << "isnull(LTRIM(RTRIM(b.TYPENAME)),''),"
            << "isnull(LTRIM(RTRIM(b.NAME)),''),"
            << "isnull(LTRIM(RTRIM(b.SEX)),''),"
            << "isnull(LTRIM(RTRIM(b.AGE)),''),"
            << "isnull(LTRIM(RTRIM(b.DEPT_NAME)),''),"
            << "isnull(LTRIM(RTRIM(b.BEDNO)),''),"
            << "isnull(LTRIM(RTRIM(b.REQ_DRN)),''),"
            << "isnull(nullif(LTRIM(RTRIM(room.ROOM_NAME)),''),isnull(LTRIM(RTRIM(CONVERT(varchar(20),b.ROOM_CODE))),'')),"
            << "isnull(LTRIM(RTRIM(b.SAMP_NAME)),''),"
            << "isnull(LTRIM(RTRIM(b.ORDER_TEXT)),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(32),b.FY))),''),"
            << "isnull(CONVERT(varchar(19),b.REQ_TIME,120),''),"
            << "isnull(CONVERT(varchar(19),b.IN_DATE,120),''),"
            << "isnull(LTRIM(RTRIM(b.OPER_CODE)),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(10),b.JZ_FLAG))),''),"
            << "isnull(nullif(LTRIM(RTRIM(CONVERT(varchar(32),b.COLLECTION_TIME))),''),isnull(CONVERT(varchar(19),b.SUB_DATE,120),'')),"
            << "isnull(CONVERT(varchar(19),b.SUB_DATE,120),''),"
            << "isnull(LTRIM(RTRIM(b.NOTE)),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(10),b.OPER_STATE))),'')"
            << " FROM LS_AS_BARCODE b WITH (NOLOCK)"
            << " LEFT JOIN LS_AS_ROOM room WITH (NOLOCK) ON b.ROOM_CODE=room.ROOM_CODE AND room.DELETE_BIT=0"
            << " WHERE b.BARCODE='" << escaped << "'"
            << " AND (b.DELETE_BIT IS NULL OR b.DELETE_BIT=0)"
            << " AND (b.ZT_FLAG IS NULL OR b.ZT_FLAG<>9)"
            << " ORDER BY b.ID";
        if (log) log("exec sql: " + sql.str() + "\n");

        SQLHSTMT stmt = SQL_NULL_HSTMT;
        if (!exec_query(db.dbc, sql.str(), stmt, error)) { disconnect(db); return false; }
        while (SQLFetch(stmt) == SQL_SUCCESS) {
            const auto col_barcode = fetch_column(stmt, 1);
            const auto col_reg_no = fetch_column(stmt, 2);
            const auto col_type_code = fetch_column(stmt, 3);
            const auto col_type_name = fetch_column(stmt, 4);
            const auto col_name = fetch_column(stmt, 5);
            const auto col_sex = fetch_column(stmt, 6);
            const auto col_age = fetch_column(stmt, 7);
            const auto col_dept_name = fetch_column(stmt, 8);
            const auto col_bed_no = fetch_column(stmt, 9);
            const auto col_requester = fetch_column(stmt, 10);
            const auto col_room_code = fetch_column(stmt, 11);
            const auto col_sample_name = fetch_column(stmt, 12);
            const auto col_order_text = fetch_column(stmt, 13);
            const auto col_fee = fetch_column(stmt, 14);
            const auto col_request_time = fetch_column(stmt, 15);
            const auto col_signed_time = fetch_column(stmt, 16);
            const auto col_receiver = fetch_column(stmt, 17);
            const auto col_jz_flag = fetch_column(stmt, 18);
            const auto col_collection_time = fetch_column(stmt, 19);
            const auto col_submit_time = fetch_column(stmt, 20);
            const auto col_note = fetch_column(stmt, 21);
            const auto col_oper_state = fetch_column(stmt, 22);

            result.has_barcode_rows = true;
            fill_if_empty(result.barcode, col_barcode);
            fill_if_empty(result.reg_no, col_reg_no);
            fill_if_empty(result.type_code, col_type_code);
            fill_if_empty(result.type_name, col_type_name);
            fill_if_empty(result.name, col_name);
            fill_if_empty(result.sex, col_sex);
            fill_if_empty(result.age, col_age);
            fill_if_empty(result.dept_name, col_dept_name);
            fill_if_empty(result.bed_no, col_bed_no);
            fill_if_empty(result.requester, col_requester);
            fill_if_empty(result.room_code, col_room_code);

            SpecimenOrderRow order;
            order.barcode = col_barcode;
            order.room_code = col_room_code;
            order.sample_name = col_sample_name;
            order.order_text = col_order_text;
            order.fee = col_fee;
            order.request_time = col_request_time;
            fill_if_empty(result.fee, order.fee);
            fill_if_empty(result.signed_time, col_signed_time);
            fill_if_empty(result.receiver, col_receiver);
            fill_if_empty(result.jz_flag, col_jz_flag);
            fill_if_empty(result.collection_time, col_collection_time);
            fill_if_empty(result.submit_time, col_submit_time);
            if (trim(col_oper_state) == "0" || trim(result.oper_state).empty()) {
                result.oper_state = col_oper_state;
            }
            order.note = col_note;
            if (!trim(order.order_text).empty() || !trim(order.sample_name).empty()) {
                add_unique_order(result.orders, order);
            }
        }
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    }

    {
        std::ostringstream sql;
        sql << "SELECT TOP 20 "
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(20),r.TYPE))),''),"
            << "isnull(LTRIM(RTRIM(pt.TYPE_NAME)),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(30),r.REP_NO))),''),"
            << "isnull(LTRIM(RTRIM(r.OPER_NO)),''),"
            << "isnull(LTRIM(RTRIM(r.REG_NO)),''),"
            << "isnull(LTRIM(RTRIM(r.NAME)),''),"
            << "CASE r.SEX WHEN '1' THEN '男' WHEN '2' THEN '女' ELSE isnull(LTRIM(RTRIM(r.SEX)),'') END,"
            << "isnull(LTRIM(RTRIM(r.AGE)),''),"
            << "isnull(LTRIM(RTRIM(r.BED_CODE)),''),"
            << "isnull(LTRIM(RTRIM(dept.NAME)),''),"
            << "isnull(nullif(LTRIM(RTRIM(mach.MACH_NAME)),''),isnull(LTRIM(RTRIM(CONVERT(varchar(20),r.MACH_CODE))),'')),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(20),r.GROUP_CODE))),''),"
            << "isnull(LTRIM(RTRIM(r.CHK_FLAG)),''),"
            << "isnull(LTRIM(RTRIM(r.CONF)),''),"
            << "isnull(CONVERT(varchar(19),r.CREATE_TIME,120),'')"
            << " FROM LS_AS_REPORT r WITH (NOLOCK)"
            << " LEFT JOIN LS_AS_PATTYPE pt WITH (NOLOCK) ON r.TYPE=pt.TYPE AND pt.DELETE_BIT=0"
            << " LEFT JOIN JC_DEPT_PROPERTY dept WITH (NOLOCK) ON r.DEPT_CODE=dept.DEPT_ID"
            << " LEFT JOIN LS_AS_MACHINE mach WITH (NOLOCK) ON r.MACH_CODE=mach.MACH_CODE AND mach.DELETE_BIT=0"
            << " WHERE r.TXM_NO='" << escaped << "'"
            << " AND (r.DELETE_BIT IS NULL OR r.DELETE_BIT=0)"
            << " ORDER BY r.CHK_DATE DESC,r.REP_NO DESC";
        if (log) log("exec sql: " + sql.str() + "\n");

        SQLHSTMT stmt = SQL_NULL_HSTMT;
        if (!exec_query(db.dbc, sql.str(), stmt, error)) { disconnect(db); return false; }
        while (SQLFetch(stmt) == SQL_SUCCESS) {
            const auto col_type_code = fetch_column(stmt, 1);
            const auto col_type_name = fetch_column(stmt, 2);
            const auto col_rep_no = fetch_column(stmt, 3);
            const auto col_oper_no = fetch_column(stmt, 4);
            const auto col_reg_no = fetch_column(stmt, 5);
            const auto col_name = fetch_column(stmt, 6);
            const auto col_sex = fetch_column(stmt, 7);
            const auto col_age = fetch_column(stmt, 8);
            const auto col_bed_no = fetch_column(stmt, 9);
            const auto col_dept_name = fetch_column(stmt, 10);
            const auto col_mach_code = fetch_column(stmt, 11);
            const auto col_group_code = fetch_column(stmt, 12);
            const auto col_chk_flag = fetch_column(stmt, 13);
            const auto col_conf = fetch_column(stmt, 14);
            const auto col_create_time = fetch_column(stmt, 15);

            result.has_report_rows = true;
            fill_if_empty(result.type_code, col_type_code);
            fill_if_empty(result.type_name, col_type_name);
            fill_if_empty(result.rep_no, col_rep_no);
            fill_if_empty(result.oper_no, col_oper_no);
            fill_if_empty(result.reg_no, col_reg_no);
            fill_if_empty(result.name, col_name);
            fill_if_empty(result.sex, col_sex);
            fill_if_empty(result.age, col_age);
            fill_if_empty(result.bed_no, col_bed_no);
            fill_if_empty(result.dept_name, col_dept_name);
            fill_if_empty(result.mach_code, col_mach_code);
            fill_if_empty(result.group_code, col_group_code);
            fill_if_empty(result.chk_flag, col_chk_flag);
            fill_if_empty(result.conf, col_conf);
            fill_if_empty(result.create_time, col_create_time);
        }
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    }

    {
        std::ostringstream sql;
        sql << "SELECT TOP 50 "
            << "isnull(LTRIM(RTRIM(TXM)),''),"
            << "isnull(LTRIM(RTRIM(blh)),''),"
            << "isnull(LTRIM(RTRIM(hzxm)),''),"
            << "CASE CONVERT(varchar(10),xb) WHEN '1' THEN '男' WHEN '2' THEN '女' ELSE isnull(LTRIM(RTRIM(CONVERT(varchar(10),xb))),'') END,"
            << "isnull(LTRIM(RTRIM(BBMC)),''),"
            << "isnull(LTRIM(RTRIM(ksname)),''),"
            << "isnull(LTRIM(RTRIM(kdys)),''),"
            << "isnull(LTRIM(RTRIM(sqnr)),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(32),ZJE))),''),"
            << "isnull(CONVERT(varchar(19),lrrq,120),''),"
            << "isnull(CONVERT(varchar(19),BBCJSJ,120),'')"
            << " FROM V_lis_mzinfo_txm"
            << " WHERE TXM='" << escaped << "'";
        SQLHSTMT stmt = SQL_NULL_HSTMT;
        if (exec_optional_query(db.dbc, sql.str(), stmt, log)) {
            while (SQLFetch(stmt) == SQL_SUCCESS) {
                const auto col_txm = fetch_column(stmt, 1);
                const auto col_blh = fetch_column(stmt, 2);
                const auto col_name = fetch_column(stmt, 3);
                const auto col_sex = fetch_column(stmt, 4);
                const auto col_sample_name = fetch_column(stmt, 5);
                const auto col_dept_name = fetch_column(stmt, 6);
                const auto col_requester = fetch_column(stmt, 7);
                const auto col_order_text = fetch_column(stmt, 8);
                const auto col_fee = fetch_column(stmt, 9);
                const auto col_request_time = fetch_column(stmt, 10);
                const auto col_collection_time = fetch_column(stmt, 11);

                result.has_outpatient_rows = true;
                fill_if_empty(result.barcode, col_txm);
                fill_if_empty(result.reg_no, col_blh);
                fill_if_empty(result.name, col_name);
                fill_if_empty(result.sex, col_sex);
                fill_if_empty(result.type_name, "门诊");
                fill_if_empty(result.dept_name, col_dept_name);
                fill_if_empty(result.requester, col_requester);
                SpecimenOrderRow order;
                order.barcode = col_txm;
                order.sample_name = col_sample_name;
                order.order_text = col_order_text;
                order.fee = col_fee;
                order.request_time = col_request_time;
                fill_if_empty(result.fee, order.fee);
                fill_if_empty(result.collection_time, col_collection_time);
                if (should_add_supplemental_orders(result) &&
                    (!trim(order.order_text).empty() || !trim(order.sample_name).empty())) {
                    add_unique_order(result.orders, order);
                }
            }
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        }
    }

    {
        std::ostringstream sql;
        sql << "SELECT TOP 50 "
            << "isnull(LTRIM(RTRIM(TXM)),''),"
            << "isnull(LTRIM(RTRIM(BLH)),''),"
            << "isnull(LTRIM(RTRIM(SQNR)),''),"
            << "isnull(LTRIM(RTRIM(BBMC)),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(32),JE))),''),"
            << "isnull(CONVERT(varchar(19),SQRQ,120),''),"
            << "isnull(CONVERT(varchar(19),BBCJSJ,120),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(20),ZXKS))),'')"
            << " FROM YJ_MZSQ WITH (NOLOCK)"
            << " WHERE TXM='" << escaped << "'"
            << " AND (BSCBZ IS NULL OR BSCBZ=0)"
            << " ORDER BY YJSQID";
        SQLHSTMT stmt = SQL_NULL_HSTMT;
        if (exec_optional_query(db.dbc, sql.str(), stmt, log)) {
            while (SQLFetch(stmt) == SQL_SUCCESS) {
                const auto col_txm = fetch_column(stmt, 1);
                const auto col_blh = fetch_column(stmt, 2);
                const auto col_order_text = fetch_column(stmt, 3);
                const auto col_sample_name = fetch_column(stmt, 4);
                const auto col_fee = fetch_column(stmt, 5);
                const auto col_request_time = fetch_column(stmt, 6);
                const auto col_collection_time = fetch_column(stmt, 7);
                const auto col_room_code = fetch_column(stmt, 8);

                result.has_outpatient_rows = true;
                fill_if_empty(result.barcode, col_txm);
                fill_if_empty(result.reg_no, col_blh);
                fill_if_empty(result.type_name, "门诊");
                SpecimenOrderRow order;
                order.barcode = col_txm;
                order.room_code = col_room_code;
                order.order_text = col_order_text;
                order.sample_name = col_sample_name;
                order.fee = col_fee;
                order.request_time = col_request_time;
                fill_if_empty(result.room_code, order.room_code);
                fill_if_empty(result.fee, order.fee);
                fill_if_empty(result.collection_time, col_collection_time);
                if (should_add_supplemental_orders(result) &&
                    (!trim(order.order_text).empty() || !trim(order.sample_name).empty())) {
                    add_unique_order(result.orders, order);
                }
            }
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        }
    }

    {
        std::ostringstream sql;
        sql << "SELECT TOP 50 "
            << "isnull(LTRIM(RTRIM(TXM)),''),"
            << "isnull(LTRIM(RTRIM(SQNR)),''),"
            << "isnull(LTRIM(RTRIM(BBMC)),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(32),JE))),''),"
            << "isnull(CONVERT(varchar(19),SQRQ,120),''),"
            << "isnull(CONVERT(varchar(19),BBCJSJ,120),''),"
            << "isnull(CONVERT(varchar(19),JSSJ,120),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(20),JSKS))),'')"
            << " FROM YJ_ZYSQ WITH (NOLOCK)"
            << " WHERE TXM='" << escaped << "'"
            << " AND (BSCBZ IS NULL OR BSCBZ=0)"
            << " ORDER BY YJSQID";
        SQLHSTMT stmt = SQL_NULL_HSTMT;
        if (exec_optional_query(db.dbc, sql.str(), stmt, log)) {
            while (SQLFetch(stmt) == SQL_SUCCESS) {
                const auto col_txm = fetch_column(stmt, 1);
                const auto col_order_text = fetch_column(stmt, 2);
                const auto col_sample_name = fetch_column(stmt, 3);
                const auto col_fee = fetch_column(stmt, 4);
                const auto col_request_time = fetch_column(stmt, 5);
                const auto col_collection_time = fetch_column(stmt, 6);
                const auto col_signed_time = fetch_column(stmt, 7);
                const auto col_room_code = fetch_column(stmt, 8);

                result.has_inpatient_rows = true;
                fill_if_empty(result.barcode, col_txm);
                fill_if_empty(result.type_name, "住院");
                SpecimenOrderRow order;
                order.barcode = col_txm;
                order.order_text = col_order_text;
                order.sample_name = col_sample_name;
                order.fee = col_fee;
                order.request_time = col_request_time;
                fill_if_empty(result.fee, order.fee);
                fill_if_empty(result.collection_time, col_collection_time);
                fill_if_empty(result.signed_time, col_signed_time);
                order.room_code = col_room_code;
                fill_if_empty(result.room_code, order.room_code);
                if (should_add_supplemental_orders(result) &&
                    (!trim(order.order_text).empty() || !trim(order.sample_name).empty())) {
                    add_unique_order(result.orders, order);
                }
            }
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        }
    }

    disconnect(db);
    error.clear();
    return true;
#endif
}

bool query_hiv_statistics(const HivStatQuery& query, HivStatSummary& summary, std::vector<HivStatDetailRow>& rows, std::string& error, LogFn log) {
    summary = HivStatSummary{};
    rows.clear();
#ifndef _WIN32
    (void)query;
    (void)log;
    error = "query_hiv_statistics is only available on Windows";
    return false;
#else
    if (query.year < 1900 || query.year > 9999 || query.month < 1 || query.month > 12) {
        error = "invalid year or month";
        return false;
    }

    char start_date[16]{};
    char end_date[16]{};
    const int next_year = query.month == 12 ? query.year + 1 : query.year;
    const int next_month = query.month == 12 ? 1 : query.month + 1;
    sprintf_s(start_date, "%04d-%02d-01", query.year, query.month);
    sprintf_s(end_date, "%04d-%02d-01", next_year, next_month);
    const std::string lab_department = trim(query.lab_department);

    DbContext db;
    if (!connect(query.connection_string, db, error, log)) {
        return false;
    }

    const char* dept_name_expr =
        "isnull(nullif(LTRIM(RTRIM(dept.NAME)),''),isnull(LTRIM(RTRIM(CONVERT(varchar(20),r.DEPT_CODE))),''))";

    std::ostringstream sql;
    sql << ";WITH raw AS ("
        << " SELECT"
        << " r.REP_NO,"
        << " r.MACH_CODE,"
        << " isnull(nullif(LTRIM(RTRIM(mach.MACH_NAME)),''),isnull(LTRIM(RTRIM(CONVERT(varchar(20),r.MACH_CODE))),'')) AS MACHINE_NAME,"
        << " CASE WHEN " << dept_name_expr << " LIKE '%滨水新城%' THEN '新院' ELSE '老院' END AS LAB_DEPARTMENT,"
        << " e.ITEM_CODE,"
        << " isnull(LTRIM(RTRIM(e.ITEM_NAME)),'') AS ITEM_NAME,"
        << " isnull(LTRIM(RTRIM(r.TXM_NO)),'') AS TXM_NO,"
        << " isnull(LTRIM(RTRIM(r.OPER_NO)),'') AS OPER_NO,"
        << " isnull(LTRIM(RTRIM(r.NAME)),'') AS NAME,"
        << " isnull(nullif(LTRIM(RTRIM(pt.TYPE_NAME)),''),isnull(LTRIM(RTRIM(CONVERT(varchar(20),r.TYPE))),'')) AS PAT_TYPE,"
        << " " << dept_name_expr << " AS DEPT_NAME,"
        << " isnull(LTRIM(RTRIM(e.RESULT)),'') AS RESULT_VALUE,"
        << " isnull(LTRIM(RTRIM(e.UPBOUND)),'') AS LOWER_BOUND,"
        << " isnull(LTRIM(RTRIM(e.DOWNBOUND)),'') AS UPPER_BOUND,"
        << " isnull(CONVERT(varchar(19),r.REP_TIME,120),'') AS REP_TIME_TEXT"
        << " FROM LS_AS_REPORT r WITH (NOLOCK)"
        << " INNER JOIN LS_AS_REPENTRY e WITH (NOLOCK)"
        << " ON e.REP_NO=r.REP_NO AND isnull(e.DELETE_BIT,0)=0"
        << " LEFT JOIN LS_AS_MACHINE mach WITH (NOLOCK) ON r.MACH_CODE=mach.MACH_CODE AND mach.DELETE_BIT=0"
        << " LEFT JOIN LS_AS_PATTYPE pt WITH (NOLOCK) ON r.TYPE=pt.TYPE AND pt.DELETE_BIT=0"
        << " LEFT JOIN JC_DEPT_PROPERTY dept WITH (NOLOCK) ON r.DEPT_CODE=dept.DEPT_ID AND isnull(dept.DELETED,0)=0"
        << " WHERE isnull(r.DELETE_BIT,0)=0"
        << " AND r.REP_TIME>='" << start_date << "'"
        << " AND r.REP_TIME<'" << end_date << "'"
        << " AND r.CHK_FLAG='T'"
        << " AND r.CONF='S'"
        << " AND NULLIF(LTRIM(RTRIM(r.NAME)),'') IS NOT NULL"
        << " AND ("
        << " (r.MACH_CODE=4005 AND e.ITEM_CODE=91593)"
        << " OR (r.MACH_CODE=914 AND e.ITEM_CODE=93053)"
        << " OR (r.MACH_CODE=4008 AND e.ITEM_CODE=91442)"
        << " )";
    if (lab_department == "新院") {
        sql << " AND " << dept_name_expr << " LIKE '%滨水新城%'";
    } else if (lab_department == "老院") {
        sql << " AND " << dept_name_expr << " NOT LIKE '%滨水新城%'";
    }
    sql
        << "), marked AS ("
        << " SELECT *,"
        << " CASE WHEN RESULT_VALUE LIKE '%待确认%' THEN 1"
        << " WHEN RESULT_VALUE LIKE '%阳性%' OR RESULT_VALUE LIKE '%+%' THEN 1"
        << " ELSE 0 END AS POSITIVE_FLAG"
        << " FROM raw"
        << "), grouped AS ("
        << " SELECT"
        << " REP_NO,MACH_CODE,ITEM_CODE,"
        << " MAX(MACHINE_NAME) AS MACHINE_NAME,"
        << " MAX(LAB_DEPARTMENT) AS LAB_DEPARTMENT,"
        << " MAX(ITEM_NAME) AS ITEM_NAME,"
        << " MAX(TXM_NO) AS TXM_NO,"
        << " MAX(OPER_NO) AS OPER_NO,"
        << " MAX(NAME) AS NAME,"
        << " MAX(PAT_TYPE) AS PAT_TYPE,"
        << " MAX(DEPT_NAME) AS DEPT_NAME,"
        << " MAX(RESULT_VALUE) AS RESULT_VALUE,"
        << " MAX(LOWER_BOUND) AS LOWER_BOUND,"
        << " MAX(UPPER_BOUND) AS UPPER_BOUND,"
        << " MAX(REP_TIME_TEXT) AS REP_TIME_TEXT,"
        << " MAX(POSITIVE_FLAG) AS POSITIVE_FLAG"
        << " FROM marked"
        << " GROUP BY REP_NO,MACH_CODE,ITEM_CODE"
        << ")"
        << " SELECT"
        << " CONVERT(varchar(20),MACH_CODE),"
        << " MACHINE_NAME,"
        << " LAB_DEPARTMENT,"
        << " CONVERT(varchar(20),ITEM_CODE),"
        << " ITEM_NAME,"
        << " CONVERT(varchar(30),REP_NO),"
        << " TXM_NO,OPER_NO,NAME,PAT_TYPE,DEPT_NAME,RESULT_VALUE,LOWER_BOUND,UPPER_BOUND,"
        << " CASE WHEN POSITIVE_FLAG=1 THEN '是' ELSE '否' END,"
        << " REP_TIME_TEXT"
        << " FROM grouped"
        << " ORDER BY MACH_CODE,ITEM_CODE,REP_NO";

    if (log) log("exec sql: " + sql.str() + "\n");

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) {
        disconnect(db);
        return false;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        HivStatDetailRow row;
        row.mach_code = fetch_column(stmt, 1);
        row.machine_name = fetch_column(stmt, 2);
        row.lab_department = fetch_column(stmt, 3);
        row.item_code = fetch_column(stmt, 4);
        row.item_name = fetch_column(stmt, 5);
        row.rep_no = fetch_column(stmt, 6);
        row.txm_no = fetch_column(stmt, 7);
        row.oper_no = fetch_column(stmt, 8);
        row.name = fetch_column(stmt, 9);
        row.patient_type = fetch_column(stmt, 10);
        row.dept_name = fetch_column(stmt, 11);
        row.result = fetch_column(stmt, 12);
        row.lower_bound = fetch_column(stmt, 13);
        row.upper_bound = fetch_column(stmt, 14);
        row.positive = fetch_column(stmt, 15);
        row.report_time = fetch_column(stmt, 16);

        ++summary.screening_count;
        const bool is_positive = trim(row.positive) == "是";
        if (is_positive) {
            ++summary.positive_count;
        }
        if (is_hiv_sti_clinic_dept(row.dept_name)) {
            add_count(summary.sti_clinic_screening_count, summary.sti_clinic_positive_count, is_positive);
        }
        if (is_hiv_other_visit_dept(row.dept_name)) {
            add_count(summary.other_visit_screening_count, summary.other_visit_positive_count, is_positive);
        }
        if (is_hiv_prenatal_dept(row.dept_name)) {
            add_count(summary.prenatal_screening_count, summary.prenatal_positive_count, is_positive);
        }
        rows.push_back(std::move(row));
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    disconnect(db);
    error.clear();
    return true;
#endif
}

}  // namespace search
