cmake_minimum_required(VERSION 3.14)
project(leveldb VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 生成 compile_commands.json，供 clangd 等编辑器插件使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug)
endif()

option(LEVELDB_BUILD_TESTS "Build leveldb unit tests" ON)
option(LEVELDB_SANITIZE "Enable AddressSanitizer and UBSan" OFF)

# ---------------- 库 ----------------
add_library(leveldb
  util/arena.cc
  util/status.cc
)
target_include_directories(leveldb
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${PROJECT_SOURCE_DIR}
)
target_compile_options(leveldb PRIVATE -Wall -Wextra)

if(LEVELDB_SANITIZE)
  target_compile_options(leveldb PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(leveldb PUBLIC -fsanitize=address,undefined)
endif()

# ---------------- 测试 ----------------
if(LEVELDB_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)
  find_package(Threads REQUIRED)
  include(GoogleTest)

  function(leveldb_test name)
    add_executable(${name} test/${name}.cc)
    target_link_libraries(${name} PRIVATE leveldb GTest::gtest_main Threads::Threads)
    # 测试需要访问内部头文件，如 "util/arena.h"
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR})
    target_compile_options(${name} PRIVATE -Wall -Wextra)
    gtest_discover_tests(${name})
  endfunction()

  leveldb_test(arena_test)
  leveldb_test(random_test)
  leveldb_test(skiplist_test)
  leveldb_test(slice_test)
  leveldb_test(status_test)
endif()
