#include "miniredis/config.hpp"

#include <cstdlib>
#include <algorithm>
#include <string>
#include <thread>

namespace miniredis {

Config parse_config(int argc, char** argv) {
    Config config;
    config.workers = std::max(1u, std::thread::hardware_concurrency());

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            config.port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--aof" && i + 1 < argc) {
            config.aof_path = argv[++i];
        } else if (arg == "--workers" && i + 1 < argc) {
            config.workers = std::max<std::size_t>(1, static_cast<std::size_t>(std::stoull(argv[++i])));
        }
    }

    return config;
}

}  // namespace miniredis
