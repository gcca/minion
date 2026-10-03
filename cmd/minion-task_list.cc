#include <algorithm>
#include <cstdio>
#include <print>
#include <string>
#include <vector>

#include <CLI11.hpp>

#include "minion/cli.hpp"
#include "minion/conf.hpp"
#include "minion/store.hpp"

int main(int argc, char *argv[]) {
  minion::conf::Settings settings = minion::conf::InitSettings();

  CLI::App app{"List allowlisted tasks"};

  app.add_option("-d,--db", settings.MINION_DB, "Path to the minion database")
      ->capture_default_str();

  if (const auto code = minion::cli::Parse(app, argc, argv))
    return *code;

  const minion::cli::Database db{minion::cli::Open(settings.MINION_DB)};
  if (!db)
    return minion::cli::Failure;

  std::vector<minion::store::TaskSpec> specs;
  std::string error;
  if (!minion::store::ListTasks(db.get(), specs, error)) {
    std::println(stderr, "{}", error);
    return minion::cli::Failure;
  }

  std::size_t name_width = 4;
  std::size_t path_width = 4;
  for (const auto &spec : specs) {
    name_width = std::max(name_width, spec.name.size());
    path_width = std::max(path_width, spec.path.size());
  }

  std::println("{:<{}}  {:<7}  {:>7}  {:<{}}  {}", "NAME", name_width,
               "ENABLED", "TIMEOUT", "PATH", path_width, "CWD");
  for (const auto &spec : specs)
    std::println("{:<{}}  {:<7}  {:>6}s  {:<{}}  {}", spec.name, name_width,
                 spec.enabled ? "yes" : "no", spec.timeout_s, spec.path,
                 path_width, spec.cwd.value_or("-"));

  return minion::cli::Ok;
}
