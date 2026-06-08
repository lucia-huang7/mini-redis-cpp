#include "miniredis/thread_pool.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace miniredis {

ThreadPool::ThreadPool(std::size_t workers) {
    const auto worker_count = std::max<std::size_t>(1, workers);
    workers_.reserve(worker_count);
    for (std::size_t i = 0; i < worker_count; ++i) {
        workers_.emplace_back(&ThreadPool::run, this);
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("thread pool is stopping");
        }
        tasks_.push(std::move(task));
    }
    condition_.notify_one();
}

std::size_t ThreadPool::size() const {
    std::lock_guard lock(mutex_);
    return workers_.size();
}

void ThreadPool::run() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] {
                return stopping_ || !tasks_.empty();
            });

            if (stopping_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        task();
    }
}

}  // namespace miniredis
