#pragma once

#include <atomic>
#include <chrono>
#include <thread>

namespace miniredis {

class Store;

class TtlCleaner {
public:
    explicit TtlCleaner(Store& store, std::chrono::milliseconds interval = std::chrono::seconds(1));
    ~TtlCleaner();

    TtlCleaner(const TtlCleaner&) = delete;
    TtlCleaner& operator=(const TtlCleaner&) = delete;

    void start();
    void stop();

private:
    void run();

    Store& store_;
    std::chrono::milliseconds interval_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace miniredis
