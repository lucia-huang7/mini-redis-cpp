#include "miniredis/server.hpp"

#include "miniredis/command.hpp"
#include "miniredis/resp.hpp"
#include "miniredis/store.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
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

void handle_client(int client_fd, CommandDispatcher& dispatcher) {
    std::string buffer;
    std::array<char, 4096> chunk{};

    while (true) {
        const auto bytes_read = ::recv(client_fd, chunk.data(), chunk.size(), 0);
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
        const auto command = resp::parse_array(buffer);
        if (!command.has_value()) {
            if (buffer.size() > 1024 * 1024) {
                send_all(client_fd, resp::error("ERR invalid RESP request"));
                return;
            }
            continue;
        }

        const auto response = dispatcher.execute(*command);
        if (!send_all(client_fd, response)) {
            return;
        }

        buffer.clear();
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

    std::cout << "Mini Redis listening on 0.0.0.0:" << config_.port << "\n";

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

        handle_client(client_fd.get(), dispatcher);
    }
}

}  // namespace miniredis
