#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace keyforge {

/* ---------- Log Level ---------- */
enum class LogLevel : uint8_t {
    TRACE = 0,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

/* ---------- LogRecord ---------- */
struct LogRecord {
    std::chrono::system_clock::time_point ts;
    std::thread::id thread_id;
    LogLevel level;
    std::optional<std::string> session_id;
    std::string message;
};

/* ---------- Sink interface ---------- */
class Sink {
public:
    virtual ~Sink() = default;
    virtual void consume(const LogRecord& rec) = 0;
    virtual void flush() {}
};

/* ---------- ConsoleSink ---------- */
class ConsoleSink : public Sink {
public:
    enum class Mode { PLAIN, COLORED, JSON };
    explicit ConsoleSink(Mode m = Mode::COLORED);
    void consume(const LogRecord& rec) override;
    void flush() override;
    void setMode(Mode m);
private:
    Mode mode_;
    std::mutex mtx_;
};

/* ---------- InMemorySink (per-session + global) ---------- */
class InMemorySink : public Sink {
public:
    explicit InMemorySink(size_t global_capacity = 4096, size_t per_session_capacity = 512);

    void consume(const LogRecord& rec) override;
    void flush() override {}

    // Query APIs (thread-safe)
    std::vector<LogRecord> recentGlobal(size_t n) const;
    std::vector<LogRecord> recentForSession(const std::string& session_id, size_t n) const;
    std::string exportSession(const std::string& session_id) const;
    void clearSession(const std::string& session_id);
    void clearAll();

private:
    // thread-safe circular buffer implementation for LogRecord
    struct Ring {
        explicit Ring(size_t cap = 1024);
        void push(const LogRecord& rec);
        std::vector<LogRecord> lastN(size_t n) const;
        void clear();
        void reset(size_t cap);
    private:
        mutable std::mutex mtx_;
        std::vector<LogRecord> buf_;
        size_t head_{0};
        size_t size_{0};
        size_t capacity_{0};
    };

    Ring global_;
    size_t per_session_capacity_;
    mutable std::shared_mutex sessions_mtx_;
    std::unordered_map<std::string, Ring> sessions_;
};

/* ---------- RotatingFileSink ---------- */
class RotatingFileSink : public Sink {
public:
    RotatingFileSink(const std::string& base_path, size_t max_bytes = 10 * 1024 * 1024, int max_files = 5, bool json = false);
    void consume(const LogRecord& rec) override;
    void flush() override;
private:
    void rotateIfNeeded(size_t nextWriteBytes);
    std::string formatRecord(const LogRecord& rec) const;

    std::string base_path_;
    size_t max_bytes_;
    int max_files_;
    bool json_;
    std::mutex mtx_;
    FILE* fp_{nullptr};
    size_t current_size_{0};
};

/* ---------- AsyncLogger (core) ---------- */
class AsyncLogger {
public:
    using Subscriber = std::function<void(const LogRecord&)>;

    AsyncLogger();
    ~AsyncLogger();

    // non-copyable
    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;

    // Start and stop
    void start();
    void shutdown(bool flush = true);

    // Sinks
    void addSink(std::shared_ptr<Sink> sink);
    void removeSink(std::shared_ptr<Sink> sink); // pointer identity removal

    // Subscribers (tail)
    int subscribe(Subscriber cb);
    void unsubscribe(int id);

    // Logging API
    void log(LogLevel level, const std::optional<std::string>& session_id, const std::string& msg);
    void flush();

    // Configuration
    void setLevel(LogLevel lvl);
    LogLevel getLevel() const;

    // Query via in-memory sink (if present)
    std::vector<LogRecord> recentGlobal(size_t n) const;
    std::vector<LogRecord> recentForSession(const std::string& sid, size_t n) const;
    std::string exportSession(const std::string& sid) const;
    void clearSession(const std::string& sid);

private:
    void workerThreadMain();

    // Internal queue item
    struct Item {
        LogRecord rec;
    };

    mutable std::mutex sinks_mtx_;
    std::vector<std::shared_ptr<Sink>> sinks_;

    // subscribers
    mutable std::mutex subs_mtx_;
    std::unordered_map<int, Subscriber> subscribers_;
    int next_sub_id_{1};

    // async queue
    std::mutex q_mtx_;
    std::condition_variable q_cv_;
    std::deque<Item> queue_;
    size_t max_queue_size_{1 << 20}; // cap to avoid OOM

    std::atomic<bool> running_{false};
    std::thread worker_;
    std::atomic<LogLevel> threshold_{LogLevel::INFO};
};

/* ---------- Singleton facade ---------- */
class Logger {
public:
    static Logger& instance() noexcept;

    // lifecycle
    void start();                      // start background worker
    void shutdown(bool flush = true);  // stop worker and flush

    // sinks
    void addSink(std::shared_ptr<Sink> sink) { core_.addSink(std::move(sink)); }
    void removeSink(std::shared_ptr<Sink> sink) { core_.removeSink(sink); }

    // log calls
    void log(LogLevel level, const std::optional<std::string>& session_id, const std::string& msg) {
        core_.log(level, session_id, msg);
    }
    void log(LogLevel level, const std::string& msg) { log(level, std::nullopt, msg); }

    // convenience
    void trace(const std::string& s) { log(LogLevel::TRACE, s); }
    void debug(const std::string& s) { log(LogLevel::DEBUG, s); }
    void info (const std::string& s) { log(LogLevel::INFO,  s); }
    void warn (const std::string& s) { log(LogLevel::WARN,  s); }
    void error(const std::string& s) { log(LogLevel::ERROR, s); }
    void fatal(const std::string& s) { log(LogLevel::FATAL, s); }

    // session convenience
    void trace(const std::string& sid, const std::string& s) { log(LogLevel::TRACE, sid, s); }
    void debug(const std::string& sid, const std::string& s) { log(LogLevel::DEBUG, sid, s); }
    void info (const std::string& sid, const std::string& s) { log(LogLevel::INFO,  sid, s); }
    void warn (const std::string& sid, const std::string& s) { log(LogLevel::WARN,  sid, s); }
    void error(const std::string& sid, const std::string& s) { log(LogLevel::ERROR, sid, s); }
    void fatal(const std::string& sid, const std::string& s) { log(LogLevel::FATAL, sid, s); }

    // config
    void setLevel(LogLevel l) { core_.setLevel(l); }
    LogLevel getLevel() const { return core_.getLevel(); }

    // subscriber
    int subscribe(AsyncLogger::Subscriber cb) { return core_.subscribe(std::move(cb)); }
    void unsubscribe(int id) { core_.unsubscribe(id); }

    // query
    std::vector<LogRecord> recentGlobal(size_t n) const { return core_.recentGlobal(n); }
    std::vector<LogRecord> recentForSession(const std::string& sid, size_t n) const { return core_.recentForSession(sid, n); }
    std::string exportSession(const std::string& sid) const { return core_.exportSession(sid); }
    void clearSession(const std::string& sid) { core_.clearSession(sid); }

private:
    Logger();
    ~Logger();

    AsyncLogger core_;
};

/* ---------- SessionLogger helper ---------- */
class SessionLogger {
public:
    explicit SessionLogger(std::string sid) : sid_(std::move(sid)) {}
    void trace(const std::string& s) { Logger::instance().log(LogLevel::TRACE, sid_, s); }
    void debug(const std::string& s) { Logger::instance().log(LogLevel::DEBUG, sid_, s); }
    void info (const std::string& s) { Logger::instance().log(LogLevel::INFO,  sid_, s); }
    void warn (const std::string& s) { Logger::instance().log(LogLevel::WARN,  sid_, s); }
    void error(const std::string& s) { Logger::instance().log(LogLevel::ERROR, sid_, s); }
    void fatal(const std::string& s) { Logger::instance().log(LogLevel::FATAL, sid_, s); }
    const std::string& id() const { return sid_; }
private:
    std::string sid_;
};

/* ---------- Macros ---------- */
#define KF_LOG(level, msg) ::keyforge::Logger::instance().log(level, msg)
#define KF_LOG_S(sid, level, msg) ::keyforge::Logger::instance().log(level, sid, msg)
#define KF_TRACE(msg) KF_LOG(::keyforge::LogLevel::TRACE, msg)
#define KF_DEBUG(msg) KF_LOG(::keyforge::LogLevel::DEBUG, msg)
#define KF_INFO(msg)  KF_LOG(::keyforge::LogLevel::INFO,  msg)
#define KF_WARN(msg)  KF_LOG(::keyforge::LogLevel::WARN,  msg)
#define KF_ERROR(msg) KF_LOG(::keyforge::LogLevel::ERROR, msg)
#define KF_FATAL(msg) KF_LOG(::keyforge::LogLevel::FATAL, msg)

#define KF_SESSION_TRACE(sid, msg) KF_LOG_S(sid, ::keyforge::LogLevel::TRACE, msg)
#define KF_SESSION_DEBUG(sid, msg) KF_LOG_S(sid, ::keyforge::LogLevel::DEBUG, msg)
#define KF_SESSION_INFO(sid, msg)  KF_LOG_S(sid, ::keyforge::LogLevel::INFO,  msg)
#define KF_SESSION_WARN(sid, msg)  KF_LOG_S(sid, ::keyforge::LogLevel::WARN,  msg)
#define KF_SESSION_ERROR(sid, msg) KF_LOG_S(sid, ::keyforge::LogLevel::ERROR, msg)
#define KF_SESSION_FATAL(sid, msg) KF_LOG_S(sid, ::keyforge::LogLevel::FATAL, msg)

} // namespace keyforge
