#include "store.hpp"

#include <format>

namespace minion::store {

namespace {

class Stmt {
public:
  Stmt(sqlite3 *db, const char *sql) : db_(db) {
    ok_ = sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) == SQLITE_OK;
  }

  Stmt(const Stmt &) = delete;
  Stmt &operator=(const Stmt &) = delete;

  ~Stmt() { sqlite3_finalize(stmt_); }

  [[nodiscard]] bool ok() const noexcept { return ok_; }
  [[nodiscard]] sqlite3_stmt *get() const noexcept { return stmt_; }

  void Text(int index, std::string_view value) {
    sqlite3_bind_text(stmt_, index, value.data(),
                      static_cast<int>(value.size()), SQLITE_TRANSIENT);
  }

  void OptionalText(int index, const std::optional<std::string> &value) {
    if (value.has_value())
      Text(index, *value);
    else
      sqlite3_bind_null(stmt_, index);
  }

  void Int(int index, int value) { sqlite3_bind_int(stmt_, index, value); }

  [[nodiscard]] std::string Error(std::string_view action) const {
    return std::format("{}: {}", action, sqlite3_errmsg(db_));
  }

private:
  sqlite3 *db_;
  sqlite3_stmt *stmt_ = nullptr;
  bool ok_ = false;
};

[[nodiscard]] std::string ColumnText(sqlite3_stmt *stmt, int index) {
  const auto *text =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, index));
  return text ? std::string{text} : std::string{};
}

[[nodiscard]] std::optional<std::string> ColumnOptionalText(sqlite3_stmt *stmt,
                                                            int index) {
  if (sqlite3_column_type(stmt, index) == SQLITE_NULL)
    return std::nullopt;
  return ColumnText(stmt, index);
}

constexpr const char *TaskColumns = "id, name, path, cwd, timeout_s, enabled";

[[nodiscard]] TaskSpec ReadTask(sqlite3_stmt *stmt) {
  return TaskSpec{.id = sqlite3_column_int64(stmt, 0),
                  .name = ColumnText(stmt, 1),
                  .path = ColumnText(stmt, 2),
                  .cwd = ColumnOptionalText(stmt, 3),
                  .timeout_s = sqlite3_column_int(stmt, 4),
                  .enabled = sqlite3_column_int(stmt, 5) != 0};
}

[[nodiscard]] bool Change(sqlite3 *db, Stmt &stmt, std::string_view action,
                          bool &found, std::string &error) {
  if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
    error = stmt.Error(action);
    return false;
  }

  found = sqlite3_changes(db) > 0;
  return true;
}

}

[[nodiscard]] bool ReadQueue(sqlite3 *db, std::optional<std::string> &queue,
                             std::string &error) {
  Stmt stmt{db, "SELECT queue FROM setting WHERE id = 1"};
  if (!stmt.ok()) {
    error = stmt.Error("reading queue");
    return false;
  }

  if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
    error = "setting row is missing, run minion-initdb";
    return false;
  }

  queue = ColumnOptionalText(stmt.get(), 0);
  return true;
}

[[nodiscard]] bool WriteQueue(sqlite3 *db,
                              const std::optional<std::string> &queue,
                              std::string &error) {
  Stmt stmt{db, "UPDATE setting SET queue = ?, updated_at = CURRENT_TIMESTAMP "
                "WHERE id = 1"};
  if (!stmt.ok()) {
    error = stmt.Error("writing queue");
    return false;
  }

  stmt.OptionalText(1, queue);

  bool found = false;
  if (!Change(db, stmt, "writing queue", found, error))
    return false;

  if (!found) {
    error = "setting row is missing, run minion-initdb";
    return false;
  }

  return true;
}

[[nodiscard]] bool AddTask(sqlite3 *db, const TaskSpec &spec,
                           std::string &error) {
  Stmt stmt{db, "INSERT INTO task (name, path, cwd, timeout_s, enabled) "
                "VALUES (?, ?, ?, ?, ?)"};
  if (!stmt.ok()) {
    error = stmt.Error("adding task");
    return false;
  }

  stmt.Text(1, spec.name);
  stmt.Text(2, spec.path);
  stmt.OptionalText(3, spec.cwd);
  stmt.Int(4, spec.timeout_s);
  stmt.Int(5, spec.enabled ? 1 : 0);

  if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
    error = stmt.Error(std::format("adding task '{}'", spec.name));
    return false;
  }

  return true;
}

[[nodiscard]] bool RemoveTask(sqlite3 *db, std::string_view name, bool &found,
                              std::string &error) {
  Stmt stmt{db, "DELETE FROM task WHERE name = ?"};
  if (!stmt.ok()) {
    error = stmt.Error("removing task");
    return false;
  }

  stmt.Text(1, name);
  return Change(db, stmt, "removing task", found, error);
}

[[nodiscard]] bool SetEnabled(sqlite3 *db, std::string_view name, bool enabled,
                              bool &found, std::string &error) {
  Stmt stmt{db, "UPDATE task SET enabled = ?, updated_at = "
                "CURRENT_TIMESTAMP WHERE name = ?"};
  if (!stmt.ok()) {
    error = stmt.Error("updating task");
    return false;
  }

  stmt.Int(1, enabled ? 1 : 0);
  stmt.Text(2, name);
  return Change(db, stmt, "updating task", found, error);
}

[[nodiscard]] bool FindTask(sqlite3 *db, std::string_view name,
                            std::optional<TaskSpec> &spec, std::string &error) {
  const std::string sql =
      std::format("SELECT {} FROM task WHERE name = ?", TaskColumns);
  Stmt stmt{db, sql.c_str()};
  if (!stmt.ok()) {
    error = stmt.Error("finding task");
    return false;
  }

  stmt.Text(1, name);

  switch (sqlite3_step(stmt.get())) {
  case SQLITE_ROW:
    spec = ReadTask(stmt.get());
    return true;
  case SQLITE_DONE:
    spec.reset();
    return true;
  default:
    error = stmt.Error("finding task");
    return false;
  }
}

[[nodiscard]] bool ListTasks(sqlite3 *db, std::vector<TaskSpec> &specs,
                             std::string &error) {
  const std::string sql =
      std::format("SELECT {} FROM task ORDER BY name", TaskColumns);
  Stmt stmt{db, sql.c_str()};
  if (!stmt.ok()) {
    error = stmt.Error("listing tasks");
    return false;
  }

  std::vector<TaskSpec> rows;
  int rc = SQLITE_ROW;
  while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW)
    rows.push_back(ReadTask(stmt.get()));

  if (rc != SQLITE_DONE) {
    error = stmt.Error("listing tasks");
    return false;
  }

  specs = std::move(rows);
  return true;
}

[[nodiscard]] bool SqliteCatalog::Find(std::string_view name,
                                       std::optional<TaskSpec> &spec,
                                       std::string &error) {
  return FindTask(db_, name, spec, error);
}

}
