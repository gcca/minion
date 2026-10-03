#include <cstdio>
#include <optional>
#include <print>
#include <string>
#include <vector>

#include <CLI11.hpp>

#include "minion/cli.hpp"
#include "minion/conf.hpp"
#include "minion/message.hpp"
#include "minion/mq/amqp.hpp"
#include "minion/runner.hpp"
#include "minion/store.hpp"

int main(int argc, char *argv[]) {
  minion::conf::Settings settings = minion::conf::InitSettings();

  CLI::App app{"Publish a task message for minion to run"};

  app.add_option("-d,--db", settings.MINION_DB,
                 "Path to the minion database, read for the target queue")
      ->capture_default_str();

  std::string queue;
  app.add_option("-q,--queue", queue,
                 "Queue to publish to instead of the one in the database");

  minion::message::Task task;
  app.add_option("--id", task.id, "Correlation id");
  app.add_option("name", task.name, "Allowlisted task name")->required();
  app.add_option("args", task.args,
                 "Arguments for the task's executable, after --");

  if (const auto code = minion::cli::Parse(app, argc, argv))
    return *code;

  std::string error;

  if (queue.empty()) {
    const minion::cli::Database db{minion::cli::Open(settings.MINION_DB)};
    if (!db)
      return minion::cli::Failure;

    std::optional<std::string> specific;
    if (!minion::store::ReadQueue(db.get(), specific, error)) {
      std::println(stderr, "{}", error);
      return minion::cli::Failure;
    }
    queue =
        minion::runner::SelectQueue(specific, settings.MINION_GENERIC_QUEUE);
  }

  std::vector<minion::conf::Node> nodes;
  if (!minion::conf::ParseNodes(settings.MINION_MQ_NODES, nodes, error)) {
    std::println(stderr, "{}", error);
    return minion::cli::Failure;
  }

  auto broker =
      minion::mq::AmqpBroker::Connect(nodes,
                                      {.user = settings.MINION_MQ_USER,
                                       .password = settings.MINION_MQ_PASSWORD,
                                       .vhost = settings.MINION_MQ_VHOST},
                                      error);
  if (!broker || !broker->DeclareQueue(queue, error) ||
      !broker->Publish(queue, minion::message::Encode(task), error)) {
    std::println(stderr, "{}", error);
    return minion::cli::Failure;
  }

  std::println("published name={} args={} queue={}", task.name,
               task.args.size(), queue);
  return minion::cli::Ok;
}
