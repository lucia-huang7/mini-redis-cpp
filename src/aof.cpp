#include "miniredis/aof.hpp"

#include "miniredis/command.hpp"
#include "miniredis/resp.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace miniredis {

Aof::Aof(std::filesystem::path path) : path_(std::move(path)) {}

void Aof::append(const std::string& encoded_command) const {
    if (path_.has_parent_path()) {
        std::filesystem::create_directories(path_.parent_path());
    }

    std::ofstream file(path_, std::ios::binary | std::ios::app);
    if (!file) {
        throw std::runtime_error("failed to open AOF file");
    }

    file.write(encoded_command.data(), static_cast<std::streamsize>(encoded_command.size()));
    if (!file) {
        throw std::runtime_error("failed to write AOF file");
    }
}

std::size_t Aof::replay(CommandDispatcher& dispatcher) const {
    if (!std::filesystem::exists(path_)) {
        return 0;
    }

    std::ifstream file(path_, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open AOF file");
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    const auto content = stream.str();

    std::size_t offset = 0;
    std::size_t count = 0;
    while (offset < content.size()) {
        const auto result = resp::parse_array_prefix(content.substr(offset));
        if (!result.has_value()) {
            throw std::runtime_error("invalid AOF file");
        }

        dispatcher.execute(result->values);
        offset += result->bytes_consumed;
        ++count;
    }

    return count;
}

}  // namespace miniredis
