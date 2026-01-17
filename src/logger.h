#pragma once

#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

class Logger {
public:
    enum class Level { Debug, Info, Warn, Error };

    static Logger &instance();

    void set_level(Level level);

    void log(Level level, const std::string &msg);

private:
    Logger() = default;
    std::string level_str(Level level);

    Level level_{Level::Info};
    std::mutex mu_;
};
