#include "capture.hpp"

#include <algorithm>
#include <format>

namespace minion::exec {

Capture::Capture(std::size_t cap)
    : cap_(cap), head_cap_((cap + 1) / 2), tail_cap_(cap - (cap + 1) / 2) {}

void Capture::Append(std::string_view chunk) {
  total_ += chunk.size();

  if (cap_ == 0)
    return;

  if (head_.size() < head_cap_) {
    const std::size_t take = std::min(head_cap_ - head_.size(), chunk.size());
    head_.append(chunk.substr(0, take));
    chunk.remove_prefix(take);
  }

  if (chunk.empty() || tail_cap_ == 0)
    return;

  tail_.append(chunk);

  if (tail_.size() > tail_cap_ * 2)
    tail_.erase(0, tail_.size() - tail_cap_);
}

[[nodiscard]] std::string Capture::Text() const {
  if (cap_ == 0)
    return {};

  if (total_ <= cap_)
    return head_ + tail_;

  std::string_view tail{tail_};
  if (tail.size() > tail_cap_)
    tail.remove_prefix(tail.size() - tail_cap_);

  const std::uint64_t dropped =
      total_ - static_cast<std::uint64_t>(head_.size()) - tail.size();

  return std::format("{}\n...[{} bytes elided]...\n{}", head_, dropped, tail);
}

[[nodiscard]] std::uint64_t Capture::TotalBytes() const noexcept {
  return total_;
}

[[nodiscard]] bool Capture::Truncated() const noexcept { return total_ > cap_; }

}
