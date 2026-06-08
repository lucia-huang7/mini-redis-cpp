#pragma once

#include <cstdint>
#include <filesystem>

namespace miniredis {

struct Config {
    std::uint16_t port = 6379;
    std::filesystem::path aof_path = "data/appendonly.aof";
};

Config parse_config(int argc, char** argv);

}  // namespace miniredis
