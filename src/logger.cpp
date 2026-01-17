#include "logger.h"

Logger &Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::set_level(Level level) {
    std::lock_guard<std::mutex> lk(mu_);
    level_ = level;
}

std::string Logger::level_str(Level level) {
    switch (level) {
    case Level::Debug: return "DEBUG";
    case Level::Info: return "INFO";
    case Level::Warn: return "WARN";
    case Level::Error: return "ERROR";
    }
    return "INFO";
}

void Logger::log(Level level, const std::string &msg) {
    std::lock_guard<std::mutex> lk(mu_);
    if (static_cast<int>(level) < static_cast<int>(level_)) {
        return;
    }
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm{};
    localtime_r(&tt, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%F %T") << '.' << std::setw(3) << std::setfill('0') << ms.count();
    std::cerr << '[' << oss.str() << "] [" << level_str(level) << "] " << msg << '\n';
}
