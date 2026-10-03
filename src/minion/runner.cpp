#include "runner.hpp"

#include <format>

#include "minion/log.hpp"
#include "minion/message.hpp"

namespace minion::runner {

namespace {

[[nodiscard]] std::string_view OrDash(std::string_view text) {
  return text.empty() ? std::string_view{"-"} : text;
}

void LogStreams(log::Level level, const message::Task &task,
                const exec::Outcome &outcome) {
  if (log::CurrentLevel() > level)
    return;

  if (!outcome.out.empty())
    log::Write(level, std::format("id={} task={} stdout:\n{}", OrDash(task.id),
                                  task.name, outcome.out));
  if (!outcome.err.empty())
    log::Write(level, std::format("id={} task={} stderr:\n{}", OrDash(task.id),
                                  task.name, outcome.err));
}

}

[[nodiscard]] std::string
SelectQueue(const std::optional<std::string> &specific,
            const std::string &generic) {
  if (specific.has_value() && !specific->empty())
    return *specific;
  return generic;
}

[[nodiscard]] std::string_view VerdictName(Verdict verdict) {
  switch (verdict) {
  case Verdict::Succeeded:
    return "succeeded";
  case Verdict::Failed:
    return "failed";
  case Verdict::SpawnFailed:
    return "spawn_failed";
  case Verdict::Malformed:
    return "malformed";
  case Verdict::Unknown:
    return "unknown";
  case Verdict::Disabled:
    return "disabled";
  case Verdict::LookupFailed:
    return "lookup_failed";
  }
  return "unknown";
}

[[nodiscard]] exec::SpawnRequest
RequestFor(const store::TaskSpec &spec, const std::vector<std::string> &args) {
  return exec::SpawnRequest{
      .command = spec.path, .args = args, .cwd = spec.cwd, .setsid = true};
}

[[nodiscard]] exec::CollectOptions OptionsFor(const store::TaskSpec &spec,
                                              const Limits &limits) {
  exec::CollectOptions options;
  options.timeout = std::chrono::seconds(spec.timeout_s);
  options.term_grace = limits.term_grace;
  options.output_cap = limits.output_cap;
  return options;
}

[[nodiscard]] Verdict Execute(std::string_view body, store::Catalog &catalog,
                              exec::Executor &executor, const Limits &limits) {
  message::Task task;
  std::string error;

  if (!message::Decode(body, task, error)) {
    log::Warn("dropped bytes={} reason=\"{}\"", body.size(), error);
    return Verdict::Malformed;
  }

  std::optional<store::TaskSpec> spec;
  if (!catalog.Find(task.name, spec, error)) {
    log::Error("dropped id={} task={} reason=\"{}\"", OrDash(task.id),
               task.name, error);
    return Verdict::LookupFailed;
  }

  if (!spec.has_value()) {
    log::Warn("rejected id={} task={} reason=not_allowlisted", OrDash(task.id),
              task.name);
    return Verdict::Unknown;
  }

  if (!spec->enabled) {
    log::Warn("rejected id={} task={} reason=disabled", OrDash(task.id),
              task.name);
    return Verdict::Disabled;
  }

  log::Info("start id={} task={} path={} args={}", OrDash(task.id), task.name,
            spec->path, task.args.size());

  const exec::Outcome outcome =
      executor.Run(RequestFor(*spec, task.args), OptionsFor(*spec, limits));

  if (!outcome.spawned) {
    log::Error("spawn_error id={} task={} reason=\"{}\"", OrDash(task.id),
               task.name, outcome.error);
    return Verdict::SpawnFailed;
  }

  if (outcome.Succeeded()) {
    log::Info("ran id={} task={} exit=0 duration={}ms truncated={}",
              OrDash(task.id), task.name, outcome.duration.count(),
              outcome.truncated);
    LogStreams(log::Level::Debug, task, outcome);
    return Verdict::Succeeded;
  }

  log::Warn("failed id={} task={} exit={} signal={} timed_out={} "
            "duration={}ms truncated={}",
            OrDash(task.id), task.name, outcome.exit_code, outcome.term_signal,
            outcome.timed_out, outcome.duration.count(), outcome.truncated);
  LogStreams(log::Level::Warn, task, outcome);

  return Verdict::Failed;
}

[[nodiscard]] bool Handle(const mq::Delivery &delivery, mq::Broker &broker,
                          store::Catalog &catalog, exec::Executor &executor,
                          const Limits &limits, Verdict &verdict,
                          std::string &error) {
  if (delivery.redelivered)
    log::Warn("redelivered delivery={}", delivery.tag);

  verdict = Execute(delivery.body, catalog, executor, limits);
  return broker.Ack(delivery.tag, error);
}

}
