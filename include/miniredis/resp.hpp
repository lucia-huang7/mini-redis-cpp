#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace miniredis::resp {

struct ParseResult {
    std::vector<std::string> values;
    std::size_t bytes_consumed = 0;
};

std::optional<std::vector<std::string>> parse_array(const std::string& input);
std::optional<ParseResult> parse_array_prefix(const std::string& input);
std::optional<ParseResult> parse_array_prefix(std::string_view input);

std::string array(const std::vector<std::string>& values);
std::string bulk_array(const std::vector<std::optional<std::string>>& values);
std::string simple_string(const std::string& value);
std::string error(const std::string& message);
std::string integer(long long value);
std::string bulk_string(const std::string& value);
std::string null_bulk_string();

}  // namespace miniredis::resp
