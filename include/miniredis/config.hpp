#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "miniredis/aof.hpp"

namespace miniredis {

struct Config {
    std::uint16_t port = 6379;
    std::filesystem::path aof_path = "data/appendonly.aof";
    AofSyncPolicy appendfsync = AofSyncPolicy::EverySec;
    std::size_t workers = 0;
};

Config parse_config(int argc, char** argv);

}  // namespace miniredis
