#include "miniredis/durable_executor.hpp"

#include "miniredis/aof.hpp"
#include "miniredis/command.hpp"
#include "miniredis/resp.hpp"

#include <cctype>
#include <charconv>
#include <chrono>
#include <exception>
#include <limits>
#include <optional>

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

bool is_write_command(const std::vector<std::string_view>& command) {
    if (command.empty()) {
        return false;
    }
    const auto name = command.front();
    return equals_ci(name, "SET") || equals_ci(name, "DEL") ||
           equals_ci(name, "EXPIRE") || equals_ci(name, "EXPIREAT") ||
           equals_ci(name, "PEXPIREAT") || equals_ci(name, "INCR") ||
           equals_ci(name, "DECR");
}

bool changed_state(const std::vector<std::string_view>& command, const std::string& response) {
    if (response.empty() || response.front() == '-') {
        return false;
    }
    const auto name = command.front();
    if (equals_ci(name, "SET")) {
        return response == "+OK\r\n";
    }
    if (equals_ci(name, "INCR") || equals_ci(name, "DECR")) {
        return response.front() == ':';
    }
    if (equals_ci(name, "DEL") || equals_ci(name, "EXPIRE") ||
        equals_ci(name, "EXPIREAT") || equals_ci(name, "PEXPIREAT")) {
        return response == resp::integer(1);
    }
    return false;
}

std::vector<std::string> copy_command(const std::vector<std::string_view>& command) {
    std::vector<std::string> copied;
    copied.reserve(command.size());
    for (const auto value : command) {
        copied.emplace_back(value);
    }
    return copied;
}

std::optional<long long> parse_integer(std::string_view value) {
    long long parsed_value = 0;
    const auto parsed = std::from_chars(
        value.data(), value.data() + value.size(), parsed_value);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed_value;
}

std::optional<long long> checked_milliseconds(long long value, long long multiplier) {
    if (value > std::numeric_limits<long long>::max() / multiplier ||
        value < std::numeric_limits<long long>::min() / multiplier) {
        return std::nullopt;
    }
    return value * multiplier;
}

std::optional<long long> absolute_deadline_ms(long long ttl, long long multiplier) {
    const auto duration_ms = checked_milliseconds(ttl, multiplier);
    if (!duration_ms.has_value()) {
        return std::nullopt;
    }
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (*duration_ms > std::numeric_limits<long long>::max() - now_ms) {
        return std::nullopt;
    }
    return now_ms + *duration_ms;
}

std::vector<std::string> normalize_expiration(const std::vector<std::string_view>& command) {
    auto normalized = copy_command(command);
    if (command.empty()) {
        return normalized;
    }

    if (equals_ci(command.front(), "EXPIRE") && command.size() == 3) {
        const auto seconds = parse_integer(command[2]);
        if (seconds.has_value() && *seconds >= 0) {
            const auto deadline = absolute_deadline_ms(*seconds, 1000);
            if (deadline.has_value()) {
                normalized[0] = "PEXPIREAT";
                normalized[2] = std::to_string(*deadline);
            }
        }
        return normalized;
    }

    if (!equals_ci(command.front(), "SET") || command.size() < 3) {
        return normalized;
    }
    for (std::size_t i = 3; i + 1 < command.size(); ++i) {
        const auto option = command[i];
        if (!equals_ci(option, "EX") && !equals_ci(option, "PX")) {
            continue;
        }
        const auto ttl = parse_integer(command[i + 1]);
        if (!ttl.has_value() || *ttl <= 0) {
            return normalized;
        }
        const auto deadline = absolute_deadline_ms(*ttl, equals_ci(option, "EX") ? 1000 : 1);
        if (!deadline.has_value()) {
            return normalized;
        }
        normalized[i] = "PXAT";
        normalized[i + 1] = std::to_string(*deadline);
        return normalized;
    }
    return normalized;
}

std::vector<std::string_view> command_views(const std::vector<std::string>& command) {
    std::vector<std::string_view> views;
    views.reserve(command.size());
    for (const auto& value : command) {
        views.emplace_back(value);
    }
    return views;
}

}  // namespace

DurableCommandExecutor::DurableCommandExecutor(CommandDispatcher& dispatcher, const Aof* aof)
    : dispatcher_(dispatcher), aof_(aof) {}

std::string DurableCommandExecutor::execute(const std::vector<std::string_view>& command) {
    if (!is_write_command(command)) {
        return dispatcher_.execute_view(command);
    }

    // The lock gives Store mutations and their AOF records one commit order.
    std::lock_guard lock(write_mutex_);
    if (write_failed_) {
        return resp::error("MISCONF AOF write failed; writes are disabled until restart");
    }

    if (aof_ == nullptr) {
        return dispatcher_.execute_view(command);
    }

    const auto durable_command = normalize_expiration(command);
    const auto durable_views = command_views(durable_command);
    const auto response = dispatcher_.execute_view(durable_views);
    if (!changed_state(durable_views, response)) {
        return response;
    }

    try {
        aof_->append(resp::array(durable_command));
    } catch (const std::exception&) {
        // The in-memory mutation may already have happened. The failed command is
        // not acknowledged, and later writes remain disabled until restart.
        write_failed_ = true;
        return resp::error("MISCONF AOF write failed; command was not acknowledged and writes are disabled");
    }
    return response;
}

bool DurableCommandExecutor::write_failed() const {
    std::lock_guard lock(write_mutex_);
    return write_failed_;
}

}  // namespace miniredis
