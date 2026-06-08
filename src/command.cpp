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

std::optional<Store::SetOptions> parse_set_options(const std::vector<std::string>& command) {
    Store::SetOptions options;

    for (std::size_t i = 3; i < command.size(); ++i) {
        const auto option = normalize_command(command[i]);
        if (option == "NX") {
            if (options.condition != Store::SetCondition::Always) {
                return std::nullopt;
            }
            options.condition = Store::SetCondition::IfNotExists;
            continue;
        }
        if (option == "XX") {
            if (options.condition != Store::SetCondition::Always) {
                return std::nullopt;
            }
            options.condition = Store::SetCondition::IfExists;
            continue;
        }
        if (option == "EX" || option == "PX") {
            if (options.ttl.has_value() || i + 1 >= command.size()) {
                return std::nullopt;
            }

            const auto ttl = parse_integer(command[++i]);
            if (!ttl.has_value() || *ttl <= 0) {
                return std::nullopt;
            }

            if (option == "EX") {
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
        if (command.size() < 3) {
            return wrong_arity("set");
        }

        const auto options = parse_set_options(command);
        if (!options.has_value()) {
            return resp::error("ERR syntax error");
        }

        if (!store_.set(command[1], command[2], *options)) {
            return resp::null_bulk_string();
        }
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

    if (name == "MGET") {
        if (command.size() < 2) {
            return wrong_arity("mget");
        }
        std::vector<std::string> keys(command.begin() + 1, command.end());
        return resp::bulk_array(store_.mget(keys));
    }

    if (name == "EXISTS") {
        if (command.size() < 2) {
            return wrong_arity("exists");
        }
        std::vector<std::string> keys(command.begin() + 1, command.end());
        return resp::integer(static_cast<long long>(store_.exists(keys)));
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

    if (name == "INCR" || name == "DECR") {
        if (command.size() != 2) {
            return wrong_arity(name == "INCR" ? "incr" : "decr");
        }

        try {
            const auto delta = name == "INCR" ? 1 : -1;
            return resp::integer(store_.increment(command[1], delta));
        } catch (const std::invalid_argument&) {
            return resp::error("ERR value is not an integer or out of range");
        } catch (const std::out_of_range&) {
            return resp::error("ERR increment or decrement would overflow");
        }
    }

    return resp::error("ERR unknown command");
}

}  // namespace miniredis
