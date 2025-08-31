#include "Logger.hpp"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

// optional: remove if you don't want JSON
 #include <json/json.h>

namespace keyforge {

/* ---------------- utilities ---------------- */

static std::string timeToString(const std::chrono::system_clock::time_point& tp) {
    using namespace std::chrono;
    auto t = system_clock::to_time_t(tp);
    std::tm tm;
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    auto ms = duration_cast<milliseconds>(tp.time_since_epoch()) % 1000;
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

/* ---------------- ConsoleSink ---------------- */

ConsoleSink::ConsoleSink(Mode m) : mode_(m) {}

void ConsoleSink::setMode(Mode m) {
    std::lock_guard<std::mutex> lg(mtx_);
    mode_ = m;
}

void ConsoleSink::consume(const LogRecord& rec) {
    std::lock_guard<std::mutex> lg(mtx_);
    std::ostringstream oss;
    if (mode_ == Mode::JSON) {
        // Quick JSON output without dependency
        oss << "{";
        oss << "\"ts\":\"" << timeToString(rec.ts) << "\",";
        oss << "\"lvl\":\"" << static_cast<int>(rec.level) << "\",";
        oss << "\"tid\":\"" << rec.thread_id << "\",";
        if (rec.session_id) oss << "\"session\":\"" << *rec.session_id << "\",";
        oss << "\"msg\":\"";
        for (char c : rec.message) {
            if (c == '"' || c == '\\') oss << '\\';
            oss << c;
        }
        oss << "\"}";
        std::cout << oss.str() << std::endl;
        return;
    }

    oss << "[" << timeToString(rec.ts) << "] ";
    switch (rec.level) {
        case LogLevel::TRACE: oss << "[TRACE] "; break;
        case LogLevel::DEBUG: oss << "[DEBUG] "; break;
        case LogLevel::INFO:  oss << "[ INFO] "; break;
        case LogLevel::WARN:  oss << "[ WARN] "; break;
        case LogLevel::ERROR: oss << "[ERROR] "; break;
        case LogLevel::FATAL: oss << "[FATAL] "; break;
    }
    oss << "(t:" << rec.thread_id << ") ";
    if (rec.session_id) oss << "<" << *rec.session_id << "> ";
    oss << rec.message;
    std::cout << oss.str() << std::endl;
}

void ConsoleSink::flush() {
    std::cout << std::flush;
}

/* ---------------- InMemorySink::Ring ---------------- */

InMemorySink::Ring::Ring(size_t cap) : buf_(), head_(0), size_(0), capacity_(cap) {
    buf_.reserve(capacity_);
    buf_.resize(0);
}

void InMemorySink::Ring::push(const LogRecord& rec) {
    std::lock_guard<std::mutex> lg(mtx_);
    if (capacity_ == 0) return;
    if (size_ < capacity_) {
        buf_.push_back(rec);
        ++size_;
        head_ = (head_ + 1) % capacity_;
    } else {
        // overwrite oldest: compute write idx as (head_ % capacity_)
        buf_[head_] = rec;
        head_ = (head_ + 1) % capacity_;
    }
}

std::vector<LogRecord> InMemorySink::Ring::lastN(size_t n) const {
    std::lock_guard<std::mutex> lg(mtx_);
    std::vector<LogRecord> out;
    if (size_ == 0) return out;
    n = std::min(n, size_);
    out.reserve(n);
    size_t start = (head_ + capacity_ - size_) % capacity_;
    size_t idx = (start + (size_ - n)) % capacity_;
    for (size_t i = 0; i < n; ++i) {
        out.push_back(buf_[(idx + i) % capacity_]);
    }
    return out;
}

void InMemorySink::Ring::clear() {
    std::lock_guard<std::mutex> lg(mtx_);
    buf_.clear();
    buf_.resize(capacity_);
    head_ = 0;
    size_ = 0;
}

void InMemorySink::Ring::reset(size_t cap) {
    std::lock_guard<std::mutex> lg(mtx_);
    buf_.clear();
    buf_.reserve(cap);
    buf_.resize(0);
    head_ = 0;
    size_ = 0;
    capacity_ = cap;
}

/* ---------------- InMemorySink ---------------- */

InMemorySink::InMemorySink(size_t global_capacity, size_t per_session_capacity)
    : global_(global_capacity), per_session_capacity_(per_session_capacity) {}

void InMemorySink::consume(const LogRecord& rec) {
    // push global
    global_.push(rec);

    // push per-session
    if (rec.session_id) {
        std::unique_lock<std::shared_mutex> lock(sessions_mtx_);
        auto it = sessions_.find(*rec.session_id);
        if (it == sessions_.end()) {
            sessions_.emplace(*rec.session_id, Ring(per_session_capacity_));
            it = sessions_.find(*rec.session_id);
        }
        it->second.push(rec);
    }
}

std::vector<LogRecord> InMemorySink::recentGlobal(size_t n) const {
    return global_.lastN(n);
}

std::vector<LogRecord> InMemorySink::recentForSession(const std::string& session_id, size_t n) const {
    std::shared_lock<std::shared_mutex> lock(sessions_mtx_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return {};
    return it->second.lastN(n);
}

std::string InMemorySink::exportSession(const std::string& session_id) const {
    std::ostringstream oss;
    auto recs = recentForSession(session_id, SIZE_MAX);
    for (const auto& r : recs) {
        oss << "[" << timeToString(r.ts) << "] ";
        // level
        switch (r.level) {
            case LogLevel::TRACE: oss << "[TRACE] "; break;
            case LogLevel::DEBUG: oss << "[DEBUG] "; break;
            case LogLevel::INFO:  oss << "[ INFO] "; break;
            case LogLevel::WARN:  oss << "[ WARN] "; break;
            case LogLevel::ERROR: oss << "[ERROR] "; break;
            case LogLevel::FATAL: oss << "[FATAL] "; break;
        }
        oss << "(t:" << r.thread_id << ") " << r.message << "\n";
    }
    return oss.str();
}

void InMemorySink::clearSession(const std::string& session_id) {
    std::unique_lock<std::shared_mutex> lock(sessions_mtx_);
    sessions_.erase(session_id);
}

void InMemorySink::clearAll() {
    global_.clear();
    std::unique_lock<std::shared_mutex> lock(sessions_mtx_);
    sessions_.clear();
}

/* ---------------- RotatingFileSink ---------------- */

RotatingFileSink::RotatingFileSink(const std::string& base_path, size_t max_bytes, int max_files, bool json)
    : base_path_(base_path), max_bytes_(max_bytes), max_files_(max_files), json_(json), fp_(nullptr), current_size_(0)
{
    std::lock_guard<std::mutex> lg(mtx_);
    // open or create base_path_.0 as current
    std::filesystem::path p(base_path_);
    std::filesystem::create_directories(p.parent_path());
    std::string cur = base_path_ + ".0.log";
    fp_ = fopen(cur.c_str(), "a");
    if (!fp_) {
        // fallback to stdout
        fp_ = stdout;
    } else {
        fseek(fp_, 0, SEEK_END);
        current_size_ = ftell(fp_);
    }
}

void RotatingFileSink::rotateIfNeeded(size_t nextWriteBytes) {
    if (!fp_ || fp_ == stdout) return;
    if (current_size_ + nextWriteBytes <= max_bytes_) return;

    // close current
    fclose(fp_);
    fp_ = nullptr;

    // rotate: base.N-1 -> base.N, ... base.0 -> base.1
    for (int i = max_files_ - 1; i >= 0; --i) {
        std::string src = base_path_ + "." + std::to_string(i) + ".log";
        std::string dst = base_path_ + "." + std::to_string(i + 1) + ".log";
        if (std::filesystem::exists(src)) {
            // remove final if exceeding max_files_
            if (i + 1 >= max_files_) {
                std::error_code ec;
                std::filesystem::remove(dst, ec); // ignore errors
            }
            std::error_code ec2;
            std::filesystem::rename(src, dst, ec2);
        }
    }
    // new file
    std::string cur = base_path_ + ".0.log";
    fp_ = fopen(cur.c_str(), "w");
    current_size_ = 0;
}

std::string RotatingFileSink::formatRecord(const LogRecord& rec) const {
    std::ostringstream oss;
    if (json_) {
        // minimal JSON escape
        oss << "{";
        oss << "\"ts\":\"" << timeToString(rec.ts) << "\",";
        oss << "\"lvl\":" << static_cast<int>(rec.level) << ",";
        oss << "\"tid\":\"" << rec.thread_id << "\"";
        if (rec.session_id) oss << ",\"session\":\"" << *rec.session_id << "\"";
        oss << ",\"msg\":\"";
        for (char c : rec.message) {
            if (c == '"' || c == '\\') oss << '\\';
            oss << c;
        }
        oss << "\"}\n";
    } else {
        oss << "[" << timeToString(rec.ts) << "] ";
        switch (rec.level) {
            case LogLevel::TRACE: oss << "[TRACE] "; break;
            case LogLevel::DEBUG: oss << "[DEBUG] "; break;
            case LogLevel::INFO:  oss << "[ INFO] "; break;
            case LogLevel::WARN:  oss << "[ WARN] "; break;
            case LogLevel::ERROR: oss << "[ERROR] "; break;
            case LogLevel::FATAL: oss << "[FATAL] "; break;
        }
        oss << "(t:" << rec.thread_id << ") ";
        if (rec.session_id) oss << "<" << *rec.session_id << "> ";
        oss << rec.message << "\n";
    }
    return oss.str();
}

void RotatingFileSink::consume(const LogRecord& rec) {
    std::lock_guard<std::mutex> lg(mtx_);
    std::string out = formatRecord(rec);
    rotateIfNeeded(out.size());
    if (!fp_) {
        fp_ = stdout;
    }
    fwrite(out.data(), 1, out.size(), fp_);
    current_size_ += out.size();
}

void RotatingFileSink::flush() {
    std::lock_guard<std::mutex> lg(mtx_);
    if (fp_) fflush(fp_);
}

/* ---------------- AsyncLogger ---------------- */

AsyncLogger::AsyncLogger() {
    running_.store(false);
}

AsyncLogger::~AsyncLogger() {
    shutdown(true);
}

void AsyncLogger::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    worker_ = std::thread(&AsyncLogger::workerThreadMain, this);
}

void AsyncLogger::shutdown(bool flush_flag) {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        // already stopped
    }
    q_cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    if (flush_flag) flush();
}

void AsyncLogger::addSink(std::shared_ptr<Sink> sink) {
    std::lock_guard<std::mutex> lg(sinks_mtx_);
    sinks_.push_back(std::move(sink));
}

void AsyncLogger::removeSink(std::shared_ptr<Sink> sink) {
    std::lock_guard<std::mutex> lg(sinks_mtx_);
    sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), sink), sinks_.end());
}

int AsyncLogger::subscribe(Subscriber cb) {
    std::lock_guard<std::mutex> lg(subs_mtx_);
    int id = next_sub_id_++;
    subscribers_.emplace(id, std::move(cb));
    return id;
}

void AsyncLogger::unsubscribe(int id) {
    std::lock_guard<std::mutex> lg(subs_mtx_);
    subscribers_.erase(id);
}

void AsyncLogger::log(LogLevel level, const std::optional<std::string>& session_id, const std::string& msg) {
    if (level < threshold_.load()) return;

    Item it;
    it.rec.ts = std::chrono::system_clock::now();
    it.rec.thread_id = std::this_thread::get_id();
    it.rec.level = level;
    it.rec.session_id = session_id;
    it.rec.message = msg;

    {
        std::lock_guard<std::mutex> lg(q_mtx_);
        if (queue_.size() >= max_queue_size_) {
            // drop oldest to make room (backpressure policy)
            queue_.pop_front();
        }
        queue_.push_back(std::move(it));
    }
    q_cv_.notify_one();

    // Also notify subscribers immediately in caller thread (low-latency tail)
    std::vector<Subscriber> subs;
    {
        std::lock_guard<std::mutex> sl(subs_mtx_);
        subs.reserve(subscribers_.size());
        for (auto &p : subscribers_) subs.push_back(p.second);
    }
    for (auto &s : subs) {
        try { s(queue_.back().rec); } catch (...) { /* swallow */ }
    }
}

void AsyncLogger::flush() {
    // ask sinks to flush (safely copy)
    std::vector<std::shared_ptr<Sink>> copy;
    {
        std::lock_guard<std::mutex> lg(sinks_mtx_);
        copy = sinks_;
    }
    for (auto &s : copy) s->flush();
}

void AsyncLogger::workerThreadMain() {
    while (running_.load() || !queue_.empty()) {
        Item it;
        {
            std::unique_lock<std::mutex> lk(q_mtx_);
            q_cv_.wait_for(lk, std::chrono::milliseconds(200), [this](){
                return !running_.load() || !queue_.empty();
            });
            if (queue_.empty()) continue;
            it = std::move(queue_.front());
            queue_.pop_front();
        }

        std::vector<std::shared_ptr<Sink>> sinks_copy;
        {
            std::lock_guard<std::mutex> lg(sinks_mtx_);
            sinks_copy = sinks_;
        }

        // deliver to sinks
        for (auto &s : sinks_copy) {
            try {
                s->consume(it.rec);
            } catch (...) {
                // sink errors ignored to avoid crashing logger
            }
        }
    }
    flush();
}

/* helper queries: if in-memory sink exists, use it */
std::vector<LogRecord> AsyncLogger::recentGlobal(size_t n) const {
    std::lock_guard<std::mutex> lg(sinks_mtx_);
    for (auto &s : sinks_) {
        auto p = std::dynamic_pointer_cast<InMemorySink>(s);
        if (p) return p->recentGlobal(n);
    }
    return {};
}

std::vector<LogRecord> AsyncLogger::recentForSession(const std::string& sid, size_t n) const {
    std::lock_guard<std::mutex> lg(sinks_mtx_);
    for (auto &s : sinks_) {
        auto p = std::dynamic_pointer_cast<InMemorySink>(s);
        if (p) return p->recentForSession(sid, n);
    }
    return {};
}

std::string AsyncLogger::exportSession(const std::string& sid) const {
    std::lock_guard<std::mutex> lg(sinks_mtx_);
    for (auto &s : sinks_) {
        auto p = std::dynamic_pointer_cast<InMemorySink>(s);
        if (p) return p->exportSession(sid);
    }
    return {};
}

void AsyncLogger::clearSession(const std::string& sid) {
    std::lock_guard<std::mutex> lg(sinks_mtx_);
    for (auto &s : sinks_) {
        auto p = std::dynamic_pointer_cast<InMemorySink>(s);
        if (p) p->clearSession(sid);
    }
}

void AsyncLogger::setLevel(LogLevel lvl) {
    threshold_.store(lvl);
}

LogLevel AsyncLogger::getLevel() const {
    return threshold_.load();
}

/* ---------------- Logger facade ---------------- */

Logger::Logger() {
    // default: console + in-memory sink
    core_.addSink(std::make_shared<ConsoleSink>(ConsoleSink::Mode::COLORED));
    core_.addSink(std::make_shared<InMemorySink>(4096, 512));
    core_.start();
}

Logger::~Logger() {
    core_.shutdown(true);
}

Logger& Logger::instance() noexcept {
    static Logger inst;
    return inst;
}

void Logger::start() { core_.start(); }
void Logger::shutdown(bool flush) { core_.shutdown(flush); }

/* ---------------- end namespace ---------------- */
} // namespace keyforge
