#include "miniredis/resp.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto input = std::string_view(reinterpret_cast<const char*>(data), size);
    const auto parsed = miniredis::resp::parse_array_prefix_view_checked(input);
    if (parsed.status == miniredis::resp::ParseStatus::Complete) {
        if (parsed.result.bytes_consumed > input.size() ||
            parsed.result.values.size() > miniredis::resp::kMaxArrayElements) {
            __builtin_trap();
        }
        for (const auto value : parsed.result.values) {
            if (value.size() > miniredis::resp::kMaxBulkStringBytes) {
                __builtin_trap();
            }
        }
    }
    return 0;
}
