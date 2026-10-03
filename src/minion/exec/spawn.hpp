#pragma once

#include <optional>
#include <string>
#include <vector>

#include <sys/types.h>

namespace minion::exec {

struct SpawnRequest {
  std::string command;
  std::vector<std::string> args;
  std::optional<std::string> cwd;
  bool setsid = true;
};

struct Spawned {
  pid_t pid = -1;
  int out_fd = -1;
  int err_fd = -1;
};

[[nodiscard]] bool Spawn(const SpawnRequest &request, Spawned &child,
                         std::string &error);

}
