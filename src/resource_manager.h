#pragma once

#include "job.h"
#include <mutex>
#include <utility>

class ResourceManager {
public:
    explicit ResourceManager(ResourceQuota quota);

    bool reserve(int cpu, std::size_t mem_mb);
    void release(int cpu, std::size_t mem_mb);
    std::pair<int, std::size_t> used() const;

private:
    ResourceQuota quota_;
    int used_cpu_{0};
    std::size_t used_mem_mb_{0};
    mutable std::mutex mu_;
};
