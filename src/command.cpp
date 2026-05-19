#include "miniredis/command.hpp"

#include "miniredis/resp.hpp"
#include "miniredis/store.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <optional>
#include <stdexcept>

namespace miniredis {
namespace {

std::string normalize_command(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return name;
}

std::string wrong_arity(const std::string& command) {
    return resp::error("ERR wrong number of arguments for '" + command + "' command");
}

std::optional<long long> parse_integer(const std::string& value) {
    try {
        std::size_t parsed = 0;
        const auto result = std::stoll(value, &parsed);
        if (parsed != value.size()) {
            return std::nullopt;
        }
        return result;
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    } catch (const std::out_of_range&) {
        return std::nullopt;
    }
}

}  // namespace

CommandDispatcher::CommandDispatcher(Store& store) : store_(store) {}

std::string CommandDispatcher::execute(const std::vector<std::string>& command) {
    if (command.empty()) {
        return resp::error("ERR empty command");
    }

    const auto name = normalize_command(command.front());

    if (name == "PING") {
        if (command.size() > 2) {
            return wrong_arity("ping");
        }
        if (command.size() == 2) {
            return resp::bulk_string(command[1]);
        }
        return resp::simple_string("PONG");
    }

    if (name == "ECHO") {
        if (command.size() != 2) {
            return wrong_arity("echo");
        }
        return resp::bulk_string(command[1]);
    }

    if (name == "SET") {
        if (command.size() != 3) {
            return wrong_arity("set");
        }
        store_.set(command[1], command[2]);
        return resp::simple_string("OK");
    }

    if (name == "GET") {
        if (command.size() != 2) {
            return wrong_arity("get");
        }
        const auto value = store_.get(command[1]);
        if (!value.has_value()) {
            return resp::null_bulk_string();
        }
        return resp::bulk_string(*value);
    }

    if (name == "DEL") {
        if (command.size() != 2) {
            return wrong_arity("del");
        }
        return resp::integer(store_.del(command[1]) ? 1 : 0);
    }

    if (name == "EXPIRE") {
        if (command.size() != 3) {
            return wrong_arity("expire");
        }

        const auto seconds = parse_integer(command[2]);
        if (!seconds.has_value() || *seconds < 0) {
            return resp::error("ERR value is not an integer or out of range");
        }

        const auto updated = store_.expire(command[1], std::chrono::seconds(*seconds));
        return resp::integer(updated ? 1 : 0);
    }

    if (name == "TTL") {
        if (command.size() != 2) {
            return wrong_arity("ttl");
        }

        const auto remaining = store_.ttl(command[1]);
        if (!remaining.has_value()) {
            return resp::integer(-2);
        }
        return resp::integer(*remaining);
    }

    return resp::error("ERR unknown command");
}

}  // namespace miniredis
