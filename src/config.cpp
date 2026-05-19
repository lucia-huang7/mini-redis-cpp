#include "miniredis/config.hpp"

#include <cstdlib>
#include <string>

namespace miniredis {

Config parse_config(int argc, char** argv) {
    Config config;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            config.port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--aof" && i + 1 < argc) {
            config.aof_path = argv[++i];
        }
    }

    return config;
}

}  // namespace miniredis
