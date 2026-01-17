#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

#include "scheduler.h"

using namespace std::chrono_literals;

TEST_CASE("submit and run basic job") {
    SchedulerOptions opts;
    opts.quota.total_cpu = 2;
    opts.quota.total_mem_mb = 512;
    opts.max_queue_size = 10;

    Scheduler sched(opts);
    sched.start();

    JobSpec spec;
    spec.cmd = "echo test";
    spec.cpu_cores = 1;
    spec.memory_mb = 64;
    spec.timeout_sec = 5;

    int id = sched.submit(spec);
    REQUIRE(id > 0);

    // wait for completion
    for (int i = 0; i < 50 && !sched.idle(); ++i) {
        std::this_thread::sleep_for(100ms);
    }
    REQUIRE(sched.idle());
    sched.stop();
}
