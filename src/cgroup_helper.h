#pragma once

#include "job.h"
#include <string>

std::string create_cgroup_for_job(int job_id, int cpu_cores, std::size_t mem_mb, const CgroupConfig &cfg);
bool attach_pid_to_cgroup(pid_t pid, const std::string &cg_path);
void cleanup_cgroup(const std::string &cg_path);
