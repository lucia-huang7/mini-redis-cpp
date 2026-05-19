#pragma once

#include <cstddef>

namespace miniredis {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t workers);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
};

}  // namespace miniredis
