#include "quality_control_store.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "search_text.h"
#include "sqlite3.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

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

std::string previous_date(const std::string& date) {
    int y = 0;
    int m = 0;
    int d = 0;
    if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return date;
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    tm.tm_hour = 12;
    const std::time_t value = std::mktime(&tm);
    if (value == static_cast<std::time_t>(-1)) return date;
    std::time_t prev = value - 24 * 60 * 60;
    std::tm out{};
    localtime_s(&out, &prev);
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", out.tm_year + 1900, out.tm_mon + 1, out.tm_mday);
    return buffer;
}

bool table_exists(sqlite3* db, const std::string& name, std::string& error) {
    Stmt stmt;
    if (!prepare(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", stmt, error)) return false;
    bind_text(stmt.handle, 1, name);
    return sqlite3_step(stmt.handle) == SQLITE_ROW;
}

bool table_has_column(sqlite3* db, const std::string& table, const std::string& column, std::string& error) {
    Stmt stmt;
    const std::string sql = "PRAGMA table_info(" + table + ")";
    if (!prepare(db, sql.c_str(), stmt, error)) return false;
    while (sqlite3_step(stmt.handle) == SQLITE_ROW) {
        if (col_text(stmt.handle, 1) == column) return true;
    }
    return false;
}

bool ensure_schema(sqlite3* db, std::string& error) {
    if (!exec_sql(db, "PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;", error)) return false;
    if (table_exists(db, "qc_lot", error) && !table_has_column(db, "qc_lot", "sample_config_id", error)) {
        if (!table_exists(db, "qc_lot_legacy", error)) {
            if (!exec_sql(db, "ALTER TABLE qc_lot RENAME TO qc_lot_legacy;", error)) return false;
        }
    }
    constexpr const char* sql = R"SQL(
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
CREATE TABLE IF NOT EXISTS qc_sample_config (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  enabled INTEGER NOT NULL DEFAULT 1,
  room_code TEXT,
  mach_code TEXT NOT NULL,
  mach_name TEXT,
  sample_no TEXT NOT NULL,
  qc_name TEXT,
  level TEXT,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  UNIQUE(mach_code, sample_no, level)
);
CREATE TABLE IF NOT EXISTS qc_sample_item (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  sample_config_id INTEGER NOT NULL,
  enabled INTEGER NOT NULL DEFAULT 1,
  item_code TEXT NOT NULL,
  item_name TEXT,
  item_eng TEXT,
  unit TEXT,
  sort_order INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  FOREIGN KEY(sample_config_id) REFERENCES qc_sample_config(id) ON DELETE CASCADE,
  UNIQUE(sample_config_id, item_code)
);
CREATE TABLE IF NOT EXISTS qc_lot (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  sample_config_id INTEGER NOT NULL,
  enabled INTEGER NOT NULL DEFAULT 1,
  lot_no TEXT NOT NULL,
  valid_from TEXT NOT NULL,
  valid_to TEXT,
  note TEXT,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  FOREIGN KEY(sample_config_id) REFERENCES qc_sample_config(id) ON DELETE CASCADE,
  UNIQUE(sample_config_id, lot_no, valid_from)
);
CREATE TABLE IF NOT EXISTS qc_lot_item_target (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  lot_id INTEGER NOT NULL,
  sample_item_id INTEGER NOT NULL,
  target_mean REAL,
  target_sd REAL,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  FOREIGN KEY(lot_id) REFERENCES qc_lot(id) ON DELETE CASCADE,
  FOREIGN KEY(sample_item_id) REFERENCES qc_sample_item(id) ON DELETE CASCADE,
  UNIQUE(lot_id, sample_item_id)
);
CREATE TABLE IF NOT EXISTS qc_result_cache (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  source_rep_no TEXT NOT NULL,
  source_entry_key TEXT NOT NULL,
  room_code TEXT,
  mach_code TEXT NOT NULL,
  mach_name TEXT,
  sample_no TEXT NOT NULL,
  barcode_no TEXT,
  tester_name TEXT,
  report_date TEXT,
  inspect_date TEXT,
  report_time TEXT,
  effective_time TEXT NOT NULL,
  chk_flag TEXT,
  conf TEXT,
  item_code TEXT NOT NULL,
  item_name TEXT,
  item_eng TEXT,
  result_text TEXT,
  result_value REAL,
  has_numeric_value INTEGER NOT NULL DEFAULT 0,
  unit TEXT,
  normal TEXT,
  qc_name TEXT,
  level TEXT,
  lot_no TEXT,
  target_mean REAL,
  target_sd REAL,
  cache_key TEXT,
  deleted_in_lis INTEGER NOT NULL DEFAULT 0,
  last_seen_at TEXT,
  cached_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  UNIQUE(source_rep_no, source_entry_key)
);
CREATE TABLE IF NOT EXISTS qc_query_cache_meta (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  cache_key TEXT NOT NULL UNIQUE,
  mach_code TEXT NOT NULL,
  start_date TEXT NOT NULL,
  end_date TEXT NOT NULL,
  level TEXT,
  item_scope TEXT,
  sample_scope TEXT,
  row_count INTEGER NOT NULL DEFAULT 0,
  latest_effective_time TEXT,
  max_entry_id TEXT,
  cached_at TEXT NOT NULL,
  refreshed_at TEXT NOT NULL,
  source TEXT NOT NULL DEFAULT 'LIS'
);
CREATE INDEX IF NOT EXISTS idx_qc_lot_config_dates ON qc_lot(sample_config_id, valid_from, valid_to);
CREATE INDEX IF NOT EXISTS idx_qc_sample_config_lookup ON qc_sample_config(mach_code, sample_no, level);
CREATE INDEX IF NOT EXISTS idx_qc_sample_item_lookup ON qc_sample_item(sample_config_id, item_code);
CREATE INDEX IF NOT EXISTS idx_qc_lot_item_target_lookup ON qc_lot_item_target(lot_id, sample_item_id);
CREATE INDEX IF NOT EXISTS idx_qc_result_cache_key ON qc_result_cache(cache_key, deleted_in_lis, effective_time);
CREATE INDEX IF NOT EXISTS idx_qc_query_cache_meta_key ON qc_query_cache_meta(cache_key);
)SQL";
    if (!exec_sql(db, sql, error)) return false;

    constexpr const char* migrate_config = R"SQL(
INSERT OR IGNORE INTO qc_sample_config(enabled,room_code,mach_code,mach_name,sample_no,qc_name,level,created_at,updated_at)
SELECT max(enabled),room_code,mach_code,max(mach_name),sample_no,max(qc_name),ifnull(level,''),min(created_at),max(updated_at)
FROM qc_config
GROUP BY room_code,mach_code,sample_no,ifnull(level,'');

INSERT OR IGNORE INTO qc_sample_item(sample_config_id,enabled,item_code,item_name,item_eng,unit,sort_order,created_at,updated_at)
SELECT sc.id,max(c.enabled),ifnull(c.item_code,''),max(c.item_name),'','',0,min(c.created_at),max(c.updated_at)
FROM qc_config c
JOIN qc_sample_config sc
  ON sc.mach_code=c.mach_code
 AND sc.sample_no=c.sample_no
 AND ifnull(sc.level,'')=ifnull(c.level,'')
WHERE ifnull(c.item_code,'')<>''
GROUP BY sc.id,ifnull(c.item_code,'');
)SQL";

    constexpr const char* migrate_lot = R"SQL(

INSERT OR IGNORE INTO qc_lot(sample_config_id,enabled,lot_no,valid_from,valid_to,note,created_at,updated_at)
SELECT sc.id,max(l.enabled),l.lot_no,l.valid_from,l.valid_to,max(ifnull(l.note,'')),min(l.created_at),max(l.updated_at)
FROM qc_lot_legacy l
JOIN qc_config c ON c.id=l.config_id
JOIN qc_sample_config sc
  ON sc.mach_code=c.mach_code
 AND sc.sample_no=c.sample_no
 AND ifnull(sc.level,'')=ifnull(c.level,'')
GROUP BY sc.id,l.lot_no,l.valid_from,ifnull(l.valid_to,'');

INSERT OR IGNORE INTO qc_lot_item_target(lot_id,sample_item_id,target_mean,target_sd,created_at,updated_at)
SELECT nl.id,si.id,l.target_mean,l.target_sd,l.created_at,l.updated_at
FROM qc_lot_legacy l
JOIN qc_config c ON c.id=l.config_id
JOIN qc_sample_config sc
  ON sc.mach_code=c.mach_code
 AND sc.sample_no=c.sample_no
 AND ifnull(sc.level,'')=ifnull(c.level,'')
JOIN qc_sample_item si
  ON si.sample_config_id=sc.id
 AND ifnull(si.item_code,'')=ifnull(c.item_code,'')
JOIN qc_lot nl
  ON nl.sample_config_id=sc.id
 AND nl.lot_no=l.lot_no
 AND nl.valid_from=l.valid_from
 AND ifnull(nl.valid_to,'')=ifnull(l.valid_to,'')
WHERE ifnull(c.item_code,'')<>'';
)SQL";
    if (!exec_sql(db, migrate_config, error)) return false;
    if (table_exists(db, "qc_lot_legacy", error)) {
        if (!exec_sql(db, migrate_lot, error)) return false;
    }
    return true;
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
                 "SELECT si.id,sc.id,ifnull(l.id,0),sc.enabled,sc.room_code,sc.mach_code,sc.mach_name,sc.sample_no,"
                 "sc.qc_name,sc.level,si.item_code,si.item_name,si.item_eng,si.unit,ifnull(l.lot_no,''),"
                 "ifnull(l.valid_from,''),ifnull(l.valid_to,''),ifnull(t.target_mean,''),ifnull(t.target_sd,'') "
                 "FROM qc_sample_config sc "
                 "JOIN qc_sample_item si ON si.sample_config_id=sc.id "
                 "LEFT JOIN qc_lot l ON l.sample_config_id=sc.id AND l.enabled=1 AND ifnull(l.valid_to,'')='' "
                 "LEFT JOIN qc_lot_item_target t ON t.lot_id=l.id AND t.sample_item_id=si.id "
                 "WHERE si.enabled=1 "
                 "ORDER BY sc.mach_code,sc.sample_no,si.sort_order,si.item_code,sc.level",
                 stmt, error)) {
        return false;
    }
    while (sqlite3_step(stmt.handle) == SQLITE_ROW) {
        Config row;
        row.id = sqlite3_column_int(stmt.handle, 0);
        row.sample_config_id = sqlite3_column_int(stmt.handle, 1);
        row.sample_item_id = row.id;
        row.lot_id = sqlite3_column_int(stmt.handle, 2);
        row.enabled = sqlite3_column_int(stmt.handle, 3) != 0;
        row.room_code = col_text(stmt.handle, 4);
        row.mach_code = col_text(stmt.handle, 5);
        row.mach_name = col_text(stmt.handle, 6);
        row.sample_no = col_text(stmt.handle, 7);
        row.qc_name = col_text(stmt.handle, 8);
        row.level = col_text(stmt.handle, 9);
        row.item_code = col_text(stmt.handle, 10);
        row.item_name = col_text(stmt.handle, 11);
        row.item_eng = col_text(stmt.handle, 12);
        row.unit = col_text(stmt.handle, 13);
        row.lot_no = col_text(stmt.handle, 14);
        row.lot_valid_from = col_text(stmt.handle, 15);
        row.lot_valid_to = col_text(stmt.handle, 16);
        row.target_mean = col_text(stmt.handle, 17);
        row.target_sd = col_text(stmt.handle, 18);
        rows.push_back(std::move(row));
    }
    return true;
}

bool load_analysis_configs(std::vector<Config>& rows, std::string& error) {
    rows.clear();
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    Stmt stmt;
    if (!prepare(db.handle,
                 "SELECT si.id,sc.id,ifnull(l.id,0),sc.enabled,sc.room_code,sc.mach_code,sc.mach_name,sc.sample_no,"
                 "sc.qc_name,sc.level,si.item_code,si.item_name,si.item_eng,si.unit,ifnull(l.lot_no,''),"
                 "ifnull(l.valid_from,''),ifnull(l.valid_to,''),ifnull(t.target_mean,''),ifnull(t.target_sd,'') "
                 "FROM qc_sample_config sc "
                 "JOIN qc_sample_item si ON si.sample_config_id=sc.id "
                 "LEFT JOIN qc_lot l ON l.sample_config_id=sc.id AND l.enabled=1 "
                 "LEFT JOIN qc_lot_item_target t ON t.lot_id=l.id AND t.sample_item_id=si.id "
                 "WHERE si.enabled=1 "
                 "ORDER BY sc.mach_code,sc.sample_no,si.sort_order,si.item_code,sc.level,l.valid_from DESC",
                 stmt, error)) {
        return false;
    }
    while (sqlite3_step(stmt.handle) == SQLITE_ROW) {
        Config row;
        row.id = sqlite3_column_int(stmt.handle, 0);
        row.sample_item_id = row.id;
        row.sample_config_id = sqlite3_column_int(stmt.handle, 1);
        row.lot_id = sqlite3_column_int(stmt.handle, 2);
        row.enabled = sqlite3_column_int(stmt.handle, 3) != 0;
        row.room_code = col_text(stmt.handle, 4);
        row.mach_code = col_text(stmt.handle, 5);
        row.mach_name = col_text(stmt.handle, 6);
        row.sample_no = col_text(stmt.handle, 7);
        row.qc_name = col_text(stmt.handle, 8);
        row.level = col_text(stmt.handle, 9);
        row.item_code = col_text(stmt.handle, 10);
        row.item_name = col_text(stmt.handle, 11);
        row.item_eng = col_text(stmt.handle, 12);
        row.unit = col_text(stmt.handle, 13);
        row.lot_no = col_text(stmt.handle, 14);
        row.lot_valid_from = col_text(stmt.handle, 15);
        row.lot_valid_to = col_text(stmt.handle, 16);
        row.target_mean = col_text(stmt.handle, 17);
        row.target_sd = col_text(stmt.handle, 18);
        rows.push_back(std::move(row));
    }
    return true;
}

bool save_config(Config& row, std::string& error) {
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    const std::string now = now_text();
    int sampleConfigId = row.sample_config_id;
    if (sampleConfigId <= 0 && row.id > 0) {
        Stmt find;
        if (!prepare(db.handle, "SELECT sample_config_id FROM qc_sample_item WHERE id=?", find, error)) return false;
        sqlite3_bind_int(find.handle, 1, row.id);
        if (sqlite3_step(find.handle) == SQLITE_ROW) sampleConfigId = sqlite3_column_int(find.handle, 0);
    }
    if (sampleConfigId > 0) {
        Stmt stmt;
        if (!prepare(db.handle,
                     "UPDATE qc_sample_config SET enabled=?,room_code=?,mach_code=?,mach_name=?,sample_no=?,"
                     "qc_name=?,level=?,updated_at=? WHERE id=?",
                     stmt, error)) return false;
        sqlite3_bind_int(stmt.handle, 1, row.enabled ? 1 : 0);
        bind_text(stmt.handle, 2, row.room_code);
        bind_text(stmt.handle, 3, row.mach_code);
        bind_text(stmt.handle, 4, row.mach_name);
        bind_text(stmt.handle, 5, row.sample_no);
        bind_text(stmt.handle, 6, row.qc_name);
        bind_text(stmt.handle, 7, row.level);
        bind_text(stmt.handle, 8, now);
        sqlite3_bind_int(stmt.handle, 9, sampleConfigId);
        if (sqlite3_step(stmt.handle) != SQLITE_DONE) {
            error = sqlite_error(db.handle);
            return false;
        }
    } else {
        Stmt stmt;
        if (!prepare(db.handle,
                     "INSERT INTO qc_sample_config(enabled,room_code,mach_code,mach_name,sample_no,qc_name,level,"
                     "created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?)",
                     stmt, error)) return false;
        sqlite3_bind_int(stmt.handle, 1, row.enabled ? 1 : 0);
        bind_text(stmt.handle, 2, row.room_code);
        bind_text(stmt.handle, 3, row.mach_code);
        bind_text(stmt.handle, 4, row.mach_name);
        bind_text(stmt.handle, 5, row.sample_no);
        bind_text(stmt.handle, 6, row.qc_name);
        bind_text(stmt.handle, 7, row.level);
        bind_text(stmt.handle, 8, now);
        bind_text(stmt.handle, 9, now);
        if (sqlite3_step(stmt.handle) != SQLITE_DONE) {
            error = sqlite_error(db.handle);
            return false;
        }
        sampleConfigId = static_cast<int>(sqlite3_last_insert_rowid(db.handle));
    }

    if (row.id > 0) {
        Stmt stmt;
        if (!prepare(db.handle,
                     "UPDATE qc_sample_item SET enabled=1,item_code=?,item_name=?,item_eng=?,unit=?,updated_at=? WHERE id=?",
                     stmt, error)) return false;
        bind_text(stmt.handle, 1, row.item_code);
        bind_text(stmt.handle, 2, row.item_name);
        bind_text(stmt.handle, 3, row.item_eng);
        bind_text(stmt.handle, 4, row.unit);
        bind_text(stmt.handle, 5, now);
        sqlite3_bind_int(stmt.handle, 6, row.id);
        if (sqlite3_step(stmt.handle) != SQLITE_DONE) {
            error = sqlite_error(db.handle);
            return false;
        }
    } else if (!search::trim(row.item_code).empty()) {
        Stmt stmt;
        if (!prepare(db.handle,
                     "INSERT OR IGNORE INTO qc_sample_item(sample_config_id,enabled,item_code,item_name,item_eng,unit,"
                     "sort_order,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?)",
                     stmt, error)) return false;
        sqlite3_bind_int(stmt.handle, 1, sampleConfigId);
        sqlite3_bind_int(stmt.handle, 2, 1);
        bind_text(stmt.handle, 3, row.item_code);
        bind_text(stmt.handle, 4, row.item_name);
        bind_text(stmt.handle, 5, row.item_eng);
        bind_text(stmt.handle, 6, row.unit);
        sqlite3_bind_int(stmt.handle, 7, 0);
        bind_text(stmt.handle, 8, now);
        bind_text(stmt.handle, 9, now);
        if (sqlite3_step(stmt.handle) != SQLITE_DONE) {
            error = sqlite_error(db.handle);
            return false;
        }
        row.id = static_cast<int>(sqlite3_last_insert_rowid(db.handle));
        if (row.id <= 0) {
            Stmt find;
            if (!prepare(db.handle, "SELECT id FROM qc_sample_item WHERE sample_config_id=? AND item_code=?", find, error)) return false;
            sqlite3_bind_int(find.handle, 1, sampleConfigId);
            bind_text(find.handle, 2, row.item_code);
            if (sqlite3_step(find.handle) == SQLITE_ROW) row.id = sqlite3_column_int(find.handle, 0);
        }
    }
    row.sample_config_id = sampleConfigId;
    row.sample_item_id = row.id;
    return true;
}

bool delete_config(int id, std::string& error) {
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    exec_sql(db.handle, "PRAGMA foreign_keys=ON;", error);
    Stmt stmt;
    if (!prepare(db.handle, "DELETE FROM qc_sample_item WHERE id=?", stmt, error)) return false;
    sqlite3_bind_int(stmt.handle, 1, id);
    if (sqlite3_step(stmt.handle) != SQLITE_DONE) {
        error = sqlite_error(db.handle);
        return false;
    }
    return true;
}

bool load_sample_configs(std::vector<SampleConfig>& rows, std::string& error) {
    rows.clear();
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    Stmt stmt;
    if (!prepare(db.handle,
                 "SELECT id,enabled,room_code,mach_code,mach_name,sample_no,qc_name,level "
                 "FROM qc_sample_config ORDER BY mach_code,sample_no,level",
                 stmt, error)) return false;
    while (sqlite3_step(stmt.handle) == SQLITE_ROW) {
        SampleConfig row;
        row.id = sqlite3_column_int(stmt.handle, 0);
        row.enabled = sqlite3_column_int(stmt.handle, 1) != 0;
        row.room_code = col_text(stmt.handle, 2);
        row.mach_code = col_text(stmt.handle, 3);
        row.mach_name = col_text(stmt.handle, 4);
        row.sample_no = col_text(stmt.handle, 5);
        row.qc_name = col_text(stmt.handle, 6);
        row.level = col_text(stmt.handle, 7);
        rows.push_back(std::move(row));
    }
    return true;
}

bool save_sample_config(SampleConfig& row, std::string& error) {
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    const std::string now = now_text();
    if (row.id <= 0) {
        Stmt find;
        if (!prepare(db.handle,
                     "SELECT id FROM qc_sample_config WHERE mach_code=? AND sample_no=? AND level=?",
                     find, error)) return false;
        bind_text(find.handle, 1, search::trim(row.mach_code));
        bind_text(find.handle, 2, search::trim(row.sample_no));
        bind_text(find.handle, 3, search::trim(row.level));
        if (sqlite3_step(find.handle) == SQLITE_ROW) row.id = sqlite3_column_int(find.handle, 0);
    }
    Stmt stmt;
    if (row.id > 0) {
        if (!prepare(db.handle,
                     "UPDATE qc_sample_config SET enabled=?,room_code=?,mach_code=?,mach_name=?,sample_no=?,"
                     "qc_name=?,level=?,updated_at=? WHERE id=?",
                     stmt, error)) return false;
        sqlite3_bind_int(stmt.handle, 9, row.id);
    } else {
        if (!prepare(db.handle,
                     "INSERT INTO qc_sample_config(enabled,room_code,mach_code,mach_name,sample_no,qc_name,level,"
                     "updated_at,created_at) VALUES(?,?,?,?,?,?,?,?,?)",
                     stmt, error)) return false;
        bind_text(stmt.handle, 9, now);
    }
    sqlite3_bind_int(stmt.handle, 1, row.enabled ? 1 : 0);
    bind_text(stmt.handle, 2, row.room_code);
    bind_text(stmt.handle, 3, row.mach_code);
    bind_text(stmt.handle, 4, row.mach_name);
    bind_text(stmt.handle, 5, row.sample_no);
    bind_text(stmt.handle, 6, row.qc_name);
    bind_text(stmt.handle, 7, row.level);
    bind_text(stmt.handle, 8, now);
    if (sqlite3_step(stmt.handle) != SQLITE_DONE) {
        error = sqlite_error(db.handle);
        return false;
    }
    if (row.id <= 0) row.id = static_cast<int>(sqlite3_last_insert_rowid(db.handle));
    return true;
}

bool delete_sample_config(int id, std::string& error) {
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    Stmt stmt;
    if (!prepare(db.handle, "DELETE FROM qc_sample_config WHERE id=?", stmt, error)) return false;
    sqlite3_bind_int(stmt.handle, 1, id);
    if (sqlite3_step(stmt.handle) != SQLITE_DONE) {
        error = sqlite_error(db.handle);
        return false;
    }
    return true;
}

bool load_sample_items(int sample_config_id, std::vector<SampleItem>& rows, std::string& error) {
    rows.clear();
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    Stmt stmt;
    if (!prepare(db.handle,
                 "SELECT id,sample_config_id,enabled,item_code,item_name,item_eng,unit,sort_order "
                 "FROM qc_sample_item WHERE sample_config_id=? ORDER BY sort_order,item_code",
                 stmt, error)) return false;
    sqlite3_bind_int(stmt.handle, 1, sample_config_id);
    while (sqlite3_step(stmt.handle) == SQLITE_ROW) {
        SampleItem row;
        row.id = sqlite3_column_int(stmt.handle, 0);
        row.sample_config_id = sqlite3_column_int(stmt.handle, 1);
        row.enabled = sqlite3_column_int(stmt.handle, 2) != 0;
        row.item_code = col_text(stmt.handle, 3);
        row.item_name = col_text(stmt.handle, 4);
        row.item_eng = col_text(stmt.handle, 5);
        row.unit = col_text(stmt.handle, 6);
        row.sort_order = sqlite3_column_int(stmt.handle, 7);
        rows.push_back(std::move(row));
    }
    return true;
}

bool save_sample_item(SampleItem& row, std::string& error) {
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    const std::string now = now_text();
    Stmt stmt;
    if (row.id > 0) {
        if (!prepare(db.handle,
                     "UPDATE qc_sample_item SET enabled=?,item_code=?,item_name=?,item_eng=?,unit=?,sort_order=?,"
                     "updated_at=? WHERE id=?",
                     stmt, error)) return false;
        sqlite3_bind_int(stmt.handle, 8, row.id);
    } else {
        if (!prepare(db.handle,
                     "INSERT OR IGNORE INTO qc_sample_item(sample_config_id,enabled,item_code,item_name,item_eng,unit,"
                     "sort_order,updated_at,created_at) VALUES(?,?,?,?,?,?,?,?,?)",
                     stmt, error)) return false;
        sqlite3_bind_int(stmt.handle, 1, row.sample_config_id);
        bind_text(stmt.handle, 9, now);
    }
    if (row.id > 0) {
        sqlite3_bind_int(stmt.handle, 1, row.enabled ? 1 : 0);
        bind_text(stmt.handle, 2, row.item_code);
        bind_text(stmt.handle, 3, row.item_name);
        bind_text(stmt.handle, 4, row.item_eng);
        bind_text(stmt.handle, 5, row.unit);
        sqlite3_bind_int(stmt.handle, 6, row.sort_order);
        bind_text(stmt.handle, 7, now);
    } else {
        sqlite3_bind_int(stmt.handle, 2, row.enabled ? 1 : 0);
        bind_text(stmt.handle, 3, row.item_code);
        bind_text(stmt.handle, 4, row.item_name);
        bind_text(stmt.handle, 5, row.item_eng);
        bind_text(stmt.handle, 6, row.unit);
        sqlite3_bind_int(stmt.handle, 7, row.sort_order);
        bind_text(stmt.handle, 8, now);
    }
    if (sqlite3_step(stmt.handle) != SQLITE_DONE) {
        error = sqlite_error(db.handle);
        return false;
    }
    if (row.id <= 0) {
        row.id = static_cast<int>(sqlite3_last_insert_rowid(db.handle));
        Stmt find;
        if (!prepare(db.handle,
                     "SELECT id FROM qc_sample_item WHERE sample_config_id=? AND item_code=?",
                     find, error)) return false;
        sqlite3_bind_int(find.handle, 1, row.sample_config_id);
        bind_text(find.handle, 2, search::trim(row.item_code));
        if (sqlite3_step(find.handle) == SQLITE_ROW) row.id = sqlite3_column_int(find.handle, 0);
    }
    return true;
}

bool delete_sample_item(int id, std::string& error) {
    return delete_config(id, error);
}

bool load_lots(std::vector<Lot>& rows, std::string& error) {
    rows.clear();
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    Stmt stmt;
    if (!prepare(db.handle,
                 "SELECT id,sample_config_id,enabled,lot_no,valid_from,ifnull(valid_to,''),ifnull(note,'') "
                 "FROM qc_lot ORDER BY sample_config_id,valid_from DESC,id DESC",
                 stmt, error)) {
        return false;
    }
    while (sqlite3_step(stmt.handle) == SQLITE_ROW) {
        Lot row;
        row.id = sqlite3_column_int(stmt.handle, 0);
        row.sample_config_id = sqlite3_column_int(stmt.handle, 1);
        row.config_id = row.sample_config_id;
        row.enabled = sqlite3_column_int(stmt.handle, 2) != 0;
        row.lot_no = col_text(stmt.handle, 3);
        row.valid_from = col_text(stmt.handle, 4);
        row.valid_to = col_text(stmt.handle, 5);
        row.note = col_text(stmt.handle, 6);
        rows.push_back(std::move(row));
    }
    return true;
}

bool load_lots_for_config(int config_id, std::vector<Lot>& rows, std::string& error) {
    int sampleConfigId = 0;
    {
        Db db;
        if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
        Stmt stmt;
        if (!prepare(db.handle, "SELECT sample_config_id FROM qc_sample_item WHERE id=?", stmt, error)) return false;
        sqlite3_bind_int(stmt.handle, 1, config_id);
        if (sqlite3_step(stmt.handle) == SQLITE_ROW) sampleConfigId = sqlite3_column_int(stmt.handle, 0);
    }
    return load_lots_for_sample_config(sampleConfigId > 0 ? sampleConfigId : config_id, rows, error);
}

bool load_lots_for_sample_config(int sample_config_id, std::vector<Lot>& rows, std::string& error) {
    rows.clear();
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    Stmt stmt;
    if (!prepare(db.handle,
                 "SELECT id,sample_config_id,enabled,lot_no,valid_from,ifnull(valid_to,''),ifnull(note,'') "
                 "FROM qc_lot WHERE sample_config_id=? ORDER BY valid_from DESC,id DESC",
                 stmt, error)) {
        return false;
    }
    sqlite3_bind_int(stmt.handle, 1, sample_config_id);
    while (sqlite3_step(stmt.handle) == SQLITE_ROW) {
        Lot row;
        row.id = sqlite3_column_int(stmt.handle, 0);
        row.sample_config_id = sqlite3_column_int(stmt.handle, 1);
        row.config_id = row.sample_config_id;
        row.enabled = sqlite3_column_int(stmt.handle, 2) != 0;
        row.lot_no = col_text(stmt.handle, 3);
        row.valid_from = col_text(stmt.handle, 4);
        row.valid_to = col_text(stmt.handle, 5);
        row.note = col_text(stmt.handle, 6);
        rows.push_back(std::move(row));
    }
    return true;
}

bool close_previous_open_lots(sqlite3* db, const Lot& row, const std::string& now, std::string& error) {
    const int sampleConfigId = row.sample_config_id > 0 ? row.sample_config_id : row.config_id;
    if (sampleConfigId <= 0 || search::trim(row.valid_from).empty()) return true;
    Stmt stmt;
    if (!prepare(db,
                 "UPDATE qc_lot SET valid_to=?,updated_at=? WHERE sample_config_id=? AND id<>? "
                 "AND enabled=1 AND (valid_to IS NULL OR valid_to='') AND valid_from<?",
                 stmt, error)) {
        return false;
    }
    bind_text(stmt.handle, 1, previous_date(search::trim(row.valid_from)));
    bind_text(stmt.handle, 2, now);
    sqlite3_bind_int(stmt.handle, 3, sampleConfigId);
    sqlite3_bind_int(stmt.handle, 4, row.id);
    bind_text(stmt.handle, 5, search::trim(row.valid_from));
    if (sqlite3_step(stmt.handle) != SQLITE_DONE) {
        error = sqlite_error(db);
        return false;
    }
    return true;
}

bool save_lot(Lot& row, std::string& error) {
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    row.lot_no = search::trim(row.lot_no);
    row.valid_from = search::trim(row.valid_from);
    row.valid_to = search::trim(row.valid_to);
    int sampleItemId = 0;
    int sampleConfigId = row.sample_config_id;
    if (sampleConfigId <= 0 && row.config_id > 0) {
        Stmt find;
        if (!prepare(db.handle, "SELECT sample_config_id FROM qc_sample_item WHERE id=?", find, error)) return false;
        sqlite3_bind_int(find.handle, 1, row.config_id);
        if (sqlite3_step(find.handle) == SQLITE_ROW) {
            sampleItemId = row.config_id;
            sampleConfigId = sqlite3_column_int(find.handle, 0);
        } else {
            sampleConfigId = row.config_id;
        }
    }
    if (sampleConfigId <= 0 || row.lot_no.empty() || row.valid_from.empty()) {
        error = "质控批号、开始日期和配置编号不能为空";
        return false;
    }
    row.sample_config_id = sampleConfigId;
    const std::string now = now_text();
    if (!close_previous_open_lots(db.handle, row, now, error)) return false;
    Stmt stmt;
    if (row.id > 0) {
        if (!prepare(db.handle,
                     "UPDATE qc_lot SET enabled=?,lot_no=?,valid_from=?,valid_to=?,note=?,updated_at=? WHERE id=?",
                     stmt, error)) {
            return false;
        }
        sqlite3_bind_int(stmt.handle, 1, row.enabled ? 1 : 0);
        bind_text(stmt.handle, 2, row.lot_no);
        bind_text(stmt.handle, 3, row.valid_from);
        if (row.valid_to.empty()) sqlite3_bind_null(stmt.handle, 4); else bind_text(stmt.handle, 4, row.valid_to);
        bind_text(stmt.handle, 5, row.note);
        bind_text(stmt.handle, 6, now);
        sqlite3_bind_int(stmt.handle, 7, row.id);
    } else {
        if (!prepare(db.handle,
                     "INSERT OR IGNORE INTO qc_lot(sample_config_id,enabled,lot_no,valid_from,valid_to,note,"
                     "created_at,updated_at) VALUES(?,?,?,?,?,?,?,?)",
                     stmt, error)) {
            return false;
        }
        sqlite3_bind_int(stmt.handle, 1, sampleConfigId);
        sqlite3_bind_int(stmt.handle, 2, row.enabled ? 1 : 0);
        bind_text(stmt.handle, 3, row.lot_no);
        bind_text(stmt.handle, 4, row.valid_from);
        if (row.valid_to.empty()) sqlite3_bind_null(stmt.handle, 5); else bind_text(stmt.handle, 5, row.valid_to);
        bind_text(stmt.handle, 6, row.note);
        bind_text(stmt.handle, 7, now);
        bind_text(stmt.handle, 8, now);
    }
    if (sqlite3_step(stmt.handle) != SQLITE_DONE) {
        error = sqlite_error(db.handle);
        return false;
    }
    if (row.id <= 0) row.id = static_cast<int>(sqlite3_last_insert_rowid(db.handle));
    if (row.id <= 0) {
        Stmt find;
        if (!prepare(db.handle,
                     "SELECT id FROM qc_lot WHERE sample_config_id=? AND lot_no=? AND valid_from=?",
                     find, error)) return false;
        sqlite3_bind_int(find.handle, 1, sampleConfigId);
        bind_text(find.handle, 2, row.lot_no);
        bind_text(find.handle, 3, row.valid_from);
        if (sqlite3_step(find.handle) == SQLITE_ROW) row.id = sqlite3_column_int(find.handle, 0);
    }
    if (sampleItemId > 0) {
        LotItemTarget target;
        target.lot_id = row.id;
        target.sample_item_id = sampleItemId;
        target.target_mean = row.target_mean;
        target.target_sd = row.target_sd;
        if (!save_lot_item_target(target, error)) return false;
    }
    return true;
}

bool delete_lot(int id, std::string& error) {
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    Stmt stmt;
    if (!prepare(db.handle, "DELETE FROM qc_lot WHERE id=?", stmt, error)) return false;
    sqlite3_bind_int(stmt.handle, 1, id);
    if (sqlite3_step(stmt.handle) != SQLITE_DONE) {
        error = sqlite_error(db.handle);
        return false;
    }
    return true;
}

bool load_lot_item_targets(int lot_id, std::vector<LotItemTarget>& rows, std::string& error) {
    rows.clear();
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    Stmt stmt;
    if (!prepare(db.handle,
                 "SELECT id,lot_id,sample_item_id,ifnull(target_mean,''),ifnull(target_sd,'') "
                 "FROM qc_lot_item_target WHERE lot_id=? ORDER BY sample_item_id",
                 stmt, error)) return false;
    sqlite3_bind_int(stmt.handle, 1, lot_id);
    while (sqlite3_step(stmt.handle) == SQLITE_ROW) {
        LotItemTarget row;
        row.id = sqlite3_column_int(stmt.handle, 0);
        row.lot_id = sqlite3_column_int(stmt.handle, 1);
        row.sample_item_id = sqlite3_column_int(stmt.handle, 2);
        row.target_mean = col_text(stmt.handle, 3);
        row.target_sd = col_text(stmt.handle, 4);
        rows.push_back(std::move(row));
    }
    return true;
}

bool save_lot_item_target(LotItemTarget& row, std::string& error) {
    Db db;
    if (!open_db(db, error) || !ensure_schema(db.handle, error)) return false;
    const std::string now = now_text();
    Stmt stmt;
    if (row.id > 0) {
        if (!prepare(db.handle,
                     "UPDATE qc_lot_item_target SET target_mean=?,target_sd=?,updated_at=? WHERE id=?",
                     stmt, error)) return false;
        bind_nullable_double(stmt.handle, 1, row.target_mean);
        bind_nullable_double(stmt.handle, 2, row.target_sd);
        bind_text(stmt.handle, 3, now);
        sqlite3_bind_int(stmt.handle, 4, row.id);
    } else {
        if (!prepare(db.handle,
                     "INSERT INTO qc_lot_item_target(lot_id,sample_item_id,target_mean,target_sd,created_at,updated_at) "
                     "VALUES(?,?,?,?,?,?) "
                     "ON CONFLICT(lot_id,sample_item_id) DO UPDATE SET target_mean=excluded.target_mean,"
                     "target_sd=excluded.target_sd,updated_at=excluded.updated_at",
                     stmt, error)) return false;
        sqlite3_bind_int(stmt.handle, 1, row.lot_id);
        sqlite3_bind_int(stmt.handle, 2, row.sample_item_id);
        bind_nullable_double(stmt.handle, 3, row.target_mean);
        bind_nullable_double(stmt.handle, 4, row.target_sd);
        bind_text(stmt.handle, 5, now);
        bind_text(stmt.handle, 6, now);
    }
    if (sqlite3_step(stmt.handle) != SQLITE_DONE) {
        error = sqlite_error(db.handle);
        return false;
    }
    if (row.id <= 0) row.id = static_cast<int>(sqlite3_last_insert_rowid(db.handle));
    return true;
}

}  // namespace qc

#endif
