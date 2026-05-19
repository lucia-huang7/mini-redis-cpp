#include "miniredis/aof.hpp"

namespace miniredis {

Aof::Aof(std::filesystem::path path) : path_(std::move(path)) {}

void Aof::append(const std::string& encoded_command) {
    (void)encoded_command;
}

void Aof::replay() {}

}  // namespace miniredis
