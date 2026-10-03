#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace minion::exec {

class Capture {
public:
  explicit Capture(std::size_t cap);

  void Append(std::string_view chunk);

  [[nodiscard]] std::string Text() const;
  [[nodiscard]] std::uint64_t TotalBytes() const noexcept;
  [[nodiscard]] bool Truncated() const noexcept;

private:
  std::size_t cap_;
  std::size_t head_cap_;
  std::size_t tail_cap_;
  std::string head_;
  std::string tail_;
  std::uint64_t total_ = 0;
};

}
