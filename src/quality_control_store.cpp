#include "quality_control_store.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "search_text.h"
#include "sqlite3.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

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
    std::wstring value = search::load_module_str(L"QualityControl", L"LocalCachePath", L"");
    if (value.empty()) {
        value = search::load_module_str(L"QualityControl", L"LocalDbPath", L"quality_control.sqlite");
    }
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

}  // namespace qc

#endif
