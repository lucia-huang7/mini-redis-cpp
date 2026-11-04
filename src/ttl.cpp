#include "miniredis/ttl.hpp"

#include <random>

namespace miniredis {

TtlCleaner::TtlCleaner(Store& store, std::chrono::milliseconds interval, Store::ActiveExpireConfig config)
    : store_(store), interval_(interval), config_(config) {}

TtlCleaner::~TtlCleaner() {
    stop();
}

void TtlCleaner::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    worker_ = std::thread(&TtlCleaner::run, this);
}

void TtlCleaner::stop() {
    {
        std::lock_guard lock(wait_mutex_);
        bool expected = true;
        if (!running_.compare_exchange_strong(expected, false)) {
            return;
        }
    }
    wake_.notify_all();

    if (worker_.joinable()) {
        worker_.join();
    }
}

void TtlCleaner::run() {
    std::mt19937_64 rng(std::random_device{}());
    while (running_.load()) {
        removed_total_.fetch_add(store_.active_expire_cycle(rng, config_), std::memory_order_relaxed);

        std::unique_lock lock(wait_mutex_);
        wake_.wait_for(lock, interval_, [this] { return !running_.load(); });
    }
}

}  // namespace miniredis
