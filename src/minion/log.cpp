#include "log.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>

namespace minion::log {

namespace {

std::atomic<Level> current{Level::Info};
std::mutex write_mutex;

[[nodiscard]] std::string Stamp() {
  const auto now = std::chrono::floor<std::chrono::seconds>(
      std::chrono::system_clock::now());
  return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
}

}

[[nodiscard]] bool ParseLevel(std::string_view text, Level &level) {
  if (text == "debug")
    level = Level::Debug;
  else if (text == "info")
    level = Level::Info;
  else if (text == "warn")
    level = Level::Warn;
  else if (text == "error")
    level = Level::Error;
  else
    return false;

  return true;
}

[[nodiscard]] std::string_view LevelName(Level level) {
  switch (level) {
  case Level::Debug:
    return "debug";
  case Level::Info:
    return "info";
  case Level::Warn:
    return "warn";
  case Level::Error:
    return "error";
  }

  return "info";
}

void SetLevel(Level level) { current.store(level, std::memory_order_relaxed); }

[[nodiscard]] Level CurrentLevel() {
  return current.load(std::memory_order_relaxed);
}

[[nodiscard]] std::string FormatLine(Level level, std::string_view stamp,
                                     std::string_view message) {
  return std::format("{} {:<5} {}", stamp, LevelName(level), message);
}

void Write(Level level, std::string_view message) {
  const std::string line = FormatLine(level, Stamp(), message);

  const std::lock_guard<std::mutex> guard(write_mutex);
  std::fputs(line.c_str(), stderr);
  std::fputc('\n', stderr);
}

}
