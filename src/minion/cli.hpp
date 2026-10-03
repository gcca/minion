#pragma once

#include <cstdio>
#include <optional>
#include <print>
#include <string>

#include <CLI11.hpp>
#include <sqlite3.h>

#include "minion/db.hpp"

namespace minion::cli {

inline constexpr int Ok = 0;
inline constexpr int Failure = 1;
inline constexpr int Usage = 2;

[[nodiscard]] inline std::optional<int> Parse(CLI::App &app, int argc,
                                              char **argv) {
  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError &e) {
    return app.exit(e) == 0 ? Ok : Usage;
  }
  return std::nullopt;
}

[[nodiscard]] inline sqlite3 *Open(const std::string &path) {
  std::string error;
  sqlite3 *db = db::Connect(path, error);
  if (db == nullptr) {
    std::println(stderr, "{}", error);
    return nullptr;
  }

  if (!db::RequireSchema(db, error)) {
    std::println(stderr, "{}", error);
    sqlite3_close(db);
    return nullptr;
  }

  return db;
}

class Database {
public:
  explicit Database(sqlite3 *db) : db_(db) {}

  Database(const Database &) = delete;
  Database &operator=(const Database &) = delete;

  ~Database() { sqlite3_close(db_); }

  [[nodiscard]] sqlite3 *get() const noexcept { return db_; }
  [[nodiscard]] explicit operator bool() const noexcept {
    return db_ != nullptr;
  }

private:
  sqlite3 *db_;
};

}
