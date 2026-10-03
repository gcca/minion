#include "schema.hpp"

#include <array>
#include <format>

#include "minion/db.hpp"

namespace minion::schema {

namespace {

constexpr std::array<Migration, 1> All{{
    {1, "initial", R"sql(CREATE TABLE setting (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  queue TEXT CHECK (queue IS NULL OR length(queue) BETWEEN 1 AND 255),
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
) STRICT;

INSERT INTO setting (id) VALUES (1);

CREATE TABLE task (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL UNIQUE CHECK (length(name) > 0),
  path TEXT NOT NULL CHECK (substr(path, 1, 1) = '/'),
  cwd TEXT CHECK (cwd IS NULL OR substr(cwd, 1, 1) = '/'),
  timeout_s INTEGER NOT NULL DEFAULT 300 CHECK (timeout_s > 0),
  enabled INTEGER NOT NULL DEFAULT 1 CHECK (enabled IN (0, 1)),
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
) STRICT;
)sql"},
}};

static_assert(All.back().version == db::SchemaVersion);

[[nodiscard]] bool Rollback(sqlite3 *db) {
  std::string ignored;
  (void)db::Exec(db, "ROLLBACK", ignored);
  return false;
}

}

[[nodiscard]] std::span<const Migration> Migrations() { return All; }

[[nodiscard]] std::string Dump() {
  std::string text;
  for (const auto &migration : All)
    text += std::format("{}\n", migration.sql);
  text += std::format("PRAGMA user_version = {};\n", db::SchemaVersion);
  return text;
}

[[nodiscard]] bool Migrate(sqlite3 *db, int &applied, std::string &error) {
  applied = 0;

  int current = 0;
  if (!db::UserVersion(db, current, error))
    return false;

  if (current > db::SchemaVersion) {
    error =
        std::format("database schema version {} is newer than this build ({})",
                    current, db::SchemaVersion);
    return false;
  }

  for (const auto &migration : All) {
    if (migration.version <= current)
      continue;

    if (!db::Exec(db, "BEGIN IMMEDIATE", error))
      return false;

    const std::string sql{migration.sql};
    if (!db::Exec(db, sql.c_str(), error)) {
      error = std::format("migration {} {}: {}", migration.version,
                          migration.name, error);
      return Rollback(db);
    }

    const std::string bump =
        std::format("PRAGMA user_version = {}", migration.version);
    if (!db::Exec(db, bump.c_str(), error) || !db::Exec(db, "COMMIT", error))
      return Rollback(db);

    ++applied;
  }

  return true;
}

}
