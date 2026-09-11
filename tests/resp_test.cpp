#include "miniredis/resp.hpp"

#include <cassert>
#include <optional>

void resp_encoder_test() {
    assert(miniredis::resp::simple_string("OK") == "+OK\r\n");
    assert(miniredis::resp::array({"PING"}) == "*1\r\n$4\r\nPING\r\n");
    assert(miniredis::resp::array({"SET", "name", "alice"}) == "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$5\r\nalice\r\n");
    assert(miniredis::resp::bulk_array(std::vector<std::optional<std::string>>{"alice", std::nullopt}) ==
           "*2\r\n$5\r\nalice\r\n$-1\r\n");
    assert(miniredis::resp::bulk_string("hello") == "$5\r\nhello\r\n");
    assert(miniredis::resp::null_bulk_string() == "$-1\r\n");

    const auto ping = miniredis::resp::parse_array("*1\r\n$4\r\nPING\r\n");
    assert(ping.has_value());
    assert(*ping == std::vector<std::string>{"PING"});

    const auto set = miniredis::resp::parse_array("*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$5\r\nalice\r\n");
    assert(set.has_value());
    assert(*set == (std::vector<std::string>{"SET", "name", "alice"}));

    const std::string pipelined = "*1\r\n$4\r\nPING\r\n*1\r\n$4\r\nPING\r\n";
    const auto first = miniredis::resp::parse_array_prefix(pipelined);
    assert(first.has_value());
    assert(first->values == std::vector<std::string>{"PING"});
    assert(first->bytes_consumed == std::string("*1\r\n$4\r\nPING\r\n").size());

    const auto empty_bulk = miniredis::resp::parse_array("*2\r\n$4\r\nECHO\r\n$0\r\n\r\n");
    assert(empty_bulk.has_value());
    assert(*empty_bulk == (std::vector<std::string>{"ECHO", ""}));

    assert(!miniredis::resp::parse_array("").has_value());
    assert(!miniredis::resp::parse_array("+PING\r\n").has_value());
    assert(!miniredis::resp::parse_array("*1\r\n$4\r\nPIN\r\n").has_value());
    assert(!miniredis::resp::parse_array("*1\r\n$A\r\nPING\r\n").has_value());

    using miniredis::resp::ParseStatus;
    assert(miniredis::resp::parse_array_prefix_view_checked(
               "*92233720368547758070\r\n").status == ParseStatus::Invalid);
    assert(miniredis::resp::parse_array_prefix_view_checked(
               "$not-an-array\r\n").status == ParseStatus::Invalid);
    assert(miniredis::resp::parse_array_prefix_view_checked(
               "*1025\r\n").status == ParseStatus::TooLarge);
    assert(miniredis::resp::parse_array_prefix_view_checked(
               "*1\r\n$92233720368547758070\r\n").status == ParseStatus::Invalid);
    assert(miniredis::resp::parse_array_prefix_view_checked(
               "*1\r\n$524289\r\n").status == ParseStatus::TooLarge);
    assert(miniredis::resp::parse_array_prefix_view_checked(
               "*1\r\n$4\r\nPI").status == ParseStatus::Incomplete);

    const std::string valid = "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n";
    for (std::size_t length = 0; length < valid.size(); ++length) {
        assert(miniredis::resp::parse_array_prefix_view_checked(
                   std::string_view(valid).substr(0, length)).status == ParseStatus::Incomplete);
    }
}
