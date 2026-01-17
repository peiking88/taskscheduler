cmake_minimum_required(VERSION 3.26)
project(taskscheduler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_PERSISTENCE "Enable SQLite job store" ON)
option(ENABLE_BACKWARD "Enable backward-cpp stack traces" ON)
option(ENABLE_TESTS "Build tests and benchmarks" ON)

find_package(SQLite3 QUIET)
if(ENABLE_PERSISTENCE AND SQLite3_FOUND)
  message(STATUS "Building with SQLite3 persistence")
  add_compile_definitions(TASKSCHEDULER_ENABLE_SQLITE)
  set(TASKSCHEDULER_SQLITE_LIB SQLite::SQLite3)
else()
  message(STATUS "SQLite3 not found or disabled; persistence stubs only")
  set(TASKSCHEDULER_SQLITE_LIB "")
endif()

set(BACKWARD_ENABLE ON)
if(ENABLE_BACKWARD AND EXISTS ${CMAKE_SOURCE_DIR}/external/backward-cpp/CMakeLists.txt)
  add_subdirectory(external/backward-cpp)
  # Fallback: if heavy dependencies (libbfd/libdw) are missing, force header-only path.
  add_compile_definitions(BACKWARD_HAS_DW=0 BACKWARD_HAS_BFD=0)
  if(TARGET backward::backward)
    set(TASKSCHEDULER_BACKWARD_LIB backward::backward)
  elseif(TARGET backward)
    add_library(backward::backward ALIAS backward)
    set(TASKSCHEDULER_BACKWARD_LIB backward::backward)
  else()
    message(WARNING "backward-cpp target not found after add_subdirectory; disabling stack traces")
    add_compile_definitions(BACKWARD_DISABLE)
    set(TASKSCHEDULER_BACKWARD_LIB "")
  endif()
  message(STATUS "Building with backward-cpp from external/backward-cpp")
else()
  message(WARNING "Backward-cpp submodule not found; stack traces will be disabled")
  add_compile_definitions(BACKWARD_DISABLE)
  set(TASKSCHEDULER_BACKWARD_LIB "")
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
if(ENABLE_BACKWARD AND EXISTS ${PROJECT_SOURCE_DIR}/external/backward-cpp)
  target_include_directories(taskscheduler PUBLIC ${PROJECT_SOURCE_DIR}/external/backward-cpp)
endif()
target_link_libraries(taskscheduler PRIVATE nanolog ${TASKSCHEDULER_SQLITE_LIB} pthread ${TASKSCHEDULER_BACKWARD_LIB})

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
