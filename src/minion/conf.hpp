#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "minion/log.hpp"

namespace minion::conf {

struct Settings {
  std::string MINION_DB = "data/minion.db";
  std::string MINION_MQ_NODES = "127.0.0.1:5672";
  std::string MINION_MQ_USER = "guest";
  std::string MINION_MQ_PASSWORD = "guest";
  std::string MINION_MQ_VHOST = "/";
  std::string MINION_GENERIC_QUEUE = "minion";
  std::chrono::seconds MINION_TERM_GRACE = std::chrono::seconds(10);
  std::size_t MINION_OUTPUT_CAP = 65536;
  log::Level MINION_LOG_LEVEL = log::Level::Info;
  std::string TZ = "UTC";
};

[[nodiscard]] Settings InitSettings();

struct Node {
  std::string host;
  std::uint16_t port = 5672;

  bool operator==(const Node &) const = default;
};

[[nodiscard]] bool ParseNodes(std::string_view text, std::vector<Node> &nodes,
                              std::string &error);

}
