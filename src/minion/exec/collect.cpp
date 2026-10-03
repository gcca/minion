#include "collect.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <format>
#include <memory>
#include <system_error>
#include <utility>

#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include "minion/exec/capture.hpp"

namespace minion::exec {

namespace {

constexpr std::size_t ReadBufferSize = 4096;
constexpr auto ReapInterval = std::chrono::milliseconds(50);

[[nodiscard]] std::string Describe(int code) {
  return std::system_category().message(code);
}

void CloseIfOpen(int &fd) {
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
  }
}

void SignalGroup(pid_t pid, int signal) {
  if (::killpg(pid, signal) != 0 && errno == ESRCH)
    ::kill(pid, signal);
}

[[nodiscard]] bool Reap(pid_t pid, int &status,
                        std::chrono::steady_clock::time_point deadline) {
  for (;;) {
    const pid_t seen = ::waitpid(pid, &status, WNOHANG);
    if (seen == pid)
      return true;

    if (seen < 0 && errno != EINTR)
      return false;

    if (std::chrono::steady_clock::now() >= deadline)
      return false;

    ::usleep(static_cast<useconds_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(ReapInterval)
            .count()));
  }
}

}

[[nodiscard]] Outcome Collect(Spawned &child, const CollectOptions &options) {
  Outcome outcome;
  outcome.spawned = true;

  const auto started = std::chrono::steady_clock::now();
  const auto deadline = started + options.timeout;

  Capture out{options.output_cap};
  Capture err{options.output_cap};

  const auto buffer = std::make_unique<char[]>(ReadBufferSize);

  while (child.out_fd >= 0 || child.err_fd >= 0) {
    std::array<pollfd, 2> fds{};
    int count = 0;

    if (child.out_fd >= 0)
      fds[count++] = pollfd{child.out_fd, POLLIN, 0};
    if (child.err_fd >= 0)
      fds[count++] = pollfd{child.err_fd, POLLIN, 0};

    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      outcome.timed_out = true;
      break;
    }

    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);

    const int ready = ::poll(fds.data(), static_cast<nfds_t>(count),
                             static_cast<int>(remaining.count()));

    if (ready < 0) {
      if (errno == EINTR)
        continue;

      outcome.error = std::format("poll: {}", Describe(errno));
      break;
    }

    if (ready == 0) {
      outcome.timed_out = true;
      break;
    }

    for (int i = 0; i < count; ++i) {
      if (fds[i].revents == 0)
        continue;

      const bool is_out = fds[i].fd == child.out_fd;
      const ssize_t read_bytes =
          ::read(fds[i].fd, buffer.get(), ReadBufferSize);

      if (read_bytes > 0) {
        const std::string_view chunk(buffer.get(),
                                     static_cast<std::size_t>(read_bytes));
        if (is_out)
          out.Append(chunk);
        else
          err.Append(chunk);
        continue;
      }

      if (read_bytes < 0 && (errno == EINTR || errno == EAGAIN))
        continue;

      CloseIfOpen(is_out ? child.out_fd : child.err_fd);
    }
  }

  CloseIfOpen(child.out_fd);
  CloseIfOpen(child.err_fd);

  int status = 0;

  const bool reaped = !outcome.timed_out && Reap(child.pid, status, deadline);

  if (!reaped &&
      (outcome.timed_out || std::chrono::steady_clock::now() >= deadline)) {
    outcome.timed_out = true;
    SignalGroup(child.pid, SIGTERM);

    if (!Reap(child.pid, status,
              std::chrono::steady_clock::now() + options.term_grace)) {
      SignalGroup(child.pid, SIGKILL);

      if (!Reap(child.pid, status,
                std::chrono::steady_clock::now() + options.kill_grace))
        outcome.unreaped = true;
    }
  } else if (!reaped) {
    outcome.unreaped = true;
  }

  if (!outcome.unreaped) {
    if (WIFEXITED(status)) {
      outcome.exited = true;
      outcome.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      outcome.term_signal = WTERMSIG(status);
    }
  } else if (outcome.error.empty()) {
    outcome.error = std::format("unreaped pid {}", child.pid);
  }

  outcome.out = out.Text();
  outcome.err = err.Text();
  outcome.out_bytes = out.TotalBytes();
  outcome.err_bytes = err.TotalBytes();
  outcome.truncated = out.Truncated() || err.Truncated();
  outcome.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  return outcome;
}

[[nodiscard]] Outcome ProcessExecutor::Run(const SpawnRequest &request,
                                           const CollectOptions &options) {
  Spawned child;
  std::string error;

  if (!Spawn(request, child, error)) {
    Outcome failed;
    failed.error = std::move(error);
    return failed;
  }

  return Collect(child, options);
}

}
