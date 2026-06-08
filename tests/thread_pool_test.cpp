#include "miniredis/thread_pool.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>

void thread_pool_runs_enqueued_tasks_test() {
    constexpr int task_count = 64;

    miniredis::ThreadPool pool(4);
    std::atomic<int> completed = 0;
    std::mutex mutex;
    std::condition_variable condition;

    for (int i = 0; i < task_count; ++i) {
        pool.enqueue([&]() {
            const auto value = completed.fetch_add(1) + 1;
            if (value == task_count) {
                std::lock_guard lock(mutex);
                condition.notify_one();
            }
        });
    }

    std::unique_lock lock(mutex);
    const auto finished = condition.wait_for(lock, std::chrono::seconds(2), [&]() {
        return completed.load() == task_count;
    });

    assert(finished);
    assert(pool.size() == 4);
}
