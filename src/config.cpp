#include "miniredis/config.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>

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
            config.workers = std::max<std::size_t>(1, static_cast<std::size_t>(std::stoull(argv[++i])));
        }
    }

    return config;
}

}  // namespace miniredis
