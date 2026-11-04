#include "miniredis/durable_executor.hpp"

#include "miniredis/aof.hpp"
#include "miniredis/command.hpp"
#include "miniredis/resp.hpp"
#include "miniredis/store.hpp"

#include <cctype>
#include <charconv>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace miniredis {
namespace {

bool equals_ci(std::string_view value, std::string_view expected) {
    if (value.size() != expected.size()) {
        return false;
    }
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (static_cast<char>(std::toupper(static_cast<unsigned char>(value[i]))) != expected[i]) {
            return false;
        }
    }
    return true;
}

bool is_write_command(const std::vector<std::string_view>& command) {
    if (command.empty()) {
        return false;
    }
    const auto name = command.front();
    return equals_ci(name, "SET") || equals_ci(name, "DEL") ||
           equals_ci(name, "EXPIRE") || equals_ci(name, "EXPIREAT") ||
           equals_ci(name, "PEXPIREAT") || equals_ci(name, "INCR") ||
           equals_ci(name, "DECR");
}

bool changed_state(const std::vector<std::string_view>& command, const std::string& response) {
    if (response.empty() || response.front() == '-') {
        return false;
    }
    const auto name = command.front();
    if (equals_ci(name, "SET")) {
        return response == "+OK\r\n";
    }
    if (equals_ci(name, "INCR") || equals_ci(name, "DECR")) {
        return response.front() == ':';
    }
    if (equals_ci(name, "DEL") || equals_ci(name, "EXPIRE") ||
        equals_ci(name, "EXPIREAT") || equals_ci(name, "PEXPIREAT")) {
        return response == resp::integer(1);
    }
    return false;
}

std::vector<std::string> copy_command(const std::vector<std::string_view>& command) {
    std::vector<std::string> copied;
    copied.reserve(command.size());
    for (const auto value : command) {
        copied.emplace_back(value);
    }
    return copied;
}

std::optional<long long> parse_integer(std::string_view value) {
    long long parsed_value = 0;
    const auto parsed = std::from_chars(
        value.data(), value.data() + value.size(), parsed_value);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed_value;
}

std::optional<long long> checked_milliseconds(long long value, long long multiplier) {
    if (value > std::numeric_limits<long long>::max() / multiplier ||
        value < std::numeric_limits<long long>::min() / multiplier) {
        return std::nullopt;
    }
    return value * multiplier;
}

std::optional<long long> absolute_deadline_ms(long long ttl, long long multiplier) {
    const auto duration_ms = checked_milliseconds(ttl, multiplier);
    if (!duration_ms.has_value()) {
        return std::nullopt;
    }
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (*duration_ms > std::numeric_limits<long long>::max() - now_ms) {
        return std::nullopt;
    }
    return now_ms + *duration_ms;
}

std::vector<std::string> normalize_expiration(const std::vector<std::string_view>& command) {
    auto normalized = copy_command(command);
    if (command.empty()) {
        return normalized;
    }

    if (equals_ci(command.front(), "EXPIRE") && command.size() == 3) {
        const auto seconds = parse_integer(command[2]);
        if (seconds.has_value() && *seconds >= 0) {
            const auto deadline = absolute_deadline_ms(*seconds, 1000);
            if (deadline.has_value()) {
                normalized[0] = "PEXPIREAT";
                normalized[2] = std::to_string(*deadline);
            }
        }
        return normalized;
    }

    if (!equals_ci(command.front(), "SET") || command.size() < 3) {
        return normalized;
    }
    for (std::size_t i = 3; i + 1 < command.size(); ++i) {
        const auto option = command[i];
        if (!equals_ci(option, "EX") && !equals_ci(option, "PX")) {
            continue;
        }
        const auto ttl = parse_integer(command[i + 1]);
        if (!ttl.has_value() || *ttl <= 0) {
            return normalized;
        }
        const auto deadline = absolute_deadline_ms(*ttl, equals_ci(option, "EX") ? 1000 : 1);
        if (!deadline.has_value()) {
            return normalized;
        }
        normalized[i] = "PXAT";
        normalized[i + 1] = std::to_string(*deadline);
        return normalized;
    }
    return normalized;
}

std::vector<std::string_view> command_views(const std::vector<std::string>& command) {
    std::vector<std::string_view> views;
    views.reserve(command.size());
    for (const auto& value : command) {
        views.emplace_back(value);
    }
    return views;
}

void write_all_fd(int fd, const std::string& content) {
    std::size_t written = 0;
    while (written < content.size()) {
        const auto result = ::write(fd, content.data() + written, content.size() - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error(std::string("failed to write rewritten AOF: ") + std::strerror(errno));
        }
        if (result == 0) {
            throw std::runtime_error("failed to write rewritten AOF: zero-length write");
        }
        written += static_cast<std::size_t>(result);
    }
}

void fsync_fd(int fd) {
    if (::fsync(fd) < 0) {
        throw std::runtime_error(std::string("failed to fsync rewritten AOF: ") + std::strerror(errno));
    }
}

std::string encode_snapshot_entry(const Store::SnapshotEntry& entry) {
    if (entry.expires_at_unix_ms.has_value()) {
        return resp::array({"SET", entry.key, entry.value, "PXAT", std::to_string(*entry.expires_at_unix_ms)});
    }
    return resp::array({"SET", entry.key, entry.value});
}

constexpr std::size_t kRewriteChunkBytes = 1 << 20;

}  // namespace

DurableCommandExecutor::DurableCommandExecutor(
    Store& store,
    CommandDispatcher& dispatcher,
    const Aof* aof,
    AofRewritePolicy rewrite_policy)
    : store_(store),
      dispatcher_(dispatcher),
      aof_(aof),
      rewrite_policy_(rewrite_policy),
      rewrite_base_size_(aof != nullptr ? aof->size() : 0) {}

DurableCommandExecutor::~DurableCommandExecutor() {
    if (rewrite_thread_.joinable()) {
        rewrite_thread_.join();
    }
}

std::string DurableCommandExecutor::execute(const std::vector<std::string_view>& command) {
    if (command.size() == 1 && equals_ci(command.front(), "BGREWRITEAOF")) {
        std::lock_guard lock(write_mutex_);
        if (aof_ == nullptr) {
            return resp::error("ERR AOF is disabled");
        }
        if (rewrite_in_progress_) {
            return resp::error("ERR Background append only file rewriting already in progress");
        }
        if (!start_background_rewrite_locked()) {
            return resp::error("ERR Background append only file rewriting could not start");
        }
        return resp::simple_string("Background append only file rewriting started");
    }

    if (!is_write_command(command)) {
        return dispatcher_.execute_view(command);
    }

    // Without an AOF there is no log order to protect: per-shard locks in Store
    // already make each write atomic, so independent keys proceed in parallel.
    if (aof_ == nullptr) {
        return dispatcher_.execute_view(command);
    }

    // The lock gives Store mutations and their AOF records one commit order.
    std::lock_guard lock(write_mutex_);
    if (write_failed_) {
        return resp::error("MISCONF AOF write failed; writes are disabled until restart");
    }

    const auto durable_command = normalize_expiration(command);
    const auto durable_views = command_views(durable_command);
    const auto response = dispatcher_.execute_view(durable_views);
    if (!changed_state(durable_views, response)) {
        return response;
    }

    const auto record = resp::array(durable_record(durable_command, response));
    try {
        aof_->append(record);
    } catch (const std::exception&) {
        // The in-memory mutation may already have happened. The failed command is
        // not acknowledged, and later writes remain disabled until restart.
        write_failed_ = true;
        return resp::error("MISCONF AOF write failed; command was not acknowledged and writes are disabled");
    }
    if (rewrite_in_progress_) {
        rewrite_buffer_ += record;
    }
    maybe_auto_rewrite_locked();
    return response;
}

std::vector<std::string> DurableCommandExecutor::durable_record(
    const std::vector<std::string>& executed,
    const std::string& response) const {
    const auto name = std::string_view(executed.front());

    // Every logged record is a blind write (SET/DEL) or an absolute expiry.
    // Replaying a suffix of such records over any intermediate state of the same
    // key converges to the same final state, which is what lets AOF rewrite
    // snapshot shards without stopping writers.
    if (equals_ci(name, "SET")) {
        std::vector<std::string> record;
        record.reserve(executed.size());
        for (std::size_t i = 0; i < executed.size(); ++i) {
            if (i >= 3 && (equals_ci(executed[i], "NX") || equals_ci(executed[i], "XX"))) {
                continue;
            }
            record.push_back(executed[i]);
        }
        return record;
    }

    if (equals_ci(name, "INCR") || equals_ci(name, "DECR")) {
        const auto& key = executed[1];
        const auto value = response.substr(1, response.size() - 3);
        const auto deadline = store_.deadline_unix_ms(key);
        if (!deadline.has_value()) {
            // Already expired and reclaimed between the update and this lookup.
            return {"DEL", key};
        }
        if (deadline->has_value()) {
            return {"SET", key, value, "PXAT", std::to_string(**deadline)};
        }
        return {"SET", key, value};
    }

    return executed;
}

bool DurableCommandExecutor::rewrite_aof() {
    {
        std::lock_guard lock(write_mutex_);
        if (aof_ == nullptr || rewrite_in_progress_ || write_failed_) {
            return false;
        }
        rewrite_in_progress_ = true;
        rewrite_buffer_.clear();
    }
    return run_rewrite_after_start();
}

bool DurableCommandExecutor::run_rewrite_after_start() {
    auto temp_path = aof_->path();
    temp_path += ".rewrite.tmp";
    int fd = -1;
    bool installed = false;

    try {
        fd = ::open(temp_path.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644);
        if (fd < 0) {
            throw std::runtime_error(std::string("failed to open rewrite file: ") + std::strerror(errno));
        }

        // Phase 1: snapshot shard by shard while writers keep running. Each
        // shard copy holds only that shard's shared lock.
        std::string chunk;
        chunk.reserve(kRewriteChunkBytes + 4096);
        for (std::size_t shard = 0; shard < Store::kShardCount; ++shard) {
            for (const auto& entry : store_.snapshot_shard(shard)) {
                chunk += encode_snapshot_entry(entry);
                if (chunk.size() >= kRewriteChunkBytes) {
                    write_all_fd(fd, chunk);
                    chunk.clear();
                }
            }
        }
        write_all_fd(fd, chunk);
        fsync_fd(fd);

        // Phase 2: append records committed during phase 1, then swap files.
        std::lock_guard lock(write_mutex_);
        write_all_fd(fd, rewrite_buffer_);
        fsync_fd(fd);
        ::close(fd);
        fd = -1;

        try {
            aof_->replace_with(temp_path);
            installed = true;
        } catch (...) {
            // If rename succeeded but reopen failed, appends would land in an
            // unlinked file. Stop accepting writes rather than lose them silently.
            write_failed_ = true;
            throw;
        }

        rewrite_base_size_ = aof_->size();
        ++completed_rewrites_;
        rewrite_in_progress_ = false;
        rewrite_buffer_.clear();
        rewrite_buffer_.shrink_to_fit();
        return true;
    } catch (const std::exception& error) {
        if (fd >= 0) {
            ::close(fd);
        }
        if (!installed) {
            std::error_code ignored;
            std::filesystem::remove(temp_path, ignored);
        }
        std::cerr << "AOF rewrite failed: " << error.what() << "\n";

        // Any phase 2 lock was released during stack unwinding.
        std::lock_guard lock(write_mutex_);
        rewrite_in_progress_ = false;
        rewrite_buffer_.clear();
        return false;
    }
}

bool DurableCommandExecutor::start_background_rewrite() {
    std::lock_guard lock(write_mutex_);
    return start_background_rewrite_locked();
}

bool DurableCommandExecutor::start_background_rewrite_locked() {
    if (aof_ == nullptr || rewrite_in_progress_ || write_failed_) {
        return false;
    }
    // A previous rewrite thread has cleared rewrite_in_progress_ inside its final
    // locked section and takes no further locks, so joining here cannot deadlock.
    if (rewrite_thread_.joinable()) {
        rewrite_thread_.join();
    }
    rewrite_in_progress_ = true;
    rewrite_buffer_.clear();
    rewrite_thread_ = std::thread([this] { run_rewrite_after_start(); });
    return true;
}

void DurableCommandExecutor::maybe_auto_rewrite_locked() {
    if (rewrite_policy_.min_bytes == 0 || rewrite_in_progress_) {
        return;
    }
    const auto current = aof_->size();
    if (current < rewrite_policy_.min_bytes) {
        return;
    }
    const auto threshold = rewrite_base_size_ + rewrite_base_size_ * rewrite_policy_.growth_percent / 100;
    if (current < threshold) {
        return;
    }
    start_background_rewrite_locked();
}

bool DurableCommandExecutor::rewrite_in_progress() const {
    std::lock_guard lock(write_mutex_);
    return rewrite_in_progress_;
}

std::size_t DurableCommandExecutor::completed_rewrites() const {
    std::lock_guard lock(write_mutex_);
    return completed_rewrites_;
}

bool DurableCommandExecutor::write_failed() const {
    std::lock_guard lock(write_mutex_);
    return write_failed_;
}

}  // namespace miniredis
