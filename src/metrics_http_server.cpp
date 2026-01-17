#include "metrics_http_server.h"

#include "NanoLogCpp17.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

using namespace NanoLog::LogLevels;

namespace {
std::string build_response(const std::string &body, const std::string &content_type = "text/plain") {
    std::string resp;
    resp += "HTTP/1.1 200 OK\r\n";
    resp += "Content-Type: " + content_type + "\r\n";
    resp += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    resp += "Connection: close\r\n\r\n";
    resp += body;
    return resp;
}
}

bool MetricsHttpServer::start(int port, MetricsHandler handler) {
    if (running_.exchange(true)) return false;
    handler_ = std::move(handler);

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        NANO_LOG(ERROR, "%s", "Failed to create socket");
        running_ = false;
        return false;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        auto msg = std::string("Failed to bind port ") + std::to_string(port);
        NANO_LOG(ERROR, "%s", msg.c_str());
        ::close(listen_fd_);
        running_ = false;
        return false;
    }
    if (listen(listen_fd_, 64) < 0) {
        NANO_LOG(ERROR, "%s", "listen() failed");
        ::close(listen_fd_);
        running_ = false;
        return false;
    }

    accept_thread_ = std::thread(&MetricsHttpServer::accept_loop, this, port);

    unsigned worker_count = std::max(2u, std::thread::hardware_concurrency());
    for (unsigned i = 0; i < worker_count; ++i) {
        workers_.emplace_back(&MetricsHttpServer::worker_loop, this);
    }
    NANO_LOG(NOTICE, "Metrics HTTP server started on port %d", port);
    return true;
}

void MetricsHttpServer::stop() {
    if (!running_.exchange(false)) return;
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    q_cv_.notify_all();
    if (accept_thread_.joinable()) accept_thread_.join();
    for (auto &t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
    NANO_LOG(NOTICE, "%s", "Metrics HTTP server stopped");
}

void MetricsHttpServer::accept_loop(int /*port*/) {
    while (running_.load()) {
        sockaddr_in cli{};
        socklen_t len = sizeof(cli);
        int fd = ::accept(listen_fd_, reinterpret_cast<sockaddr *>(&cli), &len);
        if (fd < 0) {
            if (!running_.load()) break;
            continue;
        }
        {
            std::lock_guard lk(q_mu_);
            if (conn_q_.size() < 128) {
                conn_q_.push(fd);
                q_cv_.notify_one();
            } else {
                ::close(fd); // drop when queue is full
            }
        }
    }
}

void MetricsHttpServer::worker_loop() {
    while (running_.load()) {
        int fd = -1;
        {
            std::unique_lock lk(q_mu_);
            q_cv_.wait(lk, [&] { return !running_.load() || !conn_q_.empty(); });
            if (!running_.load() && conn_q_.empty()) break;
            if (!conn_q_.empty()) {
                fd = conn_q_.front();
                conn_q_.pop();
            }
        }
        if (fd < 0) continue;

        char buf[1024]{};
        ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
        std::string body = "ok\n";
        std::string path = "/";
        if (n > 0) {
            std::string req(buf, buf + n);
            auto pos = req.find(' ');
            auto pos2 = req.find(' ', pos + 1);
            if (pos != std::string::npos && pos2 != std::string::npos) {
                path = req.substr(pos + 1, pos2 - pos - 1);
            }
            if (path == "/metrics") {
                body = handler_ ? handler_() : std::string{};
            } else if (path == "/health") {
                body = "ok\n";
            }
        }
        auto resp = build_response(body);
        ::send(fd, resp.data(), resp.size(), 0);
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }
}
