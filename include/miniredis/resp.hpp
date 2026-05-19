#pragma once

#include <optional>
#include <string>
#include <vector>

namespace miniredis::resp {

std::optional<std::vector<std::string>> parse_array(const std::string& input);

std::string simple_string(const std::string& value);
std::string error(const std::string& message);
std::string integer(long long value);
std::string bulk_string(const std::string& value);
std::string null_bulk_string();

}  // namespace miniredis::resp
