#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace miniredis {

class Store;

class CommandDispatcher {
public:
    explicit CommandDispatcher(Store& store);

    std::string execute(const std::vector<std::string>& command);
    std::string execute_view(const std::vector<std::string_view>& command);

private:
    Store& store_;
};

}  // namespace miniredis
