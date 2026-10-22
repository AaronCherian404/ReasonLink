// Thread-safe hls::stream for running SAT-Accel's kernels as concurrent
// threads, the way Vitis software emulation (sw_emu) does.
//
// Semantics: unbounded FIFO; read() blocks until data arrives; read_nb()
// returns false when empty; write()/write_nb() never block (sw_emu streams
// are effectively unbounded). A watchdog reports a read that stays blocked
// for RL_STREAM_TIMEOUT_S seconds (default 30) with the stream's name and
// then aborts, so deadlocks are diagnosable instead of silent.
#ifndef RL_SHIM_HLS_STREAM_H
#define RL_SHIM_HLS_STREAM_H

#include <chrono>
#include <climits>   // Vitis headers pull this in; SAT-Accel relies on UINT_MAX
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>

namespace hls {

inline int rl_stream_timeout_s()
{
    static const int t = [] {
        const char *e = std::getenv("RL_STREAM_TIMEOUT_S");
        return e ? std::atoi(e) : 30;
    }();
    return t;
}

template <typename T>
class stream {
public:
    stream() {}
    explicit stream(const char *name) : name_(name) {}
    explicit stream(const std::string &name) : name_(name) {}
    stream(const stream &) = delete;
    stream &operator=(const stream &) = delete;

    void set_name(const char *name) { name_ = name; }

    bool empty() const
    {
        std::lock_guard<std::mutex> l(m_);
        return q_.empty();
    }
    bool full() const { return false; }
    size_t size() const
    {
        std::lock_guard<std::mutex> l(m_);
        return q_.size();
    }

    T read()
    {
        std::unique_lock<std::mutex> l(m_);
        if (!cv_.wait_for(l, std::chrono::seconds(rl_stream_timeout_s()), [this] { return !q_.empty(); })) {
            std::fprintf(stderr, "hls::stream '%s': read blocked for %d s (deadlock?)\n",
                         name_.empty() ? "<unnamed>" : name_.c_str(), rl_stream_timeout_s());
            std::abort();
        }
        T v = q_.front();
        q_.pop_front();
        return v;
    }
    void read(T &v) { v = read(); }
    bool read_nb(T &v)
    {
        std::lock_guard<std::mutex> l(m_);
        if (q_.empty()) return false;
        v = q_.front();
        q_.pop_front();
        return true;
    }

    void write(const T &v)
    {
        {
            std::lock_guard<std::mutex> l(m_);
            q_.push_back(v);
        }
        cv_.notify_all();
    }
    bool write_nb(const T &v)
    {
        write(v);
        return true;
    }

    void operator>>(T &v) { read(v); }
    void operator<<(const T &v) { write(v); }

private:
    std::string name_;
    std::deque<T> q_;
    mutable std::mutex m_;
    std::condition_variable cv_;
};

}  // namespace hls

#endif
