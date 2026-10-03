#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace minion::message {

struct Task {
  std::string id;
  std::string name;
  std::vector<std::string> args;

  bool operator==(const Task &) const = default;
};

[[nodiscard]] std::string Encode(const Task &task);

[[nodiscard]] bool Decode(std::string_view body, Task &task,
                          std::string &error);

}
