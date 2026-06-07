#include "miniredis/command.hpp"

#include "miniredis/resp.hpp"
#include "miniredis/store.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace miniredis {
namespace {

bool equals_ci(std::string_view value, std::string_view expected) {
    if (value.size() != expected.size()) {
        return false;
    }

    for (std::size_t i = 0; i < value.size(); ++i) {
        if (static_cast<char>(std::toupper(static_cast<unsigned char>(value[i]))) != expected[i]) {
            return false;
        }
    }
    return true;
}

std::string wrong_arity(const std::string& command) {
    return resp::error("ERR wrong number of arguments for '" + command + "' command");
}

std::optional<long long> parse_integer(std::string_view value) {
    long long result = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto parsed = std::from_chars(begin, end, result);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return std::nullopt;
    }
    return result;
}

std::optional<Store::SetOptions> parse_set_options(const std::vector<std::string_view>& command) {
    Store::SetOptions options;

    for (std::size_t i = 3; i < command.size(); ++i) {
        const auto option = std::string_view(command[i]);
        if (equals_ci(option, "NX")) {
            if (options.condition != Store::SetCondition::Always) {
                return std::nullopt;
            }
            options.condition = Store::SetCondition::IfNotExists;
            continue;
        }
        if (equals_ci(option, "XX")) {
            if (options.condition != Store::SetCondition::Always) {
                return std::nullopt;
            }
            options.condition = Store::SetCondition::IfExists;
            continue;
        }
        if (equals_ci(option, "EX") || equals_ci(option, "PX")) {
            if (options.ttl.has_value() || i + 1 >= command.size()) {
                return std::nullopt;
            }

            const auto ttl = parse_integer(command[++i]);
            if (!ttl.has_value() || *ttl <= 0) {
                return std::nullopt;
            }

            if (equals_ci(option, "EX")) {
                options.ttl = std::chrono::seconds(*ttl);
            } else {
                options.ttl = std::chrono::milliseconds(*ttl);
            }
            continue;
        }

        return std::nullopt;
    }

    return options;
}

}  // namespace

CommandDispatcher::CommandDispatcher(Store& store) : store_(store) {}

std::string CommandDispatcher::execute(const std::vector<std::string>& command) {
    std::vector<std::string_view> view_command;
    view_command.reserve(command.size());
    for (const auto& value : command) {
        view_command.emplace_back(value);
    }
    return execute(view_command);
}

std::string CommandDispatcher::execute(const std::vector<std::string_view>& command) {
    if (command.empty()) {
        return resp::error("ERR empty command");
    }

    const auto name = command.front();

    if (equals_ci(name, "PING")) {
        if (command.size() > 2) {
            return wrong_arity("ping");
        }
        if (command.size() == 2) {
            return "$" + std::to_string(command[1].size()) + "\r\n" + std::string(command[1]) + "\r\n";
        }
        return "+PONG\r\n";
    }

    if (equals_ci(name, "ECHO")) {
        if (command.size() != 2) {
            return wrong_arity("echo");
        }
        return "$" + std::to_string(command[1].size()) + "\r\n" + std::string(command[1]) + "\r\n";
    }

    if (equals_ci(name, "SET")) {
        if (command.size() < 3) {
            return wrong_arity("set");
        }

        const auto options = parse_set_options(command);
        if (!options.has_value()) {
            return resp::error("ERR syntax error");
        }

        if (!store_.set(std::string(command[1]), std::string(command[2]), *options)) {
            return resp::null_bulk_string();
        }
        return "+OK\r\n";
    }

    if (equals_ci(name, "GET")) {
        if (command.size() != 2) {
            return wrong_arity("get");
        }
        const auto value = store_.get(std::string(command[1]));
        if (!value.has_value()) {
            return resp::null_bulk_string();
        }
        return resp::bulk_string(*value);
    }

    if (equals_ci(name, "MGET")) {
        if (command.size() < 2) {
            return wrong_arity("mget");
        }
        std::vector<std::string> keys(command.begin() + 1, command.end());
        return resp::bulk_array(store_.mget(keys));
    }

    if (equals_ci(name, "EXISTS")) {
        if (command.size() < 2) {
            return wrong_arity("exists");
        }
        std::vector<std::string> keys(command.begin() + 1, command.end());
        return resp::integer(static_cast<long long>(store_.exists(keys)));
    }

    if (equals_ci(name, "DEL")) {
        if (command.size() != 2) {
            return wrong_arity("del");
        }
        return resp::integer(store_.del(std::string(command[1])) ? 1 : 0);
    }

    if (equals_ci(name, "EXPIRE")) {
        if (command.size() != 3) {
            return wrong_arity("expire");
        }

        const auto seconds = parse_integer(command[2]);
        if (!seconds.has_value() || *seconds < 0) {
            return resp::error("ERR value is not an integer or out of range");
        }

        const auto updated = store_.expire(std::string(command[1]), std::chrono::seconds(*seconds));
        return resp::integer(updated ? 1 : 0);
    }

    if (equals_ci(name, "TTL")) {
        if (command.size() != 2) {
            return wrong_arity("ttl");
        }

        const auto remaining = store_.ttl(std::string(command[1]));
        if (!remaining.has_value()) {
            return resp::integer(-2);
        }
        return resp::integer(*remaining);
    }

    if (equals_ci(name, "INCR") || equals_ci(name, "DECR")) {
        if (command.size() != 2) {
            return wrong_arity(equals_ci(name, "INCR") ? "incr" : "decr");
        }

        try {
            const auto delta = equals_ci(name, "INCR") ? 1 : -1;
            return resp::integer(store_.increment(std::string(command[1]), delta));
        } catch (const std::invalid_argument&) {
            return resp::error("ERR value is not an integer or out of range");
        } catch (const std::out_of_range&) {
            return resp::error("ERR increment or decrement would overflow");
        }
    }

    return resp::error("ERR unknown command");
}

}  // namespace miniredis
