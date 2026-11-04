#include "miniredis/aof.hpp"

#include "miniredis/command.hpp"
#include "miniredis/resp.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace miniredis {

namespace {

void write_all(int fd, const std::string& content, const AofIoHooks& hooks) {
    std::size_t written = 0;
    while (written < content.size()) {
        const auto result = hooks.write
            ? hooks.write(fd, content.data() + written, content.size() - written)
            : ::write(fd, content.data() + written, content.size() - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error(std::string("failed to write AOF file: ") + std::strerror(errno));
        }
        if (result == 0) {
            throw std::runtime_error("failed to write AOF file: zero-length write");
        }
        written += static_cast<std::size_t>(result);
    }
}

}  // namespace

Aof::Aof(std::filesystem::path path, AofSyncPolicy sync_policy, AofIoHooks io_hooks)
    : path_(std::move(path)),
      sync_policy_(sync_policy),
      last_sync_(std::chrono::steady_clock::now()),
      io_hooks_(std::move(io_hooks)) {
    if (path_.has_parent_path()) {
        std::filesystem::create_directories(path_.parent_path());
    }

    fd_ = ::open(path_.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0644);
    if (fd_ < 0) {
        throw std::runtime_error(std::string("failed to open AOF file: ") + std::strerror(errno));
    }
    struct stat info {};
    if (::fstat(fd_, &info) == 0) {
        size_ = static_cast<std::size_t>(info.st_size);
    }
}

Aof::~Aof() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void Aof::append(const std::string& encoded_command) const {
    std::lock_guard lock(mutex_);
    write_all(fd_, encoded_command, io_hooks_);
    size_ += encoded_command.size();
    sync_if_needed();
}

void Aof::replace_with(const std::filesystem::path& rewritten) const {
    std::lock_guard lock(mutex_);
    if (::rename(rewritten.c_str(), path_.c_str()) < 0) {
        throw std::runtime_error(std::string("failed to install rewritten AOF: ") + std::strerror(errno));
    }

    const auto directory = path_.has_parent_path() ? path_.parent_path() : std::filesystem::path(".");
    const int dir_fd = ::open(directory.c_str(), O_RDONLY);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }

    const int new_fd = ::open(path_.c_str(), O_APPEND | O_WRONLY);
    if (new_fd < 0) {
        throw std::runtime_error(std::string("failed to reopen rewritten AOF: ") + std::strerror(errno));
    }
    struct stat info {};
    if (::fstat(new_fd, &info) < 0) {
        ::close(new_fd);
        throw std::runtime_error(std::string("failed to stat rewritten AOF: ") + std::strerror(errno));
    }

    ::close(fd_);
    fd_ = new_fd;
    size_ = static_cast<std::size_t>(info.st_size);
    last_sync_ = std::chrono::steady_clock::now();
}

std::size_t Aof::size() const {
    std::lock_guard lock(mutex_);
    return size_;
}

std::size_t Aof::replay(CommandDispatcher& dispatcher) const {
    std::lock_guard lock(mutex_);

    std::ifstream file(path_, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open AOF file for replay");
    }

    std::string buffer;
    std::size_t count = 0;
    std::size_t good_offset = 0;
    std::size_t scanned_offset = 0;
    char chunk[4096];

    while (file.read(chunk, sizeof(chunk)) || file.gcount() > 0) {
        buffer.append(chunk, static_cast<std::size_t>(file.gcount()));

        while (!buffer.empty()) {
            const auto result = resp::parse_array_prefix(buffer);
            if (!result.has_value()) {
                break;
            }

            const auto response = dispatcher.execute(result->values);
            if (!response.empty() && response.front() == '-') {
                truncate_to(good_offset);
                return count;
            }

            scanned_offset += result->bytes_consumed;
            good_offset = scanned_offset;
            buffer.erase(0, result->bytes_consumed);
            ++count;
        }
    }

    if (!buffer.empty()) {
        truncate_to(good_offset);
    }

    return count;
}

void Aof::sync_if_needed() const {
    if (sync_policy_ == AofSyncPolicy::No) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (sync_policy_ == AofSyncPolicy::EverySec && now - last_sync_ < std::chrono::seconds(1)) {
        return;
    }

    const auto result = io_hooks_.fsync ? io_hooks_.fsync(fd_) : ::fsync(fd_);
    if (result < 0) {
        throw std::runtime_error(std::string("failed to fsync AOF file: ") + std::strerror(errno));
    }
    last_sync_ = now;
}

void Aof::truncate_to(std::size_t offset) const {
    if (::ftruncate(fd_, static_cast<off_t>(offset)) < 0) {
        throw std::runtime_error(std::string("failed to truncate AOF file: ") + std::strerror(errno));
    }
    size_ = offset;
    if (::lseek(fd_, 0, SEEK_END) < 0) {
        throw std::runtime_error(std::string("failed to seek AOF file: ") + std::strerror(errno));
    }
    sync_if_needed();
}

}  // namespace miniredis
