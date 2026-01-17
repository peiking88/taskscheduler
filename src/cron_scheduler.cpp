#include "cron_scheduler.h"

#include <chrono>
#include <regex>

std::optional<CronExpression> CronExpression::parse(std::string_view expr) {
    if (expr.rfind("@every", 0) == 0) {
        std::regex re(R"(@every\s+([0-9]+)s)");
        std::cmatch m;
        std::string s(expr);
        if (std::regex_match(s.c_str(), m, re)) {
            CronExpression ce;
            ce.raw = std::string(expr);
            ce.interval = std::chrono::seconds(std::stoll(m[1]));
            return ce;
        }
    }
    return std::nullopt; // 简化：其他表达式暂未支持
}

std::chrono::system_clock::time_point CronExpression::next_run(std::chrono::system_clock::time_point from) const {
    return from + interval;
}

void CronScheduler::add_template(const CronTemplate &tpl) {
    std::lock_guard lk(mu_);
    templates_.push_back(tpl);
}

void CronScheduler::tick(const SubmitCallback &cb) {
    auto now = std::chrono::system_clock::now();
    std::lock_guard lk(mu_);
    for (auto &tpl : templates_) {
        if (!tpl.enabled) continue;
        if (now >= tpl.next_run) {
            cb(tpl.spec);
            tpl.next_run = tpl.cron.next_run(now);
        }
    }
}
