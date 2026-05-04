#include "trend_core.h"

#include "search_text.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
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
            ";DATABASE=" + db_it->second + ";UID=" + user_it->second +
            ";PWD=" + pass_it->second + ";TrustServerCertificate=Yes;",
        "DRIVER={ODBC Driver 17 for SQL Server};SERVER=" + server_it->second +
            ";DATABASE=" + db_it->second + ";UID=" + user_it->second +
            ";PWD=" + pass_it->second + ";TrustServerCertificate=Yes;",
        "DRIVER={SQL Server};SERVER=" + server_it->second +
            ";DATABASE=" + db_it->second + ";UID=" + user_it->second +
            ";PWD=" + pass_it->second + ";",
    };
}

bool parse_number(const std::string& text, double& value) {
    const auto cleaned = trim(text);
    if (cleaned.empty()) {
        return false;
    }
    char* end = nullptr;
    value = std::strtod(cleaned.c_str(), &end);
    return end && *end == '\0';
}

bool has_patient_filter(const QueryInput& input) {
    return !trim(input.patient_id).empty() || !trim(input.barcode).empty() ||
           !trim(input.patient_name).empty() || !trim(input.patient_no).empty() ||
           !trim(input.oper_no).empty();
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

#ifdef _WIN32
struct DbContext {
    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC dbc = SQL_NULL_HDBC;
};

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

std::string collect_diag(SQLSMALLINT handle_type, SQLHANDLE handle) {
    std::ostringstream oss;
    SQLSMALLINT rec = 1;
    while (true) {
        SQLWCHAR state[16] = {};
        SQLWCHAR message[1024] = {};
        SQLINTEGER native_error = 0;
        SQLSMALLINT text_len = 0;
        const SQLRETURN rc = SQLGetDiagRecW(handle_type, handle, rec, state, &native_error, message, 1024, &text_len);
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

bool connect(const std::string& connection_string, DbContext& db, std::string& error) {
    if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &db.env) != SQL_SUCCESS) {
        error = "SQLAllocHandle ENV failed";
        return false;
    }
    SQLSetEnvAttr(db.env, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);
    if (SQLAllocHandle(SQL_HANDLE_DBC, db.env, &db.dbc) != SQL_SUCCESS) {
        error = "SQLAllocHandle DBC failed";
        disconnect(db);
        return false;
    }

    for (const auto& candidate : odbc_candidates(connection_string)) {
        const auto wide = utf8_to_wide(candidate);
        SQLWCHAR out_conn[2048] = {};
        SQLSMALLINT out_len = 0;
        const SQLRETURN rc = SQLDriverConnectW(
            db.dbc, nullptr, reinterpret_cast<SQLWCHAR*>(const_cast<wchar_t*>(wide.c_str())), SQL_NTS,
            out_conn, 2048, &out_len, SQL_DRIVER_NOPROMPT);
        if (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) {
            return true;
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
#endif

}  // namespace

bool query_trend_points(const DbSettings& settings, const QueryInput& input, std::vector<TrendPoint>& rows, std::string& error) {
    rows.clear();
#ifndef _WIN32
    (void)settings;
    (void)input;
    error = "query_trend_points is only available on Windows";
    return false;
#else
    const auto connection_string = wide_to_utf8(build_connection_string_w(settings));
    if (connection_string.empty()) {
        error = "missing connection string";
        return false;
    }
    if (!has_patient_filter(input)) {
        error = "趋势查询必须至少填写一个患者相关条件。";
        return false;
    }
    if (trim(input.start_date).empty() || trim(input.end_date).empty()) {
        error = "趋势查询必须填写开始日期和结束日期。";
        return false;
    }

    DbContext db;
    if (!connect(connection_string, db, error)) {
        return false;
    }

    std::ostringstream sql;
    sql << "SELECT CAST(r.REP_NO AS varchar(20)),isnull(r.TXM_NO,''),isnull(r.OPER_NO,''),"
        << "isnull(r.NAME,''),isnull(CONVERT(varchar(19),r.REP_DATE,120),''),"
        << "CAST(e.ITEM_CODE AS varchar(20)),isnull(i.ITEM_NAME,e.ITEM_NAME),isnull(i.ENG_NAME,''),"
        << "isnull(e.RESULT,''),isnull(RTRIM(i.UNIT),''),isnull(e.UPBOUND,''),isnull(e.DOWNBOUND,''),"
        << "isnull(e.NORMAL,'')"
        << " FROM LS_AS_REPORT r"
        << " JOIN LS_AS_REPENTRY e ON e.REP_NO = r.REP_NO AND e.DELETE_BIT=0"
        << " LEFT JOIN LS_AS_ITEM i ON i.ITEM_CODE = e.ITEM_CODE AND i.DELETE_BIT=0"
        << " WHERE r.DELETE_BIT=0";
    add_eq(sql, "r.REG_NO", input.patient_id);
    add_eq(sql, "r.TXM_NO", input.barcode);
    add_like(sql, "r.NAME", input.patient_name);
    if (!trim(input.patient_no).empty()) {
        sql << " AND EXISTS (SELECT 1 FROM LS_AS_BARCODE b"
            << " WHERE isnull(b.DELETE_BIT,0)=0"
            << " AND b.REG_NO='" << sql_escape(trim(input.patient_no)) << "'"
            << " AND b.BARCODE = r.TXM_NO)";
    }
    add_eq(sql, "r.OPER_NO", input.oper_no);
    add_eq(sql, "r.ROOM_CODE", input.room_code);
    add_eq(sql, "r.MACH_CODE", input.mach_code);
    add_eq(sql, "r.TYPE", input.patient_type);
    add_eq(sql, "r.GROUP_CODE", input.group_code);
    add_eq(sql, "e.ITEM_CODE", input.item_code);
    sql << " AND r.REP_DATE >= '" << sql_escape(trim(input.start_date)) << "'";
    sql << " AND r.REP_DATE < DATEADD(day,1,'" << sql_escape(trim(input.end_date)) << "')";
    sql << " ORDER BY e.ITEM_CODE ASC,r.REP_DATE ASC,r.REP_NO ASC";

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) {
        disconnect(db);
        return false;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        TrendPoint row;
        row.rep_no = fetch_column(stmt, 1);
        row.txm_no = fetch_column(stmt, 2);
        row.oper_no = fetch_column(stmt, 3);
        row.patient_name = fetch_column(stmt, 4);
        row.report_time = fetch_column(stmt, 5);
        row.item_code = fetch_column(stmt, 6);
        row.item_name = fetch_column(stmt, 7);
        row.item_eng = fetch_column(stmt, 8);
        row.result_text = fetch_column(stmt, 9);
        row.unit = fetch_column(stmt, 10);
        row.lower_bound = fetch_column(stmt, 11);
        row.upper_bound = fetch_column(stmt, 12);
        row.normal = fetch_column(stmt, 13);
        row.has_numeric_value = parse_number(row.result_text, row.result_value);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    disconnect(db);
    error.clear();
    return true;
#endif
}

std::vector<TrendItemOption> trend_item_options(const std::vector<TrendPoint>& rows) {
    std::vector<TrendItemOption> items;
    for (const auto& row : rows) {
        const auto found = std::find_if(items.begin(), items.end(), [&](const TrendItemOption& item) {
            return item.item_code == row.item_code;
        });
        if (found == items.end()) {
            items.push_back({row.item_code, row.item_name, row.unit});
        }
    }
    return items;
}

}  // namespace search
