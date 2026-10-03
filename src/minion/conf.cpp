#include "conf.hpp"

#include <charconv>
#include <cstdlib>
#include <format>

namespace minion::conf {

namespace {

void ReadText(const char *name, std::string &slot) {
  if (const auto *raw = std::getenv(name); raw && *raw)
    slot = raw;
}

void ReadSeconds(const char *name, std::chrono::seconds &slot) {
  const auto *raw = std::getenv(name);
  if (!raw)
    return;

  if (const long seconds = std::strtol(raw, nullptr, 10); seconds > 0)
    slot = std::chrono::seconds(seconds);
}

[[nodiscard]] std::string_view Trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
    text.remove_suffix(1);
  return text;
}

}

[[nodiscard]] Settings InitSettings() {
  Settings settings;

  ReadText("MINION_DB", settings.MINION_DB);
  ReadText("MINION_MQ_NODES", settings.MINION_MQ_NODES);
  ReadText("MINION_MQ_USER", settings.MINION_MQ_USER);
  ReadText("MINION_MQ_PASSWORD", settings.MINION_MQ_PASSWORD);
  ReadText("MINION_MQ_VHOST", settings.MINION_MQ_VHOST);
  ReadText("MINION_GENERIC_QUEUE", settings.MINION_GENERIC_QUEUE);
  ReadSeconds("MINION_TERM_GRACE", settings.MINION_TERM_GRACE);

  if (const auto *MINION_OUTPUT_CAP = std::getenv("MINION_OUTPUT_CAP")) {
    if (const long bytes = std::strtol(MINION_OUTPUT_CAP, nullptr, 10);
        bytes > 0)
      settings.MINION_OUTPUT_CAP = static_cast<std::size_t>(bytes);
  }

  if (const auto *MINION_LOG_LEVEL = std::getenv("MINION_LOG_LEVEL")) {
    if (log::Level level; log::ParseLevel(MINION_LOG_LEVEL, level))
      settings.MINION_LOG_LEVEL = level;
  }

  ReadText("TZ", settings.TZ);

  return settings;
}

[[nodiscard]] bool ParseNodes(std::string_view text, std::vector<Node> &nodes,
                              std::string &error) {
  std::vector<Node> parsed;

  while (!text.empty()) {
    const std::size_t comma = text.find(',');
    const std::string_view entry = Trim(text.substr(0, comma));
    text = comma == std::string_view::npos ? std::string_view{}
                                           : text.substr(comma + 1);

    if (entry.empty())
      continue;

    Node node;
    const std::size_t colon = entry.rfind(':');

    if (colon == std::string_view::npos) {
      node.host = entry;
    } else {
      node.host = entry.substr(0, colon);
      const std::string_view digits = entry.substr(colon + 1);

      unsigned port = 0;
      const auto [end, ec] =
          std::from_chars(digits.data(), digits.data() + digits.size(), port);
      if (ec != std::errc{} || end != digits.data() + digits.size() ||
          port == 0 || port > 65535) {
        error = std::format("bad port in MQ node '{}'", entry);
        return false;
      }
      node.port = static_cast<std::uint16_t>(port);
    }

    if (node.host.empty()) {
      error = std::format("missing host in MQ node '{}'", entry);
      return false;
    }

    parsed.push_back(std::move(node));
  }

  if (parsed.empty()) {
    error = "no MQ nodes configured";
    return false;
  }

  nodes = std::move(parsed);
  return true;
}

}
