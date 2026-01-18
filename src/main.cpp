#include "scheduler.h"

#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "NanoLogCpp17.h"

#if defined(TASKSCHEDULER_USE_STACKTRACE) && TASKSCHEDULER_USE_STACKTRACE
#include <stacktrace>
#endif
#if defined(TASKSCHEDULER_USE_BACKWARD) && TASKSCHEDULER_USE_BACKWARD
#include <backward.hpp>
#endif

namespace {
std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

void print_stack(const char *ctx) {
#if defined(TASKSCHEDULER_USE_BACKWARD) && TASKSCHEDULER_USE_BACKWARD
    backward::StackTrace st;
    st.load_here(64);
    backward::Printer p;
    p.object = true;
    p.color_mode = backward::ColorMode::automatic;
    std::cerr << "[STACK] " << ctx << "\n";
    p.print(st, std::cerr);
#elif defined(TASKSCHEDULER_USE_STACKTRACE) && TASKSCHEDULER_USE_STACKTRACE && defined(TASKSCHEDULER_HAVE_STACKTRACE) && TASKSCHEDULER_HAVE_STACKTRACE
    std::cerr << "[STACK] " << ctx << "\n";
    std::cerr << std::stacktrace::current() << "\n";
#else
    std::cerr << "[STACK] " << ctx << " (stacktrace unavailable)\n";
#endif
}

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
            print_stack("init_nano_log");
        }
    }
    std::cerr << "NanoLog initialization failed; continuing without logging\n";
}
}

int main(int argc, char **argv) {
    try {
        init_nano_log();

        SchedulerOptions opts;
        JobSpec spec;
        bool has_cmd = false;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto need = [&](const std::string &name) {
                if (i + 1 >= argc) { std::cerr << name << " needs a value\n"; std::exit(1); }
                return std::string(argv[++i]);
            };

            if (arg == "--cmd") { spec.cmd = need(arg); has_cmd = true; }
            else if (arg == "--cpu") { spec.cpu_cores = std::stoi(need(arg)); }
            else if (arg == "--mem") { spec.memory_mb = static_cast<std::size_t>(std::stol(need(arg))); }
            else if (arg == "--timeout") { spec.timeout_sec = std::stoi(need(arg)); }
            else if (arg == "--priority") { spec.priority = std::stoi(need(arg)); }
            else if (arg == "--total-cpu") { opts.quota.total_cpu = std::stoi(need(arg)); }
            else if (arg == "--total-mem") { opts.quota.total_mem_mb = static_cast<std::size_t>(std::stol(need(arg))); }
            else if (arg == "--cgroup") { opts.cgroup.enabled = true; }
            else if (arg == "--enable-priority") { opts.enable_priority = true; }
            else if (arg == "--metrics-port") { opts.metrics_http_port = std::stoi(need(arg)); }
            else if (arg == "--whitelist") { opts.cmd_whitelist = split(need(arg), ','); }
            else if (arg == "--blacklist") { opts.cmd_blacklist = split(need(arg), ','); }
            else if (arg == "--workdir") { opts.workdir = need(arg); }
            else if (arg == "--rlimit-nofile") { opts.rlimit_nofile = std::stoi(need(arg)); }
            else if (arg == "--db-path") { opts.db_path = need(arg); opts.enable_persistence = true; }
            else if (arg == "--enable-cron") { opts.enable_cron = true; }
            else if (arg == "--cron-tick-ms") { opts.cron_tick_ms = std::stoi(need(arg)); }
            else {
                std::cerr << "Unknown arg: " << arg << "\n";
            }
        }

        Scheduler sched(opts);
        sched.start();

        if (has_cmd) {
            int id = sched.submit(spec);
            if (id < 0) {
                std::cerr << "Submit failed" << std::endl;
            } else {
                std::cout << "Submitted job id=" << id << std::endl;
            }
        }

        // 简易阻塞：等待任务全部完成再退出
        while (!sched.idle()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        sched.stop();
        NanoLog::sync();
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << "Unhandled exception: " << ex.what() << "\n";
        print_stack("main std::exception");
        NanoLog::sync();
        return 1;
    } catch (...) {
        std::cerr << "Unhandled unknown exception" << "\n";
        print_stack("main unknown exception");
        NanoLog::sync();
        return 1;
    }
}
