#pragma once

#include <string>

#include <sqlite3.h>

namespace minion::db {

inline constexpr int SchemaVersion = 1;

[[nodiscard]] sqlite3 *Connect(const std::string &path, std::string &error);

[[nodiscard]] sqlite3 *Create(const std::string &path, std::string &error);

[[nodiscard]] bool RequireSchema(sqlite3 *db, std::string &error);

[[nodiscard]] bool UserVersion(sqlite3 *db, int &version, std::string &error);

[[nodiscard]] bool Exec(sqlite3 *db, const char *sql, std::string &error);

}
