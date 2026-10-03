#pragma once

#include <span>
#include <string>
#include <string_view>

#include <sqlite3.h>

namespace minion::schema {

struct Migration {
  int version;
  std::string_view name;
  std::string_view sql;
};

[[nodiscard]] std::span<const Migration> Migrations();

[[nodiscard]] std::string Dump();

[[nodiscard]] bool Migrate(sqlite3 *db, int &applied, std::string &error);

}
