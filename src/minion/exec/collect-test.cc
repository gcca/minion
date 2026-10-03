#include <string>

#include <gtest/gtest.h>

#include "minion/exec/collect.hpp"
#include "minion/exec/spawn.hpp"

namespace {

using minion::exec::CollectOptions;
using minion::exec::Outcome;
using minion::exec::Spawned;
using minion::exec::SpawnRequest;

[[nodiscard]] Outcome Execute(const SpawnRequest &request,
                              CollectOptions options = {}) {
  Spawned child;
  std::string error;

  if (!minion::exec::Spawn(request, child, error)) {
    Outcome failed;
    failed.error = error;
    return failed;
  }

  return minion::exec::Collect(child, options);
}

[[nodiscard]] SpawnRequest Shell(std::string script) {
  return SpawnRequest{.command = "/bin/sh",
                      .args = {"-c", std::move(script)},
                      .cwd = std::nullopt,
                      .setsid = true};
}

TEST(Collect, CapturesStdoutAndReportsTheDecodedExitCode) {
  const Outcome outcome = Execute(Shell("printf 'hello world'"));

  EXPECT_TRUE(outcome.Succeeded());
  EXPECT_TRUE(outcome.exited);
  EXPECT_EQ(outcome.exit_code, 0);
  EXPECT_EQ(outcome.out, "hello world");
  EXPECT_TRUE(outcome.err.empty());
}

TEST(Collect, ANonZeroExitIsReportedAsThatExactCode) {
  const Outcome outcome = Execute(Shell("exit 42"));

  EXPECT_FALSE(outcome.Succeeded());
  EXPECT_TRUE(outcome.exited);
  EXPECT_EQ(outcome.exit_code, 42);
  EXPECT_EQ(outcome.term_signal, 0);
}

TEST(Collect, StreamsAreKeptSeparate) {
  const Outcome outcome = Execute(Shell("printf out; printf err >&2"));

  EXPECT_EQ(outcome.out, "out");
  EXPECT_EQ(outcome.err, "err");
  EXPECT_TRUE(outcome.Succeeded());
}

TEST(Collect, ArgumentsWithSpacesArriveAsOneArgument) {
  const Outcome outcome = Execute(SpawnRequest{.command = "/bin/echo",
                                               .args = {"a b", "c"},
                                               .cwd = std::nullopt,
                                               .setsid = true});

  EXPECT_EQ(outcome.out, "a b c\n");
}

TEST(Collect, AMissingBinaryIsASpawnErrorNotACrash) {
  Spawned child;
  std::string error;

  const bool ok = minion::exec::Spawn(
      SpawnRequest{.command = "/nonexistent/minion-should-not-exist",
                   .args = {},
                   .cwd = std::nullopt,
                   .setsid = true},
      child, error);

  EXPECT_FALSE(ok);
  EXPECT_FALSE(error.empty());
  EXPECT_EQ(child.pid, -1);
}

TEST(Collect, ATimeoutTerminatesTheChildAndIsReported) {
  CollectOptions options;
  options.timeout = std::chrono::seconds{1};
  options.term_grace = std::chrono::seconds{2};

  const Outcome outcome = Execute(Shell("printf early; sleep 30"), options);

  EXPECT_TRUE(outcome.timed_out);
  EXPECT_FALSE(outcome.Succeeded());
  EXPECT_FALSE(outcome.unreaped);
  EXPECT_EQ(outcome.out, "early");
  EXPECT_LT(outcome.duration, std::chrono::seconds{10});
}

TEST(Collect, AChildIgnoringSigtermIsKilled) {
  CollectOptions options;
  options.timeout = std::chrono::seconds{1};
  options.term_grace = std::chrono::seconds{1};
  options.kill_grace = std::chrono::seconds{5};

  const Outcome outcome = Execute(Shell("trap '' TERM; sleep 30"), options);

  EXPECT_TRUE(outcome.timed_out);
  EXPECT_FALSE(outcome.unreaped);
  EXPECT_LT(outcome.duration, std::chrono::seconds{15});
}

TEST(Collect, AChildThatClosesItsStreamsIsStillTimedOut) {
  CollectOptions options;
  options.timeout = std::chrono::seconds{1};
  options.term_grace = std::chrono::seconds{2};
  options.kill_grace = std::chrono::seconds{1};

  const Outcome outcome =
      Execute(Shell("exec >/dev/null 2>&1; sleep 30"), options);

  EXPECT_TRUE(outcome.timed_out);
  EXPECT_FALSE(outcome.unreaped);
  EXPECT_LT(outcome.duration, std::chrono::seconds{10});
}

TEST(Collect, AChildThatClosesItsStreamsAndExitsSucceeds) {
  CollectOptions options;
  options.timeout = std::chrono::seconds{10};
  options.kill_grace = std::chrono::seconds{1};

  const Outcome outcome =
      Execute(Shell("exec >/dev/null 2>&1; sleep 2"), options);

  EXPECT_TRUE(outcome.Succeeded());
  EXPECT_FALSE(outcome.unreaped);
}

TEST(Collect, AChildFloodingOneStreamDoesNotDeadlock) {
  CollectOptions options;
  options.timeout = std::chrono::seconds{20};
  options.output_cap = 1024;

  const Outcome outcome = Execute(
      Shell("i=0; while [ $i -lt 400 ]; do "
            "printf '%0.sy' $(seq 1 1000) >&2; i=$((i+1)); done; printf done"),
      options);

  EXPECT_TRUE(outcome.Succeeded());
  EXPECT_EQ(outcome.out, "done");
  EXPECT_GE(outcome.err_bytes, 400u * 1000u);
  EXPECT_TRUE(outcome.truncated);
  EXPECT_LT(outcome.err.size(), 1024u + 64u);
}

TEST(Collect, TheWorkingDirectoryIsHonouredWhenSupported) {
  const Outcome outcome = Execute(SpawnRequest{.command = "/bin/pwd",
                                               .args = {},
                                               .cwd = std::string{"/tmp"},
                                               .setsid = true});

  ASSERT_TRUE(outcome.Succeeded());
#ifdef HAVE_SPAWN_ADDCHDIR
  EXPECT_NE(outcome.out.find("tmp"), std::string::npos);
#else
  GTEST_SKIP() << "posix_spawn_file_actions_addchdir_np unavailable";
#endif
}

TEST(Collect, StdinIsDevNull) {
  CollectOptions options;
  options.timeout = std::chrono::seconds{5};

  const Outcome outcome = Execute(Shell("cat; printf eof"), options);

  EXPECT_TRUE(outcome.Succeeded());
  EXPECT_EQ(outcome.out, "eof");
}

TEST(ProcessExecutor, RunsAndCollects) {
  minion::exec::ProcessExecutor executor;

  const Outcome outcome = executor.Run(Shell("printf ok; exit 3"), {});

  EXPECT_TRUE(outcome.spawned);
  EXPECT_TRUE(outcome.exited);
  EXPECT_EQ(outcome.exit_code, 3);
  EXPECT_EQ(outcome.out, "ok");
}

TEST(ProcessExecutor, ASpawnFailureIsAnOutcomeNotACrash) {
  minion::exec::ProcessExecutor executor;

  const Outcome outcome =
      executor.Run(SpawnRequest{.command = "/nonexistent/minion-missing",
                                .args = {},
                                .cwd = std::nullopt,
                                .setsid = true},
                   {});

  EXPECT_FALSE(outcome.spawned);
  EXPECT_FALSE(outcome.Succeeded());
  EXPECT_FALSE(outcome.error.empty());
}

}
