#include <cstdio>
#include <print>
#include <string>

#include <CLI11.hpp>

#include "minion/cli.hpp"
#include "minion/conf.hpp"
#include "minion/store.hpp"

int main(int argc, char *argv[]) {
  minion::conf::Settings settings = minion::conf::InitSettings();

  CLI::App app{"Disable an allowlisted task"};

  app.add_option("-d,--db", settings.MINION_DB, "Path to the minion database")
      ->capture_default_str();

  std::string name;
  app.add_option("name", name, "Task name")->required();

  if (const auto code = minion::cli::Parse(app, argc, argv))
    return *code;

  const minion::cli::Database db{minion::cli::Open(settings.MINION_DB)};
  if (!db)
    return minion::cli::Failure;

  bool found = false;
  std::string error;
  if (!minion::store::SetEnabled(db.get(), name, false, found, error)) {
    std::println(stderr, "{}", error);
    return minion::cli::Failure;
  }

  if (!found) {
    std::println(stderr, "no task named '{}'", name);
    return minion::cli::Failure;
  }

  std::println("disabled '{}'", name);
  return minion::cli::Ok;
}
