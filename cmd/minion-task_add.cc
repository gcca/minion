#include <cstdio>
#include <optional>
#include <print>
#include <string>

#include <CLI11.hpp>

#include "minion/cli.hpp"
#include "minion/conf.hpp"
#include "minion/store.hpp"

int main(int argc, char *argv[]) {
  minion::conf::Settings settings = minion::conf::InitSettings();

  CLI::App app{"Register a task name and the executable it runs"};

  app.add_option("-d,--db", settings.MINION_DB, "Path to the minion database")
      ->capture_default_str();

  minion::store::TaskSpec spec;
  app.add_option("name", spec.name, "Name publishers use in Task.name")
      ->required();
  app.add_option("path", spec.path, "Absolute path of the executable")
      ->required();

  std::string cwd;
  app.add_option("--cwd", cwd, "Absolute working directory for the child");
  app.add_option("--timeout", spec.timeout_s, "Timeout in seconds")
      ->check(CLI::PositiveNumber)
      ->capture_default_str();

  bool disabled = false;
  app.add_flag("--disabled", disabled, "Add the task without enabling it");

  if (const auto code = minion::cli::Parse(app, argc, argv))
    return *code;

  if (spec.name.empty()) {
    std::println(stderr, "name must not be empty");
    return minion::cli::Usage;
  }

  if (!spec.path.starts_with('/')) {
    std::println(stderr, "path must be absolute: '{}'", spec.path);
    return minion::cli::Usage;
  }

  if (!cwd.empty()) {
    if (!cwd.starts_with('/')) {
      std::println(stderr, "cwd must be absolute: '{}'", cwd);
      return minion::cli::Usage;
    }
    spec.cwd = cwd;
  }

  spec.enabled = !disabled;

  const minion::cli::Database db{minion::cli::Open(settings.MINION_DB)};
  if (!db)
    return minion::cli::Failure;

  std::string error;
  if (!minion::store::AddTask(db.get(), spec, error)) {
    std::println(stderr, "{}", error);
    return minion::cli::Failure;
  }

  std::println("added '{}' -> {}{}", spec.name, spec.path,
               spec.enabled ? "" : " (disabled)");
  return minion::cli::Ok;
}
