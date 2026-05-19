#include "miniredis/resp.hpp"

#include <cassert>

void resp_encoder_test() {
    assert(miniredis::resp::simple_string("OK") == "+OK\r\n");
    assert(miniredis::resp::bulk_string("hello") == "$5\r\nhello\r\n");
    assert(miniredis::resp::null_bulk_string() == "$-1\r\n");
}
