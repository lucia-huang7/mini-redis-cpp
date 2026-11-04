#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#if !defined(MINI_REDIS_POLLER_EPOLL) && !defined(MINI_REDIS_POLLER_KQUEUE) && \
    !defined(MINI_REDIS_POLLER_POLL)
#if defined(__linux__)
#define MINI_REDIS_POLLER_EPOLL 1
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#define MINI_REDIS_POLLER_KQUEUE 1
#else
#define MINI_REDIS_POLLER_POLL 1
#endif
#endif

#if defined(MINI_REDIS_POLLER_POLL)
#include <poll.h>
#endif

namespace miniredis {

struct PollEvent {
    int fd = -1;
    bool readable = false;
    bool writable = false;
};

// Level-triggered readiness notification over epoll (Linux), kqueue (macOS/BSD),
// or poll (portable fallback). One Poller is owned by exactly one event-loop thread.
class Poller {
public:
    Poller();
    ~Poller();

    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;

    void add(int fd, bool want_read, bool want_write);
    void modify(int fd, bool want_read, bool want_write);
    void remove(int fd);

    // Fills events with ready descriptors. timeout_ms < 0 blocks indefinitely.
    void wait(std::vector<PollEvent>& events, int timeout_ms);

    static const char* backend_name();

private:
#if defined(MINI_REDIS_POLLER_POLL)
    std::vector<pollfd> fds_;
    std::unordered_map<int, std::size_t> index_;
#else
    int handle_ = -1;
#endif
#if defined(MINI_REDIS_POLLER_KQUEUE)
    struct Interest {
        bool read = false;
        bool write = false;
        bool read_registered = false;
        bool write_registered = false;
    };
    std::unordered_map<int, Interest> interest_;
#endif
};

}  // namespace miniredis
