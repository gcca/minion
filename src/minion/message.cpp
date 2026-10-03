#include "message.hpp"

#include <cstdint>

#include <flatbuffers/flatbuffers.h>
#include <minion_task_generated.h>

namespace minion::message {

[[nodiscard]] std::string Encode(const Task &task) {
  flatbuffers::FlatBufferBuilder builder;

  std::vector<flatbuffers::Offset<flatbuffers::String>> args;
  args.reserve(task.args.size());
  for (const auto &arg : task.args)
    args.push_back(builder.CreateString(arg));

  flatbuffers::Offset<flatbuffers::String> id;
  if (!task.id.empty())
    id = builder.CreateString(task.id);
  const auto name = builder.CreateString(task.name);
  const auto vector = builder.CreateVector(args);

  schema::TaskBuilder root{builder};
  if (!task.id.empty())
    root.add_id(id);
  root.add_name(name);
  root.add_args(vector);
  schema::FinishTaskBuffer(builder, root.Finish());

  return {reinterpret_cast<const char *>(builder.GetBufferPointer()),
          builder.GetSize()};
}

[[nodiscard]] bool Decode(std::string_view body, Task &task,
                          std::string &error) {
  const auto *data = reinterpret_cast<const std::uint8_t *>(body.data());

  if (body.size() <
          sizeof(flatbuffers::uoffset_t) + flatbuffers::kFileIdentifierLength ||
      !schema::TaskBufferHasIdentifier(data)) {
    error = "not a minion task (missing MNTK identifier)";
    return false;
  }

  flatbuffers::Verifier verifier{data, body.size()};
  if (!schema::VerifyTaskBuffer(verifier)) {
    error = "malformed minion task";
    return false;
  }

  const auto *root = schema::GetTask(data);

  Task decoded;
  decoded.name = root->name()->str();
  if (root->id())
    decoded.id = root->id()->str();
  if (root->args()) {
    decoded.args.reserve(root->args()->size());
    for (const auto *arg : *root->args())
      decoded.args.push_back(arg ? arg->str() : std::string{});
  }

  if (decoded.name.empty()) {
    error = "minion task has an empty name";
    return false;
  }

  task = std::move(decoded);
  return true;
}

}
