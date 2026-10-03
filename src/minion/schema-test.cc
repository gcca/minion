#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "minion/db.hpp"
#include "minion/schema.hpp"

namespace {

class SchemaTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(sqlite3_open(":memory:", &db_), SQLITE_OK);
  }

  void TearDown() override { sqlite3_close(db_); }

  [[nodiscard]] bool Exec(const char *sql) {
    std::string error;
    return minion::db::Exec(db_, sql, error);
  }

  sqlite3 *db_ = nullptr;
};

TEST_F(SchemaTest, MigrateBringsAnEmptyDatabaseToTheBuildVersion) {
  int applied = 0;
  std::string error;

  ASSERT_TRUE(minion::schema::Migrate(db_, applied, error)) << error;
  EXPECT_EQ(applied, 1);

  int version = 0;
  ASSERT_TRUE(minion::db::UserVersion(db_, version, error)) << error;
  EXPECT_EQ(version, minion::db::SchemaVersion);
  EXPECT_TRUE(minion::db::RequireSchema(db_, error)) << error;
}

TEST_F(SchemaTest, MigrateIsIdempotent) {
  int applied = 0;
  std::string error;

  ASSERT_TRUE(minion::schema::Migrate(db_, applied, error)) << error;
  ASSERT_TRUE(minion::schema::Migrate(db_, applied, error)) << error;
  EXPECT_EQ(applied, 0);
}

TEST_F(SchemaTest, MigrateRefusesANewerDatabase) {
  ASSERT_TRUE(Exec("PRAGMA user_version = 99"));

  int applied = 0;
  std::string error;

  EXPECT_FALSE(minion::schema::Migrate(db_, applied, error));
  EXPECT_NE(error.find("newer"), std::string::npos);
}

TEST_F(SchemaTest, RequireSchemaRejectsUninitializedAndMismatchedDatabases) {
  std::string error;

  EXPECT_FALSE(minion::db::RequireSchema(db_, error));
  EXPECT_NE(error.find("minion-initdb"), std::string::npos);

  ASSERT_TRUE(Exec("PRAGMA user_version = 7"));
  EXPECT_FALSE(minion::db::RequireSchema(db_, error));
  EXPECT_NE(error.find("7"), std::string::npos);
}

TEST_F(SchemaTest, SettingIsASingleRowWithANullQueue) {
  int applied = 0;
  std::string error;
  ASSERT_TRUE(minion::schema::Migrate(db_, applied, error)) << error;

  EXPECT_FALSE(Exec("INSERT INTO setting (id) VALUES (2)"));
  EXPECT_FALSE(Exec("UPDATE setting SET queue = '' WHERE id = 1"));
  EXPECT_TRUE(Exec("UPDATE setting SET queue = 'jobs' WHERE id = 1"));
}

TEST_F(SchemaTest, TaskPathsAndDirectoriesMustBeAbsolute) {
  int applied = 0;
  std::string error;
  ASSERT_TRUE(minion::schema::Migrate(db_, applied, error)) << error;

  EXPECT_FALSE(Exec("INSERT INTO task (name, path) VALUES ('a', 'rel')"));
  EXPECT_FALSE(Exec(
      "INSERT INTO task (name, path, cwd) VALUES ('b', '/bin/true', 'x')"));
  EXPECT_FALSE(
      Exec("INSERT INTO task (name, path, timeout_s) VALUES ('c', '/x', 0)"));
  EXPECT_TRUE(Exec("INSERT INTO task (name, path) VALUES ('d', '/x')"));
}

TEST_F(SchemaTest, DumpEndsWithTheSchemaVersion) {
  const std::string dump = minion::schema::Dump();

  EXPECT_NE(dump.find("CREATE TABLE task"), std::string::npos);
  EXPECT_NE(dump.find("PRAGMA user_version = 1;"), std::string::npos);
}

TEST(Db, ConnectRefusesAMissingFileAndCreateMakesParents) {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("minion-db-test-" +
       std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
  std::filesystem::remove_all(root);
  const std::string path = (root / "nested" / "minion.db").string();

  std::string error;
  EXPECT_EQ(minion::db::Connect(path, error), nullptr);
  EXPECT_NE(error.find("minion-initdb"), std::string::npos);

  sqlite3 *db = minion::db::Create(path, error);
  ASSERT_NE(db, nullptr) << error;
  sqlite3_close(db);

  db = minion::db::Connect(path, error);
  ASSERT_NE(db, nullptr) << error;
  sqlite3_close(db);

  std::filesystem::remove_all(root);
}

}
