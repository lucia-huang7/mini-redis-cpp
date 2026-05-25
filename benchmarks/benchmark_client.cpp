#include "miniredis/resp.hpp"

#include <chrono>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

struct Options {
    std::string host = "127.0.0.1";
    std::uint16_t port = 6379;
    std::size_t requests = 10000;
    std::string command = "PING";
};

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

Options parse_options(int argc, char** argv) {
    Options options;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            options.host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            options.port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--requests" && i + 1 < argc) {
            options.requests = static_cast<std::size_t>(std::stoull(argv[++i]));
        } else if (arg == "--command" && i + 1 < argc) {
            options.command = argv[++i];
        } else {
            throw std::runtime_error("unknown or incomplete argument");
        }
    }

    return options;
}

SocketHandle connect_to_server(const Options& options) {
    SocketHandle fd(::socket(AF_INET, SOCK_STREAM, 0));
    if (!fd) {
        throw std::runtime_error(std::string("socket failed: ") + std::strerror(errno));
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(options.port);

    if (::inet_pton(AF_INET, options.host.c_str(), &address.sin_addr) != 1) {
        throw std::runtime_error("invalid host address");
    }

    if (::connect(fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        throw std::runtime_error(std::string("connect failed: ") + std::strerror(errno));
    }

    return fd;
}

void send_all(int fd, const std::string& request) {
    std::size_t sent = 0;
    while (sent < request.size()) {
        const auto written = ::send(fd, request.data() + sent, request.size() - sent, 0);
        if (written <= 0) {
            throw std::runtime_error(std::string("send failed: ") + std::strerror(errno));
        }
        sent += static_cast<std::size_t>(written);
    }
}

std::string read_response(int fd) {
    std::string response;
    char buffer[4096];

    while (true) {
        const auto bytes_read = ::recv(fd, buffer, sizeof(buffer), 0);
        if (bytes_read <= 0) {
            throw std::runtime_error(std::string("recv failed: ") + std::strerror(errno));
        }

        response.append(buffer, static_cast<std::size_t>(bytes_read));
        if (response.find("\r\n") != std::string::npos) {
            return response;
        }
    }
}

std::string request_for(const std::string& command, std::size_t index) {
    if (command == "PING") {
        return miniredis::resp::array({"PING"});
    }

    if (command == "SETGET") {
        if (index % 2 == 0) {
            return miniredis::resp::array({"SET", "bench:key", std::to_string(index)});
        }
        return miniredis::resp::array({"GET", "bench:key"});
    }

    throw std::runtime_error("unsupported command; use PING or SETGET");
}

void print_usage() {
    std::cerr
        << "Usage: miniredis_benchmark [--host 127.0.0.1] [--port 6379] "
        << "[--requests 10000] [--command PING|SETGET]\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        auto fd = connect_to_server(options);

        const auto started = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < options.requests; ++i) {
            send_all(fd.get(), request_for(options.command, i));
            (void)read_response(fd.get());
        }
        const auto finished = std::chrono::steady_clock::now();

        const auto elapsed = std::chrono::duration<double>(finished - started).count();
        const auto throughput = static_cast<double>(options.requests) / elapsed;
        const auto avg_latency_ms = (elapsed * 1000.0) / static_cast<double>(options.requests);

        std::cout << "Command: " << options.command << "\n";
        std::cout << "Requests: " << options.requests << "\n";
        std::cout << "Total time: " << elapsed << " sec\n";
        std::cout << "Throughput: " << throughput << " req/sec\n";
        std::cout << "Average latency: " << avg_latency_ms << " ms\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        print_usage();
        return 1;
    }
}
