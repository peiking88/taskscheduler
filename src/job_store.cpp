#include "job_store.h"

#include "NanoLogCpp17.h"

#ifdef TASKSCHEDULER_ENABLE_SQLITE
#include <sqlite3.h>
#endif

#include <chrono>

using namespace NanoLog::LogLevels;
namespace {
const char *persist_status_str(PersistStatus s) {
    switch (s) {
    case PersistStatus::Queued: return "queued";
    case PersistStatus::Running: return "running";
    case PersistStatus::Succeeded: return "succeeded";
    case PersistStatus::Failed: return "failed";
    case PersistStatus::Timeout: return "timeout";
    case PersistStatus::LaunchFailed: return "launch_failed";
    }
    return "unknown";
}
}

bool JobStore::init(const std::string &path) {
    path_ = path;
#ifdef TASKSCHEDULER_ENABLE_SQLITE
    sqlite3 *db = nullptr;
    if (sqlite3_open(path_.c_str(), &db) != SQLITE_OK) {
        NANO_LOG(ERROR, "%s", "Failed to open sqlite db");
        return false;
    }
    const char *ddl = R"(
CREATE TABLE IF NOT EXISTS jobs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  cmd TEXT NOT NULL,
  cpu_cores INTEGER,
  memory_mb INTEGER,
  timeout_sec INTEGER,
  priority INTEGER,
  status TEXT,
  submit_ms INTEGER,
  start_ms INTEGER,
  end_ms INTEGER,
  exit_code INTEGER
);
)";
    char *errmsg = nullptr;
    if (sqlite3_exec(db, ddl, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        auto msg = std::string("Failed to create table: ") + (errmsg ? errmsg : "");
        NANO_LOG(ERROR, "%s", msg.c_str());
        sqlite3_free(errmsg);
        sqlite3_close(db);
        return false;
    }
    sqlite3_close(db);
    return true;
#else
    NANO_LOG(NOTICE, "%s", "Persistence disabled; init is a no-op");
    (void)path;
    return true;
#endif
}

int JobStore::insert_job(const JobSpec &spec, PersistStatus status, int64_t submit_ms) {
#ifdef TASKSCHEDULER_ENABLE_SQLITE
    sqlite3 *db = nullptr;
    if (sqlite3_open(path_.c_str(), &db) != SQLITE_OK) return -1;
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "INSERT INTO jobs(cmd,cpu_cores,memory_mb,timeout_sec,priority,status,submit_ms) VALUES(?,?,?,?,?,?,?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, spec.cmd.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, spec.cpu_cores);
    sqlite3_bind_int(stmt, 3, static_cast<int>(spec.memory_mb));
    sqlite3_bind_int(stmt, 4, spec.timeout_sec);
    sqlite3_bind_int(stmt, 5, spec.priority);
    sqlite3_bind_text(stmt, 6, persist_status_str(status), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 7, submit_ms);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return -1;
    }
    int id = static_cast<int>(sqlite3_last_insert_rowid(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return id;
#else
    (void)spec; (void)status; (void)submit_ms;
    return -1;
#endif
}

void JobStore::update_status(int id, PersistStatus status, int exit_code, int64_t start_ms, int64_t end_ms) {
#ifdef TASKSCHEDULER_ENABLE_SQLITE
    sqlite3 *db = nullptr;
    if (sqlite3_open(path_.c_str(), &db) != SQLITE_OK) return;
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "UPDATE jobs SET status=?, exit_code=?, start_ms=?, end_ms=? WHERE id=?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) { sqlite3_close(db); return; }
    sqlite3_bind_text(stmt, 1, persist_status_str(status), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, exit_code);
    sqlite3_bind_int64(stmt, 3, start_ms);
    sqlite3_bind_int64(stmt, 4, end_ms);
    sqlite3_bind_int(stmt, 5, id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
#else
    (void)id; (void)status; (void)exit_code; (void)start_ms; (void)end_ms;
#endif
}

std::vector<PersistedJob> JobStore::load_unfinished() {
#ifdef TASKSCHEDULER_ENABLE_SQLITE
    sqlite3 *db = nullptr;
    std::vector<PersistedJob> res;
    if (sqlite3_open(path_.c_str(), &db) != SQLITE_OK) return res;
    const char *sql = "SELECT id, cmd, cpu_cores, memory_mb, timeout_sec, priority FROM jobs WHERE status IN ('queued','running')";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) { sqlite3_close(db); return res; }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PersistedJob pj;
        pj.id = sqlite3_column_int(stmt, 0);
        pj.spec.cmd = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        pj.spec.cpu_cores = sqlite3_column_int(stmt, 2);
        pj.spec.memory_mb = static_cast<std::size_t>(sqlite3_column_int(stmt, 3));
        pj.spec.timeout_sec = sqlite3_column_int(stmt, 4);
        pj.spec.priority = sqlite3_column_int(stmt, 5);
        pj.status = PersistStatus::Queued;
        res.push_back(std::move(pj));
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return res;
#else
    return {};
#endif
}
