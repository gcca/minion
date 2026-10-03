cmake_minimum_required(VERSION 4.3)

project(minion LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_CXX_COMPILER_LAUNCHER
      ${CMAKE_COMMAND}
      -E
      env
      CCACHE_SLOPPINESS=pch_defines,time_macros
      "CCACHE_BASEDIR=${CMAKE_SOURCE_DIR}"
      "${CCACHE_PROGRAM}")
endif()

option(MINION_TEST "Build the unit tests" ON)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles(
  "#include <spawn.h>
   int main() {
     posix_spawn_file_actions_t actions;
     posix_spawn_file_actions_init(&actions);
     return posix_spawn_file_actions_addchdir(&actions, \"/\");
   }"
  HAVE_SPAWN_ADDCHDIR_POSIX)

check_cxx_source_compiles(
  "#include <spawn.h>
   int main() {
     posix_spawn_file_actions_t actions;
     posix_spawn_file_actions_init(&actions);
     return posix_spawn_file_actions_addchdir_np(&actions, \"/\");
   }"
  HAVE_SPAWN_ADDCHDIR_NP)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBRABBITMQ REQUIRED IMPORTED_TARGET librabbitmq)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FlatGen.cmake)
add_flatbuffers_library(minion-schemas schemas/minion_task.fbs)

add_library(
  minion-core OBJECT
  src/minion/conf.cpp
  src/minion/db.cpp
  src/minion/exec/capture.cpp
  src/minion/exec/collect.cpp
  src/minion/exec/spawn.cpp
  src/minion/log.cpp
  src/minion/loop.cpp
  src/minion/message.cpp
  src/minion/mq/amqp.cpp
  src/minion/runner.cpp
  src/minion/schema.cpp
  src/minion/store.cpp)

target_include_directories(minion-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(minion-core SYSTEM
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty)
target_link_libraries(minion-core PUBLIC minion-schemas PkgConfig::LIBRABBITMQ
                                         sqlite3)

if(HAVE_SPAWN_ADDCHDIR_POSIX OR HAVE_SPAWN_ADDCHDIR_NP)
  target_compile_definitions(minion-core PUBLIC HAVE_SPAWN_ADDCHDIR)
endif()
if(HAVE_SPAWN_ADDCHDIR_POSIX)
  target_compile_definitions(minion-core PUBLIC HAVE_SPAWN_ADDCHDIR_POSIX)
endif()

add_executable(minion src/minion.cc)
target_link_libraries(minion PRIVATE minion-core)

function(minion_add_command name)
  add_executable(${name} cmd/${name}.cc)
  target_link_libraries(${name} PRIVATE minion-core)
endfunction()

minion_add_command(minion-initdb)
minion_add_command(minion-queue_set)
minion_add_command(minion-queue_show)
minion_add_command(minion-task_add)
minion_add_command(minion-task_remove)
minion_add_command(minion-task_enable)
minion_add_command(minion-task_disable)
minion_add_command(minion-task_list)
minion_add_command(minion-task_publish)

if(MINION_TEST)
  find_package(GTest REQUIRED)

  enable_testing()

  add_executable(
    minion-test
    src/minion/conf-test.cc
    src/minion/exec/capture-test.cc
    src/minion/exec/collect-test.cc
    src/minion/log-test.cc
    src/minion/message-test.cc
    src/minion/runner-test.cc
    src/minion/schema-test.cc
    src/minion/store-test.cc)

  target_link_libraries(minion-test PRIVATE minion-core GTest::gmock_main)

  include(GoogleTest)
  gtest_discover_tests(minion-test)
endif()
