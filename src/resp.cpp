#include "miniredis/resp.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace miniredis::resp {
namespace {

std::optional<std::string_view> read_line(std::string_view input, std::size_t& pos) {
    const auto end = input.find("\r\n", pos);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }

    const auto line = input.substr(pos, end - pos);
    pos = end + 2;
    return line;
}

std::optional<long long> parse_number(std::string_view line) {
    if (line.empty()) {
        return std::nullopt;
    }

    long long value = 0;
    std::size_t pos = 0;
    const bool negative = line[0] == '-';
    if (negative) {
        if (line.size() == 1) {
            return std::nullopt;
        }
        pos = 1;
    }

    for (; pos < line.size(); ++pos) {
        const char c = line[pos];
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = value * 10 + (c - '0');
    }

    return negative ? -value : value;
}

}  // namespace

std::optional<std::vector<std::string>> parse_array(const std::string& input) {
    std::string_view view(input);
    std::size_t pos = 0;

    if (view.empty() || view[pos] != '*') {
        return std::nullopt;
    }

    ++pos;
    const auto array_len_line = read_line(view, pos);
    if (!array_len_line.has_value()) {
        return std::nullopt;
    }

    const auto array_len = parse_number(*array_len_line);
    if (!array_len.has_value() || *array_len < 0) {
        return std::nullopt;
    }

    std::vector<std::string> values;
    values.reserve(static_cast<std::size_t>(*array_len));

    for (long long i = 0; i < *array_len; ++i) {
        if (pos >= view.size() || view[pos] != '$') {
            return std::nullopt;
        }

        ++pos;
        const auto bulk_len_line = read_line(view, pos);
        if (!bulk_len_line.has_value()) {
            return std::nullopt;
        }

        const auto bulk_len = parse_number(*bulk_len_line);
        if (!bulk_len.has_value() || *bulk_len < 0) {
            return std::nullopt;
        }

        const auto len = static_cast<std::size_t>(*bulk_len);
        if (pos + len + 2 > view.size()) {
            return std::nullopt;
        }

        values.emplace_back(view.substr(pos, len));
        pos += len;

        if (view.substr(pos, 2) != "\r\n") {
            return std::nullopt;
        }
        pos += 2;
    }

    if (pos != view.size()) {
        return std::nullopt;
    }

    return values;
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
