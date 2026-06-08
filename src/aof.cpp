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
#include <unistd.h>

namespace miniredis {

namespace {

void write_all(int fd, const std::string& content) {
    std::size_t written = 0;
    while (written < content.size()) {
        const auto result = ::write(fd, content.data() + written, content.size() - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error(std::string("failed to write AOF file: ") + std::strerror(errno));
        }
        written += static_cast<std::size_t>(result);
    }
}

}  // namespace

Aof::Aof(std::filesystem::path path, AofSyncPolicy sync_policy)
    : path_(std::move(path)),
      sync_policy_(sync_policy),
      last_sync_(std::chrono::steady_clock::now()) {
    if (path_.has_parent_path()) {
        std::filesystem::create_directories(path_.parent_path());
    }

    fd_ = ::open(path_.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0644);
    if (fd_ < 0) {
        throw std::runtime_error(std::string("failed to open AOF file: ") + std::strerror(errno));
    }
}

Aof::~Aof() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void Aof::append(const std::string& encoded_command) const {
    std::lock_guard lock(mutex_);
    write_all(fd_, encoded_command);
    sync_if_needed();
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

    if (::fsync(fd_) < 0) {
        throw std::runtime_error(std::string("failed to fsync AOF file: ") + std::strerror(errno));
    }
    last_sync_ = now;
}

void Aof::truncate_to(std::size_t offset) const {
    if (::ftruncate(fd_, static_cast<off_t>(offset)) < 0) {
        throw std::runtime_error(std::string("failed to truncate AOF file: ") + std::strerror(errno));
    }
    if (::lseek(fd_, 0, SEEK_END) < 0) {
        throw std::runtime_error(std::string("failed to seek AOF file: ") + std::strerror(errno));
    }
    sync_if_needed();
}

}  // namespace miniredis
