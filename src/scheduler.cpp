#include "scheduler.h"

#include <algorithm>
#include <csignal>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <functional>
#include <sstream>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "NanoLogCpp17.h"

#if defined(TASKSCHEDULER_USE_STACKTRACE) && TASKSCHEDULER_USE_STACKTRACE
#include <stacktrace>
#endif
#if defined(TASKSCHEDULER_USE_BACKWARD) && TASKSCHEDULER_USE_BACKWARD
#include <backward.hpp>
#endif

using namespace NanoLog::LogLevels;
namespace {
void print_stack(const char *ctx) {
#if defined(TASKSCHEDULER_USE_BACKWARD) && TASKSCHEDULER_USE_BACKWARD
    backward::StackTrace st;
    st.load_here(64);
    backward::Printer p;
    p.object = true;
    p.color_mode = backward::ColorMode::automatic;
    NANO_LOG(ERROR, "[STACK] %s", ctx);
    std::ostringstream oss;
    p.print(st, oss);
    NANO_LOG(ERROR, "%s", oss.str().c_str());
#elif defined(TASKSCHEDULER_USE_STACKTRACE) && TASKSCHEDULER_USE_STACKTRACE && defined(TASKSCHEDULER_HAVE_STACKTRACE) && TASKSCHEDULER_HAVE_STACKTRACE
    NANO_LOG(ERROR, "[STACK] %s", ctx);
    std::ostringstream oss;
    oss << std::stacktrace::current();
    NANO_LOG(ERROR, "%s", oss.str().c_str());
#else
    // 可能原因：工具链 std::stacktrace 链接实现缺失；或显式选择关闭。
    NANO_LOG(ERROR, "[STACK] %s (stacktrace unavailable)", ctx);
#endif
}

template <class Fn>
void run_guarded(const char *ctx, Fn &&fn) {
    try {
        fn();
    } catch (const std::exception &e) {
        auto msg = std::string("Exception in ") + ctx + ": " + e.what();
        NANO_LOG(ERROR, "%s", msg.c_str());
        print_stack(ctx);
    } catch (...) {
        auto msg = std::string("Unknown exception in ") + ctx;
        NANO_LOG(ERROR, "%s", msg.c_str());
        print_stack(ctx);
    }
}
}

Scheduler::Scheduler(SchedulerOptions opts)
    : opts_(std::move(opts)), rm_(opts_.quota) {
    if (opts_.enable_persistence) {
        store_ = std::make_unique<JobStore>();
        store_->init(opts_.db_path);
    }
    if (opts_.enable_cron) {
        cron_sched_ = std::make_unique<CronScheduler>();
    }
    if (opts_.metrics_http_port > 0) {
        metrics_server_ = std::make_unique<MetricsHttpServer>();
    }
}

Scheduler::~Scheduler() { stop(); }

bool Scheduler::validate_cmd(const std::string &cmd) const {
    auto first_space = cmd.find(' ');
    std::string bin = first_space == std::string::npos ? cmd : cmd.substr(0, first_space);
    if (!opts_.cmd_whitelist.empty()) {
        bool allowed = std::any_of(opts_.cmd_whitelist.begin(), opts_.cmd_whitelist.end(), [&](const std::string &w) { return w == bin; });
        if (!allowed) return false;
    }
    bool blocked = std::any_of(opts_.cmd_blacklist.begin(), opts_.cmd_blacklist.end(), [&](const std::string &b) { return b == bin; });
    return !blocked;
}

int Scheduler::submit(const JobSpec &spec) {
    if (!validate_cmd(spec.cmd)) {
        metrics_.inc_rejected();
        NANO_LOG(WARNING, "%s", "command rejected by whitelist/blacklist");
        return -1;
    }

    std::unique_lock lk(mu_);
    if (static_cast<int>(pending_.size()) >= opts_.max_queue_size) {
        metrics_.inc_rejected();
        NANO_LOG(WARNING, "queue full size=%zu, cmd=%s", pending_.size(), spec.cmd.c_str());
        return -1;
    }
    Job job;
    job.id = next_id_++;
    job.spec = spec;
    job.status = JobStatus::Pending;
    job.enqueue_time = std::chrono::steady_clock::now();
    pending_.push_back(job);
    metrics_.inc_submitted();
    metrics_.set_pending(static_cast<long long>(pending_.size()));

    if (store_) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        store_->insert_job(spec, PersistStatus::Queued, ms);
    }

    NANO_LOG(NOTICE, "job queued id=%d cmd=%s cpu=%d mem_mb=%zu pending=%zu", job.id, spec.cmd.c_str(), spec.cpu_cores, spec.memory_mb, pending_.size());

    lk.unlock();
    cv_.notify_all();
    return job.id;
}

void Scheduler::start() {
    shutting_down_.store(false);
    restore_from_store();

    threads_.emplace_back([this] { run_guarded("dispatcher_loop", [this] { dispatcher_loop(); }); });
    threads_.emplace_back([this] { run_guarded("reaper_loop", [this] { reaper_loop(); }); });
    if (opts_.enable_psi_monitor) {
        threads_.emplace_back([this] { run_guarded("psi_loop", [this] { psi_loop(); }); });
    }
    if (opts_.enable_cron && cron_sched_) {
        threads_.emplace_back([this] { run_guarded("cron_loop", [this] { cron_loop(); }); });
    }
    if (metrics_server_) {
        metrics_server_->start(opts_.metrics_http_port, [this] { return metrics_.to_prometheus(); });
    }
}

void Scheduler::stop() {
    if (shutting_down_.exchange(true)) return;
    cv_.notify_all();
    if (metrics_server_) metrics_server_->stop();
    for (auto &t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
}

bool Scheduler::idle() const {
    std::lock_guard lk(mu_);
    return pending_.empty() && running_.empty();
}

Metrics::Snapshot Scheduler::metrics_snapshot() const { return metrics_.snapshot(); }

bool Scheduler::pick_next_job(Job &out) {
    if (pending_.empty()) return false;
    if (opts_.enable_priority) {
        auto it = std::max_element(pending_.begin(), pending_.end(), [](const Job &a, const Job &b) {
            if (a.spec.priority == b.spec.priority) return a.id > b.id; // FIFO when equal priority
            return a.spec.priority < b.spec.priority;
        });
        out = *it;
        pending_.erase(it);
    } else {
        out = pending_.front();
        pending_.pop_front();
    }
    metrics_.set_pending(static_cast<long long>(pending_.size()));
    return true;
}

bool Scheduler::launch_job(Job &job) {
    std::string cg_path;
    if (opts_.cgroup.enabled) {
        cg_path = create_cgroup_for_job(job.id, job.spec.cpu_cores, job.spec.memory_mb, opts_.cgroup);
        if (cg_path.empty()) {
            NANO_LOG(WARNING, "create_cgroup failed for job id=%d", job.id);
        }
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        auto msg = std::string("fork failed: ") + std::strerror(errno);
        NANO_LOG(ERROR, "%s", msg.c_str());
        metrics_.inc_launch_failed();
        rm_.release(job.spec.cpu_cores, job.spec.memory_mb);
        return false;
    }

    if (pid == 0) {
        // child
        ::setpgid(0, 0);
        if (!cg_path.empty()) {
            attach_pid_to_cgroup(::getpid(), cg_path);
        }
        if (opts_.rlimit_nofile >= 0) {
            struct rlimit rl{static_cast<rlim_t>(opts_.rlimit_nofile), static_cast<rlim_t>(opts_.rlimit_nofile)};
            setrlimit(RLIMIT_NOFILE, &rl);
        }
        if (opts_.disable_core_dump) {
            struct rlimit rl{0, 0};
            setrlimit(RLIMIT_CORE, &rl);
        }
        if (!opts_.workdir.empty()) {
            chdir(opts_.workdir.c_str());
        }
        execl("/bin/sh", "sh", "-c", job.spec.cmd.c_str(), nullptr);
        _exit(127);
    }

    // parent
    job.pid = pid;
    job.pgid = pid;
    job.start_time = std::chrono::steady_clock::now();
    job.status = JobStatus::Running;
    job.cgroup_path = cg_path;

    if (store_) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        store_->update_status(job.id, PersistStatus::Running, 0, ms, 0);
    }

    running_[job.id] = job;
    metrics_.inc_running();
    NANO_LOG(NOTICE, "job started id=%d pid=%d cmd=%s cpu=%d mem_mb=%zu cg=%s", job.id, job.pid, job.spec.cmd.c_str(), job.spec.cpu_cores, job.spec.memory_mb, job.cgroup_path.c_str());
    return true;
}

void Scheduler::dispatcher_loop() {
    while (!shutting_down_.load()) {
        std::unique_lock lk(mu_);
        cv_.wait(lk, [&] { return shutting_down_.load() || !pending_.empty(); });
        if (shutting_down_.load()) break;
        if (opts_.enable_psi_monitor && psi_backpressure_.load()) {
            metrics_.inc_pressure_blocked();
            NANO_LOG(WARNING, "%s", "dispatcher blocked by PSI backpressure");
            lk.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        Job job;
        if (!pick_next_job(job)) {
            continue;
        }
        auto now = std::chrono::steady_clock::now();
        auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - job.enqueue_time).count();
        metrics_.record_queue_wait(wait_ms);
        NANO_LOG(DEBUG, "dispatching job id=%d cmd=%s queue_wait_ms=%lld cpu=%d mem_mb=%zu pending=%zu", job.id, job.spec.cmd.c_str(), static_cast<long long>(wait_ms), job.spec.cpu_cores, job.spec.memory_mb, pending_.size());
        if (!rm_.reserve(job.spec.cpu_cores, job.spec.memory_mb)) {
            // 资源不足，重新放回队列尾部
            pending_.push_back(job);
            metrics_.set_pending(static_cast<long long>(pending_.size()));
            NANO_LOG(NOTICE, "resource busy requeue job id=%d pending=%zu", job.id, pending_.size());
            lk.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        bool launched = launch_job(job);
        lk.unlock();
        if (!launched) {
            // 失败时释放资源
            rm_.release(job.spec.cpu_cores, job.spec.memory_mb);
        }
    }
}

void Scheduler::reaper_loop() {
    using namespace std::chrono_literals;
    while (!shutting_down_.load()) {
        std::this_thread::sleep_for(100ms);
        std::lock_guard lk(mu_);
        auto now = std::chrono::steady_clock::now();
        for (auto it = running_.begin(); it != running_.end();) {
            Job &job = it->second;
            if (job.spec.timeout_sec > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - job.start_time).count();
                if (elapsed >= job.spec.timeout_sec) {
                    if (!job.sigterm_sent) {
                        kill(-job.pgid, SIGTERM);
                        job.sigterm_sent = true;
                        job.kill_deadline = now + std::chrono::seconds(opts_.kill_grace_sec);
                        NANO_LOG(WARNING, "sent SIGTERM for timeout job id=%d pid=%d", job.id, job.pid);
                    } else if (job.kill_deadline && now >= *job.kill_deadline) {
                        kill(-job.pgid, SIGKILL);
                        NANO_LOG(ERROR, "sent SIGKILL after grace job id=%d pid=%d", job.id, job.pid);
                    }
                }
            }

            int status = 0;
            pid_t ret = waitpid(job.pid, &status, WNOHANG);
            if (ret > 0) {
                job.end_time = now;
                PersistStatus ps = PersistStatus::Succeeded;
                if (job.sigterm_sent) {
                    job.status = JobStatus::Timeout;
                    ps = PersistStatus::Timeout;
                    metrics_.inc_timeout();
                } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                    job.status = JobStatus::Succeeded;
                    ps = PersistStatus::Succeeded;
                    metrics_.inc_succeeded();
                } else {
                    job.status = JobStatus::Failed;
                    ps = PersistStatus::Failed;
                    metrics_.inc_failed();
                }
                rm_.release(job.spec.cpu_cores, job.spec.memory_mb);
                metrics_.dec_running();
                if (opts_.cgroup.enabled) {
                    cleanup_cgroup(job.cgroup_path);
                }
                if (store_) {
                    auto start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(job.start_time.time_since_epoch()).count();
                    auto end_ms = std::chrono::duration_cast<std::chrono::milliseconds>(job.end_time.time_since_epoch()).count();
                    store_->update_status(job.id, ps, status, start_ms, end_ms);
                }
                auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(job.end_time - job.start_time).count();
                if (job.status == JobStatus::Succeeded) {
                    NANO_LOG(NOTICE, "job finished success id=%d pid=%d exit=%d duration_ms=%lld", job.id, job.pid, WEXITSTATUS(status), static_cast<long long>(dur_ms));
                } else if (job.status == JobStatus::Timeout) {
                    NANO_LOG(WARNING, "job timeout id=%d pid=%d duration_ms=%lld", job.id, job.pid, static_cast<long long>(dur_ms));
                } else {
                    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                    int sig = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
                    NANO_LOG(ERROR, "job failed id=%d pid=%d exit=%d sig=%d duration_ms=%lld", job.id, job.pid, exit_code, sig, static_cast<long long>(dur_ms));
                }
                it = running_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

double Scheduler::parse_psi_avg10(std::istream &ifs) {
    // 格式：some avg10=12.34 avg60=... total=...
    std::string token;
    while (ifs >> token) {
        auto pos = token.find("avg10=");
        if (pos != std::string::npos) {
            return std::stod(token.substr(pos + 6));
        }
    }
    return 0.0;
}

void Scheduler::psi_loop() {
    while (!shutting_down_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::string mem_pressure_file = opts_.cgroup.base_path + "/memory.pressure";
        std::ifstream ifs(mem_pressure_file);
        double avg10 = ifs ? parse_psi_avg10(ifs) : 0.0;
        bool pressure = (avg10 > 50.0);
        if (pressure != psi_backpressure_.load()) {
            psi_backpressure_.store(pressure);
            metrics_.set_pressure_active(pressure);
            NANO_LOG(NOTICE, "%s", pressure ? "PSI backpressure activated" : "PSI backpressure cleared");
        }
    }
}

void Scheduler::cron_loop() {
    using namespace std::chrono_literals;
    while (!shutting_down_.load()) {
        if (cron_sched_) {
            cron_sched_->tick([this](const JobSpec &spec) { this->submit(spec); });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(opts_.cron_tick_ms));
    }
}

void Scheduler::restore_from_store() {
    if (!store_) return;
    auto jobs = store_->load_unfinished();
    for (auto &pj : jobs) {
        Job job;
        job.id = pj.id;
        job.spec = pj.spec;
        job.status = JobStatus::Pending;
        job.enqueue_time = std::chrono::steady_clock::now();
        pending_.push_back(job);
        next_id_ = std::max(next_id_, job.id + 1);
    }
    if (!jobs.empty()) cv_.notify_all();
}
