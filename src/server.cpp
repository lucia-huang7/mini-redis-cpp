#include "miniredis/server.hpp"

#include "miniredis/aof.hpp"
#include "miniredis/command.hpp"
#include "miniredis/durable_executor.hpp"
#include "miniredis/poller.hpp"
#include "miniredis/resp.hpp"
#include "miniredis/store.hpp"
#include "miniredis/ttl.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace miniredis {
namespace {

using SteadyClock = std::chrono::steady_clock;

constexpr std::size_t kReadChunkBytes = std::size_t{64} * 1024;
// Stop parsing new requests from a client once this many reply bytes are queued.
// Reading resumes after the socket drains, so a slow reader cannot grow memory.
constexpr std::size_t kOutputHighWaterBytes = std::size_t{1} << 20;

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

class SocketHandle {
public:
    explicit SocketHandle(int fd = -1) : fd_(fd) {}

    ~SocketHandle() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;

    SocketHandle(SocketHandle&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

    SocketHandle& operator=(SocketHandle&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) {
                ::close(fd_);
            }
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    int get() const {
        return fd_;
    }

    int release() {
        return std::exchange(fd_, -1);
    }

    explicit operator bool() const {
        return fd_ >= 0;
    }

private:
    int fd_;
};

bool set_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void set_cloexec(int fd) {
    const int flags = ::fcntl(fd, F_GETFD, 0);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
}

struct Connection {
    explicit Connection(SocketHandle socket) : fd(std::move(socket)), last_active(SteadyClock::now()) {}

    std::size_t pending_output() const {
        return output.size() - output_offset;
    }

    SocketHandle fd;
    std::string input;
    std::string output;
    std::size_t output_offset = 0;
    SteadyClock::time_point last_active;
    bool want_read = true;
    bool want_write = false;
    // Set after a protocol error: flush the error reply, then close.
    bool closing = false;
};

// One thread, one Poller, many connections. Commands execute inline on the
// loop thread; with several loops, unrelated keys still run in parallel
// because Store locking is per shard.
class EventLoop {
public:
    EventLoop(
        DurableCommandExecutor& executor,
        std::chrono::milliseconds idle_timeout,
        std::atomic<std::size_t>& active_clients)
        : executor_(executor),
          idle_timeout_(idle_timeout),
          sweep_interval_(std::clamp<std::chrono::milliseconds>(
              idle_timeout / 4, std::chrono::milliseconds(5), std::chrono::milliseconds(250))),
          active_clients_(active_clients) {
        int fds[2];
        if (::pipe(fds) < 0) {
            throw std::runtime_error(std::string("pipe failed: ") + std::strerror(errno));
        }
        wake_read_ = SocketHandle(fds[0]);
        wake_write_ = SocketHandle(fds[1]);
        for (const int fd : fds) {
            set_nonblocking(fd);
            set_cloexec(fd);
        }
        poller_.add(wake_read_.get(), true, false);
    }

    ~EventLoop() {
        stop();
    }

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void start() {
        running_.store(true);
        thread_ = std::thread(&EventLoop::run, this);
    }

    void stop() {
        if (!running_.exchange(false)) {
            return;
        }
        wake();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // Called from the accept thread.
    void submit(SocketHandle client) {
        {
            std::lock_guard lock(pending_mutex_);
            pending_.push_back(client.release());
        }
        wake();
    }

private:
    void wake() {
        const char byte = 1;
        // A full pipe already guarantees a pending wakeup.
        [[maybe_unused]] const auto ignored = ::write(wake_write_.get(), &byte, 1);
    }

    void run() {
        std::vector<PollEvent> events;
        auto last_sweep = SteadyClock::now();

        while (running_.load()) {
            try {
                poller_.wait(events, static_cast<int>(sweep_interval_.count()));
            } catch (const std::exception& error) {
                std::cerr << "event loop wait failed: " << error.what() << "\n";
                continue;
            }

            for (const auto& event : events) {
                if (event.fd == wake_read_.get()) {
                    drain_wakeups();
                    adopt_pending();
                    continue;
                }
                handle_event(event);
            }

            const auto now = SteadyClock::now();
            if (now - last_sweep >= sweep_interval_) {
                close_idle(now);
                last_sweep = now;
            }
        }

        std::vector<int> open;
        open.reserve(connections_.size());
        for (const auto& [fd, connection] : connections_) {
            open.push_back(fd);
        }
        for (const int fd : open) {
            close_connection(fd);
        }
        adopt_pending();
    }

    void drain_wakeups() {
        char buffer[256];
        while (::read(wake_read_.get(), buffer, sizeof(buffer)) > 0) {
        }
    }

    void adopt_pending() {
        std::vector<int> adopted;
        {
            std::lock_guard lock(pending_mutex_);
            adopted.swap(pending_);
        }
        for (const int fd : adopted) {
            SocketHandle socket(fd);
            if (!running_.load()) {
                active_clients_.fetch_sub(1);
                continue;
            }
            try {
                poller_.add(fd, true, false);
            } catch (const std::exception& error) {
                std::cerr << "register client failed: " << error.what() << "\n";
                active_clients_.fetch_sub(1);
                continue;
            }
            connections_.emplace(fd, std::make_unique<Connection>(std::move(socket)));
        }
    }

    void handle_event(const PollEvent& event) {
        const auto it = connections_.find(event.fd);
        if (it == connections_.end()) {
            return;
        }
        auto& connection = *it->second;

        if (event.readable && connection.want_read && !read_input(connection)) {
            close_connection(event.fd);
            return;
        }
        if (!service(connection)) {
            close_connection(event.fd);
        }
    }

    // Returns false when the peer closed or the socket failed.
    bool read_input(Connection& connection) {
        char chunk[kReadChunkBytes];
        while (true) {
            const auto bytes = ::recv(connection.fd.get(), chunk, sizeof(chunk), 0);
            if (bytes > 0) {
                connection.input.append(chunk, static_cast<std::size_t>(bytes));
                connection.last_active = SteadyClock::now();
                // Level-triggered: leave any remainder for the next wakeup so one
                // busy client cannot starve the others on this loop.
                return true;
            }
            if (bytes == 0) {
                return false;
            }
            if (errno == EINTR) {
                continue;
            }
            return errno == EAGAIN || errno == EWOULDBLOCK;
        }
    }

    // Executes every complete request in the input buffer. Returns true if it
    // stopped early because the output buffer reached the high-water mark.
    bool process_input(Connection& connection) {
        std::size_t consumed = 0;
        bool output_bound = false;

        while (!connection.closing && consumed < connection.input.size()) {
            if (connection.pending_output() >= kOutputHighWaterBytes) {
                output_bound = true;
                break;
            }

            const auto remaining = std::string_view(connection.input).substr(consumed);
            const auto command = resp::parse_array_prefix_view_checked(remaining);
            if (command.status == resp::ParseStatus::Complete) {
                connection.output.append(executor_.execute(command.result.values));
                consumed += command.result.bytes_consumed;
                continue;
            }

            if (command.status == resp::ParseStatus::Invalid ||
                command.status == resp::ParseStatus::TooLarge ||
                remaining.size() > resp::kMaxRequestBytes) {
                connection.output.append(resp::error(
                    command.status == resp::ParseStatus::TooLarge
                        ? "ERR request exceeds configured limits"
                        : "ERR invalid RESP request"));
                connection.closing = true;
                consumed = connection.input.size();
            }
            break;
        }

        if (consumed > 0) {
            connection.input.erase(0, consumed);
        }
        return output_bound;
    }

    // Returns false on a hard socket error.
    bool flush_output(Connection& connection) {
        while (connection.pending_output() > 0) {
            const auto bytes = ::send(
                connection.fd.get(),
                connection.output.data() + connection.output_offset,
                connection.pending_output(),
                kSendFlags);
            if (bytes > 0) {
                connection.output_offset += static_cast<std::size_t>(bytes);
                connection.last_active = SteadyClock::now();
                continue;
            }
            if (bytes < 0 && errno == EINTR) {
                continue;
            }
            if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }
            return false;
        }

        if (connection.pending_output() == 0) {
            connection.output.clear();
            connection.output_offset = 0;
        } else if (connection.output_offset * 2 > connection.output.size()) {
            connection.output.erase(0, connection.output_offset);
            connection.output_offset = 0;
        }
        return true;
    }

    // Runs requests, writes replies, and re-arms interest. Returns false when
    // the connection should be closed.
    bool service(Connection& connection) {
        while (true) {
            const bool output_bound = process_input(connection);
            if (!flush_output(connection)) {
                return false;
            }
            if (connection.closing) {
                if (connection.pending_output() == 0) {
                    return false;
                }
                break;
            }
            if (!output_bound || connection.pending_output() >= kOutputHighWaterBytes) {
                break;
            }
        }

        const bool want_read = !connection.closing && connection.pending_output() < kOutputHighWaterBytes;
        const bool want_write = connection.pending_output() > 0;
        if (want_read != connection.want_read || want_write != connection.want_write) {
            try {
                poller_.modify(connection.fd.get(), want_read, want_write);
            } catch (const std::exception&) {
                return false;
            }
            connection.want_read = want_read;
            connection.want_write = want_write;
        }
        return true;
    }

    void close_idle(SteadyClock::time_point now) {
        std::vector<int> expired;
        for (const auto& [fd, connection] : connections_) {
            if (now - connection->last_active >= idle_timeout_) {
                expired.push_back(fd);
            }
        }
        for (const int fd : expired) {
            close_connection(fd);
        }
    }

    void close_connection(int fd) {
        poller_.remove(fd);
        if (connections_.erase(fd) > 0) {
            active_clients_.fetch_sub(1);
        }
    }

    DurableCommandExecutor& executor_;
    std::chrono::milliseconds idle_timeout_;
    std::chrono::milliseconds sweep_interval_;
    std::atomic<std::size_t>& active_clients_;

    Poller poller_;
    SocketHandle wake_read_;
    SocketHandle wake_write_;
    std::atomic<bool> running_{false};
    std::thread thread_;

    std::mutex pending_mutex_;
    std::vector<int> pending_;
    std::unordered_map<int, std::unique_ptr<Connection>> connections_;
};

}  // namespace

Server::Server(Config config) : config_(std::move(config)) {}

int Server::run() {
    std::signal(SIGPIPE, SIG_IGN);

    SocketHandle server_fd(::socket(AF_INET, SOCK_STREAM, 0));
    if (!server_fd) {
        std::cerr << "socket failed: " << std::strerror(errno) << "\n";
        return 1;
    }
    set_cloexec(server_fd.get());

    int reuse = 1;
    if (::setsockopt(server_fd.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt failed: " << std::strerror(errno) << "\n";
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(config_.port);

    if (::bind(server_fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "bind failed: " << std::strerror(errno) << "\n";
        return 1;
    }

    if (::listen(server_fd.get(), SOMAXCONN) < 0) {
        std::cerr << "listen failed: " << std::strerror(errno) << "\n";
        return 1;
    }

    Store store;
    CommandDispatcher dispatcher(store);
    std::unique_ptr<Aof> aof;
    if (config_.aof_path.has_value()) {
        aof = std::make_unique<Aof>(*config_.aof_path, config_.appendfsync);
    }
    TtlCleaner ttl_cleaner(store);

    if (aof) {
        try {
            const auto replayed = aof->replay(dispatcher);
            if (replayed > 0) {
                std::cout << "Replayed " << replayed << " AOF command(s)\n";
            }
        } catch (const std::exception& error) {
            std::cerr << "AOF replay failed: " << error.what() << "\n";
            return 1;
        }
    }

    DurableCommandExecutor executor(store, dispatcher, aof.get(), config_.aof_rewrite);

    std::atomic<std::size_t> active_clients{0};
    const auto loop_count = std::max<std::size_t>(1, config_.workers);
    std::vector<std::unique_ptr<EventLoop>> loops;
    loops.reserve(loop_count);
    for (std::size_t i = 0; i < loop_count; ++i) {
        loops.push_back(std::make_unique<EventLoop>(
            executor, std::chrono::milliseconds(config_.idle_timeout_ms), active_clients));
        loops.back()->start();
    }

    ttl_cleaner.start();
    std::cout << "Mini Redis listening on 0.0.0.0:" << config_.port
              << " with " << loop_count << " " << Poller::backend_name()
              << " event loop(s)\n";

    std::size_t next_loop = 0;
    while (true) {
        sockaddr_in client_address{};
        socklen_t client_address_len = sizeof(client_address);
        SocketHandle client_fd(::accept(
            server_fd.get(),
            reinterpret_cast<sockaddr*>(&client_address),
            &client_address_len));

        if (!client_fd) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "accept failed: " << std::strerror(errno) << "\n";
            if (errno == EMFILE || errno == ENFILE) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue;
        }

        if (active_clients.load() >= config_.max_clients) {
            // Closing immediately gives the client a clean EOF instead of a hang.
            continue;
        }
        if (!set_nonblocking(client_fd.get())) {
            continue;
        }
        set_cloexec(client_fd.get());
        int nodelay = 1;
        ::setsockopt(client_fd.get(), IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        active_clients.fetch_add(1);
        loops[next_loop]->submit(std::move(client_fd));
        next_loop = (next_loop + 1) % loops.size();
    }
}

}  // namespace miniredis
