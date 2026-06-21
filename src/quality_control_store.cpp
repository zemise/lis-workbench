#include "quality_control_store.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "search_text.h"
#include "sqlite3.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sstream>

namespace qc {
namespace {

struct Db {
    sqlite3* handle = nullptr;
    ~Db() {
        if (handle) sqlite3_close(handle);
    }
};

struct Stmt {
    sqlite3_stmt* handle = nullptr;
    ~Stmt() {
        if (handle) sqlite3_finalize(handle);
    }
};

std::wstring join_path(const std::wstring& dir, const std::wstring& file) {
    if (dir.empty()) return file;
    const wchar_t last = dir.back();
    if (last == L'\\' || last == L'/') return dir + file;
    return dir + L"\\" + file;
}

std::wstring configured_db_path() {
    const std::wstring value = search::load_module_str(L"QualityControl", L"LocalDbPath", L"quality_control.sqlite");
    if (value.find(L":") != std::wstring::npos || value.rfind(L"\\\\", 0) == 0 ||
        value.rfind(L"\\", 0) == 0 || value.rfind(L"/", 0) == 0) {
        return value;
    }
    return join_path(search::module_dir(), value);
}

std::string sqlite_error(sqlite3* db) {
    return db ? sqlite3_errmsg(db) : "sqlite error";
}

bool open_db(Db& db, std::string& error) {
    const std::wstring path = configured_db_path();
    if (sqlite3_open16(path.c_str(), &db.handle) != SQLITE_OK) {
        error = sqlite_error(db.handle);
        return false;
    }
    sqlite3_busy_timeout(db.handle, 5000);
    return true;
}

bool exec_sql(sqlite3* db, const char* sql, std::string& error) {
    char* errmsg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        error = errmsg ? errmsg : sqlite_error(db);
        sqlite3_free(errmsg);
        return false;
    }
    return true;
}

bool prepare(sqlite3* db, const char* sql, Stmt& stmt, std::string& error) {
    if (sqlite3_prepare_v2(db, sql, -1, &stmt.handle, nullptr) != SQLITE_OK) {
        error = sqlite_error(db);
        return false;
    }
    return true;
}

void bind_text(sqlite3_stmt* stmt, int index, const std::string& value) {
    sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

void bind_nullable_double(sqlite3_stmt* stmt, int index, const std::string& value) {
    const std::string text = search::trim(value);
    if (text.empty()) {
        sqlite3_bind_null(stmt, index);
        return;
    }
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end && *end == '\0') {
        sqlite3_bind_double(stmt, index, parsed);
    } else {
        sqlite3_bind_null(stmt, index);
    }
}

std::string col_text(sqlite3_stmt* stmt, int col) {
    const unsigned char* text = sqlite3_column_text(stmt, col);
    return text ? reinterpret_cast<const char*>(text) : "";
}

std::string now_text() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buffer;
}

bool ensure_schema(sqlite3* db, std::string& error) {
    constexpr const char* sql = R"SQL(
PRAGMA journal_mode=WAL;
CREATE TABLE IF NOT EXISTS qc_config (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  enabled INTEGER NOT NULL DEFAULT 1,
  room_code TEXT,
  mach_code TEXT NOT NULL,
  mach_name TEXT,
  sample_no TEXT NOT NULL,
  qc_name TEXT,
  level TEXT,
  item_code TEXT,
  item_name TEXT,
  target_mean REAL,
  target_sd REAL,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  UNIQUE(mach_code, sample_no, item_code, level)
);
CREATE TABLE IF NOT EXISTS qc_result (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  source_rep_no TEXT NOT NULL,
  source_entry_key TEXT NOT NULL,
  room_code TEXT,
  mach_code TEXT NOT NULL,
  mach_name TEXT,
  sample_no TEXT NOT NULL,
  barcode_no TEXT,
  report_date TEXT,
  inspect_date TEXT,
  report_time TEXT,
  effective_time TEXT NOT NULL,
  chk_flag TEXT,
  conf TEXT,
  item_code TEXT NOT NULL,
  item_name TEXT,
  result_text TEXT,
  result_value REAL,
  has_numeric_value INTEGER NOT NULL DEFAULT 0,
  unit TEXT,
  normal TEXT,
  qc_name TEXT,
  level TEXT,
  target_mean REAL,
  target_sd REAL,
  imported_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  UNIQUE(source_rep_no, source_entry_key)
);
CREATE TABLE IF NOT EXISTS qc_import_log (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  started_at TEXT NOT NULL,
  finished_at TEXT,
  start_date TEXT NOT NULL,
  end_date TEXT NOT NULL,
  mach_code TEXT,
  imported_count INTEGER NOT NULL DEFAULT 0,
  updated_count INTEGER NOT NULL DEFAULT 0,
  skipped_count INTEGER NOT NULL DEFAULT 0,
  status TEXT NOT NULL,
  error TEXT
);
CREATE INDEX IF NOT EXISTS idx_qc_result_time ON qc_result(effective_time);
CREATE INDEX IF NOT EXISTS idx_qc_result_group ON qc_result(mach_code, sample_no, item_code, level, effective_time);
CREATE INDEX IF NOT EXISTS idx_qc_result_report ON qc_result(source_rep_no);
)SQL";
    return exec_sql(db, sql, error);
}

}  // namespace

std::wstring default_db_path() {
    return configured_db_path();
}

bool ensure_store(std::string& error) {
    Db db;
    return open_db(db, error) && ensure_schema(db.handle, error);
}

bool load_configs(std::vector<Config>& rows, std::string& error) {
    rows.clear();
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    Stmt stmt;
    if (!prepare(db.handle,
                 "SELECT id,enabled,room_code,mach_code,mach_name,sample_no,qc_name,level,item_code,item_name,"
                 "ifnull(target_mean,''),ifnull(target_sd,'') FROM qc_config ORDER BY mach_code,sample_no,item_code,level",
                 stmt, error)) {
        return false;
    }
    while (sqlite3_step(stmt.handle) == SQLITE_ROW) {
        Config row;
        row.id = sqlite3_column_int(stmt.handle, 0);
        row.enabled = sqlite3_column_int(stmt.handle, 1) != 0;
        row.room_code = col_text(stmt.handle, 2);
        row.mach_code = col_text(stmt.handle, 3);
        row.mach_name = col_text(stmt.handle, 4);
        row.sample_no = col_text(stmt.handle, 5);
        row.qc_name = col_text(stmt.handle, 6);
        row.level = col_text(stmt.handle, 7);
        row.item_code = col_text(stmt.handle, 8);
        row.item_name = col_text(stmt.handle, 9);
        row.target_mean = col_text(stmt.handle, 10);
        row.target_sd = col_text(stmt.handle, 11);
        rows.push_back(std::move(row));
    }
    return true;
}

bool save_config(Config& row, std::string& error) {
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    const std::string now = now_text();
    Stmt stmt;
    if (row.id > 0) {
        if (!prepare(db.handle,
                     "UPDATE qc_config SET enabled=?,room_code=?,mach_code=?,mach_name=?,sample_no=?,qc_name=?,"
                     "level=?,item_code=?,item_name=?,target_mean=?,target_sd=?,updated_at=? WHERE id=?",
                     stmt, error)) {
            return false;
        }
        sqlite3_bind_int(stmt.handle, 1, row.enabled ? 1 : 0);
        bind_text(stmt.handle, 2, row.room_code);
        bind_text(stmt.handle, 3, row.mach_code);
        bind_text(stmt.handle, 4, row.mach_name);
        bind_text(stmt.handle, 5, row.sample_no);
        bind_text(stmt.handle, 6, row.qc_name);
        bind_text(stmt.handle, 7, row.level);
        bind_text(stmt.handle, 8, row.item_code);
        bind_text(stmt.handle, 9, row.item_name);
        bind_nullable_double(stmt.handle, 10, row.target_mean);
        bind_nullable_double(stmt.handle, 11, row.target_sd);
        bind_text(stmt.handle, 12, now);
        sqlite3_bind_int(stmt.handle, 13, row.id);
    } else {
        if (!prepare(db.handle,
                     "INSERT INTO qc_config(enabled,room_code,mach_code,mach_name,sample_no,qc_name,level,item_code,"
                     "item_name,target_mean,target_sd,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)",
                     stmt, error)) {
            return false;
        }
        sqlite3_bind_int(stmt.handle, 1, row.enabled ? 1 : 0);
        bind_text(stmt.handle, 2, row.room_code);
        bind_text(stmt.handle, 3, row.mach_code);
        bind_text(stmt.handle, 4, row.mach_name);
        bind_text(stmt.handle, 5, row.sample_no);
        bind_text(stmt.handle, 6, row.qc_name);
        bind_text(stmt.handle, 7, row.level);
        bind_text(stmt.handle, 8, row.item_code);
        bind_text(stmt.handle, 9, row.item_name);
        bind_nullable_double(stmt.handle, 10, row.target_mean);
        bind_nullable_double(stmt.handle, 11, row.target_sd);
        bind_text(stmt.handle, 12, now);
        bind_text(stmt.handle, 13, now);
    }
    if (sqlite3_step(stmt.handle) != SQLITE_DONE) {
        error = sqlite_error(db.handle);
        return false;
    }
    if (row.id <= 0) row.id = static_cast<int>(sqlite3_last_insert_rowid(db.handle));
    return true;
}

bool delete_config(int id, std::string& error) {
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    Stmt stmt;
    if (!prepare(db.handle, "DELETE FROM qc_config WHERE id=?", stmt, error)) return false;
    sqlite3_bind_int(stmt.handle, 1, id);
    if (sqlite3_step(stmt.handle) != SQLITE_DONE) {
        error = sqlite_error(db.handle);
        return false;
    }
    return true;
}

bool query_results(const Query& query, std::vector<Result>& rows, std::string& error) {
    rows.clear();
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    std::vector<std::string> binds;
    std::ostringstream sql;
    sql << "SELECT id,source_rep_no,source_entry_key,room_code,mach_code,mach_name,sample_no,barcode_no,"
        << "report_date,inspect_date,report_time,effective_time,chk_flag,conf,item_code,item_name,result_text,"
        << "ifnull(result_value,0),has_numeric_value,unit,normal,qc_name,level,ifnull(target_mean,''),"
        << "ifnull(target_sd,''),imported_at,updated_at FROM qc_result WHERE 1=1";
    if (!search::trim(query.start_date).empty()) {
        sql << " AND effective_time>=?";
        binds.push_back(search::trim(query.start_date));
    }
    if (!search::trim(query.end_date).empty()) {
        sql << " AND effective_time<datetime(?,'+1 day')";
        binds.push_back(search::trim(query.end_date));
    }
    if (!search::trim(query.mach_code).empty()) {
        sql << " AND mach_code=?";
        binds.push_back(search::trim(query.mach_code));
    }
    if (!search::trim(query.item_code).empty()) {
        sql << " AND item_code=?";
        binds.push_back(search::trim(query.item_code));
    }
    if (!search::trim(query.level).empty()) {
        sql << " AND level=?";
        binds.push_back(search::trim(query.level));
    }
    sql << " ORDER BY mach_code,item_code,level,effective_time,source_rep_no";
    Stmt stmt;
    if (!prepare(db.handle, sql.str().c_str(), stmt, error)) return false;
    for (int i = 0; i < static_cast<int>(binds.size()); ++i) {
        bind_text(stmt.handle, i + 1, binds[static_cast<size_t>(i)]);
    }
    while (sqlite3_step(stmt.handle) == SQLITE_ROW) {
        Result row;
        int c = 0;
        row.id = sqlite3_column_int(stmt.handle, c++);
        row.source_rep_no = col_text(stmt.handle, c++);
        row.source_entry_key = col_text(stmt.handle, c++);
        row.room_code = col_text(stmt.handle, c++);
        row.mach_code = col_text(stmt.handle, c++);
        row.mach_name = col_text(stmt.handle, c++);
        row.sample_no = col_text(stmt.handle, c++);
        row.barcode_no = col_text(stmt.handle, c++);
        row.report_date = col_text(stmt.handle, c++);
        row.inspect_date = col_text(stmt.handle, c++);
        row.report_time = col_text(stmt.handle, c++);
        row.effective_time = col_text(stmt.handle, c++);
        row.chk_flag = col_text(stmt.handle, c++);
        row.conf = col_text(stmt.handle, c++);
        row.item_code = col_text(stmt.handle, c++);
        row.item_name = col_text(stmt.handle, c++);
        row.result_text = col_text(stmt.handle, c++);
        row.result_value = sqlite3_column_double(stmt.handle, c++);
        row.has_numeric_value = sqlite3_column_int(stmt.handle, c++) != 0;
        row.unit = col_text(stmt.handle, c++);
        row.normal = col_text(stmt.handle, c++);
        row.qc_name = col_text(stmt.handle, c++);
        row.level = col_text(stmt.handle, c++);
        row.target_mean = col_text(stmt.handle, c++);
        row.target_sd = col_text(stmt.handle, c++);
        row.imported_at = col_text(stmt.handle, c++);
        row.updated_at = col_text(stmt.handle, c++);
        rows.push_back(std::move(row));
    }
    return true;
}

bool upsert_result(Result& row, bool& inserted, std::string& error) {
    inserted = false;
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    const std::string now = now_text();
    row.updated_at = now;
    if (row.imported_at.empty()) row.imported_at = now;
    Stmt stmt;
    if (!prepare(db.handle,
                 "INSERT OR IGNORE INTO qc_result(source_rep_no,source_entry_key,room_code,mach_code,mach_name,"
                 "sample_no,barcode_no,report_date,inspect_date,report_time,effective_time,chk_flag,conf,item_code,"
                 "item_name,result_text,result_value,has_numeric_value,unit,normal,qc_name,level,target_mean,target_sd,"
                 "imported_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                 stmt, error)) {
        return false;
    }
    int i = 1;
    bind_text(stmt.handle, i++, row.source_rep_no);
    bind_text(stmt.handle, i++, row.source_entry_key);
    bind_text(stmt.handle, i++, row.room_code);
    bind_text(stmt.handle, i++, row.mach_code);
    bind_text(stmt.handle, i++, row.mach_name);
    bind_text(stmt.handle, i++, row.sample_no);
    bind_text(stmt.handle, i++, row.barcode_no);
    bind_text(stmt.handle, i++, row.report_date);
    bind_text(stmt.handle, i++, row.inspect_date);
    bind_text(stmt.handle, i++, row.report_time);
    bind_text(stmt.handle, i++, row.effective_time);
    bind_text(stmt.handle, i++, row.chk_flag);
    bind_text(stmt.handle, i++, row.conf);
    bind_text(stmt.handle, i++, row.item_code);
    bind_text(stmt.handle, i++, row.item_name);
    bind_text(stmt.handle, i++, row.result_text);
    if (row.has_numeric_value) sqlite3_bind_double(stmt.handle, i++, row.result_value); else sqlite3_bind_null(stmt.handle, i++);
    sqlite3_bind_int(stmt.handle, i++, row.has_numeric_value ? 1 : 0);
    bind_text(stmt.handle, i++, row.unit);
    bind_text(stmt.handle, i++, row.normal);
    bind_text(stmt.handle, i++, row.qc_name);
    bind_text(stmt.handle, i++, row.level);
    bind_nullable_double(stmt.handle, i++, row.target_mean);
    bind_nullable_double(stmt.handle, i++, row.target_sd);
    bind_text(stmt.handle, i++, row.imported_at);
    bind_text(stmt.handle, i++, row.updated_at);
    if (sqlite3_step(stmt.handle) != SQLITE_DONE) {
        error = sqlite_error(db.handle);
        return false;
    }
    inserted = sqlite3_changes(db.handle) > 0;
    if (inserted) return true;

    Stmt update;
    if (!prepare(db.handle,
                 "UPDATE qc_result SET room_code=?,mach_code=?,mach_name=?,sample_no=?,barcode_no=?,report_date=?,"
                 "inspect_date=?,report_time=?,effective_time=?,chk_flag=?,conf=?,item_code=?,item_name=?,result_text=?,"
                 "result_value=?,has_numeric_value=?,unit=?,normal=?,qc_name=?,level=?,target_mean=?,target_sd=?,"
                 "updated_at=? WHERE source_rep_no=? AND source_entry_key=?",
                 update, error)) {
        return false;
    }
    i = 1;
    bind_text(update.handle, i++, row.room_code);
    bind_text(update.handle, i++, row.mach_code);
    bind_text(update.handle, i++, row.mach_name);
    bind_text(update.handle, i++, row.sample_no);
    bind_text(update.handle, i++, row.barcode_no);
    bind_text(update.handle, i++, row.report_date);
    bind_text(update.handle, i++, row.inspect_date);
    bind_text(update.handle, i++, row.report_time);
    bind_text(update.handle, i++, row.effective_time);
    bind_text(update.handle, i++, row.chk_flag);
    bind_text(update.handle, i++, row.conf);
    bind_text(update.handle, i++, row.item_code);
    bind_text(update.handle, i++, row.item_name);
    bind_text(update.handle, i++, row.result_text);
    if (row.has_numeric_value) sqlite3_bind_double(update.handle, i++, row.result_value); else sqlite3_bind_null(update.handle, i++);
    sqlite3_bind_int(update.handle, i++, row.has_numeric_value ? 1 : 0);
    bind_text(update.handle, i++, row.unit);
    bind_text(update.handle, i++, row.normal);
    bind_text(update.handle, i++, row.qc_name);
    bind_text(update.handle, i++, row.level);
    bind_nullable_double(update.handle, i++, row.target_mean);
    bind_nullable_double(update.handle, i++, row.target_sd);
    bind_text(update.handle, i++, row.updated_at);
    bind_text(update.handle, i++, row.source_rep_no);
    bind_text(update.handle, i++, row.source_entry_key);
    if (sqlite3_step(update.handle) != SQLITE_DONE) {
        error = sqlite_error(db.handle);
        return false;
    }
    return true;
}

bool insert_import_log(const ImportLog& log, std::string& error) {
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    Stmt stmt;
    if (!prepare(db.handle,
                 "INSERT INTO qc_import_log(started_at,finished_at,start_date,end_date,mach_code,imported_count,"
                 "updated_count,skipped_count,status,error) VALUES(?,?,?,?,?,?,?,?,?,?)",
                 stmt, error)) {
        return false;
    }
    bind_text(stmt.handle, 1, log.started_at);
    bind_text(stmt.handle, 2, log.finished_at);
    bind_text(stmt.handle, 3, log.start_date);
    bind_text(stmt.handle, 4, log.end_date);
    bind_text(stmt.handle, 5, log.mach_code);
    sqlite3_bind_int(stmt.handle, 6, log.imported_count);
    sqlite3_bind_int(stmt.handle, 7, log.updated_count);
    sqlite3_bind_int(stmt.handle, 8, log.skipped_count);
    bind_text(stmt.handle, 9, log.status);
    bind_text(stmt.handle, 10, log.error);
    if (sqlite3_step(stmt.handle) != SQLITE_DONE) {
        error = sqlite_error(db.handle);
        return false;
    }
    return true;
}

bool latest_import_log(ImportLog& log, bool& found, std::string& error) {
    found = false;
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    Stmt stmt;
    if (!prepare(db.handle,
                 "SELECT id,started_at,finished_at,start_date,end_date,mach_code,imported_count,updated_count,"
                 "skipped_count,status,error FROM qc_import_log ORDER BY id DESC LIMIT 1",
                 stmt, error)) {
        return false;
    }
    if (sqlite3_step(stmt.handle) == SQLITE_ROW) {
        found = true;
        log.id = sqlite3_column_int(stmt.handle, 0);
        log.started_at = col_text(stmt.handle, 1);
        log.finished_at = col_text(stmt.handle, 2);
        log.start_date = col_text(stmt.handle, 3);
        log.end_date = col_text(stmt.handle, 4);
        log.mach_code = col_text(stmt.handle, 5);
        log.imported_count = sqlite3_column_int(stmt.handle, 6);
        log.updated_count = sqlite3_column_int(stmt.handle, 7);
        log.skipped_count = sqlite3_column_int(stmt.handle, 8);
        log.status = col_text(stmt.handle, 9);
        log.error = col_text(stmt.handle, 10);
    }
    return true;
}

}  // namespace qc

#endif
