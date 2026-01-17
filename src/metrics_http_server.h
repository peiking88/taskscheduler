#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class MetricsHttpServer {
public:
    using MetricsHandler = std::function<std::string()>;

    bool start(int port, MetricsHandler handler);
    void stop();

private:
    void accept_loop(int port);
    void worker_loop();

    std::atomic<bool> running_{false};
    MetricsHandler handler_;
    int listen_fd_{-1};

    std::thread accept_thread_;
    std::vector<std::thread> workers_;

    std::mutex q_mu_;
    std::condition_variable q_cv_;
    std::queue<int> conn_q_;
};
