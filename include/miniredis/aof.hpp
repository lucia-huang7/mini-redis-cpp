#pragma once

#include <filesystem>
#include <string>

namespace miniredis {

class Aof {
public:
    explicit Aof(std::filesystem::path path);

    void append(const std::string& encoded_command);
    void replay();

private:
    std::filesystem::path path_;
};

}  // namespace miniredis
