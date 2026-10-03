#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <rabbitmq-c/amqp.h>

#include "minion/conf.hpp"
#include "minion/mq/broker.hpp"

namespace minion::mq {

struct Credentials {
  std::string user;
  std::string password;
  std::string vhost;
};

class AmqpBroker final : public Broker {
public:
  [[nodiscard]] static std::unique_ptr<AmqpBroker>
  Connect(const std::vector<conf::Node> &nodes, const Credentials &credentials,
          std::string &error);

  AmqpBroker(const AmqpBroker &) = delete;
  AmqpBroker &operator=(const AmqpBroker &) = delete;

  ~AmqpBroker() override;

  [[nodiscard]] const conf::Node &node() const noexcept { return node_; }

  [[nodiscard]] bool DeclareQueue(const std::string &queue, std::string &error);

  [[nodiscard]] bool Consume(const std::string &queue, std::string &error);

  [[nodiscard]] bool Publish(const std::string &queue, std::string_view body,
                             std::string &error);

  [[nodiscard]] Receive Next(Delivery &delivery, std::chrono::milliseconds wait,
                             std::string &error) override;

  [[nodiscard]] bool Ack(std::uint64_t tag, std::string &error) override;

private:
  AmqpBroker(amqp_connection_state_t connection, conf::Node node);

  [[nodiscard]] bool Expect(std::string_view action, std::string &error);

  amqp_connection_state_t connection_;
  conf::Node node_;
  bool open_ = true;
};

}
