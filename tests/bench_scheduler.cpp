#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

#include "scheduler.h"

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
