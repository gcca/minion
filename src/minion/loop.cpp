#include "loop.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "minion/exec/collect.hpp"
#include "minion/log.hpp"
#include "minion/mq/amqp.hpp"
#include "minion/runner.hpp"
#include "minion/store.hpp"

namespace minion::loop {

namespace {

constexpr auto PollWait = std::chrono::milliseconds(500);
constexpr auto SleepSlice = std::chrono::milliseconds(100);
constexpr auto MinBackoff = std::chrono::seconds(1);
constexpr auto MaxBackoff = std::chrono::seconds(30);

std::atomic<bool> stop_requested{false};

void RequestStop(int) { stop_requested.store(true, std::memory_order_relaxed); }

[[nodiscard]] bool Stopping() {
  return stop_requested.load(std::memory_order_relaxed);
}

void InstallSignalHandlers() {
  struct sigaction action{};
  action.sa_handler = RequestStop;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;

  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);

  struct sigaction ignore{};
  ignore.sa_handler = SIG_IGN;
  sigemptyset(&ignore.sa_mask);
  sigaction(SIGPIPE, &ignore, nullptr);
}

void SleepFor(std::chrono::milliseconds duration) {
  const auto until = std::chrono::steady_clock::now() + duration;
  while (!Stopping() && std::chrono::steady_clock::now() < until)
    std::this_thread::sleep_for(SleepSlice);
}

}

[[nodiscard]] int Serve(sqlite3 *db, const conf::Settings &settings,
                        const Options &options) {
  InstallSignalHandlers();

  std::string error;

  std::vector<conf::Node> nodes;
  if (!conf::ParseNodes(settings.MINION_MQ_NODES, nodes, error)) {
    log::Error("{}", error);
    return 1;
  }

  std::optional<std::string> specific;
  if (!store::ReadQueue(db, specific, error)) {
    log::Error("{}", error);
    return 1;
  }

  const std::string queue =
      runner::SelectQueue(specific, settings.MINION_GENERIC_QUEUE);
  log::Info("queue={} mode={}", queue,
            specific.has_value() ? "specific" : "generic");

  const mq::Credentials credentials{.user = settings.MINION_MQ_USER,
                                    .password = settings.MINION_MQ_PASSWORD,
                                    .vhost = settings.MINION_MQ_VHOST};
  const runner::Limits limits{.term_grace = settings.MINION_TERM_GRACE,
                              .output_cap = settings.MINION_OUTPUT_CAP};

  store::SqliteCatalog catalog{db};
  exec::ProcessExecutor executor;

  std::chrono::milliseconds backoff = MinBackoff;

  while (!Stopping()) {
    auto broker = mq::AmqpBroker::Connect(nodes, credentials, error);

    if (broker &&
        (!broker->DeclareQueue(queue, error) || !broker->Consume(queue, error)))
      broker.reset();

    if (!broker) {
      log::Warn("mq unavailable retry_in={}ms reason=\"{}\"", backoff.count(),
                error);
      SleepFor(backoff);
      backoff = std::min<std::chrono::milliseconds>(backoff * 2, MaxBackoff);
      continue;
    }

    backoff = MinBackoff;
    log::Info("consuming queue={} node={}:{}", queue, broker->node().host,
              broker->node().port);

    while (!Stopping()) {
      mq::Delivery delivery;
      const mq::Receive received = broker->Next(delivery, PollWait, error);

      if (received == mq::Receive::Timeout)
        continue;

      if (received == mq::Receive::Lost) {
        log::Warn("mq lost node={}:{} reason=\"{}\"", broker->node().host,
                  broker->node().port, error);
        break;
      }

      runner::Verdict verdict = runner::Verdict::Malformed;
      if (!runner::Handle(delivery, *broker, catalog, executor, limits, verdict,
                          error)) {
        log::Warn("ack failed delivery={} verdict={} reason=\"{}\"",
                  delivery.tag, runner::VerdictName(verdict), error);
        break;
      }

      if (options.once)
        return 0;
    }
  }

  log::Info("stopping");
  return 0;
}

}
