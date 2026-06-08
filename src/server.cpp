#include "miniredis/server.hpp"

#include "miniredis/aof.hpp"
#include "miniredis/command.hpp"
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

bool should_append_to_aof(const std::vector<std::string>& command, const std::string& response) {
    if (response.empty() || response.front() == '-') {
        return false;
    }

    const auto name = std::string_view(command.front());
    if (equals_ci(name, "SET")) {
        return response == resp::simple_string("OK");
    }
    if (equals_ci(name, "INCR") || equals_ci(name, "DECR")) {
        return response.front() == ':';
    }
    if (equals_ci(name, "DEL") || equals_ci(name, "EXPIRE")) {
        return response == resp::integer(1);
    }

    return false;
}

void configure_client_socket(int fd) {
    int nodelay = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
}

void handle_client(SocketHandle client_fd, CommandDispatcher& dispatcher, const Aof* aof) {
    std::string buffer;
    buffer.reserve(8192);
    std::string output;
    output.reserve(8192);
    std::array<char, 4096> chunk{};

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
            const auto command = resp::parse_array_prefix(std::string_view(buffer).substr(consumed));
            if (!command.has_value()) {
                if (buffer.size() > 1024 * 1024) {
                    send_all(client_fd.get(), resp::error("ERR invalid RESP request"));
                    return;
                }
                break;
            }

            const auto response = dispatcher.execute(command->values);
            if (aof != nullptr && should_append_to_aof(command->values, response)) {
                try {
                    aof->append(resp::array(command->values));
                } catch (const std::exception& error) {
                    send_all(client_fd.get(), resp::error(error.what()));
                    return;
                }
            }

            output.append(response);
            consumed += command->bytes_consumed;
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
    ThreadPool workers(config_.workers);

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
        configure_client_socket(client_fd.get());

        auto client = std::make_shared<SocketHandle>(std::move(client_fd));
        Aof* aof_ptr = aof.get();
        try {
            workers.enqueue([client, &dispatcher, aof_ptr]() mutable {
                handle_client(std::move(*client), dispatcher, aof_ptr);
            });
        } catch (const std::exception& error) {
            std::cerr << "enqueue failed: " << error.what() << "\n";
        }
    }
}

}  // namespace miniredis
