#include <string>

#include <gtest/gtest.h>

#include "minion/log.hpp"

namespace {

using minion::log::Level;

TEST(Log, ParseLevelAcceptsEveryName) {
  Level level = Level::Error;

  ASSERT_TRUE(minion::log::ParseLevel("debug", level));
  EXPECT_EQ(level, Level::Debug);
  ASSERT_TRUE(minion::log::ParseLevel("info", level));
  EXPECT_EQ(level, Level::Info);
  ASSERT_TRUE(minion::log::ParseLevel("warn", level));
  EXPECT_EQ(level, Level::Warn);
  ASSERT_TRUE(minion::log::ParseLevel("error", level));
  EXPECT_EQ(level, Level::Error);
}

TEST(Log, ParseLevelRejectsUnknownAndLeavesTargetUntouched) {
  Level level = Level::Warn;

  EXPECT_FALSE(minion::log::ParseLevel("INFO", level));
  EXPECT_FALSE(minion::log::ParseLevel("", level));
  EXPECT_FALSE(minion::log::ParseLevel("trace", level));
  EXPECT_EQ(level, Level::Warn);
}

TEST(Log, LevelNameRoundTrips) {
  for (const Level level :
       {Level::Debug, Level::Info, Level::Warn, Level::Error}) {
    Level parsed = Level::Error;
    ASSERT_TRUE(minion::log::ParseLevel(minion::log::LevelName(level), parsed));
    EXPECT_EQ(parsed, level);
  }
}

TEST(Log, FormatLinePadsTheLevelAndKeepsFieldOrder) {
  EXPECT_EQ(
      minion::log::FormatLine(Level::Info, "2026-09-26T00:00:00Z", "ran x"),
      "2026-09-26T00:00:00Z info  ran x");
  EXPECT_EQ(
      minion::log::FormatLine(Level::Error, "2026-09-26T00:00:00Z", "boom"),
      "2026-09-26T00:00:00Z error boom");
}

TEST(Log, SetLevelIsObservable) {
  const Level restore = minion::log::CurrentLevel();

  minion::log::SetLevel(Level::Error);
  EXPECT_EQ(minion::log::CurrentLevel(), Level::Error);
  minion::log::SetLevel(Level::Debug);
  EXPECT_EQ(minion::log::CurrentLevel(), Level::Debug);

  minion::log::SetLevel(restore);
}

}
