#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <thread>

namespace keyforge {

enum class LogLevel {
    TRACE = 0,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

class Logger {
public:
    /// Singleton accessor
    static Logger& instance();

    /// Change log level threshold
    void setLevel(LogLevel level);

    /// Log message with a given severity
    void log(LogLevel level, const std::string& message);

    /// Convenience wrappers
    void trace(const std::string& msg) { log(LogLevel::TRACE, msg); }
    void debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }
    void info(const std::string& msg)  { log(LogLevel::INFO, msg); }
    void warn(const std::string& msg)  { log(LogLevel::WARN, msg); }
    void error(const std::string& msg) { log(LogLevel::ERROR, msg); }
    void fatal(const std::string& msg) { log(LogLevel::FATAL, msg); }

    /// Get a snapshot of the current logs
    std::vector<std::string> dump();

    /// Clear logs
    void clear();

    /// Reset buffer capacity
    void resetBuffer(size_t capacity);

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /// Timestamp helper
    std::string timestamp();

    /// Circular buffer for in-memory logs
    struct CircularBuffer {
        std::vector<std::string> buffer_;
        size_t head_ = 0;
        size_t tail_ = 0;
        size_t capacity_;
        bool full_ = false;
        std::mutex mtx_;

        CircularBuffer(size_t capacity = 1024)
            : buffer_(capacity), capacity_(capacity) {}

        void push(const std::string& msg) {
            std::lock_guard<std::mutex> lock(mtx_);
            buffer_[head_] = msg;
            head_ = (head_ + 1) % capacity_;
            if (full_) {
                tail_ = (tail_ + 1) % capacity_;
            }
            full_ = head_ == tail_;
        }

        std::vector<std::string> snapshot() {
            std::lock_guard<std::mutex> lock(mtx_);
            std::vector<std::string> out;
            if (!full_ && head_ == tail_) return out; // empty
            size_t idx = tail_;
            do {
                out.push_back(buffer_[idx]);
                idx = (idx + 1) % capacity_;
            } while (idx != head_);
            return out;
        }

        void clear() {
            std::lock_guard<std::mutex> lock(mtx_);
            buffer_.clear();
            buffer_.resize(capacity_);
            head_ = tail_ = 0;
            full_ = false;
        }

        /// 🔥 Reset with new capacity
        void reset(size_t new_capacity) {
            std::lock_guard<std::mutex> lock(mtx_);
            buffer_.clear();
            buffer_.resize(new_capacity);
            head_ = tail_ = 0;
            capacity_ = new_capacity;
            full_ = false;
        }
    };

    std::atomic<LogLevel> threshold_{LogLevel::TRACE};
    CircularBuffer global_buf_{1024};
};

} // namespace keyforge
