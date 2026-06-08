#include "miniredis/resp.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
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
    std::size_t clients = 1;
    std::string command = "PING";
};

struct WorkerResult {
    std::size_t requests = 0;
    double elapsed_seconds = 0.0;
    std::vector<double> latencies_ms;
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
        } else if (arg == "--clients" && i + 1 < argc) {
            options.clients = static_cast<std::size_t>(std::stoull(argv[++i]));
        } else if (arg == "--command" && i + 1 < argc) {
            options.command = argv[++i];
        } else {
            throw std::runtime_error("unknown or incomplete argument");
        }
    }

    if (options.requests == 0) {
        throw std::runtime_error("requests must be greater than zero");
    }
    if (options.clients == 0) {
        throw std::runtime_error("clients must be greater than zero");
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

std::size_t find_line_end(const std::string& buffer, std::size_t start = 0) {
    return buffer.find("\r\n", start);
}

std::size_t expected_response_size(const std::string& buffer) {
    if (buffer.empty()) {
        return 0;
    }

    const auto line_end = find_line_end(buffer);
    if (line_end == std::string::npos) {
        return 0;
    }

    switch (buffer.front()) {
        case '+':
        case '-':
        case ':':
            return line_end + 2;
        case '$': {
            const auto len = std::stoll(buffer.substr(1, line_end - 1));
            if (len < 0) {
                return line_end + 2;
            }
            return line_end + 2 + static_cast<std::size_t>(len) + 2;
        }
        default:
            throw std::runtime_error("invalid RESP response");
    }
}

void read_response(int fd, std::string& buffer) {
    char chunk[4096];

    while (true) {
        const auto expected = expected_response_size(buffer);
        if (expected > 0 && buffer.size() >= expected) {
            buffer.erase(0, expected);
            return;
        }

        const auto bytes_read = ::recv(fd, chunk, sizeof(chunk), 0);
        if (bytes_read <= 0) {
            throw std::runtime_error(std::string("recv failed: ") + std::strerror(errno));
        }

        buffer.append(chunk, static_cast<std::size_t>(bytes_read));
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

WorkerResult run_worker(const Options& options, std::size_t client_index, std::size_t requests) {
    auto fd = connect_to_server(options);
    std::string response_buffer;
    std::vector<double> latencies_ms;
    latencies_ms.reserve(requests);

    const auto started = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < requests; ++i) {
        const auto request_index = client_index * requests + i;
        const auto request_started = std::chrono::steady_clock::now();
        send_all(fd.get(), request_for(options.command, request_index));
        read_response(fd.get(), response_buffer);
        const auto request_finished = std::chrono::steady_clock::now();
        latencies_ms.push_back(
            std::chrono::duration<double, std::milli>(request_finished - request_started).count());
    }
    const auto finished = std::chrono::steady_clock::now();

    return WorkerResult{
        requests,
        std::chrono::duration<double>(finished - started).count(),
        std::move(latencies_ms),
    };
}

double percentile(const std::vector<double>& values, double percentile) {
    if (values.empty()) {
        return 0.0;
    }

    const auto index = static_cast<std::size_t>(
        (percentile / 100.0) * static_cast<double>(values.size() - 1));
    return values[index];
}

void print_usage() {
    std::cerr
        << "Usage: miniredis_benchmark [--host 127.0.0.1] [--port 6379] "
        << "[--requests 10000] [--clients 1] [--command PING|SETGET]\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        std::vector<std::thread> workers;
        std::vector<WorkerResult> results(options.clients);
        std::vector<std::exception_ptr> errors(options.clients);

        const auto base_requests = options.requests / options.clients;
        const auto extra_requests = options.requests % options.clients;

        const auto started = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < options.clients; ++i) {
            const auto worker_requests = base_requests + (i < extra_requests ? 1 : 0);
            workers.emplace_back([&, i, worker_requests]() {
                try {
                    results[i] = run_worker(options, i, worker_requests);
                } catch (...) {
                    errors[i] = std::current_exception();
                }
            });
        }

        for (auto& worker : workers) {
            worker.join();
        }

        for (const auto& error : errors) {
            if (error) {
                std::rethrow_exception(error);
            }
        }
        const auto finished = std::chrono::steady_clock::now();

        const auto elapsed = std::chrono::duration<double>(finished - started).count();
        const auto throughput = static_cast<double>(options.requests) / elapsed;
        std::vector<double> latencies_ms;
        latencies_ms.reserve(options.requests);
        for (const auto& result : results) {
            latencies_ms.insert(
                latencies_ms.end(),
                result.latencies_ms.begin(),
                result.latencies_ms.end());
        }

        std::sort(latencies_ms.begin(), latencies_ms.end());

        double total_latency_ms = 0.0;
        for (const auto latency : latencies_ms) {
            total_latency_ms += latency;
        }
        const auto avg_latency_ms = total_latency_ms / static_cast<double>(latencies_ms.size());

        std::cout << "Command: " << options.command << "\n";
        std::cout << "Requests: " << options.requests << "\n";
        std::cout << "Clients: " << options.clients << "\n";
        std::cout << "Total time: " << elapsed << " sec\n";
        std::cout << "Throughput: " << throughput << " req/sec\n";
        std::cout << "Average latency: " << avg_latency_ms << " ms\n";
        std::cout << "p50 latency: " << percentile(latencies_ms, 50.0) << " ms\n";
        std::cout << "p95 latency: " << percentile(latencies_ms, 95.0) << " ms\n";
        std::cout << "p99 latency: " << percentile(latencies_ms, 99.0) << " ms\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        print_usage();
        return 1;
    }
}
