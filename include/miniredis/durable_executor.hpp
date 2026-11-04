#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace miniredis {

class Aof;
class CommandDispatcher;
class Store;

struct AofRewritePolicy {
    // Automatic rewrite starts once the AOF is at least min_bytes and has grown by
    // growth_percent over its size after the previous rewrite (or at startup).
    // min_bytes == 0 disables automatic rewrite; BGREWRITEAOF still works.
    std::size_t min_bytes = 64ull * 1024 * 1024;
    std::size_t growth_percent = 100;
};

class DurableCommandExecutor {
public:
    DurableCommandExecutor(
        Store& store,
        CommandDispatcher& dispatcher,
        const Aof* aof,
        AofRewritePolicy rewrite_policy = {});
    ~DurableCommandExecutor();

    DurableCommandExecutor(const DurableCommandExecutor&) = delete;
    DurableCommandExecutor& operator=(const DurableCommandExecutor&) = delete;

    std::string execute(const std::vector<std::string_view>& command);
    bool write_failed() const;

    // Compacts the AOF into one record per live key while writes continue.
    // Blocks the caller until the new file is installed. Returns false if AOF is
    // disabled, a rewrite is already running, or the rewrite failed.
    bool rewrite_aof();
    bool start_background_rewrite();
    bool rewrite_in_progress() const;
    std::size_t completed_rewrites() const;

private:
    bool start_background_rewrite_locked();
    bool run_rewrite_after_start();
    void maybe_auto_rewrite_locked();
    std::vector<std::string> durable_record(
        const std::vector<std::string>& executed,
        const std::string& response) const;

    Store& store_;
    CommandDispatcher& dispatcher_;
    const Aof* aof_;
    AofRewritePolicy rewrite_policy_;

    // Serializes AOF-backed mutations so Store state and log order agree.
    mutable std::mutex write_mutex_;
    bool write_failed_ = false;
    bool rewrite_in_progress_ = false;
    std::string rewrite_buffer_;
    std::size_t rewrite_base_size_ = 0;
    std::size_t completed_rewrites_ = 0;
    std::thread rewrite_thread_;
};

}  // namespace miniredis
