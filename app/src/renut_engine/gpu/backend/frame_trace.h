#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Opt-in CPU wall-time trace. It records no game data. Times share the process's
// steady clock; nested spans and worker threads must not be summed as GPU time.
namespace renut::gpu::frame_trace {
using Clock = std::chrono::steady_clock;

struct Sink {
  struct Record {
    long long start_us;
    long long duration_us;
    size_t thread;
    const char *event;  // Call sites use string literals.
    std::string detail;
  };
  static constexpr size_t kCapacity = 8192;
  std::mutex mutex;
  std::condition_variable ready;
  std::vector<Record> pending;
  size_t dropped = 0;
  FILE *file = nullptr;
  Sink() {
    const char *path = std::getenv("LOS_FRAME_TRACE");
    if (path && *path) {
      file = std::fopen(path, "w");
      if (file) {
        std::fputs("start_us,duration_us,thread,event,detail\n", file);
        std::fflush(file);
        pending.reserve(kCapacity);
        std::thread([this] { Write(); }).detach();
      } else {
        std::fprintf(stderr, "Cannot open LOS_FRAME_TRACE: %s\n", path);
      }
    }
  }
  void Add(Record record) {
    std::lock_guard lock(mutex);
    if (pending.size() < kCapacity) {
      pending.push_back(std::move(record));
      if (pending.size() == kCapacity / 2) ready.notify_one();
    } else {
      ++dropped;  // Diagnostics must not stall rendering when disk I/O is slow.
    }
  }
  void Write() {
    std::vector<Record> batch;
    batch.reserve(kCapacity);
    for (;;) {
      size_t lost;
      {
        std::unique_lock lock(mutex);
        ready.wait_for(lock, std::chrono::milliseconds(100), [this] {
          return pending.size() >= kCapacity / 2;
        });
        batch.swap(pending);
        lost = std::exchange(dropped, 0);
      }
      for (const auto &r : batch)
        std::fprintf(file, "%lld,%lld,%zu,%s,%s\n", r.start_us, r.duration_us,
                     r.thread, r.event, r.detail.c_str());
      std::fflush(file);
      if (lost)
        std::fprintf(stderr, "LOS_FRAME_TRACE dropped %zu events; capture is incomplete\n", lost);
      batch.clear();
    }
  }
};

// The renderer uses process-lifetime threads and hard-exits at shutdown. Keep
// their trace sink alive too; the writer flushes every 100 ms (no fsync).
inline Sink &GetSink() { static Sink *sink = new Sink; return *sink; }

class Scope {
 public:
  explicit Scope(const char *event, const char *detail = "", long long min_duration_us = 0)
      : sink_(GetSink().file ? &GetSink() : nullptr), event_(event), detail_(detail),
        min_duration_us_(min_duration_us) {
    if (sink_) start_ = Clock::now();
  }
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;
  ~Scope() { Stop(); }
  void Stop() {
    if (!sink_) return;
    const auto end = Clock::now();
    const auto start_us = std::chrono::duration_cast<std::chrono::microseconds>(start_.time_since_epoch()).count();
    const auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
    if (duration_us < min_duration_us_) {
      sink_ = nullptr;
      return;
    }
    const auto thread = std::hash<std::thread::id>{}(std::this_thread::get_id());
    sink_->Add({static_cast<long long>(start_us), static_cast<long long>(duration_us),
                thread, event_, detail_ ? detail_ : ""});
    sink_ = nullptr;
  }
 private:
  Sink *sink_;
  const char *event_;
  const char *detail_;
  long long min_duration_us_;
  Clock::time_point start_{};
};
}  // namespace renut::gpu::frame_trace
