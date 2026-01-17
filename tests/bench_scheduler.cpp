#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "NanoLogCpp17.h"
#include "scheduler.h"

namespace {
void init_nano_log() {
    const std::vector<std::string> candidates = {
        "/tmp/taskscheduler.log",
        "./taskscheduler.log",
        "/dev/null",
    };
    for (const auto &path : candidates) {
        try {
            NanoLog::setLogFile(path.c_str());
            NanoLog::setLogLevel(NanoLog::LogLevels::NOTICE);
            NanoLog::preallocate();
            return;
        } catch (const std::exception &e) {
            std::cerr << "NanoLog setLogFile failed for " << path << ": " << e.what() << "\n";
        }
    }
    std::cerr << "NanoLog initialization failed; continuing without logging\n";
}

struct NanoLogInit {
    NanoLogInit() { init_nano_log(); }
};
const NanoLogInit nanolog_init;
} // namespace

using namespace std::chrono_literals;

TEST_CASE("enqueue throughput benchmark") {
    SchedulerOptions opts;
    opts.quota.total_cpu = 8;
    opts.quota.total_mem_mb = 4096;
    opts.max_queue_size = 1000;

    Scheduler sched(opts);
    sched.start();

    BENCHMARK("submit trivial echo") {
        JobSpec spec;
        spec.cmd = "echo bench";
        spec.cpu_cores = 1;
        spec.memory_mb = 32;
        spec.timeout_sec = 2;
        int id = sched.submit(spec);
        return id;
    };

    // allow in-flight tasks to finish
    for (int i = 0; i < 50 && !sched.idle(); ++i) {
        std::this_thread::sleep_for(50ms);
    }
    sched.stop();
}
