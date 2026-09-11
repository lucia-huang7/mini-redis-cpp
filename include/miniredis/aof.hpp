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

private:
    void sync_if_needed() const;
    void truncate_to(std::size_t offset) const;

    std::filesystem::path path_;
    AofSyncPolicy sync_policy_;
    int fd_ = -1;
    mutable std::mutex mutex_;
    mutable std::chrono::steady_clock::time_point last_sync_;
    AofIoHooks io_hooks_;
};

}  // namespace miniredis
