#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>

#include "miniredis/store.hpp"

namespace miniredis {

// Background active expiration. Every interval it runs one sampled
// Store::active_expire_cycle instead of scanning the whole keyspace, so the
// work per tick is bounded by the time budget rather than by the key count.
class TtlCleaner {
public:
    explicit TtlCleaner(
        Store& store,
        std::chrono::milliseconds interval = std::chrono::milliseconds(100),
        Store::ActiveExpireConfig config = {});
    ~TtlCleaner();

    TtlCleaner(const TtlCleaner&) = delete;
    TtlCleaner& operator=(const TtlCleaner&) = delete;

    void start();
    void stop();

    std::size_t removed_total() const {
        return removed_total_.load(std::memory_order_relaxed);
    }

private:
    void run();

    Store& store_;
    std::chrono::milliseconds interval_;
    Store::ActiveExpireConfig config_;
    std::atomic<bool> running_{false};
    std::atomic<std::size_t> removed_total_{0};
    std::mutex wait_mutex_;
    std::condition_variable wake_;
    std::thread worker_;
};

}  // namespace miniredis
