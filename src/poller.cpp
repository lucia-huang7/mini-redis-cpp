#include "miniredis/poller.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

#include <unistd.h>

#if defined(MINI_REDIS_POLLER_EPOLL)
#include <sys/epoll.h>
#elif defined(MINI_REDIS_POLLER_KQUEUE)
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#endif

namespace miniredis {
namespace {

[[noreturn]] void throw_errno(const char* what) {
    throw std::runtime_error(std::string(what) + ": " + std::strerror(errno));
}

[[maybe_unused]] constexpr int kMaxEventsPerWait = 256;

}  // namespace

#if defined(MINI_REDIS_POLLER_EPOLL)

namespace {

std::uint32_t epoll_mask(bool want_read, bool want_write) {
    std::uint32_t mask = 0;
    if (want_read) {
        mask |= EPOLLIN | EPOLLRDHUP;
    }
    if (want_write) {
        mask |= EPOLLOUT;
    }
    return mask;
}

}  // namespace

Poller::Poller() : handle_(::epoll_create1(EPOLL_CLOEXEC)) {
    if (handle_ < 0) {
        throw_errno("epoll_create1 failed");
    }
}

Poller::~Poller() {
    ::close(handle_);
}

void Poller::add(int fd, bool want_read, bool want_write) {
    epoll_event event{};
    event.events = epoll_mask(want_read, want_write);
    event.data.fd = fd;
    if (::epoll_ctl(handle_, EPOLL_CTL_ADD, fd, &event) < 0) {
        throw_errno("epoll_ctl(ADD) failed");
    }
}

void Poller::modify(int fd, bool want_read, bool want_write) {
    epoll_event event{};
    event.events = epoll_mask(want_read, want_write);
    event.data.fd = fd;
    if (::epoll_ctl(handle_, EPOLL_CTL_MOD, fd, &event) < 0) {
        throw_errno("epoll_ctl(MOD) failed");
    }
}

void Poller::remove(int fd) {
    ::epoll_ctl(handle_, EPOLL_CTL_DEL, fd, nullptr);
}

void Poller::wait(std::vector<PollEvent>& events, int timeout_ms) {
    events.clear();
    epoll_event ready[kMaxEventsPerWait];
    const int count = ::epoll_wait(handle_, ready, kMaxEventsPerWait, timeout_ms);
    if (count < 0) {
        if (errno == EINTR) {
            return;
        }
        throw_errno("epoll_wait failed");
    }
    for (int i = 0; i < count; ++i) {
        const auto mask = ready[i].events;
        PollEvent event;
        event.fd = ready[i].data.fd;
        // Errors and hangups surface as readable so the next recv() observes them.
        event.readable = (mask & (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR)) != 0;
        event.writable = (mask & EPOLLOUT) != 0;
        events.push_back(event);
    }
}

const char* Poller::backend_name() {
    return "epoll";
}

#elif defined(MINI_REDIS_POLLER_KQUEUE)

Poller::Poller() : handle_(::kqueue()) {
    if (handle_ < 0) {
        throw_errno("kqueue failed");
    }
}

Poller::~Poller() {
    ::close(handle_);
}

namespace {

// EV_RECEIPT reports each change's result individually instead of failing the
// whole call, which also keeps libkqueue from writing into a null event list.
void apply_changes(int kq, struct kevent* changes, int count, bool ignore_missing) {
    if (count == 0) {
        return;
    }
    struct kevent results[2];
    for (int i = 0; i < count; ++i) {
        changes[i].flags |= EV_RECEIPT;
    }
    const int returned = ::kevent(kq, changes, count, results, count, nullptr);
    if (returned < 0) {
        throw_errno("kevent(change) failed");
    }
    for (int i = 0; i < returned; ++i) {
        if ((results[i].flags & EV_ERROR) == 0 || results[i].data == 0) {
            continue;
        }
        if (ignore_missing && results[i].data == ENOENT) {
            continue;
        }
        errno = static_cast<int>(results[i].data);
        throw_errno("kevent(change) failed");
    }
}

}  // namespace

// A filter is registered the first time it is wanted and afterwards toggled
// with EV_ENABLE/EV_DISABLE, which keeps level-triggered readiness intact.
// Registering an unused EVFILT_WRITE up front fails on some descriptors
// (e.g. a pipe's read end), so write filters are only added on demand.
void Poller::add(int fd, bool want_read, bool want_write) {
    interest_[fd] = Interest{};
    modify(fd, want_read, want_write);
}

void Poller::modify(int fd, bool want_read, bool want_write) {
    auto& current = interest_[fd];
    struct kevent changes[2];
    int count = 0;
    const auto plan = [&](short filter, bool want, bool& enabled, bool& registered) {
        if (want == enabled) {
            return;
        }
        unsigned short flags = 0;
        if (want) {
            flags = registered ? EV_ENABLE : (EV_ADD | EV_ENABLE);
        } else {
            flags = EV_DISABLE;
        }
        EV_SET(&changes[count++], fd, filter, flags, 0, 0, nullptr);
    };
    plan(EVFILT_READ, want_read, current.read, current.read_registered);
    plan(EVFILT_WRITE, want_write, current.write, current.write_registered);
    apply_changes(handle_, changes, count, false);

    if (want_read) {
        current.read_registered = true;
    }
    if (want_write) {
        current.write_registered = true;
    }
    current.read = want_read;
    current.write = want_write;
}

void Poller::remove(int fd) {
    const auto it = interest_.find(fd);
    if (it == interest_.end()) {
        return;
    }
    struct kevent changes[2];
    int count = 0;
    if (it->second.read_registered) {
        EV_SET(&changes[count++], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    }
    if (it->second.write_registered) {
        EV_SET(&changes[count++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    }
    interest_.erase(it);
    // Closing a descriptor also drops its filters, so failures here are benign.
    try {
        apply_changes(handle_, changes, count, true);
    } catch (const std::exception&) {
    }
}

void Poller::wait(std::vector<PollEvent>& events, int timeout_ms) {
    events.clear();
    struct kevent ready[kMaxEventsPerWait];
    timespec timeout{};
    timespec* timeout_ptr = nullptr;
    if (timeout_ms >= 0) {
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_nsec = static_cast<long>(timeout_ms % 1000) * 1000000L;
        timeout_ptr = &timeout;
    }
    const int count = ::kevent(handle_, nullptr, 0, ready, kMaxEventsPerWait, timeout_ptr);
    if (count < 0) {
        if (errno == EINTR) {
            return;
        }
        throw_errno("kevent(wait) failed");
    }
    for (int i = 0; i < count; ++i) {
        PollEvent event;
        event.fd = static_cast<int>(ready[i].ident);
        const bool failed = (ready[i].flags & (EV_EOF | EV_ERROR)) != 0;
        event.readable = ready[i].filter == EVFILT_READ || failed;
        event.writable = ready[i].filter == EVFILT_WRITE;
        events.push_back(event);
    }
}

const char* Poller::backend_name() {
    return "kqueue";
}

#else  // MINI_REDIS_POLLER_POLL

namespace {

short poll_mask(bool want_read, bool want_write) {
    short mask = 0;
    if (want_read) {
        mask |= POLLIN;
    }
    if (want_write) {
        mask |= POLLOUT;
    }
    return mask;
}

}  // namespace

Poller::Poller() = default;

Poller::~Poller() = default;

void Poller::add(int fd, bool want_read, bool want_write) {
    if (index_.count(fd) != 0) {
        throw std::runtime_error("poll add: descriptor already registered");
    }
    index_[fd] = fds_.size();
    fds_.push_back(pollfd{fd, poll_mask(want_read, want_write), 0});
}

void Poller::modify(int fd, bool want_read, bool want_write) {
    const auto it = index_.find(fd);
    if (it == index_.end()) {
        throw std::runtime_error("poll modify: descriptor not registered");
    }
    fds_[it->second].events = poll_mask(want_read, want_write);
}

void Poller::remove(int fd) {
    const auto it = index_.find(fd);
    if (it == index_.end()) {
        return;
    }
    const auto position = it->second;
    index_.erase(it);
    if (position != fds_.size() - 1) {
        fds_[position] = fds_.back();
        index_[fds_[position].fd] = position;
    }
    fds_.pop_back();
}

void Poller::wait(std::vector<PollEvent>& events, int timeout_ms) {
    events.clear();
    const int count = ::poll(fds_.data(), static_cast<nfds_t>(fds_.size()), timeout_ms);
    if (count < 0) {
        if (errno == EINTR) {
            return;
        }
        throw_errno("poll failed");
    }
    for (const auto& entry : fds_) {
        if (entry.revents == 0) {
            continue;
        }
        PollEvent event;
        event.fd = entry.fd;
        event.readable = (entry.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0;
        event.writable = (entry.revents & POLLOUT) != 0;
        events.push_back(event);
    }
}

const char* Poller::backend_name() {
    return "poll";
}

#endif

}  // namespace miniredis
