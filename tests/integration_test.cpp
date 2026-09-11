#include "miniredis/resp.hpp"

#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

class Fd {
public:
    explicit Fd(int fd = -1) : fd_(fd) {}

    ~Fd() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;

    Fd(Fd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

    Fd& operator=(Fd&& other) noexcept {
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

std::uint16_t find_free_port() {
    Fd fd(::socket(AF_INET, SOCK_STREAM, 0));
    if (!fd) {
        throw std::runtime_error("socket failed");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    if (::bind(fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        throw std::runtime_error("bind failed");
    }

    socklen_t length = sizeof(address);
    if (::getsockname(fd.get(), reinterpret_cast<sockaddr*>(&address), &length) < 0) {
        throw std::runtime_error("getsockname failed");
    }

    return ntohs(address.sin_port);
}

Fd connect_to(std::uint16_t port) {
    Fd fd(::socket(AF_INET, SOCK_STREAM, 0));
    if (!fd) {
        throw std::runtime_error("socket failed");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

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
            throw std::runtime_error("send failed");
        }
        sent += static_cast<std::size_t>(written);
    }
}

std::size_t expected_response_size(const std::string& buffer) {
    if (buffer.empty()) {
        return 0;
    }

    const auto line_end = buffer.find("\r\n");
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
            throw std::runtime_error("invalid response");
    }
}

std::string read_response(int fd, std::string& buffer) {
    char chunk[4096];

    while (true) {
        const auto expected = expected_response_size(buffer);
        if (expected > 0 && buffer.size() >= expected) {
            auto response = buffer.substr(0, expected);
            buffer.erase(0, expected);
            return response;
        }

        const auto bytes_read = ::recv(fd, chunk, sizeof(chunk), 0);
        if (bytes_read <= 0) {
            throw std::runtime_error("recv failed");
        }

        buffer.append(chunk, static_cast<std::size_t>(bytes_read));
    }
}

class Client {
public:
    explicit Client(std::uint16_t port) : fd_(connect_to(port)) {}

    std::string command(const std::vector<std::string>& values) {
        send_all(fd_.get(), miniredis::resp::array(values));
        return read_response(fd_.get(), buffer_);
    }

    void send_raw(const std::string& request) {
        send_all(fd_.get(), request);
    }

    std::string read() {
        return read_response(fd_.get(), buffer_);
    }

private:
    Fd fd_;
    std::string buffer_;
};

class ServerProcess {
public:
    ServerProcess(
        std::string server_path,
        std::uint16_t port,
        std::filesystem::path aof_path,
        std::size_t workers = 4,
        std::size_t idle_timeout_ms = 30000,
        std::size_t max_queued_clients = 1024)
        : port_(port) {
        pid_ = ::fork();
        if (pid_ < 0) {
            throw std::runtime_error("fork failed");
        }

        if (pid_ == 0) {
            const auto port_arg = std::to_string(port);
            const auto workers_arg = std::to_string(workers);
            const auto aof_arg = aof_path.string();
            const auto idle_timeout_arg = std::to_string(idle_timeout_ms);
            const auto max_queued_clients_arg = std::to_string(max_queued_clients);
            ::execl(
                server_path.c_str(),
                server_path.c_str(),
                "--port",
                port_arg.c_str(),
                "--workers",
                workers_arg.c_str(),
                "--aof",
                aof_arg.c_str(),
                "--idle-timeout-ms",
                idle_timeout_arg.c_str(),
                "--max-queued-clients",
                max_queued_clients_arg.c_str(),
                static_cast<char*>(nullptr));
            _exit(127);
        }

        wait_until_ready();
    }

    ~ServerProcess() {
        stop();
    }

    ServerProcess(const ServerProcess&) = delete;
    ServerProcess& operator=(const ServerProcess&) = delete;

    void stop() {
        if (pid_ <= 0) {
            return;
        }

        ::kill(pid_, SIGTERM);
        int status = 0;
        ::waitpid(pid_, &status, 0);
        pid_ = -1;
    }

private:
    void wait_until_ready() const {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline) {
            try {
                auto client = connect_to(port_);
                return;
            } catch (const std::exception&) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
        }

        throw std::runtime_error("server did not start");
    }

    pid_t pid_ = -1;
    std::uint16_t port_;
};

std::filesystem::path temp_aof_path(const std::string& name) {
    return std::filesystem::temp_directory_path() /
           ("miniredis_" + name + "_" + std::to_string(::getpid()) + ".aof");
}

void pipeline_test(std::uint16_t port) {
    Client client(port);
    client.send_raw(
        miniredis::resp::array({"PING"}) +
        miniredis::resp::array({"ECHO", "hello"}));

    assert(client.read() == "+PONG\r\n");
    assert(client.read() == "$5\r\nhello\r\n");
}

void split_resp_test(std::uint16_t port) {
    Client client(port);
    client.send_raw("*3\r\n$3\r\nSET\r\n$4\r\nname\r\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    client.send_raw("$5\r\nalice\r\n");
    assert(client.read() == "+OK\r\n");
    assert(client.command({"GET", "name"}) == "$5\r\nalice\r\n");
}

void error_input_test(std::uint16_t port) {
    Client client(port);
    assert(client.command({"GET"}) == "-ERR wrong number of arguments for 'get' command\r\n");
    assert(client.command({"UNKNOWN"}) == "-ERR unknown command\r\n");
}

void hostile_resp_test(std::uint16_t port) {
    {
        Client client(port);
        client.send_raw("*999999999999999999999999\r\n");
        assert(client.read() == "-ERR invalid RESP request\r\n");
    }
    {
        Client client(port);
        client.send_raw("*1025\r\n");
        assert(client.read() == "-ERR request exceeds configured limits\r\n");
    }
    {
        Client client(port);
        client.send_raw("*1\r\n$524289\r\n");
        assert(client.read() == "-ERR request exceeds configured limits\r\n");
    }
}

void concurrent_clients_test(std::uint16_t port) {
    constexpr std::size_t clients = 8;
    constexpr std::size_t iterations = 50;
    std::vector<std::thread> threads;

    for (std::size_t i = 0; i < clients; ++i) {
        threads.emplace_back([port, i]() {
            Client client(port);
            for (std::size_t j = 0; j < iterations; ++j) {
                const auto key = "client:" + std::to_string(i) + ":" + std::to_string(j);
                const auto value = std::to_string(j);
                assert(client.command({"SET", key, value}) == "+OK\r\n");
                assert(client.command({"GET", key}) == miniredis::resp::bulk_string(value));
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }
}

void ttl_boundary_test(std::uint16_t port) {
    Client client(port);
    assert(client.command({"SET", "short", "lived"}) == "+OK\r\n");
    assert(client.command({"EXPIRE", "short", "1"}) == ":1\r\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(1300));
    assert(client.command({"GET", "short"}) == "$-1\r\n");
    assert(client.command({"TTL", "short"}) == ":-2\r\n");
}

void aof_replay_test(const std::string& server_path) {
    const auto port = find_free_port();
    const auto aof_path = temp_aof_path("replay");
    std::filesystem::remove(aof_path);

    {
        ServerProcess server(server_path, port, aof_path);
        Client client(port);
        assert(client.command({"SET", "persisted", "value"}) == "+OK\r\n");
        server.stop();
    }

    {
        ServerProcess server(server_path, port, aof_path);
        Client client(port);
        assert(client.command({"GET", "persisted"}) == "$5\r\nvalue\r\n");
        server.stop();
    }

    std::filesystem::remove(aof_path);
}

void aof_ttl_deadline_replay_test(const std::string& server_path) {
    const auto port = find_free_port();
    const auto aof_path = temp_aof_path("ttl_deadline");
    std::filesystem::remove(aof_path);

    {
        ServerProcess server(server_path, port, aof_path);
        Client client(port);
        assert(client.command({"SET", "expire-command", "value"}) == "+OK\r\n");
        assert(client.command({"EXPIRE", "expire-command", "1"}) == ":1\r\n");
        assert(client.command({"SET", "set-option", "value", "PX", "1000"}) == "+OK\r\n");
        assert(client.command({"SET", "remaining", "value"}) == "+OK\r\n");
        assert(client.command({"EXPIRE", "remaining", "10"}) == ":1\r\n");
        server.stop();
    }

    {
        std::ifstream file(aof_path, std::ios::binary);
        const std::string contents(
            (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        assert(contents.find("PEXPIREAT") != std::string::npos);
        assert(contents.find("PXAT") != std::string::npos);
        assert(contents.find("$6\r\nEXPIRE\r\n") == std::string::npos);
        assert(contents.find("$2\r\nPX\r\n") == std::string::npos);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1300));

    {
        ServerProcess server(server_path, port, aof_path);
        Client client(port);
        assert(client.command({"GET", "expire-command"}) == "$-1\r\n");
        assert(client.command({"GET", "set-option"}) == "$-1\r\n");
        assert(client.command({"GET", "remaining"}) == "$5\r\nvalue\r\n");
        const auto remaining = client.command({"TTL", "remaining"});
        const auto remaining_seconds = std::stoll(remaining.substr(1));
        assert(remaining_seconds >= 0 && remaining_seconds <= 8);
        server.stop();
    }

    std::filesystem::remove(aof_path);
}

void concurrent_aof_order_test(const std::string& server_path) {
    const auto port = find_free_port();
    const auto aof_path = temp_aof_path("concurrent_order");
    std::filesystem::remove(aof_path);
    std::string expected;

    {
        ServerProcess server(server_path, port, aof_path, 8);
        constexpr std::size_t clients = 8;
        constexpr std::size_t iterations = 100;
        std::vector<std::thread> threads;
        for (std::size_t i = 0; i < clients; ++i) {
            threads.emplace_back([port, i] {
                Client client(port);
                for (std::size_t j = 0; j < iterations; ++j) {
                    const auto value = std::to_string(i) + ":" + std::to_string(j);
                    assert(client.command({"SET", "shared-key", value}) == "+OK\r\n");
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        Client client(port);
        expected = client.command({"GET", "shared-key"});
        assert(expected != "$-1\r\n");
        server.stop();
    }

    {
        ServerProcess server(server_path, port, aof_path, 8);
        Client client(port);
        assert(client.command({"GET", "shared-key"}) == expected);
        server.stop();
    }
    std::filesystem::remove(aof_path);
}

void idle_and_slow_client_timeout_test(const std::string& server_path) {
    const auto port = find_free_port();
    const auto aof_path = temp_aof_path("idle_timeout");
    std::filesystem::remove(aof_path);
    ServerProcess server(server_path, port, aof_path, 1, 150, 4);

    auto idle = connect_to(port);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    Client after_idle(port);
    assert(after_idle.command({"PING"}) == "+PONG\r\n");

    auto slow = connect_to(port);
    send_all(slow.get(), "*2\r\n$4\r\nECHO\r\n$100\r\npartial");
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    Client after_slow(port);
    assert(after_slow.command({"PING"}) == "+PONG\r\n");

    server.stop();
    std::filesystem::remove(aof_path);
}

void connection_queue_exhaustion_test(const std::string& server_path) {
    const auto port = find_free_port();
    const auto aof_path = temp_aof_path("queue_exhaustion");
    std::filesystem::remove(aof_path);
    ServerProcess server(server_path, port, aof_path, 1, 200, 2);

    std::vector<Fd> clients;
    for (int i = 0; i < 12; ++i) {
        clients.emplace_back(connect_to(port));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::size_t rejected = 0;
    char byte = 0;
    for (auto& client : clients) {
        const auto result = ::recv(client.get(), &byte, 1, MSG_DONTWAIT);
        if (result == 0) {
            ++rejected;
        }
    }
    assert(rejected > 0);
    clients.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    Client recovered(port);
    assert(recovered.command({"PING"}) == "+PONG\r\n");
    server.stop();
    std::filesystem::remove(aof_path);
}

void run_server_integration_tests(const std::string& server_path) {
    const auto port = find_free_port();
    const auto aof_path = temp_aof_path("integration");
    std::filesystem::remove(aof_path);

    ServerProcess server(server_path, port, aof_path);
    pipeline_test(port);
    split_resp_test(port);
    error_input_test(port);
    hostile_resp_test(port);
    concurrent_clients_test(port);
    ttl_boundary_test(port);
    server.stop();

    std::filesystem::remove(aof_path);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: miniredis_integration_tests <server_path>\n";
        return 1;
    }

    run_server_integration_tests(argv[1]);
    aof_replay_test(argv[1]);
    aof_ttl_deadline_replay_test(argv[1]);
    concurrent_aof_order_test(argv[1]);
    idle_and_slow_client_timeout_test(argv[1]);
    connection_queue_exhaustion_test(argv[1]);

    std::cout << "Integration tests passed\n";
    return 0;
}
