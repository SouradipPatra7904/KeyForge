#include "keyforge/Logger.hpp"
#include <iostream>

namespace keyforge {

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::setLevel(LogLevel level) {
    threshold_.store(level);
}

void Logger::log(LogLevel level, const std::string& message) {
    if (level < threshold_.load()) return;

    std::ostringstream oss;
    oss << "[" << timestamp() << "] ";
    switch (level) {
        case LogLevel::TRACE: oss << "[TRACE] "; break;
        case LogLevel::DEBUG: oss << "[DEBUG] "; break;
        case LogLevel::INFO:  oss << "[INFO ] "; break;
        case LogLevel::WARN:  oss << "[WARN ] "; break;
        case LogLevel::ERROR: oss << "[ERROR] "; break;
        case LogLevel::FATAL: oss << "[FATAL] "; break;
    }
    oss << message;

    std::string formatted = oss.str();
    global_buf_.push(formatted);

    // Optional: echo to stdout (can disable later if needed)
    std::cout << formatted << std::endl;
}

std::vector<std::string> Logger::dump() {
    return global_buf_.snapshot();
}

void Logger::clear() {
    global_buf_.clear();
}

void Logger::resetBuffer(size_t capacity) {
    global_buf_.reset(capacity);
}

std::string Logger::timestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto itt = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream ss;
    ss << std::put_time(std::localtime(&itt), "%F %T")
       << '.' << std::setfill('0') << std::setw(3) << ms.count()
       << " [T:" << std::this_thread::get_id() << "]";
    return ss.str();
}

} // namespace keyforge
