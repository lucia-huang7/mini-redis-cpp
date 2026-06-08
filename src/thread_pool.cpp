#include "miniredis/thread_pool.hpp"

namespace miniredis {

ThreadPool::ThreadPool(std::size_t workers) {
    (void)workers;
}

ThreadPool::~ThreadPool() = default;

}  // namespace miniredis
