#pragma once

#include <atomic>
#include <string>
#include <vector>

class Metrics {
public:
    struct Snapshot {
        long long submitted{0};
        long long rejected{0};
        long long running{0};
        long long succeeded{0};
        long long failed{0};
        long long timeout{0};
        long long launch_failed{0};
        long long pressure_blocked{0};
        long long pressure_active{0};
        long long queue_wait_ms_total{0};
        long long queue_wait_count{0};
        long long queue_wait_ms_max{0};
        long long pending{0};
    };

    void inc_submitted();
    void inc_rejected();
    void inc_running();
    void dec_running();
    void inc_succeeded();
    void inc_failed();
    void inc_timeout();
    void inc_launch_failed();
    void inc_pressure_blocked();
    void set_pressure_active(bool active);
    void record_queue_wait(long long ms);
    void set_pending(long long n);

    Snapshot snapshot() const;
    std::string to_prometheus() const;

private:
    std::atomic<long long> submitted_{0};
    std::atomic<long long> rejected_{0};
    std::atomic<long long> running_{0};
    std::atomic<long long> succeeded_{0};
    std::atomic<long long> failed_{0};
    std::atomic<long long> timeout_{0};
    std::atomic<long long> launch_failed_{0};
    std::atomic<long long> pressure_blocked_{0};
    std::atomic<long long> pressure_active_{0};
    std::atomic<long long> queue_wait_ms_total_{0};
    std::atomic<long long> queue_wait_count_{0};
    std::atomic<long long> queue_wait_ms_max_{0};
    std::atomic<long long> pending_{0};
};
