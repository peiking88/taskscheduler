#include "metrics.h"

#include <algorithm>
#include <sstream>

void Metrics::inc_submitted() { submitted_.fetch_add(1); }
void Metrics::inc_rejected() { rejected_.fetch_add(1); }
void Metrics::inc_running() { running_.fetch_add(1); }
void Metrics::dec_running() { running_.fetch_sub(1); }
void Metrics::inc_succeeded() { succeeded_.fetch_add(1); }
void Metrics::inc_failed() { failed_.fetch_add(1); }
void Metrics::inc_timeout() { timeout_.fetch_add(1); }
void Metrics::inc_launch_failed() { launch_failed_.fetch_add(1); }
void Metrics::inc_pressure_blocked() { pressure_blocked_.fetch_add(1); }
void Metrics::set_pressure_active(bool active) { pressure_active_.store(active ? 1 : 0); }
void Metrics::record_queue_wait(long long ms) {
    queue_wait_ms_total_.fetch_add(ms);
    queue_wait_count_.fetch_add(1);
    long long prev = queue_wait_ms_max_.load();
    while (ms > prev && !queue_wait_ms_max_.compare_exchange_weak(prev, ms)) {
    }
}
void Metrics::set_pending(long long n) { pending_.store(n); }

Metrics::Snapshot Metrics::snapshot() const {
    Snapshot s;
    s.submitted = submitted_.load();
    s.rejected = rejected_.load();
    s.running = running_.load();
    s.succeeded = succeeded_.load();
    s.failed = failed_.load();
    s.timeout = timeout_.load();
    s.launch_failed = launch_failed_.load();
    s.pressure_blocked = pressure_blocked_.load();
    s.pressure_active = pressure_active_.load();
    s.queue_wait_ms_total = queue_wait_ms_total_.load();
    s.queue_wait_count = queue_wait_count_.load();
    s.queue_wait_ms_max = queue_wait_ms_max_.load();
    s.pending = pending_.load();
    return s;
}

std::string Metrics::to_prometheus() const {
    auto s = snapshot();
    std::ostringstream oss;
    oss << "# TYPE tasks_total counter\n";
    oss << "tasks_total{status=\"submitted\"} " << s.submitted << "\n";
    oss << "tasks_total{status=\"rejected\"} " << s.rejected << "\n";
    oss << "tasks_total{status=\"succeeded\"} " << s.succeeded << "\n";
    oss << "tasks_total{status=\"failed\"} " << s.failed << "\n";
    oss << "tasks_total{status=\"timeout\"} " << s.timeout << "\n";
    oss << "tasks_total{status=\"launch_failed\"} " << s.launch_failed << "\n";
    oss << "# TYPE tasks_running_current gauge\n";
    oss << "tasks_running_current " << s.running << "\n";
    oss << "# TYPE tasks_pending_current gauge\n";
    oss << "tasks_pending_current " << s.pending << "\n";
    oss << "# TYPE tasks_pressure_blocked_total counter\n";
    oss << "tasks_pressure_blocked_total " << s.pressure_blocked << "\n";
    oss << "# TYPE tasks_pressure_active gauge\n";
    oss << "tasks_pressure_active " << s.pressure_active << "\n";
    oss << "# TYPE tasks_queue_wait_ms_total counter\n";
    oss << "tasks_queue_wait_ms_total " << s.queue_wait_ms_total << "\n";
    oss << "# TYPE tasks_queue_wait_count counter\n";
    oss << "tasks_queue_wait_count " << s.queue_wait_count << "\n";
    oss << "# TYPE tasks_queue_wait_ms_max gauge\n";
    oss << "tasks_queue_wait_ms_max " << s.queue_wait_ms_max << "\n";
    return oss.str();
}
