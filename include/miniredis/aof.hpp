#pragma once

#include <cstddef>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <functional>
#include <sys/types.h>

namespace miniredis {

class CommandDispatcher;

enum class AofSyncPolicy {
    Always,
    EverySec,
    No,
};

struct AofIoHooks {
    std::function<ssize_t(int, const void*, std::size_t)> write;
    std::function<int(int)> fsync;
};

class Aof {
public:
    explicit Aof(
        std::filesystem::path path,
        AofSyncPolicy sync_policy = AofSyncPolicy::EverySec,
        AofIoHooks io_hooks = {});
    ~Aof();

    Aof(const Aof&) = delete;
    Aof& operator=(const Aof&) = delete;

    void append(const std::string& encoded_command) const;
    std::size_t replay(CommandDispatcher& dispatcher) const;

    // Atomically installs a fully written and fsynced file as the new AOF:
    // rename over the current path, fsync the directory, then reopen for append.
    void replace_with(const std::filesystem::path& rewritten) const;

    std::size_t size() const;
    const std::filesystem::path& path() const {
        return path_;
    }

private:
    void sync_if_needed() const;
    void truncate_to(std::size_t offset) const;

    std::filesystem::path path_;
    AofSyncPolicy sync_policy_;
    mutable int fd_ = -1;
    mutable std::size_t size_ = 0;
    mutable std::mutex mutex_;
    mutable std::chrono::steady_clock::time_point last_sync_;
    AofIoHooks io_hooks_;
};

}  // namespace miniredis
