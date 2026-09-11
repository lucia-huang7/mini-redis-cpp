#include "miniredis/server.hpp"

#include "miniredis/aof.hpp"
#include "miniredis/command.hpp"
#include "miniredis/durable_executor.hpp"
#include "miniredis/resp.hpp"
#include "miniredis/store.hpp"
#include "miniredis/thread_pool.hpp"
#include "miniredis/ttl.hpp"

#include <array>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cctype>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace miniredis {
namespace {

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

    explicit operator bool() const {
        return fd_ >= 0;
    }

private:
    int fd_;
};

bool send_all(int fd, const std::string& response) {
    std::size_t sent = 0;
    while (sent < response.size()) {
        const auto written = ::send(fd, response.data() + sent, response.size() - sent, 0);
        if (written <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

void configure_client_socket(int fd, std::size_t idle_timeout_ms) {
    int nodelay = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    timeval timeout{};
    timeout.tv_sec = static_cast<time_t>(idle_timeout_ms / 1000);
    timeout.tv_usec = static_cast<suseconds_t>((idle_timeout_ms % 1000) * 1000);
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

void handle_client(SocketHandle client_fd, DurableCommandExecutor& executor) {
    std::string buffer;
    buffer.reserve(65536);
    std::string output;
    output.reserve(65536);
    std::array<char, 16384> chunk{};

    while (true) {
        const auto bytes_read = ::recv(client_fd.get(), chunk.data(), chunk.size(), 0);
        if (bytes_read == 0) {
            return;
        }
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }

        buffer.append(chunk.data(), static_cast<std::size_t>(bytes_read));
        std::size_t consumed = 0;
        output.clear();

        while (consumed < buffer.size()) {
            const auto command = resp::parse_array_prefix_view_checked(std::string_view(buffer).substr(consumed));
            if (command.status != resp::ParseStatus::Complete) {
                if (command.status == resp::ParseStatus::Invalid ||
                    command.status == resp::ParseStatus::TooLarge ||
                    buffer.size() > resp::kMaxRequestBytes) {
                    send_all(client_fd.get(), resp::error(
                        command.status == resp::ParseStatus::TooLarge
                            ? "ERR request exceeds configured limits"
                            : "ERR invalid RESP request"));
                    return;
                }
                break;
            }

            const auto response = executor.execute(command.result.values);
            output.append(response);
            consumed += command.result.bytes_consumed;
        }

        if (!output.empty() && !send_all(client_fd.get(), output)) {
            return;
        }
        if (consumed > 0) {
            buffer.erase(0, consumed);
        }
    }
}

}  // namespace

Server::Server(Config config) : config_(std::move(config)) {}

int Server::run() {
    std::signal(SIGPIPE, SIG_IGN);

    SocketHandle server_fd(::socket(AF_INET, SOCK_STREAM, 0));
    if (!server_fd) {
        std::cerr << "socket failed: " << std::strerror(errno) << "\n";
        return 1;
    }

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
    ThreadPool workers(config_.workers, config_.max_queued_clients);

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

    DurableCommandExecutor executor(dispatcher, aof.get());

    ttl_cleaner.start();
    std::cout << "Mini Redis listening on 0.0.0.0:" << config_.port
              << " with " << workers.size() << " worker(s)\n";

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
            continue;
        }
        configure_client_socket(client_fd.get(), config_.idle_timeout_ms);

        auto client = std::make_shared<SocketHandle>(std::move(client_fd));
        try {
            workers.enqueue([client, &executor]() mutable {
                handle_client(std::move(*client), executor);
            });
        } catch (const std::exception& error) {
            std::cerr << "enqueue failed: " << error.what() << "\n";
        }
    }
}

}  // namespace miniredis
