#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

namespace minion::store {

struct TaskSpec {
  std::int64_t id = 0;
  std::string name;
  std::string path;
  std::optional<std::string> cwd;
  int timeout_s = 300;
  bool enabled = true;
};

[[nodiscard]] bool ReadQueue(sqlite3 *db, std::optional<std::string> &queue,
                             std::string &error);

[[nodiscard]] bool WriteQueue(sqlite3 *db,
                              const std::optional<std::string> &queue,
                              std::string &error);

[[nodiscard]] bool AddTask(sqlite3 *db, const TaskSpec &spec,
                           std::string &error);

[[nodiscard]] bool RemoveTask(sqlite3 *db, std::string_view name, bool &found,
                              std::string &error);

[[nodiscard]] bool SetEnabled(sqlite3 *db, std::string_view name, bool enabled,
                              bool &found, std::string &error);

[[nodiscard]] bool FindTask(sqlite3 *db, std::string_view name,
                            std::optional<TaskSpec> &spec, std::string &error);

[[nodiscard]] bool ListTasks(sqlite3 *db, std::vector<TaskSpec> &specs,
                             std::string &error);

class Catalog {
public:
  virtual ~Catalog() = default;

  [[nodiscard]] virtual bool Find(std::string_view name,
                                  std::optional<TaskSpec> &spec,
                                  std::string &error) = 0;
};

class SqliteCatalog final : public Catalog {
public:
  explicit SqliteCatalog(sqlite3 *db) : db_(db) {}

  [[nodiscard]] bool Find(std::string_view name, std::optional<TaskSpec> &spec,
                          std::string &error) override;

private:
  sqlite3 *db_;
};

}
