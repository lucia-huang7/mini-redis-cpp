#pragma once

#include <cstddef>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>

namespace miniredis {

class CommandDispatcher;

enum class AofSyncPolicy {
    Always,
    EverySec,
    No,
};

class Aof {
public:
    explicit Aof(std::filesystem::path path, AofSyncPolicy sync_policy = AofSyncPolicy::EverySec);
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
};

}  // namespace miniredis
