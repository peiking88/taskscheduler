#pragma once

#include "job.h"
#include <optional>
#include <string>
#include <vector>

struct PersistedJob {
    int id{0};
    JobSpec spec;
    PersistStatus status{PersistStatus::Queued};
};

class JobStore {
public:
    bool init(const std::string &path);
    int insert_job(const JobSpec &spec, PersistStatus status, int64_t submit_ms);
    void update_status(int id, PersistStatus status, int exit_code = 0, int64_t start_ms = 0, int64_t end_ms = 0);
    std::vector<PersistedJob> load_unfinished();

private:
    std::string path_;
};
