#include "miniredis/resp.hpp"

#include <cassert>

void resp_encoder_test() {
    assert(miniredis::resp::simple_string("OK") == "+OK\r\n");
    assert(miniredis::resp::array({"PING"}) == "*1\r\n$4\r\nPING\r\n");
    assert(miniredis::resp::array({"SET", "name", "alice"}) == "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$5\r\nalice\r\n");
    assert(miniredis::resp::bulk_string("hello") == "$5\r\nhello\r\n");
    assert(miniredis::resp::null_bulk_string() == "$-1\r\n");

    const auto ping = miniredis::resp::parse_array("*1\r\n$4\r\nPING\r\n");
    assert(ping.has_value());
    assert(*ping == std::vector<std::string>{"PING"});

    const auto set = miniredis::resp::parse_array("*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$5\r\nalice\r\n");
    assert(set.has_value());
    assert(*set == (std::vector<std::string>{"SET", "name", "alice"}));

    const auto empty_bulk = miniredis::resp::parse_array("*2\r\n$4\r\nECHO\r\n$0\r\n\r\n");
    assert(empty_bulk.has_value());
    assert(*empty_bulk == (std::vector<std::string>{"ECHO", ""}));

    assert(!miniredis::resp::parse_array("").has_value());
    assert(!miniredis::resp::parse_array("+PING\r\n").has_value());
    assert(!miniredis::resp::parse_array("*1\r\n$4\r\nPIN\r\n").has_value());
    assert(!miniredis::resp::parse_array("*1\r\n$A\r\nPING\r\n").has_value());
}
