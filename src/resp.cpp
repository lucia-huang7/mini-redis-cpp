#include "miniredis/resp.hpp"

#include <cstddef>
#include <charconv>
#include <limits>
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
    const auto parsed = std::from_chars(line.data(), line.data() + line.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != line.data() + line.size()) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

std::optional<std::vector<std::string>> parse_array(const std::string& input) {
    const auto result = parse_array_prefix(input);
    if (!result.has_value() || result->bytes_consumed != input.size()) {
        return std::nullopt;
    }
    return result->values;
}

std::optional<ParseResult> parse_array_prefix(const std::string& input) {
    return parse_array_prefix(std::string_view(input));
}

std::optional<ParseResult> parse_array_prefix(std::string_view input) {
    const auto result = parse_array_prefix_view(input);
    if (!result.has_value()) {
        return std::nullopt;
    }

    std::vector<std::string> values;
    values.reserve(result->values.size());
    for (const auto value : result->values) {
        values.emplace_back(value);
    }
    return ParseResult{std::move(values), result->bytes_consumed};
}

std::optional<ParseViewResult> parse_array_prefix_view(std::string_view input) {
    auto checked = parse_array_prefix_view_checked(input);
    if (checked.status != ParseStatus::Complete) {
        return std::nullopt;
    }
    return std::move(checked.result);
}

CheckedParseViewResult parse_array_prefix_view_checked(std::string_view input) {
    std::string_view view(input);
    std::size_t pos = 0;

    if (view.empty()) {
        return {ParseStatus::Incomplete, {}};
    }
    if (view[pos] != '*') {
        return {ParseStatus::Invalid, {}};
    }

    ++pos;
    const auto array_len_line = read_line(view, pos);
    if (!array_len_line.has_value()) {
        return {view.size() > 32 ? ParseStatus::Invalid : ParseStatus::Incomplete, {}};
    }

    const auto array_len = parse_number(*array_len_line);
    if (!array_len.has_value() || *array_len < 0) {
        return {ParseStatus::Invalid, {}};
    }
    if (static_cast<unsigned long long>(*array_len) > kMaxArrayElements) {
        return {ParseStatus::TooLarge, {}};
    }

    std::vector<std::string_view> values;
    values.reserve(static_cast<std::size_t>(*array_len));

    for (long long i = 0; i < *array_len; ++i) {
        if (pos >= view.size() || view[pos] != '$') {
            return {pos >= view.size() ? ParseStatus::Incomplete : ParseStatus::Invalid, {}};
        }

        ++pos;
        const auto bulk_len_line = read_line(view, pos);
        if (!bulk_len_line.has_value()) {
            return {view.size() - pos > 32 ? ParseStatus::Invalid : ParseStatus::Incomplete, {}};
        }

        const auto bulk_len = parse_number(*bulk_len_line);
        if (!bulk_len.has_value() || *bulk_len < 0) {
            return {ParseStatus::Invalid, {}};
        }
        if (static_cast<unsigned long long>(*bulk_len) > kMaxBulkStringBytes) {
            return {ParseStatus::TooLarge, {}};
        }

        const auto len = static_cast<std::size_t>(*bulk_len);
        if (len > view.size() - pos || view.size() - pos - len < 2) {
            return {ParseStatus::Incomplete, {}};
        }

        values.emplace_back(view.substr(pos, len));
        pos += len;

        if (view.substr(pos, 2) != "\r\n") {
            return {ParseStatus::Invalid, {}};
        }
        pos += 2;
    }

    if (pos > kMaxRequestBytes) {
        return {ParseStatus::TooLarge, {}};
    }
    return {ParseStatus::Complete, ParseViewResult{std::move(values), pos}};
}

std::string array(const std::vector<std::string>& values) {
    std::string output = "*" + std::to_string(values.size()) + "\r\n";
    for (const auto& value : values) {
        output += bulk_string(value);
    }
    return output;
}

std::string bulk_array(const std::vector<std::optional<std::string>>& values) {
    std::string output = "*" + std::to_string(values.size()) + "\r\n";
    for (const auto& value : values) {
        output += value.has_value() ? bulk_string(*value) : null_bulk_string();
    }
    return output;
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
