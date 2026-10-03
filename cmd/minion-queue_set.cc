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

  CLI::App app{"Set the specific queue minion consumes, or clear it to use "
               "the generic queue"};

  app.add_option("-d,--db", settings.MINION_DB, "Path to the minion database")
      ->capture_default_str();

  std::string name;
  auto *queue = app.add_option("queue", name, "Specific queue name");

  bool generic = false;
  auto *clear = app.add_flag("--generic", generic,
                             "Clear the specific queue and use the generic "
                             "one (MINION_GENERIC_QUEUE)");

  queue->excludes(clear);

  if (const auto code = minion::cli::Parse(app, argc, argv))
    return *code;

  if (!generic && queue->count() == 0) {
    std::println(stderr, "give a queue name or --generic");
    return minion::cli::Usage;
  }

  if (!generic && (name.empty() || name.size() > 255)) {
    std::println(stderr, "queue name must be 1 to 255 bytes");
    return minion::cli::Usage;
  }

  const minion::cli::Database db{minion::cli::Open(settings.MINION_DB)};
  if (!db)
    return minion::cli::Failure;

  const std::optional<std::string> value =
      generic ? std::nullopt : std::optional<std::string>{name};

  std::string error;
  if (!minion::store::WriteQueue(db.get(), value, error)) {
    std::println(stderr, "{}", error);
    return minion::cli::Failure;
  }

  if (generic)
    std::println("queue cleared, minion uses the generic queue '{}'",
                 settings.MINION_GENERIC_QUEUE);
  else
    std::println("queue set to '{}'", name);

  std::println("restart minion to apply");
  return minion::cli::Ok;
}
