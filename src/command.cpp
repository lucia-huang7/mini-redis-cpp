#include "miniredis/command.hpp"

#include "miniredis/resp.hpp"
#include "miniredis/store.hpp"

#include <algorithm>
#include <cctype>

namespace miniredis {

CommandDispatcher::CommandDispatcher(Store& store) : store_(store) {}

std::string CommandDispatcher::execute(const std::vector<std::string>& command) {
    if (command.empty()) {
        return resp::error("ERR empty command");
    }

    auto name = command.front();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });

    if (name == "PING") {
        return resp::simple_string("PONG");
    }

    return resp::error("ERR unknown command");
}

}  // namespace miniredis
