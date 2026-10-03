#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace minion::mq {

struct Delivery {
  std::uint64_t tag = 0;
  std::string body;
  bool redelivered = false;
};

enum class Receive { Message, Timeout, Lost };

class Broker {
public:
  virtual ~Broker() = default;

  [[nodiscard]] virtual Receive Next(Delivery &delivery,
                                     std::chrono::milliseconds wait,
                                     std::string &error) = 0;

  [[nodiscard]] virtual bool Ack(std::uint64_t tag, std::string &error) = 0;
};

}
