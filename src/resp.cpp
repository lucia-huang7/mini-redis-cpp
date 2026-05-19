#include "miniredis/resp.hpp"

namespace miniredis::resp {

std::optional<std::vector<std::string>> parse_array(const std::string& input) {
    (void)input;
    return std::nullopt;
}

std::string simple_string(const std::string& value) {
    return "+" + value + "\r\n";
}

std::string error(const std::string& message) {
    return "-" + message + "\r\n";
}

std::string integer(long long value) {
    return ":" + std::to_string(value) + "\r\n";
}

std::string bulk_string(const std::string& value) {
    return "$" + std::to_string(value.size()) + "\r\n" + value + "\r\n";
}

std::string null_bulk_string() {
    return "$-1\r\n";
}

}  // namespace miniredis::resp
