#include "barcode_report_query.h"
#ifdef _WIN32
#include "query_diagnostics.h"
#include "search_text.h"
#include "odbc_connection_policy.h"
#include <algorithm>
#include <chrono>
#include <mutex>

namespace search {
namespace {
constexpr std::size_t kBatchSize = 100;
constexpr std::size_t kRowLimit = 5000;
constexpr SQLULEN kQueryTimeoutSeconds = 8;
constexpr std::size_t kMaxCachedSources = 8;
constexpr auto kDictionaryLifetime = std::chrono::minutes(5);
using Table = std::vector<std::vector<std::string>>;
using Cancel = std::function<bool()>;
bool stopped(const Cancel& cancelled, std::string& error) {
    if (!cancelled || !cancelled()) return false;
    error = "查询已取消。";
    return true;
}
struct Statement {
    SQLHSTMT value = SQL_NULL_HSTMT;
    ~Statement() { if (value != SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT, value); }
};
// Bind Unicode values, then CAST placeholders to the column type in SQL so the
// indexed varchar/numeric/date columns are never converted to nvarchar.
bool query(SQLHDBC connection, const std::string& sql, const std::vector<std::string>& parameters,
           unsigned columns, Table& rows, std::string& error, const Cancel& cancelled) {
    rows.clear();
    if (stopped(cancelled, error)) return false;
    Statement statement;
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, connection, &statement.value))) {
        error = querydiag::safe_error(querydiag::Operation::statement); return false;
    }
    const auto fail = [&](querydiag::Operation operation) {
        error = querydiag::odbc_error(operation, SQL_HANDLE_STMT, statement.value); return false;
    };
    if (!SQL_SUCCEEDED(SQLSetStmtAttr(statement.value, SQL_ATTR_QUERY_TIMEOUT,
                                      reinterpret_cast<SQLPOINTER>(kQueryTimeoutSeconds), 0))) return fail(querydiag::Operation::timeout);
    const auto wide_sql = utf8_to_wide(sql);
    if (!SQL_SUCCEEDED(SQLPrepareW(statement.value, reinterpret_cast<SQLWCHAR*>(const_cast<wchar_t*>(wide_sql.c_str())), SQL_NTS)))
        return fail(querydiag::Operation::execute);
    std::vector<std::wstring> values;
    for (const auto& parameter : parameters) values.push_back(utf8_to_wide(parameter));
    std::vector<SQLLEN> lengths(values.size(), SQL_NTS);
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (!SQL_SUCCEEDED(SQLBindParameter(statement.value, static_cast<SQLUSMALLINT>(i + 1), SQL_PARAM_INPUT,
            SQL_C_WCHAR, SQL_WVARCHAR, (std::max)(SQLULEN(1), static_cast<SQLULEN>(values[i].size())), 0,
            values[i].data(), static_cast<SQLLEN>((values[i].size() + 1) * sizeof(wchar_t)), &lengths[i])))
            return fail(querydiag::Operation::execute);
    }
    querydiag::stage_event(querydiag::Operation::execute);
    if (!SQL_SUCCEEDED(SQLExecute(statement.value))) return fail(querydiag::Operation::execute);
    while (true) {
        if (stopped(cancelled, error)) return false;
        const auto fetched = SQLFetch(statement.value);
        if (fetched == SQL_NO_DATA) break;
        if (!SQL_SUCCEEDED(fetched)) return fail(querydiag::Operation::fetch);
        if (rows.size() + 1 >= kRowLimit) {
            error = "报告补查超过单批读取上限，请缩小日期或专业组范围。"; return false;
        }
        std::vector<std::string> row;
        if (!read_barcode_columns(statement.value, columns, row, error)) return false;
        rows.push_back(std::move(row));
    }
    return true;
}
std::string placeholders(std::size_t count, const char* type) {
    std::string result;
    for (std::size_t i = 0; i < count; ++i) {
        if (i) result += ',';
        result += "CAST(? AS "; result += type; result += ')';
    }
    return result;
}
std::string cache_source(const std::string& connection) {
    static const std::set<std::string> identity_keys = {
        "SERVER", "DATA SOURCE", "DATABASE", "INITIAL CATALOG", "UID", "USER ID",
        "DSN", "DRIVER", "INTEGRATED SECURITY", "TRUSTED_CONNECTION"};
    std::string result;
    for (const auto& item : odbc::parse_connection_kv(connection)) {
        if (!identity_keys.count(item.first)) continue;
        result += item.first + '=' + item.second + '\n';
    }
    return result;
}
struct Dictionaries {
    std::chrono::steady_clock::time_point expires{};
    std::map<std::string, std::vector<BarcodeOrderMapping>> orders;
    std::map<std::string, std::vector<BarcodeExpectedItem>> groups;
};
struct Cache {
    std::mutex mutex;
    std::map<std::string, Dictionaries> databases;
};
Cache& dictionary_cache() { static Cache cache; return cache; }
Dictionaries cached_dictionaries(const std::string& key, bool refresh) {
    if (key.empty()) return {};
    auto& cache = dictionary_cache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    const auto found = cache.databases.find(key);
    if (refresh || found == cache.databases.end() || found->second.expires <= std::chrono::steady_clock::now()) {
        cache.databases.erase(key); return {};
    }
    return found->second;
}
void cache_dictionaries(const std::string& key, Dictionaries dictionary) {
    if (key.empty()) return;
    auto& cache = dictionary_cache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    if (cache.databases.size() >= kMaxCachedSources && !cache.databases.count(key)) cache.databases.erase(cache.databases.begin());
    // Fixed expiry: accessing cached entries must not keep old definitions alive indefinitely.
    if (dictionary.expires <= std::chrono::steady_clock::now())
        dictionary.expires = std::chrono::steady_clock::now() + kDictionaryLifetime;
    cache.databases[key] = std::move(dictionary);
}
} // namespace

bool read_barcode_columns(SQLHSTMT statement, unsigned count, std::vector<std::string>& columns, std::string& error) {
    columns.clear();
    columns.reserve(count);
    for (unsigned column = 1; column <= count; ++column) {
        std::wstring value;
        while (true) {
            wchar_t buffer[1024]{};
            SQLLEN length = 0;
            const auto result = SQLGetData(statement, static_cast<SQLUSMALLINT>(column), SQL_C_WCHAR,
                                           buffer, sizeof(buffer), &length);
            if (result == SQL_NO_DATA || length == SQL_NULL_DATA) break;
            if (!SQL_SUCCEEDED(result)) {
                error = querydiag::odbc_error(querydiag::Operation::read, SQL_HANDLE_STMT, statement); return false;
            }
            value += buffer;
            if (value.size() > 65536) { error = querydiag::safe_error(querydiag::Operation::read); return false; }
            if (result == SQL_SUCCESS) break;
        }
        columns.push_back(trim(wide_to_utf8(value)));
    }
    return true;
}

bool supplement_barcode_reports(SQLHDBC connection, const BarcodeQueryFilters& filters,
    const std::vector<BarcodeMatchSource>& sources, std::vector<BarcodeQueryRow>& rows,
    std::string& error, const Cancel& cancelled) {
    if (sources.size() != rows.size()) { error = querydiag::safe_error(querydiag::Operation::read); return false; }
    if (stopped(cancelled, error)) return false;
    // A cancelled receipt is a historical view, not evidence that its current
    // order belongs to another specimen's report. Keep its original status.
    if (filters.canceled) {
        for (auto& row : rows) row.report_link = row.report_no.empty() ? "取消签收：未补关联" : "主条码关联";
        return true;
    }
    const auto key = cache_source(filters.connection_string);
    auto dictionary = cached_dictionaries(key, filters.refresh_report_dictionary);
    std::set<std::string> needed_orders;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        if (!rows[i].report_no.empty()) continue;
        const auto& source = sources[i];
        if (trim(source.patient_no).empty() || trim(source.patient_type).empty() ||
            barcode_receive_day(source.receive_time).empty()) continue;
        if (barcode_numeric_id(source.machine).empty() || barcode_numeric_id(source.group).empty()) {
            const auto order = barcode_numeric_id(source.order_code);
            if (!order.empty() && !dictionary.orders.count(order)) needed_orders.insert(order);
        }
    }
    Table table;
    std::vector<std::string> ids(needed_orders.begin(), needed_orders.end());
    for (std::size_t start = 0; start < ids.size(); start += kBatchSize) {
        const auto end = (std::min)(start + kBatchSize, ids.size());
        std::vector<std::string> parameters(ids.begin() + start, ids.begin() + end);
        const std::string sql = "SELECT DISTINCT TOP (5001) m.ORDER_CODE,g.GROUP_CODE,g.MACH_CODE,g.ROOM_CODE"
            " FROM LS_AS_LABMATCH m JOIN LS_AS_GROUP g ON g.GROUP_CODE=m.GROUP_CODE"
            " WHERE m.DELETE_BIT=0 AND g.DELETE_BIT=0 AND m.ORDER_CODE IN (" +
            placeholders(parameters.size(), "bigint") + ") OPTION (MAXDOP 1)";
        if (!query(connection, sql, parameters, 4, table, error, cancelled)) return false;
        for (const auto& id : parameters) dictionary.orders[id] = {};
        for (const auto& r : table) dictionary.orders[r[0]].push_back({r[0], r[1], r[2], r[3]});
    }
    std::vector<std::vector<BarcodeMatchProfile>> profiles(rows.size());
    std::set<BarcodeReportKey> report_keys;
    std::set<std::string> needed_groups;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (stopped(cancelled, error)) return false;
        if (!rows[i].report_no.empty()) { rows[i].report_link = "主条码关联"; continue; }
        const auto found = dictionary.orders.find(barcode_numeric_id(sources[i].order_code));
        static const std::vector<BarcodeOrderMapping> empty;
        profiles[i] = barcode_match_profiles(sources[i], found == dictionary.orders.end() ? empty : found->second);
        const auto keys = barcode_report_keys(sources[i], profiles[i]);
        report_keys.insert(keys.begin(), keys.end());
        if (keys.empty()) continue;
        for (const auto& profile : profiles[i]) for (const auto& group : profile.groups)
            if (!dictionary.groups.count(group)) needed_groups.insert(group);
    }
    ids.assign(needed_groups.begin(), needed_groups.end());
    for (std::size_t start = 0; start < ids.size(); start += kBatchSize) {
        const auto end = (std::min)(start + kBatchSize, ids.size());
        std::vector<std::string> parameters(ids.begin() + start, ids.begin() + end);
        const std::string sql = "SELECT DISTINCT TOP (5001) gi.GROUP_CODE,g.MACH_CODE,g.ROOM_CODE,gi.ITEM_CODE"
            " FROM LS_AS_GROUP_ITEM gi JOIN LS_AS_GROUP g ON g.GROUP_CODE=gi.GROUP_CODE"
            " JOIN LS_AS_ITEM i ON i.ITEM_CODE=gi.ITEM_CODE"
            " WHERE g.DELETE_BIT=0 AND i.DELETE_BIT<>1 AND gi.GROUP_CODE IN (" +
            placeholders(parameters.size(), "int") + ") OPTION (MAXDOP 1)";
        if (!query(connection, sql, parameters, 4, table, error, cancelled)) return false;
        for (const auto& id : parameters) dictionary.groups[id] = {};
        for (const auto& r : table) dictionary.groups[r[0]].push_back({r[0], r[1], r[2], r[3]});
    }
    // Publish definitions only after all dictionary batches completed. Reports are never cached.
    if (stopped(cancelled, error)) return false;
    cache_dictionaries(key, dictionary);
    std::vector<BarcodeExpectedItem> expected;
    for (const auto& entry : dictionary.groups) expected.insert(expected.end(), entry.second.begin(), entry.second.end());
    std::vector<BarcodeReportKey> keys(report_keys.begin(), report_keys.end());
    std::vector<BarcodeMatchReport> reports;
    std::map<std::string, std::size_t> by_report;
    for (std::size_t start = 0; start < keys.size(); start += kBatchSize) {
        const auto end = (std::min)(start + kBatchSize, keys.size());
        std::vector<std::string> parameters;
        std::string values;
        for (auto i = start; i < end; ++i) {
            if (!values.empty()) values += ',';
            values += "(CAST(? AS varchar(50)),CAST(? AS varchar(1)),CAST(? AS int),CAST(? AS smallint),CAST(? AS datetime))";
            parameters.insert(parameters.end(), {keys[i].patient_no, keys[i].patient_type, keys[i].machine, keys[i].room, keys[i].day});
        }
        const std::string sql = "SELECT TOP (5001) r.REP_NO,r.TXM_NO,r.REG_NO,r.TYPE,r.MACH_CODE,r.ROOM_CODE,"
            "CONVERT(varchar(19),r.CHK_DATE,120),CONVERT(varchar(19),r.REP_TIME,120),r.CHK_FLAG,r.CONF,"
            "r.OPER_NO,r.OPER_CODE,r.REP_OPER,COALESCE(m.MACH_NAME,CONVERT(varchar(20),r.MACH_CODE))"
            " FROM LS_AS_REPORT r JOIN (VALUES " + values + ") k(REG_NO,TYPE,MACH_CODE,ROOM_CODE,DAY)"
            " ON r.REG_NO=k.REG_NO AND r.TYPE=k.TYPE AND r.MACH_CODE=k.MACH_CODE AND r.ROOM_CODE=k.ROOM_CODE"
            " AND r.CHK_DATE>=k.DAY AND r.CHK_DATE<DATEADD(day,1,k.DAY)"
            " LEFT JOIN LS_AS_MACHINE m ON m.MACH_CODE=r.MACH_CODE AND m.ROOM_CODE=r.ROOM_CODE AND m.DELETE_BIT=0"
            " WHERE ISNULL(r.DELETE_BIT,0)=0 OPTION (MAXDOP 1)";
        if (!query(connection, sql, parameters, 14, table, error, cancelled)) return false;
        for (const auto& r : table) {
            if (by_report.count(r[0])) continue;
            BarcodeMatchReport report;
            auto& f = report.fields;
            f.report_no = r[0];
            f.barcode = r[1];
            f.reg_no = r[2];
            report.patient_type = r[3];
            f.machine_code = r[4];
            f.room_code = r[5];
            f.inspect_date = r[6];
            f.review_time = r[7];
            report.reviewed = r[8] == "T";
            report.sent = r[9] == "S";
            f.sample_no = r[10];
            f.tester = r[11];
            f.reviewer = r[12];
            f.machine_name = r[13];
            by_report[r[0]] = reports.size();
            reports.push_back(std::move(report));
        }
    }
    ids.clear();
    for (const auto& entry : by_report) ids.push_back(entry.first);
    for (std::size_t start = 0; start < ids.size(); start += kBatchSize) {
        const auto end = (std::min)(start + kBatchSize, ids.size());
        std::vector<std::string> parameters(ids.begin() + start, ids.begin() + end);
        const std::string sql = "SELECT TOP (5001) REP_NO,ITEM_CODE,CASE WHEN NULLIF(LTRIM(RTRIM(RESULT)),'') IS NULL THEN 0 ELSE 1 END"
            " FROM LS_AS_REPENTRY WHERE DELETE_BIT=0 AND REP_NO IN (" +
            placeholders(parameters.size(), "decimal(18,0)") + ") OPTION (MAXDOP 1)";
        if (!query(connection, sql, parameters, 3, table, error, cancelled)) return false;
        for (const auto& r : table) {
            const auto found = by_report.find(r[0]);
            if (found == by_report.end()) { error=querydiag::safe_error(querydiag::Operation::read); return false; }
            const auto item = barcode_numeric_id(r[1]);
            if (item.empty()) continue;
            auto& report = reports[found->second];
            report.items.insert(item);
            if (r[2]=="1") report.result_items.insert(item);
        }
    }
    BarcodeReportMatcher matcher(expected, reports);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (stopped(cancelled, error)) return false;
        if (!rows[i].report_no.empty()) continue;
        const auto result = matcher.match(sources[i], profiles[i]);
        rows[i].report_link = barcode_match_reason_text(result.reason);
        if (result.report_index) apply_barcode_report_match(rows[i], reports[*result.report_index]);
    }
    return true;
}
} // namespace search
#endif
