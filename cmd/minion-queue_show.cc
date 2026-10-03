#include <cstdio>
#include <optional>
#include <print>
#include <string>

#include <CLI11.hpp>

#include "minion/cli.hpp"
#include "minion/conf.hpp"
#include "minion/runner.hpp"
#include "minion/store.hpp"

int main(int argc, char *argv[]) {
  minion::conf::Settings settings = minion::conf::InitSettings();

  CLI::App app{"Show the queue minion consumes"};

  app.add_option("-d,--db", settings.MINION_DB, "Path to the minion database")
      ->capture_default_str();

  if (const auto code = minion::cli::Parse(app, argc, argv))
    return *code;

  const minion::cli::Database db{minion::cli::Open(settings.MINION_DB)};
  if (!db)
    return minion::cli::Failure;

  std::optional<std::string> specific;
  std::string error;
  if (!minion::store::ReadQueue(db.get(), specific, error)) {
    std::println(stderr, "{}", error);
    return minion::cli::Failure;
  }

  std::println(
      "{} ({})",
      minion::runner::SelectQueue(specific, settings.MINION_GENERIC_QUEUE),
      specific.has_value() ? "specific" : "generic");
  return minion::cli::Ok;
}
