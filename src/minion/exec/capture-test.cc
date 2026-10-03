#include <string>

#include <gtest/gtest.h>

#include "minion/exec/capture.hpp"

namespace {

using minion::exec::Capture;

TEST(Capture, KeepsEverythingUnderTheCapVerbatim) {
  Capture capture{64};
  capture.Append("hello ");
  capture.Append("world");

  EXPECT_EQ(capture.Text(), "hello world");
  EXPECT_EQ(capture.TotalBytes(), 11u);
  EXPECT_FALSE(capture.Truncated());
}

TEST(Capture, BoundsMemoryWhileStillCountingEveryByte) {
  constexpr std::size_t cap = 256;
  Capture capture{cap};

  const std::string chunk(4096, 'x');
  for (int i = 0; i < 256; ++i)
    capture.Append(chunk);

  EXPECT_EQ(capture.TotalBytes(), 256u * 4096u);
  EXPECT_TRUE(capture.Truncated());
  EXPECT_LT(capture.Text().size(), cap + 64);
}

TEST(Capture, KeepsBothTheHeadAndTheTail) {
  Capture capture{32};
  capture.Append("HEAD");
  capture.Append(std::string(4096, '.'));
  capture.Append("TAIL");

  const std::string text = capture.Text();

  EXPECT_TRUE(text.starts_with("HEAD"));
  EXPECT_TRUE(text.ends_with("TAIL"));
  EXPECT_NE(text.find("bytes elided"), std::string::npos);
}

TEST(Capture, AZeroCapDiscardsEverythingButStillCounts) {
  Capture capture{0};
  capture.Append("anything");

  EXPECT_EQ(capture.Text(), "");
  EXPECT_EQ(capture.TotalBytes(), 8u);
  EXPECT_TRUE(capture.Truncated());
}

TEST(Capture, ExactlyAtTheCapIsNotTruncated) {
  Capture capture{4};
  capture.Append("abcd");

  EXPECT_EQ(capture.Text(), "abcd");
  EXPECT_FALSE(capture.Truncated());
}

}
