#include "resource_manager.h"

ResourceManager::ResourceManager(ResourceQuota quota) : quota_(quota) {}

bool ResourceManager::reserve(int cpu, std::size_t mem_mb) {
    std::lock_guard lk(mu_);
    if (used_cpu_ + cpu > quota_.total_cpu || used_mem_mb_ + mem_mb > quota_.total_mem_mb) {
        return false;
    }
    used_cpu_ += cpu;
    used_mem_mb_ += mem_mb;
    return true;
}

void ResourceManager::release(int cpu, std::size_t mem_mb) {
    std::lock_guard lk(mu_);
    used_cpu_ = std::max(0, used_cpu_ - cpu);
    used_mem_mb_ = used_mem_mb_ > mem_mb ? used_mem_mb_ - mem_mb : 0;
}

std::pair<int, std::size_t> ResourceManager::used() const {
    std::lock_guard lk(mu_);
    return {used_cpu_, used_mem_mb_};
}
