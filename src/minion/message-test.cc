#include <string>

#include <flatbuffers/flatbuffers.h>
#include <gtest/gtest.h>
#include <minion_task_generated.h>

#include "minion/message.hpp"

namespace {

using minion::message::Task;

TEST(Message, RoundTripsEveryField) {
  const Task task{
      .id = "abc-1", .name = "hydrant", .args = {"--once", "a b", ""}};

  Task decoded;
  std::string error;
  ASSERT_TRUE(
      minion::message::Decode(minion::message::Encode(task), decoded, error))
      << error;

  EXPECT_EQ(decoded, task);
}

TEST(Message, IdAndArgsAreOptional) {
  const Task task{.id = "", .name = "hydrant", .args = {}};

  Task decoded;
  std::string error;
  ASSERT_TRUE(
      minion::message::Decode(minion::message::Encode(task), decoded, error))
      << error;

  EXPECT_EQ(decoded, task);
}

TEST(Message, DecodesABufferWithoutAnArgsVector) {
  flatbuffers::FlatBufferBuilder builder;
  const auto name = builder.CreateString("bare");
  minion::schema::TaskBuilder root{builder};
  root.add_name(name);
  minion::schema::FinishTaskBuffer(builder, root.Finish());

  const std::string body{
      reinterpret_cast<const char *>(builder.GetBufferPointer()),
      builder.GetSize()};

  Task decoded;
  std::string error;
  ASSERT_TRUE(minion::message::Decode(body, decoded, error)) << error;
  EXPECT_EQ(decoded.name, "bare");
  EXPECT_TRUE(decoded.args.empty());
}

TEST(Message, RejectsAnotherIdentifier) {
  std::string body = minion::message::Encode({.name = "x"});
  body[4] = 'X';

  Task decoded;
  std::string error;
  EXPECT_FALSE(minion::message::Decode(body, decoded, error));
  EXPECT_NE(error.find("MNTK"), std::string::npos);
}

TEST(Message, RejectsTruncatedAndGarbageBuffers) {
  const std::string body = minion::message::Encode({.name = "hydrant"});

  Task decoded;
  std::string error;
  EXPECT_FALSE(minion::message::Decode("", decoded, error));
  EXPECT_FALSE(minion::message::Decode("MNTK", decoded, error));
  EXPECT_FALSE(
      minion::message::Decode(body.substr(0, body.size() / 2), decoded, error));

  std::string garbage(64, '\xff');
  garbage.replace(4, 4, "MNTK");
  EXPECT_FALSE(minion::message::Decode(garbage, decoded, error));
}

TEST(Message, RejectsAnEmptyName) {
  Task decoded;
  std::string error;

  EXPECT_FALSE(minion::message::Decode(minion::message::Encode({.name = ""}),
                                       decoded, error));
}

}
