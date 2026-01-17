#pragma once

#include "job.h"
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

class CronScheduler {
public:
    using SubmitCallback = std::function<void(const JobSpec &)>;

    void add_template(const CronTemplate &tpl);
    void tick(const SubmitCallback &cb);

private:
    std::vector<CronTemplate> templates_;
    std::mutex mu_;
};

