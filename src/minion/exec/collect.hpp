#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include "minion/exec/spawn.hpp"

namespace minion::exec {

struct CollectOptions {
  std::chrono::seconds timeout{300};
  std::chrono::seconds term_grace{10};
  std::chrono::seconds kill_grace{5};
  std::size_t output_cap = 65536;
};

struct Outcome {
  bool spawned = false;
  bool timed_out = false;
  bool exited = false;
  bool unreaped = false;
  int exit_code = -1;
  int term_signal = 0;
  std::string out;
  std::string err;
  std::string error;
  std::uint64_t out_bytes = 0;
  std::uint64_t err_bytes = 0;
  bool truncated = false;
  std::chrono::milliseconds duration{0};

  [[nodiscard]] bool Succeeded() const noexcept {
    return spawned && !timed_out && exited && exit_code == 0;
  }
};

[[nodiscard]] Outcome Collect(Spawned &child, const CollectOptions &options);

class Executor {
public:
  virtual ~Executor() = default;

  [[nodiscard]] virtual Outcome Run(const SpawnRequest &request,
                                    const CollectOptions &options) = 0;
};

class ProcessExecutor final : public Executor {
public:
  [[nodiscard]] Outcome Run(const SpawnRequest &request,
                            const CollectOptions &options) override;
};

}
