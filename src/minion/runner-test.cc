#include <optional>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "minion/log.hpp"
#include "minion/message.hpp"
#include "minion/runner.hpp"

namespace {

using minion::exec::CollectOptions;
using minion::exec::Outcome;
using minion::exec::SpawnRequest;
using minion::runner::Verdict;
using minion::store::TaskSpec;
using ::testing::_;
using ::testing::AllOf;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::Return;
using ::testing::SetArgReferee;

class MockCatalog : public minion::store::Catalog {
public:
  MOCK_METHOD(bool, Find,
              (std::string_view name, std::optional<TaskSpec> &spec,
               std::string &error),
              (override));
};

class MockExecutor : public minion::exec::Executor {
public:
  MOCK_METHOD(Outcome, Run,
              (const SpawnRequest &request, const CollectOptions &options),
              (override));
};

class MockBroker : public minion::mq::Broker {
public:
  MOCK_METHOD(minion::mq::Receive, Next,
              (minion::mq::Delivery & delivery, std::chrono::milliseconds wait,
               std::string &error),
              (override));
  MOCK_METHOD(bool, Ack, (std::uint64_t tag, std::string &error), (override));
};

[[nodiscard]] TaskSpec Hydrant(bool enabled = true) {
  TaskSpec spec;
  spec.name = "hydrant";
  spec.path = "/usr/local/bin/blackkeys-hydrant";
  spec.cwd = "/app";
  spec.timeout_s = 120;
  spec.enabled = enabled;
  return spec;
}

[[nodiscard]] Outcome Exited(int code) {
  Outcome outcome;
  outcome.spawned = true;
  outcome.exited = true;
  outcome.exit_code = code;
  return outcome;
}

[[nodiscard]] minion::mq::Delivery DeliveryOf(std::string body) {
  return {.tag = 7, .body = std::move(body), .redelivered = false};
}

[[nodiscard]] std::string HydrantTask() {
  return minion::message::Encode(
      {.id = "t-1", .name = "hydrant", .args = {"--once"}});
}

class RunnerTest : public ::testing::Test {
protected:
  void SetUp() override { minion::log::SetLevel(minion::log::Level::Error); }
  void TearDown() override { minion::log::SetLevel(minion::log::Level::Info); }

  [[nodiscard]] Verdict HandleAndAck(const std::string &body) {
    EXPECT_CALL(broker_, Ack(7, _)).WillOnce(Return(true));

    Verdict verdict = Verdict::Malformed;
    std::string error;
    EXPECT_TRUE(minion::runner::Handle(DeliveryOf(body), broker_, catalog_,
                                       executor_, limits_, verdict, error))
        << error;
    return verdict;
  }

  ::testing::StrictMock<MockCatalog> catalog_;
  ::testing::StrictMock<MockExecutor> executor_;
  ::testing::StrictMock<MockBroker> broker_;
  minion::runner::Limits limits_{.term_grace = std::chrono::seconds(3),
                                 .output_cap = 512};
};

TEST(SelectQueue, PrefersTheSpecificQueue) {
  EXPECT_EQ(minion::runner::SelectQueue("jobs-a", "minion"), "jobs-a");
}

TEST(SelectQueue, FallsBackToTheGenericQueue) {
  EXPECT_EQ(minion::runner::SelectQueue(std::nullopt, "minion"), "minion");
  EXPECT_EQ(minion::runner::SelectQueue(std::string{}, "minion"), "minion");
}

TEST_F(RunnerTest, AnAllowlistedTaskRunsWithItsDatabaseSettings) {
  EXPECT_CALL(catalog_, Find("hydrant", _, _))
      .WillOnce(DoAll(SetArgReferee<1>(std::optional<TaskSpec>{Hydrant()}),
                      Return(true)));

  EXPECT_CALL(
      executor_,
      Run(AllOf(
              Field(&SpawnRequest::command, "/usr/local/bin/blackkeys-hydrant"),
              Field(&SpawnRequest::args, ElementsAre("--once")),
              Field(&SpawnRequest::cwd, std::optional<std::string>{"/app"}),
              Field(&SpawnRequest::setsid, true)),
          AllOf(Field(&CollectOptions::timeout, std::chrono::seconds(120)),
                Field(&CollectOptions::term_grace, std::chrono::seconds(3)),
                Field(&CollectOptions::output_cap, 512u))))
      .WillOnce(Return(Exited(0)));

  EXPECT_EQ(HandleAndAck(HydrantTask()), Verdict::Succeeded);
}

TEST_F(RunnerTest, ANonZeroExitIsStillAcked) {
  EXPECT_CALL(catalog_, Find("hydrant", _, _))
      .WillOnce(DoAll(SetArgReferee<1>(std::optional<TaskSpec>{Hydrant()}),
                      Return(true)));
  EXPECT_CALL(executor_, Run(_, _)).WillOnce(Return(Exited(1)));

  EXPECT_EQ(HandleAndAck(HydrantTask()), Verdict::Failed);
}

TEST_F(RunnerTest, ATimeoutIsAFailure) {
  Outcome outcome = Exited(0);
  outcome.timed_out = true;

  EXPECT_CALL(catalog_, Find("hydrant", _, _))
      .WillOnce(DoAll(SetArgReferee<1>(std::optional<TaskSpec>{Hydrant()}),
                      Return(true)));
  EXPECT_CALL(executor_, Run(_, _)).WillOnce(Return(outcome));

  EXPECT_EQ(HandleAndAck(HydrantTask()), Verdict::Failed);
}

TEST_F(RunnerTest, ASpawnErrorIsAcked) {
  Outcome outcome;
  outcome.error = "No such file or directory";

  EXPECT_CALL(catalog_, Find("hydrant", _, _))
      .WillOnce(DoAll(SetArgReferee<1>(std::optional<TaskSpec>{Hydrant()}),
                      Return(true)));
  EXPECT_CALL(executor_, Run(_, _)).WillOnce(Return(outcome));

  EXPECT_EQ(HandleAndAck(HydrantTask()), Verdict::SpawnFailed);
}

TEST_F(RunnerTest, AnUnknownTaskIsAckedWithoutRunning) {
  EXPECT_CALL(catalog_, Find("hydrant", _, _))
      .WillOnce(
          DoAll(SetArgReferee<1>(std::optional<TaskSpec>{}), Return(true)));

  EXPECT_EQ(HandleAndAck(HydrantTask()), Verdict::Unknown);
}

TEST_F(RunnerTest, ADisabledTaskIsAckedWithoutRunning) {
  EXPECT_CALL(catalog_, Find("hydrant", _, _))
      .WillOnce(DoAll(SetArgReferee<1>(std::optional<TaskSpec>{Hydrant(false)}),
                      Return(true)));

  EXPECT_EQ(HandleAndAck(HydrantTask()), Verdict::Disabled);
}

TEST_F(RunnerTest, ALookupFailureIsAckedWithoutRunning) {
  EXPECT_CALL(catalog_, Find("hydrant", _, _))
      .WillOnce(DoAll(SetArgReferee<2>(std::string{"database is locked"}),
                      Return(false)));

  EXPECT_EQ(HandleAndAck(HydrantTask()), Verdict::LookupFailed);
}

TEST_F(RunnerTest, AMalformedBodyIsAckedWithoutALookup) {
  EXPECT_EQ(HandleAndAck("not a flatbuffer"), Verdict::Malformed);
}

TEST_F(RunnerTest, APathInTheNameFieldIsOnlyEverANameLookup) {
  EXPECT_CALL(catalog_, Find("/bin/sh", _, _))
      .WillOnce(
          DoAll(SetArgReferee<1>(std::optional<TaskSpec>{}), Return(true)));

  EXPECT_EQ(HandleAndAck(minion::message::Encode(
                {.name = "/bin/sh", .args = {"-c", "id"}})),
            Verdict::Unknown);
}

TEST_F(RunnerTest, AFailedAckIsReported) {
  EXPECT_CALL(broker_, Ack(7, _))
      .WillOnce(DoAll(SetArgReferee<1>(std::string{"connection reset"}),
                      Return(false)));

  Verdict verdict = Verdict::Succeeded;
  std::string error;
  EXPECT_FALSE(minion::runner::Handle(DeliveryOf("junk"), broker_, catalog_,
                                      executor_, limits_, verdict, error));
  EXPECT_EQ(verdict, Verdict::Malformed);
  EXPECT_EQ(error, "connection reset");
}

}
