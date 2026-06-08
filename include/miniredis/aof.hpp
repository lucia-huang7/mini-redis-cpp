#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace miniredis {

class CommandDispatcher;

class Aof {
public:
    explicit Aof(std::filesystem::path path);

    void append(const std::string& encoded_command) const;
    std::size_t replay(CommandDispatcher& dispatcher) const;

private:
    std::filesystem::path path_;
};

}  // namespace miniredis
