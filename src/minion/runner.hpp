#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "minion/exec/collect.hpp"
#include "minion/mq/broker.hpp"
#include "minion/store.hpp"

namespace minion::runner {

[[nodiscard]] std::string
SelectQueue(const std::optional<std::string> &specific,
            const std::string &generic);

struct Limits {
  std::chrono::seconds term_grace{10};
  std::size_t output_cap = 65536;
};

enum class Verdict {
  Succeeded,
  Failed,
  SpawnFailed,
  Malformed,
  Unknown,
  Disabled,
  LookupFailed,
};

[[nodiscard]] std::string_view VerdictName(Verdict verdict);

[[nodiscard]] exec::SpawnRequest
RequestFor(const store::TaskSpec &spec, const std::vector<std::string> &args);

[[nodiscard]] exec::CollectOptions OptionsFor(const store::TaskSpec &spec,
                                              const Limits &limits);

[[nodiscard]] Verdict Execute(std::string_view body, store::Catalog &catalog,
                              exec::Executor &executor, const Limits &limits);

[[nodiscard]] bool Handle(const mq::Delivery &delivery, mq::Broker &broker,
                          store::Catalog &catalog, exec::Executor &executor,
                          const Limits &limits, Verdict &verdict,
                          std::string &error);

}
