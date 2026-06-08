#include "miniredis/ttl.hpp"

#include "miniredis/store.hpp"

namespace miniredis {

TtlCleaner::TtlCleaner(Store& store, std::chrono::milliseconds interval)
    : store_(store), interval_(interval) {}

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
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }

    if (worker_.joinable()) {
        worker_.join();
    }
}

void TtlCleaner::run() {
    while (running_.load()) {
        store_.remove_expired();
        std::this_thread::sleep_for(interval_);
    }
}

}  // namespace miniredis
