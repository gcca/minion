#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "minion/schema.hpp"
#include "minion/store.hpp"

namespace {

using minion::store::TaskSpec;

class StoreTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(sqlite3_open(":memory:", &db_), SQLITE_OK);

    int applied = 0;
    std::string error;
    ASSERT_TRUE(minion::schema::Migrate(db_, applied, error)) << error;
  }

  void TearDown() override { sqlite3_close(db_); }

  [[nodiscard]] static TaskSpec Make(std::string name, std::string path) {
    TaskSpec spec;
    spec.name = std::move(name);
    spec.path = std::move(path);
    return spec;
  }

  sqlite3 *db_ = nullptr;
};

TEST_F(StoreTest, QueueStartsGenericAndCanBeSetAndCleared) {
  std::optional<std::string> queue{"sentinel"};
  std::string error;

  ASSERT_TRUE(minion::store::ReadQueue(db_, queue, error)) << error;
  EXPECT_FALSE(queue.has_value());

  ASSERT_TRUE(minion::store::WriteQueue(db_, "jobs-a", error)) << error;
  ASSERT_TRUE(minion::store::ReadQueue(db_, queue, error)) << error;
  EXPECT_EQ(queue, "jobs-a");

  ASSERT_TRUE(minion::store::WriteQueue(db_, std::nullopt, error)) << error;
  ASSERT_TRUE(minion::store::ReadQueue(db_, queue, error)) << error;
  EXPECT_FALSE(queue.has_value());
}

TEST_F(StoreTest, AddedTasksCanBeFoundWithTheirSettings) {
  TaskSpec spec = Make("hydrant", "/usr/local/bin/blackkeys-hydrant");
  spec.cwd = "/app";
  spec.timeout_s = 42;

  std::string error;
  ASSERT_TRUE(minion::store::AddTask(db_, spec, error)) << error;

  std::optional<TaskSpec> found;
  ASSERT_TRUE(minion::store::FindTask(db_, "hydrant", found, error)) << error;
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->path, "/usr/local/bin/blackkeys-hydrant");
  EXPECT_EQ(found->cwd, "/app");
  EXPECT_EQ(found->timeout_s, 42);
  EXPECT_TRUE(found->enabled);
}

TEST_F(StoreTest, AMissingTaskIsNotAnError) {
  std::optional<TaskSpec> found = Make("x", "/x");
  std::string error;

  ASSERT_TRUE(minion::store::FindTask(db_, "nope", found, error)) << error;
  EXPECT_FALSE(found.has_value());
}

TEST_F(StoreTest, DuplicateNamesAreRejected) {
  std::string error;

  ASSERT_TRUE(minion::store::AddTask(db_, Make("a", "/a"), error)) << error;
  EXPECT_FALSE(minion::store::AddTask(db_, Make("a", "/b"), error));
  EXPECT_NE(error.find("UNIQUE"), std::string::npos);
}

TEST_F(StoreTest, TasksCanBeDisabledEnabledAndRemoved) {
  std::string error;
  ASSERT_TRUE(minion::store::AddTask(db_, Make("a", "/a"), error)) << error;

  bool found = false;
  ASSERT_TRUE(minion::store::SetEnabled(db_, "a", false, found, error));
  EXPECT_TRUE(found);

  std::optional<TaskSpec> spec;
  ASSERT_TRUE(minion::store::FindTask(db_, "a", spec, error));
  EXPECT_FALSE(spec->enabled);

  ASSERT_TRUE(minion::store::SetEnabled(db_, "missing", true, found, error));
  EXPECT_FALSE(found);

  ASSERT_TRUE(minion::store::RemoveTask(db_, "a", found, error));
  EXPECT_TRUE(found);
  ASSERT_TRUE(minion::store::RemoveTask(db_, "a", found, error));
  EXPECT_FALSE(found);
}

TEST_F(StoreTest, ListIsOrderedByName) {
  std::string error;
  ASSERT_TRUE(minion::store::AddTask(db_, Make("b", "/b"), error));
  ASSERT_TRUE(minion::store::AddTask(db_, Make("a", "/a"), error));

  std::vector<TaskSpec> specs;
  ASSERT_TRUE(minion::store::ListTasks(db_, specs, error)) << error;
  ASSERT_EQ(specs.size(), 2u);
  EXPECT_EQ(specs[0].name, "a");
  EXPECT_EQ(specs[1].name, "b");
}

TEST_F(StoreTest, SqliteCatalogDelegatesToFindTask) {
  std::string error;
  ASSERT_TRUE(minion::store::AddTask(db_, Make("a", "/a"), error));

  minion::store::SqliteCatalog catalog{db_};
  std::optional<TaskSpec> spec;

  ASSERT_TRUE(catalog.Find("a", spec, error)) << error;
  ASSERT_TRUE(spec.has_value());
  EXPECT_EQ(spec->path, "/a");
}

}
