#pragma once

#include <format>
#include <string_view>
#include <utility>

namespace minion::log {

enum class Level { Debug, Info, Warn, Error };

[[nodiscard]] bool ParseLevel(std::string_view text, Level &level);
[[nodiscard]] std::string_view LevelName(Level level);

void SetLevel(Level level);
[[nodiscard]] Level CurrentLevel();

[[nodiscard]] std::string FormatLine(Level level, std::string_view stamp,
                                     std::string_view message);

void Write(Level level, std::string_view message);

template <typename... Args>
void Debug(std::format_string<Args...> fmt, Args &&...args) {
  if (CurrentLevel() <= Level::Debug)
    Write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void Info(std::format_string<Args...> fmt, Args &&...args) {
  if (CurrentLevel() <= Level::Info)
    Write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void Warn(std::format_string<Args...> fmt, Args &&...args) {
  if (CurrentLevel() <= Level::Warn)
    Write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void Error(std::format_string<Args...> fmt, Args &&...args) {
  if (CurrentLevel() <= Level::Error)
    Write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

}
