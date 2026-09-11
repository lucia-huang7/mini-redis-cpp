#include "miniredis/config.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>
#include <charconv>
#include <limits>

namespace miniredis {
namespace {

AofSyncPolicy parse_appendfsync(const std::string& value) {
    if (value == "always") {
        return AofSyncPolicy::Always;
    }
    if (value == "everysec") {
        return AofSyncPolicy::EverySec;
    }
    if (value == "no") {
        return AofSyncPolicy::No;
    }

    throw std::runtime_error("appendfsync must be one of: always, everysec, no");
}

std::size_t parse_positive_size(const std::string& value, const char* option) {
    unsigned long long parsed_value = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), parsed_value);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        parsed_value == 0 || parsed_value > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error(std::string(option) + " must be a positive integer");
    }
    return static_cast<std::size_t>(parsed_value);
}

}  // namespace

Config parse_config(int argc, char** argv) {
    Config config;
    config.workers = std::max(1u, std::thread::hardware_concurrency());

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            config.port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--aof" && i + 1 < argc) {
            const std::string path = argv[++i];
            config.aof_path = path.empty()
                ? std::optional<std::filesystem::path>{}
                : std::optional<std::filesystem::path>{path};
        } else if (arg == "--appendfsync" && i + 1 < argc) {
            config.appendfsync = parse_appendfsync(argv[++i]);
        } else if (arg == "--workers" && i + 1 < argc) {
            config.workers = parse_positive_size(argv[++i], "--workers");
        } else if (arg == "--max-queued-clients" && i + 1 < argc) {
            config.max_queued_clients = parse_positive_size(argv[++i], "--max-queued-clients");
        } else if (arg == "--idle-timeout-ms" && i + 1 < argc) {
            config.idle_timeout_ms = parse_positive_size(argv[++i], "--idle-timeout-ms");
        }
    }

    return config;
}

}  // namespace miniredis
