#pragma once

#include "cron_scheduler.h"
#include "cgroup_helper.h"
#include "job.h"
#include "job_store.h"
#include "logger.h"
#include "metrics.h"
#include "metrics_http_server.h"
#include "resource_manager.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

class Scheduler {
public:
    explicit Scheduler(SchedulerOptions opts);
    ~Scheduler();

    int submit(const JobSpec &spec);
    void start();
    void stop();
    bool idle() const;
    Metrics::Snapshot metrics_snapshot() const;

private:
    bool validate_cmd(const std::string &cmd) const;
    bool pick_next_job(Job &out);
    bool launch_job(Job &job);
    void dispatcher_loop();
    void reaper_loop();
    void psi_loop();
    void cron_loop();
    void restore_from_store();
    double parse_psi_avg10(std::istream &ifs);

    SchedulerOptions opts_;
    ResourceManager rm_;
    std::deque<Job> pending_;
    std::unordered_map<int, Job> running_;
    mutable std::mutex mu_;
    std::condition_variable cv_;

    std::atomic<bool> shutting_down_{false};
    std::atomic<bool> psi_backpressure_{false};

    Metrics metrics_;
    std::unique_ptr<JobStore> store_;
    std::unique_ptr<CronScheduler> cron_sched_;
    std::unique_ptr<MetricsHttpServer> metrics_server_;

    std::vector<std::thread> threads_;
    int next_id_{1};
};
