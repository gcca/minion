#include "db.hpp"

#include <filesystem>
#include <format>
#include <system_error>

namespace minion::db {

namespace {

[[nodiscard]] sqlite3 *Open(const std::string &path, int flags,
                            std::string &error) {
  sqlite3 *db = nullptr;
  if (sqlite3_open_v2(path.c_str(), &db, flags | SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK) {
    error = std::format("cannot open '{}': {}", path,
                        db ? sqlite3_errmsg(db) : "out of memory");
    sqlite3_close(db);
    return nullptr;
  }

  sqlite3_busy_timeout(db, 5000);

  if (!Exec(db, "PRAGMA foreign_keys = ON", error) ||
      !Exec(db, "PRAGMA journal_mode = WAL", error) ||
      !Exec(db, "PRAGMA synchronous = NORMAL", error)) {
    sqlite3_close(db);
    return nullptr;
  }

  return db;
}

}

[[nodiscard]] bool Exec(sqlite3 *db, const char *sql, std::string &error) {
  char *raw = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &raw) == SQLITE_OK)
    return true;

  error = raw ? raw : "unknown sqlite error";
  sqlite3_free(raw);

  return false;
}

[[nodiscard]] sqlite3 *Connect(const std::string &path, std::string &error) {
  if (!std::filesystem::exists(path)) {
    error =
        std::format("database '{}' does not exist, run minion-initdb", path);
    return nullptr;
  }

  return Open(path, SQLITE_OPEN_READWRITE, error);
}

[[nodiscard]] sqlite3 *Create(const std::string &path, std::string &error) {
  const auto parent = std::filesystem::path(path).parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      error =
          std::format("cannot create '{}': {}", parent.string(), ec.message());
      return nullptr;
    }
  }

  return Open(path, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, error);
}

[[nodiscard]] bool UserVersion(sqlite3 *db, int &version, std::string &error) {
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &stmt, nullptr) !=
      SQLITE_OK) {
    error = std::format("reading schema version: {}", sqlite3_errmsg(db));
    return false;
  }

  const bool ok = sqlite3_step(stmt) == SQLITE_ROW;
  if (ok)
    version = sqlite3_column_int(stmt, 0);
  else
    error = std::format("reading schema version: {}", sqlite3_errmsg(db));
  sqlite3_finalize(stmt);

  return ok;
}

[[nodiscard]] bool RequireSchema(sqlite3 *db, std::string &error) {
  int version = -1;
  if (!UserVersion(db, version, error))
    return false;

  if (version == SchemaVersion)
    return true;

  if (version <= 0) {
    error = "database is not initialized, run minion-initdb";
    return false;
  }

  error = std::format(
      "database schema version is {} but this build requires {}, run "
      "minion-initdb",
      version, SchemaVersion);

  return false;
}

}
