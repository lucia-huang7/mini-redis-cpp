#pragma once

#include <string>
#include <vector>

namespace miniredis {

class Store;

class CommandDispatcher {
public:
    explicit CommandDispatcher(Store& store);

    std::string execute(const std::vector<std::string>& command);

private:
    Store& store_;
};

}  // namespace miniredis
