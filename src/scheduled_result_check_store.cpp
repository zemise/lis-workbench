#include "scheduled_result_check_store.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "sqlite3.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <unordered_set>
#include <windows.h>

namespace scheduled_check {
namespace {

struct Db {
  sqlite3 *p = nullptr;
  ~Db() {
    if (p)
      sqlite3_close(p);
  }
};
struct Stmt {
  sqlite3_stmt *p = nullptr;
  ~Stmt() {
    if (p)
      sqlite3_finalize(p);
  }
};

std::string text(sqlite3_stmt *s, int col) {
  const auto *p = sqlite3_column_text(s, col);
  return p ? reinterpret_cast<const char *>(p) : "";
}
void bind(sqlite3_stmt *s, int col, const std::string &v) {
  sqlite3_bind_text(s, col, v.c_str(), -1, SQLITE_TRANSIENT);
}
std::string now_text() {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  char out[32]{};
  snprintf(out, sizeof(out), "%04u-%02u-%02u %02u:%02u:%02u",
           static_cast<unsigned>(st.wYear), static_cast<unsigned>(st.wMonth),
           static_cast<unsigned>(st.wDay), static_cast<unsigned>(st.wHour),
           static_cast<unsigned>(st.wMinute),
           static_cast<unsigned>(st.wSecond));
  return out;
}
bool open(Db &db, std::string &error) {
  const std::wstring path =
      search::module_dir() + L"\\scheduled_result_check.sqlite";
  if (sqlite3_open16(path.c_str(), &db.p) != SQLITE_OK) {
    error = db.p ? sqlite3_errmsg(db.p) : "sqlite open failed";
    return false;
  }
  sqlite3_busy_timeout(db.p, 5000);
  return true;
}
bool exec(sqlite3 *db, const char *sql, std::string &error) {
  char *msg = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &msg);
  if (rc == SQLITE_OK)
    return true;
  error = msg ? msg : sqlite3_errmsg(db);
  sqlite3_free(msg);
  return false;
}
bool prepare(sqlite3 *db, const char *sql, Stmt &st, std::string &error) {
  if (sqlite3_prepare_v2(db, sql, -1, &st.p, nullptr) == SQLITE_OK)
    return true;
  error = sqlite3_errmsg(db);
  return false;
}
bool column_exists(sqlite3 *db, const char *table, const char *column) {
  Stmt statement;
  const std::string sql =
      std::string("PRAGMA table_info(") + table + ")";
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement.p, nullptr) !=
      SQLITE_OK)
    return false;
  while (sqlite3_step(statement.p) == SQLITE_ROW) {
    if (text(statement.p, 1) == column)
      return true;
  }
  return false;
}
bool add_column_if_missing(sqlite3 *db, const char *table,
                           const char *column, const char *ddl,
                           std::string &error) {
  if (column_exists(db, table, column))
    return true;
  const std::string sql =
      std::string("ALTER TABLE ") + table + " ADD COLUMN " + ddl;
  return exec(db, sql.c_str(), error);
}
bool ensure_rule_columns(sqlite3 *db, std::string &error) {
  return add_column_if_missing(
             db, "scheduled_result_rule", "compare_with_value",
             "compare_with_value INTEGER NOT NULL DEFAULT 0", error) &&
         add_column_if_missing(
             db, "scheduled_result_rule", "right_value_text",
             "right_value_text TEXT NOT NULL DEFAULT ''", error) &&
         add_column_if_missing(db, "scheduled_result_rule", "room_code",
                               "room_code TEXT NOT NULL DEFAULT ''", error) &&
         add_column_if_missing(db, "scheduled_result_rule", "mach_code",
                               "mach_code TEXT NOT NULL DEFAULT ''", error) &&
         add_column_if_missing(db, "scheduled_result_rule", "mach_name",
                               "mach_name TEXT NOT NULL DEFAULT ''", error);
}
bool ensure_alert_columns(sqlite3 *db, std::string &error) {
  return add_column_if_missing(
             db, "scheduled_result_alert", "compare_with_value",
             "compare_with_value INTEGER NOT NULL DEFAULT 0", error) &&
         add_column_if_missing(
             db, "scheduled_result_alert", "left_item_eng",
             "left_item_eng TEXT NOT NULL DEFAULT ''", error) &&
         add_column_if_missing(
             db, "scheduled_result_alert", "right_item_eng",
             "right_item_eng TEXT NOT NULL DEFAULT ''", error);
}
int user_version(sqlite3 *db) {
  Stmt statement;
  if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &statement.p,
                         nullptr) != SQLITE_OK)
    return 0;
  int version = 0;
  if (sqlite3_step(statement.p) == SQLITE_ROW)
    version = sqlite3_column_int(statement.p, 0);
  return version;
}

bool schema(sqlite3 *db, std::string &error) {
  if (!exec(db, R"SQL(
PRAGMA foreign_keys=ON;
CREATE TABLE IF NOT EXISTS scheduled_result_rule(
 id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT NOT NULL,enabled INTEGER NOT NULL DEFAULT 1,
 left_item_code TEXT NOT NULL,left_item_name TEXT NOT NULL DEFAULT '',left_item_unit TEXT NOT NULL DEFAULT '',
 operator TEXT NOT NULL,right_item_code TEXT NOT NULL,right_item_name TEXT NOT NULL DEFAULT '',
 right_item_unit TEXT NOT NULL DEFAULT '',
 compare_with_value INTEGER NOT NULL DEFAULT 0,right_value_text TEXT NOT NULL DEFAULT '',
 room_code TEXT NOT NULL DEFAULT '',mach_code TEXT NOT NULL DEFAULT '',mach_name TEXT NOT NULL DEFAULT '',
 created_at TEXT NOT NULL,updated_at TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS scheduled_result_alert(
 id INTEGER PRIMARY KEY AUTOINCREMENT,rule_id INTEGER NOT NULL,rule_name TEXT NOT NULL DEFAULT '',
 rep_no TEXT NOT NULL,oper_no TEXT NOT NULL DEFAULT '',room_code TEXT NOT NULL DEFAULT '',
 mach_code TEXT NOT NULL DEFAULT '',mach_name TEXT NOT NULL DEFAULT '',inspect_date TEXT NOT NULL DEFAULT '',
 left_entry_id TEXT NOT NULL DEFAULT '',left_item_code TEXT NOT NULL,left_item_name TEXT NOT NULL DEFAULT '',
 left_item_eng TEXT NOT NULL DEFAULT '',
 left_result_text TEXT NOT NULL,left_result_value REAL,operator TEXT NOT NULL,
 right_entry_id TEXT NOT NULL DEFAULT '',right_item_code TEXT NOT NULL,right_item_name TEXT NOT NULL DEFAULT '',
 right_item_eng TEXT NOT NULL DEFAULT '',
 right_result_text TEXT NOT NULL,right_result_value REAL,
 compare_with_value INTEGER NOT NULL DEFAULT 0,fingerprint TEXT NOT NULL,
 handled INTEGER NOT NULL DEFAULT 0,handled_at TEXT,discovered_at TEXT NOT NULL,
 UNIQUE(rule_id,fingerprint),FOREIGN KEY(rule_id) REFERENCES scheduled_result_rule(id) ON DELETE CASCADE);
CREATE TABLE IF NOT EXISTS scheduled_result_observation(
 rule_id INTEGER NOT NULL,fingerprint TEXT NOT NULL,observed_at TEXT NOT NULL,
 PRIMARY KEY(rule_id,fingerprint),FOREIGN KEY(rule_id) REFERENCES scheduled_result_rule(id) ON DELETE CASCADE);
CREATE TABLE IF NOT EXISTS scheduled_result_scan_progress(
 id INTEGER PRIMARY KEY CHECK(id=1),day TEXT NOT NULL,rule_signature TEXT NOT NULL,
 high_watermark TEXT NOT NULL,day_min_rep_no TEXT NOT NULL,
 sweep_max_rep_no TEXT NOT NULL,
 sweep_step INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS scheduled_result_pending_report(
 rep_no TEXT PRIMARY KEY,first_seen INTEGER NOT NULL,next_scan INTEGER NOT NULL);
CREATE INDEX IF NOT EXISTS idx_scheduled_pending_due
 ON scheduled_result_pending_report(next_scan);
CREATE INDEX IF NOT EXISTS idx_scheduled_alert_day ON scheduled_result_alert(discovered_at,handled);
CREATE INDEX IF NOT EXISTS idx_scheduled_alert_rep ON scheduled_result_alert(rep_no,rule_id);
CREATE INDEX IF NOT EXISTS idx_scheduled_alert_pending ON scheduled_result_alert(handled,id DESC);
)SQL",
            error))
    return false;
  // Existing databases from earlier development builds lack the single-item
  // threshold columns; add them without touching the user's data.
  if (!ensure_rule_columns(db, error) || !ensure_alert_columns(db, error))
    return false;
  // Earlier builds stored the first scan as silent observations. Remove only
  // those legacy observations that have no matching alert so the upgraded first
  // scan can report all of today's matches. Normal alerted observations remain.
  // Gate behind user_version so the cleanup runs once instead of on every scan.
  if (user_version(db) < 1) {
    if (!exec(db, R"SQL(
DELETE FROM scheduled_result_observation
WHERE NOT EXISTS(
 SELECT 1 FROM scheduled_result_alert a
 WHERE a.rule_id=scheduled_result_observation.rule_id
   AND a.fingerprint=scheduled_result_observation.fingerprint);
)SQL",
              error))
      return false;
    if (!exec(db, "PRAGMA user_version=1", error))
      return false;
  }
  return true;
}
bool ready(Db &db, std::string &error) {
  return open(db, error) && schema(db.p, error);
}

Alert read_alert(sqlite3_stmt *s) {
  Alert a;
  int c = 0;
  a.id = sqlite3_column_int(s, c++);
  a.rule_id = sqlite3_column_int(s, c++);
  a.rule_name = text(s, c++);
  a.rep_no = text(s, c++);
  a.oper_no = text(s, c++);
  a.room_code = text(s, c++);
  a.mach_code = text(s, c++);
  a.mach_name = text(s, c++);
  a.inspect_date = text(s, c++);
  a.left_entry_id = text(s, c++);
  a.left_item_code = text(s, c++);
  a.left_item_name = text(s, c++);
  a.left_item_eng = text(s, c++);
  a.left_result_text = text(s, c++);
  a.left_value = sqlite3_column_double(s, c++);
  a.op = text(s, c++);
  a.right_entry_id = text(s, c++);
  a.right_item_code = text(s, c++);
  a.right_item_name = text(s, c++);
  a.right_item_eng = text(s, c++);
  a.right_result_text = text(s, c++);
  a.right_value = sqlite3_column_double(s, c++);
  a.compare_with_value = sqlite3_column_int(s, c++) != 0;
  a.fingerprint = text(s, c++);
  a.handled = sqlite3_column_int(s, c++) != 0;
  a.discovered_at = text(s, c++);
  return a;
}

bool load_alerts(sqlite3 *db, const char *where_clause,
                 std::vector<Alert> &rows, std::string &error) {
  constexpr const char *columns =
      "id,rule_id,rule_name,rep_no,oper_no,room_code,mach_code,mach_name,"
      "inspect_date,left_entry_id,left_item_code,left_item_name,left_item_eng,"
      "left_result_text,left_result_value,operator,right_entry_id,"
      "right_item_code,right_item_name,right_item_eng,right_result_text,"
      "right_result_value,"
      "compare_with_value,fingerprint,handled,discovered_at";
  const std::string sql = std::string("SELECT ") + columns +
                          " FROM scheduled_result_alert " + where_clause +
                          " ORDER BY handled,id DESC";
  Stmt statement;
  if (!prepare(db, sql.c_str(), statement, error))
    return false;
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(statement.p)) == SQLITE_ROW)
    rows.push_back(read_alert(statement.p));
  if (rc != SQLITE_DONE) {
    error = sqlite3_errmsg(db);
    return false;
  }
  error.clear();
  return true;
}

bool delete_rule_records(sqlite3 *db, int rule_id, std::string &error) {
  for (const char *sql : {
           "DELETE FROM scheduled_result_alert WHERE rule_id=?",
           "DELETE FROM scheduled_result_observation WHERE rule_id=?",
       }) {
    Stmt statement;
    if (!prepare(db, sql, statement, error))
      return false;
    sqlite3_bind_int(statement.p, 1, rule_id);
    if (sqlite3_step(statement.p) != SQLITE_DONE) {
      error = sqlite3_errmsg(db);
      return false;
    }
  }
  return true;
}

bool rule_is_current(sqlite3 *db, const Rule &rule, bool &current,
                     std::string &error) {
  current = false;
  Stmt statement;
  if (!prepare(db,
               "SELECT enabled,name,left_item_code,left_item_name,left_item_"
               "unit,operator,right_item_code,right_item_name,right_item_unit,"
               "compare_with_value,right_value_text,room_code,mach_code,mach_name "
               "FROM scheduled_result_rule WHERE id=?",
               statement, error))
    return false;
  sqlite3_bind_int(statement.p, 1, rule.id);
  const int rc = sqlite3_step(statement.p);
  if (rc == SQLITE_DONE)
    return true;
  if (rc != SQLITE_ROW) {
    error = sqlite3_errmsg(db);
    return false;
  }
  current = sqlite3_column_int(statement.p, 0) != 0 &&
            text(statement.p, 1) == rule.name &&
            text(statement.p, 2) == rule.left_item_code &&
            text(statement.p, 3) == rule.left_item_name &&
            text(statement.p, 4) == rule.left_item_unit &&
            text(statement.p, 5) == rule.op &&
            text(statement.p, 6) == rule.right_item_code &&
            text(statement.p, 7) == rule.right_item_name &&
            text(statement.p, 8) == rule.right_item_unit &&
            (sqlite3_column_int(statement.p, 9) != 0) ==
                rule.compare_with_value &&
            text(statement.p, 10) == rule.right_value_text &&
            text(statement.p, 11) == rule.room_code &&
            text(statement.p, 12) == rule.mach_code &&
            text(statement.p, 13) == rule.mach_name;
  return true;
}

void rollback(sqlite3 *db) {
  std::string ignored;
  exec(db, "ROLLBACK", ignored);
}

} // namespace

bool ensure_store(std::string &error) {
  Db db;
  return ready(db, error);
}

bool load_rules(std::vector<Rule> &rows, std::string &error) {
  rows.clear();
  Db db;
  if (!ready(db, error))
    return false;
  Stmt st;
  if (!prepare(db.p,
               "SELECT "
               "id,name,enabled,left_item_code,left_item_name,left_item_unit,"
               "operator,"
               "right_item_code,right_item_name,right_item_unit,"
               "compare_with_value,right_value_text,room_code,mach_code,"
               "mach_name,created_at,updated_at "
               "FROM scheduled_result_rule ORDER BY id",
               st, error))
    return false;
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(st.p)) == SQLITE_ROW) {
    Rule r;
    r.id = sqlite3_column_int(st.p, 0);
    r.name = text(st.p, 1);
    r.enabled = sqlite3_column_int(st.p, 2) != 0;
    r.left_item_code = text(st.p, 3);
    r.left_item_name = text(st.p, 4);
    r.left_item_unit = text(st.p, 5);
    r.op = text(st.p, 6);
    r.right_item_code = text(st.p, 7);
    r.right_item_name = text(st.p, 8);
    r.right_item_unit = text(st.p, 9);
    r.compare_with_value = sqlite3_column_int(st.p, 10) != 0;
    r.right_value_text = text(st.p, 11);
    r.room_code = text(st.p, 12);
    r.mach_code = text(st.p, 13);
    r.mach_name = text(st.p, 14);
    r.created_at = text(st.p, 15);
    r.updated_at = text(st.p, 16);
    rows.push_back(std::move(r));
  }
  if (rc != SQLITE_DONE) {
    error = sqlite3_errmsg(db.p);
    return false;
  }
  error.clear();
  return true;
}

bool save_rule(Rule &r, std::string &error) {
  Db db;
  if (!ready(db, error))
    return false;
  const std::string now = now_text();
  if (r.id <= 0) {
    Stmt st;
    if (!prepare(
            db.p,
            "INSERT INTO "
            "scheduled_result_rule(name,enabled,left_item_code,left_item_name,"
            "left_item_unit,operator,right_item_code,right_item_name,right_"
            "item_unit,compare_with_value,right_value_text,room_code,mach_code,"
            "mach_name,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            st, error))
      return false;
    bind(st.p, 1, r.name);
    sqlite3_bind_int(st.p, 2, r.enabled ? 1 : 0);
    bind(st.p, 3, r.left_item_code);
    bind(st.p, 4, r.left_item_name);
    bind(st.p, 5, r.left_item_unit);
    bind(st.p, 6, r.op);
    bind(st.p, 7, r.right_item_code);
    bind(st.p, 8, r.right_item_name);
    bind(st.p, 9, r.right_item_unit);
    sqlite3_bind_int(st.p, 10, r.compare_with_value ? 1 : 0);
    bind(st.p, 11, r.right_value_text);
    bind(st.p, 12, r.room_code);
    bind(st.p, 13, r.mach_code);
    bind(st.p, 14, r.mach_name);
    bind(st.p, 15, now);
    bind(st.p, 16, now);
    if (sqlite3_step(st.p) != SQLITE_DONE) {
      error = sqlite3_errmsg(db.p);
      return false;
    }
    r.id = static_cast<int>(sqlite3_last_insert_rowid(db.p));
    error.clear();
    return true;
  }

  if (!exec(db.p, "BEGIN IMMEDIATE", error))
    return false;
  {
    Stmt st;
    if (!prepare(
            db.p,
            "UPDATE scheduled_result_rule SET "
            "name=?,enabled=?,left_item_code=?,left_item_name=?,"
            "left_item_unit=?,operator=?,right_item_code=?,right_item_name=?,"
            "right_item_unit=?,compare_with_value=?,right_value_text=?,"
            "room_code=?,mach_code=?,mach_name=?,updated_at=? WHERE id=?",
            st, error)) {
      rollback(db.p);
      return false;
    }
    bind(st.p, 1, r.name);
    sqlite3_bind_int(st.p, 2, r.enabled ? 1 : 0);
    bind(st.p, 3, r.left_item_code);
    bind(st.p, 4, r.left_item_name);
    bind(st.p, 5, r.left_item_unit);
    bind(st.p, 6, r.op);
    bind(st.p, 7, r.right_item_code);
    bind(st.p, 8, r.right_item_name);
    bind(st.p, 9, r.right_item_unit);
    sqlite3_bind_int(st.p, 10, r.compare_with_value ? 1 : 0);
    bind(st.p, 11, r.right_value_text);
    bind(st.p, 12, r.room_code);
    bind(st.p, 13, r.mach_code);
    bind(st.p, 14, r.mach_name);
    bind(st.p, 15, now);
    sqlite3_bind_int(st.p, 16, r.id);
    if (sqlite3_step(st.p) != SQLITE_DONE) {
      error = sqlite3_errmsg(db.p);
      rollback(db.p);
      return false;
    }
    if (sqlite3_changes(db.p) != 1) {
      error = "规则已不存在，请刷新后重试";
      rollback(db.p);
      return false;
    }
  }
  if (!delete_rule_records(db.p, r.id, error)) {
    rollback(db.p);
    return false;
  }
  if (!exec(db.p, "COMMIT", error)) {
    rollback(db.p);
    return false;
  }
  error.clear();
  return true;
}
bool delete_rule(int id, std::string &error) {
  Db db;
  if (!ready(db, error))
    return false;
  if (!exec(db.p, "BEGIN IMMEDIATE", error))
    return false;
  if (!delete_rule_records(db.p, id, error)) {
    rollback(db.p);
    return false;
  }
  {
    Stmt st;
    if (!prepare(db.p, "DELETE FROM scheduled_result_rule WHERE id=?", st,
                 error)) {
      rollback(db.p);
      return false;
    }
    sqlite3_bind_int(st.p, 1, id);
    if (sqlite3_step(st.p) != SQLITE_DONE) {
      error = sqlite3_errmsg(db.p);
      rollback(db.p);
      return false;
    }
    if (sqlite3_changes(db.p) != 1) {
      error = "规则已不存在，请刷新后重试";
      rollback(db.p);
      return false;
    }
  }
  if (!exec(db.p, "COMMIT", error)) {
    rollback(db.p);
    return false;
  }
  error.clear();
  return true;
}
bool set_rule_enabled(int id, bool enabled, std::string &error) {
  Db db;
  if (!ready(db, error))
    return false;
  if (!exec(db.p, "BEGIN IMMEDIATE", error))
    return false;
  {
    Stmt st;
    if (!prepare(db.p,
                 "UPDATE scheduled_result_rule SET enabled=?,updated_at=? WHERE "
                 "id=?",
                 st, error)) {
      rollback(db.p);
      return false;
    }
    sqlite3_bind_int(st.p, 1, enabled ? 1 : 0);
    bind(st.p, 2, now_text());
    sqlite3_bind_int(st.p, 3, id);
    if (sqlite3_step(st.p) != SQLITE_DONE) {
      error = sqlite3_errmsg(db.p);
      rollback(db.p);
      return false;
    }
    if (sqlite3_changes(db.p) != 1) {
      error = "规则已不存在，请刷新后重试";
      rollback(db.p);
      return false;
    }
  }
  if (!enabled && !delete_rule_records(db.p, id, error)) {
    rollback(db.p);
    return false;
  }
  if (!exec(db.p, "COMMIT", error)) {
    rollback(db.p);
    return false;
  }
  error.clear();
  return true;
}

bool record_matches(const std::vector<Rule> &rules,
                    const std::vector<Match> &matches,
                    std::vector<Alert> &fresh, std::string &error) {
  fresh.clear();
  Db db;
  if (!ready(db, error))
    return false;
  if (!exec(db.p, "BEGIN IMMEDIATE", error))
    return false;
  const std::string now = now_text();
  bool ok = true;
  std::unordered_set<int> current_rule_ids;
  for (const auto &rule : rules) {
    bool current = false;
    if (!rule_is_current(db.p, rule, current, error)) {
      ok = false;
      break;
    }
    if (current)
      current_rule_ids.insert(rule.id);
  }
  for (const auto &m : matches) {
    if (!ok)
      break;
    auto it = std::find_if(rules.begin(), rules.end(),
                           [&](const Rule &r) { return r.id == m.rule_id; });
    if (it == rules.end() || current_rule_ids.count(m.rule_id) == 0)
      continue;
    Stmt seen;
    if (!prepare(db.p,
                 "INSERT OR IGNORE INTO "
                 "scheduled_result_observation(rule_id,fingerprint,observed_at)"
                 " VALUES(?,?,?)",
                 seen, error)) {
      ok = false;
      break;
    }
    sqlite3_bind_int(seen.p, 1, m.rule_id);
    bind(seen.p, 2, m.fingerprint);
    bind(seen.p, 3, now);
    if (sqlite3_step(seen.p) != SQLITE_DONE) {
      error = sqlite3_errmsg(db.p);
      ok = false;
      break;
    }
    const bool newlyObserved = sqlite3_changes(db.p) > 0;
    // The first scan must report every matching result from today, regardless
    // of review/send state. Observations only suppress identical later scans.
    if (!newlyObserved)
      continue;
    Stmt st;
    if (!prepare(db.p,
                 "INSERT INTO "
                 "scheduled_result_alert(rule_id,rule_name,rep_no,oper_no,room_"
                 "code,mach_code,mach_name,inspect_date,left_entry_id,left_"
                 "item_code,left_item_name,left_item_eng,left_result_text,"
                 "left_result_value,operator,right_entry_id,right_item_code,"
                 "right_item_name,right_item_eng,"
                 "right_result_text,right_result_value,compare_with_value,"
                 "fingerprint,discovered_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,"
                 "?,?,?,?,?,?,?,?,?,?,?,?)",
                 st, error)) {
      ok = false;
      break;
    }
    int c = 1;
    sqlite3_bind_int(st.p, c++, m.rule_id);
    bind(st.p, c++, m.rule_name);
    bind(st.p, c++, m.rep_no);
    bind(st.p, c++, m.oper_no);
    bind(st.p, c++, m.room_code);
    bind(st.p, c++, m.mach_code);
    bind(st.p, c++, m.mach_name);
    bind(st.p, c++, m.inspect_date);
    bind(st.p, c++, m.left_entry_id);
    bind(st.p, c++, m.left_item_code);
    bind(st.p, c++, m.left_item_name);
    bind(st.p, c++, m.left_item_eng);
    bind(st.p, c++, m.left_result_text);
    sqlite3_bind_double(st.p, c++, m.left_value);
    bind(st.p, c++, m.op);
    bind(st.p, c++, m.right_entry_id);
    bind(st.p, c++, m.right_item_code);
    bind(st.p, c++, m.right_item_name);
    bind(st.p, c++, m.right_item_eng);
    bind(st.p, c++, m.right_result_text);
    sqlite3_bind_double(st.p, c++, m.right_value);
    sqlite3_bind_int(st.p, c++, m.compare_with_value ? 1 : 0);
    bind(st.p, c++, m.fingerprint);
    bind(st.p, c++, now);
    if (sqlite3_step(st.p) != SQLITE_DONE) {
      error = sqlite3_errmsg(db.p);
      ok = false;
      break;
    }
    Alert a;
    static_cast<Match &>(a) = m;
    a.id = static_cast<int>(sqlite3_last_insert_rowid(db.p));
    a.discovered_at = now;
    fresh.push_back(std::move(a));
  }
  if (!exec(db.p, ok ? "COMMIT" : "ROLLBACK", error))
    return false;
  if (ok)
    error.clear();
  return ok;
}

bool load_review_alerts(std::vector<Alert> &rows, std::string &error) {
  rows.clear();
  Db db;
  if (!ready(db, error))
    return false;
  return load_alerts(
      db.p, "WHERE handled=0 OR discovered_at>=date('now','localtime')", rows,
      error);
}

bool load_unhandled_alerts(std::vector<Alert> &rows, std::string &error) {
  rows.clear();
  Db db;
  if (!ready(db, error))
    return false;
  return load_alerts(db.p, "WHERE handled=0", rows, error);
}
bool set_alert_handled(int id, bool handled, std::string &error) {
  Db db;
  if (!ready(db, error))
    return false;
  Stmt st;
  if (!prepare(db.p,
               "UPDATE scheduled_result_alert SET handled=?,handled_at=CASE "
               "WHEN ?=1 THEN ? ELSE NULL END WHERE id=?",
               st, error))
    return false;
  sqlite3_bind_int(st.p, 1, handled ? 1 : 0);
  sqlite3_bind_int(st.p, 2, handled ? 1 : 0);
  bind(st.p, 3, now_text());
  sqlite3_bind_int(st.p, 4, id);
  if (sqlite3_step(st.p) != SQLITE_DONE) {
    error = sqlite3_errmsg(db.p);
    return false;
  }
  if (sqlite3_changes(db.p) != 1) {
    error = "待处理记录已不存在，请刷新后重试";
    return false;
  }
  error.clear();
  return true;
}

bool load_scan_progress(ScanProgress &progress,
                        std::vector<PendingReport> &pending,
                        std::string &error) {
  progress = {};
  pending.clear();
  Db db;
  if (!ready(db, error))
    return false;
  {
    Stmt st;
    if (!prepare(db.p,
                 "SELECT day,rule_signature,high_watermark,day_min_rep_no,"
                 "sweep_max_rep_no,sweep_step "
                 "FROM scheduled_result_scan_progress "
                 "WHERE id=1", st, error))
      return false;
    const int rc = sqlite3_step(st.p);
    if (rc == SQLITE_ROW) {
      progress.day = text(st.p, 0);
      progress.rule_signature = text(st.p, 1);
      progress.high_watermark = text(st.p, 2);
      progress.day_min_rep_no = text(st.p, 3);
      progress.sweep_max_rep_no = text(st.p, 4);
      progress.sweep_step = sqlite3_column_int(st.p, 5);
    } else if (rc != SQLITE_DONE) {
      error = sqlite3_errmsg(db.p);
      return false;
    }
  }
  Stmt st;
  if (!prepare(db.p,
               "SELECT rep_no,first_seen,next_scan FROM "
               "scheduled_result_pending_report", st, error))
    return false;
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(st.p)) == SQLITE_ROW) {
    PendingReport row;
    row.rep_no = text(st.p, 0);
    row.first_seen = sqlite3_column_int64(st.p, 1);
    row.next_scan = sqlite3_column_int64(st.p, 2);
    pending.push_back(std::move(row));
  }
  if (rc != SQLITE_DONE) {
    error = sqlite3_errmsg(db.p);
    return false;
  }
  error.clear();
  return true;
}

bool save_scan_progress(const ScanProgress &progress,
                        const std::vector<PendingReport> &pending,
                        std::string &error) {
  Db db;
  if (!ready(db, error) || !exec(db.p, "BEGIN IMMEDIATE", error))
    return false;
  bool ok = false;
  do {
    Stmt state;
    if (!prepare(db.p,
                 "INSERT OR REPLACE INTO scheduled_result_scan_progress"
                 "(id,day,rule_signature,high_watermark,day_min_rep_no,"
                 "sweep_max_rep_no,sweep_step) "
                 "VALUES(1,?,?,?,?,?,?)",
                 state, error))
      break;
    bind(state.p, 1, progress.day);
    bind(state.p, 2, progress.rule_signature);
    bind(state.p, 3, progress.high_watermark);
    bind(state.p, 4, progress.day_min_rep_no);
    bind(state.p, 5, progress.sweep_max_rep_no);
    sqlite3_bind_int(state.p, 6, progress.sweep_step);
    if (sqlite3_step(state.p) != SQLITE_DONE) {
      error = sqlite3_errmsg(db.p);
      break;
    }
    if (!exec(db.p, "DELETE FROM scheduled_result_pending_report", error))
      break;
    Stmt row;
    if (!prepare(db.p,
                 "INSERT INTO scheduled_result_pending_report"
                 "(rep_no,first_seen,next_scan) VALUES(?,?,?)",
                 row, error))
      break;
    ok = true;
    for (const auto &item : pending) {
      sqlite3_reset(row.p);
      sqlite3_clear_bindings(row.p);
      bind(row.p, 1, item.rep_no);
      sqlite3_bind_int64(row.p, 2, item.first_seen);
      sqlite3_bind_int64(row.p, 3, item.next_scan);
      if (sqlite3_step(row.p) != SQLITE_DONE) {
        error = sqlite3_errmsg(db.p);
        ok = false;
        break;
      }
    }
  } while (false);
  if (!ok) {
    std::string rollback_error;
    exec(db.p, "ROLLBACK", rollback_error);
    return false;
  }
  if (!exec(db.p, "COMMIT", error))
    return false;
  error.clear();
  return true;
}

} // namespace scheduled_check
#endif
