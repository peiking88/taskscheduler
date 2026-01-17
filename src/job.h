#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

struct JobSpec {
    std::string cmd;        // 要执行的命令
    int cpu_cores{1};       // 需要的CPU核数
    std::size_t memory_mb{256};
    int timeout_sec{0};     // 0 表示无限制
    int priority{0};        // 越大优先级越高
};

enum class JobStatus {
    Pending,
    Running,
    Succeeded,
    Failed,
    Timeout,
    Cancelled
};

enum class PersistStatus {
    Queued,
    Running,
    Succeeded,
    Failed,
    Timeout,
    LaunchFailed
};

struct ResourceQuota {
    int total_cpu{4};
    std::size_t total_mem_mb{2048};
};

struct CgroupConfig {
    bool enabled{false};
    std::string base_path{"/sys/fs/cgroup/scheduler"};
    int cpu_period_us{100000};
};

struct SchedulerOptions {
    ResourceQuota quota;
    CgroupConfig cgroup;
    int max_queue_size{1000};
    int kill_grace_sec{2};
    bool enable_priority{false};
    bool enable_psi_monitor{false};
    std::vector<std::string> cmd_whitelist;
    std::vector<std::string> cmd_blacklist;
    std::string workdir;
    int metrics_http_port{-1};
    int rlimit_nofile{-1};
    bool disable_core_dump{true};
    bool enable_persistence{false};
    std::string db_path{"state/tasks.db"};
    bool enable_cron{false};
    int cron_tick_ms{1000};
};

struct CronExpression {
    // 仅支持 @every <n>s 简化表达式
    std::string raw;
    std::chrono::seconds interval{0};
    static std::optional<CronExpression> parse(std::string_view expr);
    std::chrono::system_clock::time_point next_run(std::chrono::system_clock::time_point from) const;
};

struct CronTemplate {
    bool enabled{true};
    CronExpression cron;
    JobSpec spec;
    std::chrono::system_clock::time_point next_run;
};

struct Job {
    int id{0};
    JobSpec spec;
    JobStatus status{JobStatus::Pending};
    pid_t pid{-1};
    pid_t pgid{-1};
    bool sigterm_sent{false};
    std::optional<std::chrono::steady_clock::time_point> kill_deadline;
    std::chrono::steady_clock::time_point enqueue_time{};
    std::chrono::steady_clock::time_point start_time{};
    std::chrono::steady_clock::time_point end_time{};
    int exit_code{0};
    std::string cgroup_path;
};

inline std::string to_string(JobStatus s) {
    switch (s) {
    case JobStatus::Pending: return "pending";
    case JobStatus::Running: return "running";
    case JobStatus::Succeeded: return "succeeded";
    case JobStatus::Failed: return "failed";
    case JobStatus::Timeout: return "timeout";
    case JobStatus::Cancelled: return "cancelled";
    }
    return "unknown";
}
