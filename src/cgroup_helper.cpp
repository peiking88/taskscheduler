#include "cgroup_helper.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>

#include "NanoLogCpp17.h"

using namespace NanoLog::LogLevels;

namespace fs = std::filesystem;

static bool write_value(const fs::path &file, const std::string &value) {
    std::ofstream ofs(file);
    if (!ofs) return false;
    ofs << value;
    return ofs.good();
}

std::string create_cgroup_for_job(int job_id, int cpu_cores, std::size_t mem_mb, const CgroupConfig &cfg) {
    fs::path base(cfg.base_path);
    fs::path cg_dir = base / ("job_" + std::to_string(job_id));
    std::error_code ec;
    fs::create_directories(cg_dir, ec);
    if (ec) {
        auto msg = "Failed to create cgroup dir: " + ec.message();
        NANO_LOG(WARNING, "%s", msg.c_str());
        return {};
    }

    // cpu.max = <quota_us> <period_us>
    long quota = static_cast<long>(cpu_cores * cfg.cpu_period_us);
    if (!write_value(cg_dir / "cpu.max", std::to_string(quota) + " " + std::to_string(cfg.cpu_period_us))) {
        NANO_LOG(WARNING, "%s", "Failed to write cpu.max");
    }

    // memory.max in bytes
    std::size_t bytes = mem_mb * 1024ull * 1024ull;
    if (!write_value(cg_dir / "memory.max", std::to_string(bytes))) {
        NANO_LOG(WARNING, "%s", "Failed to write memory.max");
    }

    return cg_dir.string();
}

bool attach_pid_to_cgroup(pid_t pid, const std::string &cg_path) {
    if (cg_path.empty()) return false;
    std::ofstream ofs(std::filesystem::path(cg_path) / "cgroup.procs");
    if (!ofs) return false;
    ofs << pid;
    return ofs.good();
}

void cleanup_cgroup(const std::string &cg_path) {
    if (cg_path.empty()) return;
    std::error_code ec;
    std::filesystem::remove_all(cg_path, ec);
    if (ec) {
        auto msg = "Failed to cleanup cgroup: " + ec.message();
        NANO_LOG(WARNING, "%s", msg.c_str());
    }
}
