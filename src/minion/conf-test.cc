#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "minion/conf.hpp"

namespace {

using minion::conf::Node;

class ScopedEnv {
public:
  ScopedEnv(std::string name, const char *value) : name_(std::move(name)) {
    if (const auto *previous = std::getenv(name_.c_str())) {
      had_previous_ = true;
      previous_ = previous;
    }

    if (value)
      ::setenv(name_.c_str(), value, 1);
    else
      ::unsetenv(name_.c_str());
  }

  ScopedEnv(const ScopedEnv &) = delete;
  ScopedEnv &operator=(const ScopedEnv &) = delete;

  ~ScopedEnv() {
    if (had_previous_)
      ::setenv(name_.c_str(), previous_.c_str(), 1);
    else
      ::unsetenv(name_.c_str());
  }

private:
  std::string name_;
  std::string previous_;
  bool had_previous_ = false;
};

TEST(Conf, DefaultsApplyWhenTheEnvironmentIsEmpty) {
  const ScopedEnv db("MINION_DB", nullptr);
  const ScopedEnv nodes("MINION_MQ_NODES", nullptr);
  const ScopedEnv vhost("MINION_MQ_VHOST", nullptr);
  const ScopedEnv generic("MINION_GENERIC_QUEUE", nullptr);
  const ScopedEnv grace("MINION_TERM_GRACE", nullptr);
  const ScopedEnv cap("MINION_OUTPUT_CAP", nullptr);
  const ScopedEnv level("MINION_LOG_LEVEL", nullptr);

  const auto settings = minion::conf::InitSettings();

  EXPECT_EQ(settings.MINION_DB, "data/minion.db");
  EXPECT_EQ(settings.MINION_MQ_NODES, "127.0.0.1:5672");
  EXPECT_EQ(settings.MINION_MQ_VHOST, "/");
  EXPECT_EQ(settings.MINION_GENERIC_QUEUE, "minion");
  EXPECT_EQ(settings.MINION_TERM_GRACE, std::chrono::seconds(10));
  EXPECT_EQ(settings.MINION_OUTPUT_CAP, 65536u);
  EXPECT_EQ(settings.MINION_LOG_LEVEL, minion::log::Level::Info);
}

TEST(Conf, EnvironmentOverridesEveryField) {
  const ScopedEnv db("MINION_DB", "/tmp/other.db");
  const ScopedEnv nodes("MINION_MQ_NODES", "mq1:5672,mq2:5673");
  const ScopedEnv user("MINION_MQ_USER", "worker");
  const ScopedEnv vhost("MINION_MQ_VHOST", "jobs");
  const ScopedEnv generic("MINION_GENERIC_QUEUE", "everyone");
  const ScopedEnv grace("MINION_TERM_GRACE", "3");
  const ScopedEnv cap("MINION_OUTPUT_CAP", "1024");
  const ScopedEnv level("MINION_LOG_LEVEL", "warn");

  const auto settings = minion::conf::InitSettings();

  EXPECT_EQ(settings.MINION_DB, "/tmp/other.db");
  EXPECT_EQ(settings.MINION_MQ_NODES, "mq1:5672,mq2:5673");
  EXPECT_EQ(settings.MINION_MQ_USER, "worker");
  EXPECT_EQ(settings.MINION_MQ_VHOST, "jobs");
  EXPECT_EQ(settings.MINION_GENERIC_QUEUE, "everyone");
  EXPECT_EQ(settings.MINION_TERM_GRACE, std::chrono::seconds(3));
  EXPECT_EQ(settings.MINION_OUTPUT_CAP, 1024u);
  EXPECT_EQ(settings.MINION_LOG_LEVEL, minion::log::Level::Warn);
}

TEST(Conf, EmptyNonPositiveAndUnparseableValuesKeepTheDefault) {
  const ScopedEnv generic("MINION_GENERIC_QUEUE", "");
  const ScopedEnv grace("MINION_TERM_GRACE", "0");
  const ScopedEnv cap("MINION_OUTPUT_CAP", "not-a-number");
  const ScopedEnv level("MINION_LOG_LEVEL", "verbose");

  const auto settings = minion::conf::InitSettings();

  EXPECT_EQ(settings.MINION_GENERIC_QUEUE, "minion");
  EXPECT_EQ(settings.MINION_TERM_GRACE, std::chrono::seconds(10));
  EXPECT_EQ(settings.MINION_OUTPUT_CAP, 65536u);
  EXPECT_EQ(settings.MINION_LOG_LEVEL, minion::log::Level::Info);
}

TEST(ParseNodes, ReadsACommaSeparatedListWithDefaultPort) {
  std::vector<Node> nodes;
  std::string error;

  ASSERT_TRUE(
      minion::conf::ParseNodes("mq1:5673, mq2 ,,10.0.0.3:1", nodes, error))
      << error;

  EXPECT_EQ(nodes,
            (std::vector<Node>{{"mq1", 5673}, {"mq2", 5672}, {"10.0.0.3", 1}}));
}

TEST(ParseNodes, RejectsBadPortsAndEmptyLists) {
  std::vector<Node> nodes{{"keep", 1}};
  std::string error;

  EXPECT_FALSE(minion::conf::ParseNodes("mq:0", nodes, error));
  EXPECT_FALSE(minion::conf::ParseNodes("mq:70000", nodes, error));
  EXPECT_FALSE(minion::conf::ParseNodes("mq:56x", nodes, error));
  EXPECT_FALSE(minion::conf::ParseNodes(":5672", nodes, error));
  EXPECT_FALSE(minion::conf::ParseNodes(" , ", nodes, error));
  EXPECT_FALSE(error.empty());

  EXPECT_EQ(nodes, (std::vector<Node>{{"keep", 1}}));
}

}
