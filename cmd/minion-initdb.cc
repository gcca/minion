#include <cstdio>
#include <print>
#include <string>

#include <CLI11.hpp>

#include "minion/cli.hpp"
#include "minion/conf.hpp"
#include "minion/db.hpp"
#include "minion/schema.hpp"

int main(int argc, char *argv[]) {
  minion::conf::Settings settings = minion::conf::InitSettings();

  CLI::App app{"Create the minion database or migrate it to this build"};

  app.add_option("-d,--db", settings.MINION_DB, "Path to the minion database")
      ->capture_default_str();

  bool print_schema = false;
  app.add_flag("--print-schema", print_schema,
               "Print the schema SQL and exit without touching a database");

  if (const auto code = minion::cli::Parse(app, argc, argv))
    return *code;

  if (print_schema) {
    std::print("{}", minion::schema::Dump());
    return minion::cli::Ok;
  }

  std::string error;
  const minion::cli::Database db{minion::db::Create(settings.MINION_DB, error)};
  if (!db) {
    std::println(stderr, "{}", error);
    return minion::cli::Failure;
  }

  int applied = 0;
  if (!minion::schema::Migrate(db.get(), applied, error)) {
    std::println(stderr, "{}", error);
    return minion::cli::Failure;
  }

  std::println("{} at schema version {} ({} migration(s) applied)",
               settings.MINION_DB, minion::db::SchemaVersion, applied);
  return minion::cli::Ok;
}
