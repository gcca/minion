#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include <CLI11.hpp>

#include "minion/conf.hpp"
#include "minion/db.hpp"
#include "minion/log.hpp"
#include "minion/loop.hpp"

int main(int argc, char *argv[]) {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);

  minion::conf::Settings settings = minion::conf::InitSettings();

  CLI::App app{"Run allowlisted tasks named by RabbitMQ messages"};

  app.add_option("-d,--db", settings.MINION_DB, "Path to the minion database")
      ->capture_default_str();

  std::string log_level{minion::log::LevelName(settings.MINION_LOG_LEVEL)};
  app.add_option("--log-level", log_level, "debug, info, warn or error")
      ->check(CLI::IsMember({"debug", "info", "warn", "error"}))
      ->capture_default_str();

  minion::loop::Options options;
  app.add_flag("--once", options.once, "Handle one delivery, then exit");

  CLI11_PARSE(app, argc, argv);

  if (minion::log::Level level; minion::log::ParseLevel(log_level, level))
    minion::log::SetLevel(level);

  ::setenv("TZ", settings.TZ.c_str(), 1);
  ::tzset();

  std::string error;
  sqlite3 *db = minion::db::Connect(settings.MINION_DB, error);
  if (db == nullptr) {
    minion::log::Error("{}", error);
    return 1;
  }

  if (!minion::db::RequireSchema(db, error)) {
    minion::log::Error("{}", error);
    sqlite3_close(db);
    return 1;
  }

  const int status = minion::loop::Serve(db, settings, options);

  sqlite3_close(db);

  return status;
}
