cmake_minimum_required(VERSION 3.26)
project(taskscheduler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_PERSISTENCE "Enable SQLite job store" ON)
option(ENABLE_TESTS "Build tests and benchmarks" ON)
option(ENABLE_BACKWARD "Enable backward-cpp stack trace backend" ON)

set(TASKSCHEDULER_STACKTRACE_BACKEND "auto" CACHE STRING "Stacktrace backend: auto|stacktrace|backward|none")
set_property(CACHE TASKSCHEDULER_STACKTRACE_BACKEND PROPERTY STRINGS auto stacktrace backward none)

find_package(SQLite3 QUIET)
if(ENABLE_PERSISTENCE AND SQLite3_FOUND)
  message(STATUS "Building with SQLite3 persistence")
  add_compile_definitions(TASKSCHEDULER_ENABLE_SQLITE)
  set(TASKSCHEDULER_SQLITE_LIB SQLite::SQLite3)
else()
  message(STATUS "SQLite3 not found or disabled; persistence stubs only")
  set(TASKSCHEDULER_SQLITE_LIB "")
endif()

# Stack traces use C++23 <stacktrace> (no backward-cpp dependency)
# Note: some libstdc++ builds require extra link libs (e.g. libstdc++_libbacktrace)
# for std::stacktrace; detect it at configure-time.
include(CheckCXXSourceCompiles)

find_library(TASKSCHEDULER_STDLIB_STACKTRACE_LIB NAMES stdc++_libbacktrace)

set(_TASKSCHEDULER_OLD_REQUIRED_LIBS "${CMAKE_REQUIRED_LIBRARIES}")
if(TASKSCHEDULER_STDLIB_STACKTRACE_LIB)
  set(CMAKE_REQUIRED_LIBRARIES "${TASKSCHEDULER_STDLIB_STACKTRACE_LIB}")
else()
  set(CMAKE_REQUIRED_LIBRARIES "")
endif()

check_cxx_source_compiles(
"#include <stacktrace>
#include <sstream>
int main() {
    auto st = std::stacktrace::current();
    std::ostringstream oss;
    oss << st;
    return (int)oss.str().size();
}"
TASKSCHEDULER_HAVE_STACKTRACE)

set(CMAKE_REQUIRED_LIBRARIES "${_TASKSCHEDULER_OLD_REQUIRED_LIBS}")

if(TASKSCHEDULER_HAVE_STACKTRACE)
  add_compile_definitions(TASKSCHEDULER_HAVE_STACKTRACE=1)
  message(STATUS "C++23 std::stacktrace is available")
  if(TASKSCHEDULER_STDLIB_STACKTRACE_LIB)
    set(TASKSCHEDULER_STACKTRACE_LIB "${TASKSCHEDULER_STDLIB_STACKTRACE_LIB}")
    message(STATUS "Linking stacktrace helper lib: ${TASKSCHEDULER_STACKTRACE_LIB}")
  else()
    set(TASKSCHEDULER_STACKTRACE_LIB "")
  endif()
else()
  add_compile_definitions(TASKSCHEDULER_HAVE_STACKTRACE=0)
  set(TASKSCHEDULER_STACKTRACE_LIB "")
  message(WARNING "C++23 std::stacktrace not linkable in this toolchain; stack traces will be disabled")
endif()

# Decide which stacktrace backend to use.
set(TASKSCHEDULER_USE_STACKTRACE 0)
set(TASKSCHEDULER_USE_BACKWARD 0)

if(TASKSCHEDULER_STACKTRACE_BACKEND STREQUAL "stacktrace")
  if(NOT TASKSCHEDULER_HAVE_STACKTRACE)
    message(FATAL_ERROR "TASKSCHEDULER_STACKTRACE_BACKEND=stacktrace but std::stacktrace is not linkable in this toolchain")
  endif()
  set(TASKSCHEDULER_USE_STACKTRACE 1)
elseif(TASKSCHEDULER_STACKTRACE_BACKEND STREQUAL "backward")
  if(NOT ENABLE_BACKWARD)
    message(FATAL_ERROR "TASKSCHEDULER_STACKTRACE_BACKEND=backward but ENABLE_BACKWARD=OFF")
  endif()
  set(TASKSCHEDULER_USE_BACKWARD 1)
elseif(TASKSCHEDULER_STACKTRACE_BACKEND STREQUAL "auto")
  if(TASKSCHEDULER_HAVE_STACKTRACE)
    set(TASKSCHEDULER_USE_STACKTRACE 1)
  else()
    if(ENABLE_BACKWARD)
      set(TASKSCHEDULER_USE_BACKWARD 1)
    else()
      message(WARNING "No stacktrace backend available (stacktrace not linkable and backward disabled)")
    endif()
  endif()
elseif(TASKSCHEDULER_STACKTRACE_BACKEND STREQUAL "none")
  # no-op
else()
  message(FATAL_ERROR "Unknown TASKSCHEDULER_STACKTRACE_BACKEND='${TASKSCHEDULER_STACKTRACE_BACKEND}'")
endif()

# Optional: backward-cpp backend
set(TASKSCHEDULER_BACKWARD_LIB "")
if(TASKSCHEDULER_USE_BACKWARD)
  if(EXISTS ${CMAKE_SOURCE_DIR}/external/backward-cpp/CMakeLists.txt)
    add_subdirectory(external/backward-cpp)

    if(TARGET backward::backward)
      set(TASKSCHEDULER_BACKWARD_LIB backward::backward)
    elseif(TARGET backward)
      add_library(backward::backward ALIAS backward)
      set(TASKSCHEDULER_BACKWARD_LIB backward::backward)
    else()
      message(FATAL_ERROR "backward-cpp target not found after add_subdirectory")
    endif()
    message(STATUS "Stacktrace backend: backward-cpp")
  else()
    message(FATAL_ERROR "backward-cpp backend requested but external/backward-cpp not found")
  endif()
elseif(TASKSCHEDULER_USE_STACKTRACE)
  message(STATUS "Stacktrace backend: std::stacktrace")
else()
  message(STATUS "Stacktrace backend: disabled")
endif()

if(ENABLE_TESTS AND EXISTS ${CMAKE_SOURCE_DIR}/external/Catch2/CMakeLists.txt)
  add_subdirectory(external/Catch2)
  list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/external/Catch2/extras)
  message(STATUS "Building with Catch2 from external/Catch2")
else()
  if(ENABLE_TESTS)
    message(WARNING "Catch2 submodule not found; tests will be skipped")
  endif()
endif()

file(GLOB TASKSCHEDULER_SOURCES CONFIGURE_DEPENDS
  src/resource_manager.cpp
  src/cgroup_helper.cpp
  src/metrics.cpp
  src/metrics_http_server.cpp
  src/cron_scheduler.cpp
  src/job_store.cpp
  src/scheduler.cpp
  src/backward.cpp
)

# NanoLog (from external/NanoLog)
set(NANOLOG_SRC
  ${PROJECT_SOURCE_DIR}/external/NanoLog/runtime/NanoLog.cc
  ${PROJECT_SOURCE_DIR}/external/NanoLog/runtime/RuntimeLogger.cc
  ${PROJECT_SOURCE_DIR}/external/NanoLog/runtime/Log.cc
  ${PROJECT_SOURCE_DIR}/external/NanoLog/runtime/LogDecompressor.cc
  ${PROJECT_SOURCE_DIR}/external/NanoLog/runtime/Cycles.cc
  ${PROJECT_SOURCE_DIR}/external/NanoLog/runtime/Perf.cc
  ${PROJECT_SOURCE_DIR}/external/NanoLog/runtime/PerfHelper.cc
  ${PROJECT_SOURCE_DIR}/external/NanoLog/runtime/TimeTrace.cc
  ${PROJECT_SOURCE_DIR}/external/NanoLog/runtime/Util.cc
  ${PROJECT_SOURCE_DIR}/src/nanolog_generated_stubs.cpp
)
add_library(nanolog STATIC ${NANOLOG_SRC})
target_include_directories(nanolog PUBLIC ${PROJECT_SOURCE_DIR}/external/NanoLog/runtime)
target_compile_options(nanolog PRIVATE -UPREPROCESSOR_NANOLOG)

add_library(taskscheduler ${TASKSCHEDULER_SOURCES})
target_include_directories(taskscheduler PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_include_directories(taskscheduler PUBLIC ${PROJECT_SOURCE_DIR}/external/NanoLog/runtime)
target_link_libraries(taskscheduler PRIVATE nanolog ${TASKSCHEDULER_SQLITE_LIB} pthread ${TASKSCHEDULER_STACKTRACE_LIB} ${TASKSCHEDULER_BACKWARD_LIB})

target_compile_definitions(taskscheduler PUBLIC
  TASKSCHEDULER_USE_STACKTRACE=${TASKSCHEDULER_USE_STACKTRACE}
  TASKSCHEDULER_USE_BACKWARD=${TASKSCHEDULER_USE_BACKWARD}
)

if(TASKSCHEDULER_USE_BACKWARD)
  target_include_directories(taskscheduler PUBLIC ${PROJECT_SOURCE_DIR}/external/backward-cpp)
endif()

target_compile_options(taskscheduler PRIVATE -UPREPROCESSOR_NANOLOG)
target_compile_definitions(taskscheduler PRIVATE _GNU_SOURCE)

add_executable(scheduler src/main.cpp)
target_link_libraries(scheduler PRIVATE taskscheduler)

if(ENABLE_TESTS AND TARGET Catch2::Catch2WithMain)
  include(CTest)
  enable_testing()

  add_executable(scheduler_tests tests/test_scheduler.cpp)
  target_link_libraries(scheduler_tests PRIVATE taskscheduler Catch2::Catch2WithMain)
  add_test(NAME scheduler_tests COMMAND scheduler_tests)

  add_executable(scheduler_bench tests/bench_scheduler.cpp)
  target_link_libraries(scheduler_bench PRIVATE taskscheduler Catch2::Catch2WithMain)
endif()
