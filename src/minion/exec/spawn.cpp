#include "spawn.hpp"

#include <cerrno>
#include <format>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>

extern "C" char **environ;

namespace minion::exec {

namespace {

[[nodiscard]] std::string Describe(int code) {
  return std::system_category().message(code);
}

void CloseIfOpen(int &fd) {
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
  }
}

}

[[nodiscard]] bool Spawn(const SpawnRequest &request, Spawned &child,
                         std::string &error) {
  int out_pipe[2] = {-1, -1};
  int err_pipe[2] = {-1, -1};

  if (::pipe(out_pipe) != 0) {
    error = std::format("pipe: {}", Describe(errno));
    return false;
  }

  if (::pipe(err_pipe) != 0) {
    error = std::format("pipe: {}", Describe(errno));
    CloseIfOpen(out_pipe[0]);
    CloseIfOpen(out_pipe[1]);
    return false;
  }

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null",
                                   O_RDONLY, 0);
  posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, err_pipe[1], STDERR_FILENO);
  posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
  posix_spawn_file_actions_addclose(&actions, out_pipe[1]);
  posix_spawn_file_actions_addclose(&actions, err_pipe[0]);
  posix_spawn_file_actions_addclose(&actions, err_pipe[1]);

#ifdef HAVE_SPAWN_ADDCHDIR

  if (request.cwd.has_value()) {
#ifdef HAVE_SPAWN_ADDCHDIR_POSIX
    posix_spawn_file_actions_addchdir(&actions, request.cwd->c_str());
#else
    posix_spawn_file_actions_addchdir_np(&actions, request.cwd->c_str());
#endif
  }
#endif

  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);

#ifdef POSIX_SPAWN_SETSID

  if (request.setsid)
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
#endif

  std::vector<char *> argv;
  argv.reserve(request.args.size() + 2);
  argv.push_back(const_cast<char *>(request.command.c_str()));
  for (const auto &arg : request.args)
    argv.push_back(const_cast<char *>(arg.c_str()));
  argv.push_back(nullptr);

  pid_t pid = -1;
  const int rc = ::posix_spawnp(&pid, request.command.c_str(), &actions, &attr,
                                argv.data(), environ);

  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attr);

  CloseIfOpen(out_pipe[1]);
  CloseIfOpen(err_pipe[1]);

  if (rc != 0) {
    error = std::format("spawn {}: {}", request.command, Describe(rc));
    CloseIfOpen(out_pipe[0]);
    CloseIfOpen(err_pipe[0]);
    return false;
  }

  child.pid = pid;
  child.out_fd = out_pipe[0];
  child.err_fd = err_pipe[0];

  return true;
}

}
