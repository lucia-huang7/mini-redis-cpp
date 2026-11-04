#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

#include "miniredis/aof.hpp"
#include "miniredis/durable_executor.hpp"

namespace miniredis {

struct Config {
    std::uint16_t port = 6379;
    std::optional<std::filesystem::path> aof_path = std::filesystem::path("data/appendonly.aof");
    AofSyncPolicy appendfsync = AofSyncPolicy::EverySec;
    // Number of event-loop threads. Each one multiplexes many connections.
    std::size_t workers = 0;
    // Connections beyond this limit are accepted and closed immediately.
    std::size_t max_clients = 10000;
    std::size_t idle_timeout_ms = 30000;
    AofRewritePolicy aof_rewrite;
};

Config parse_config(int argc, char** argv);

}  // namespace miniredis
