#include "miniredis/thread_pool.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

void thread_pool_runs_enqueued_tasks_test() {
    constexpr int task_count = 64;

    std::atomic<int> completed = 0;
    std::mutex mutex;
    std::condition_variable condition;
    miniredis::ThreadPool pool(4);

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

void thread_pool_survives_task_exception_test() {
    std::atomic<bool> completed = false;
    std::mutex mutex;
    std::condition_variable condition;
    miniredis::ThreadPool pool(1);

    pool.enqueue([] { throw std::runtime_error("expected test exception"); });
    pool.enqueue([&] {
        completed = true;
        condition.notify_one();
    });

    std::unique_lock lock(mutex);
    const auto finished = condition.wait_for(
        lock, std::chrono::seconds(2), [&] { return completed.load(); });
    assert(finished);
}

void thread_pool_rejects_tasks_when_queue_is_full_test() {
    std::mutex mutex;
    std::condition_variable condition;
    bool first_started = false;
    bool release_first = false;
    miniredis::ThreadPool pool(1, 1);

    pool.enqueue([&] {
        std::unique_lock lock(mutex);
        first_started = true;
        condition.notify_all();
        condition.wait(lock, [&] { return release_first; });
    });

    {
        std::unique_lock lock(mutex);
        const auto started = condition.wait_for(
            lock, std::chrono::seconds(2), [&] { return first_started; });
        assert(started);
    }
    pool.enqueue([] {});

    bool rejected = false;
    try {
        pool.enqueue([] {});
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected);

    {
        std::lock_guard lock(mutex);
        release_first = true;
    }
    condition.notify_all();
}
