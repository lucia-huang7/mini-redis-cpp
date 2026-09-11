#include "miniredis/command.hpp"
#include "miniredis/store.hpp"

#include <cassert>
#include <chrono>

void command_dispatcher_ping_test() {
    miniredis::Store store;
    miniredis::CommandDispatcher dispatcher(store);

    assert(dispatcher.execute({"PING"}) == "+PONG\r\n");
    assert(dispatcher.execute({"PING", "hello"}) == "$5\r\nhello\r\n");
    assert(dispatcher.execute({"ECHO", "hello"}) == "$5\r\nhello\r\n");

    assert(dispatcher.execute({"SET", "name", "alice"}) == "+OK\r\n");
    assert(dispatcher.execute({"GET", "name"}) == "$5\r\nalice\r\n");
    assert(dispatcher.execute({"GET", "missing"}) == "$-1\r\n");
    assert(dispatcher.execute({"MGET", "name", "missing"}) == "*2\r\n$5\r\nalice\r\n$-1\r\n");
    assert(dispatcher.execute({"EXISTS", "name", "missing", "name"}) == ":2\r\n");

    assert(dispatcher.execute({"SET", "name", "bob", "NX"}) == "$-1\r\n");
    assert(dispatcher.execute({"GET", "name"}) == "$5\r\nalice\r\n");
    assert(dispatcher.execute({"SET", "name", "bob", "XX"}) == "+OK\r\n");
    assert(dispatcher.execute({"GET", "name"}) == "$3\r\nbob\r\n");
    assert(dispatcher.execute({"SET", "lease", "active", "EX", "10", "NX"}) == "+OK\r\n");
    assert(dispatcher.execute({"TTL", "lease"}) != ":-1\r\n");
    const auto future_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 10000;
    assert(dispatcher.execute({"SET", "absolute", "active", "PXAT", std::to_string(future_ms)}) == "+OK\r\n");
    assert(dispatcher.execute({"TTL", "absolute"}) != ":-1\r\n");
    assert(dispatcher.execute({"SET", "already-expired", "value", "PXAT", "1"}) == "+OK\r\n");
    assert(dispatcher.execute({"GET", "already-expired"}) == "$-1\r\n");
    assert(dispatcher.execute({"SET", "bad", "value", "EX"}) == "-ERR syntax error\r\n");
    assert(dispatcher.execute({"SET", "bad", "value", "NX", "XX"}) == "-ERR syntax error\r\n");

    assert(dispatcher.execute({"INCR", "counter"}) == ":1\r\n");
    assert(dispatcher.execute({"INCR", "counter"}) == ":2\r\n");
    assert(dispatcher.execute({"DECR", "counter"}) == ":1\r\n");
    assert(dispatcher.execute({"SET", "counter", "abc"}) == "+OK\r\n");
    assert(dispatcher.execute({"INCR", "counter"}) == "-ERR value is not an integer or out of range\r\n");

    assert(dispatcher.execute({"DEL", "name"}) == ":1\r\n");
    assert(dispatcher.execute({"DEL", "name"}) == ":0\r\n");

    assert(dispatcher.execute({"SET", "session", "active"}) == "+OK\r\n");
    assert(dispatcher.execute({"TTL", "session"}) == ":-1\r\n");
    assert(dispatcher.execute({"EXPIRE", "session", "10"}) == ":1\r\n");
    assert(dispatcher.execute({"TTL", "session"}) != ":-1\r\n");
    assert(dispatcher.execute({"PEXPIREAT", "session", std::to_string(future_ms)}) == ":1\r\n");
    assert(dispatcher.execute({"EXPIRE", "missing", "10"}) == ":0\r\n");

    assert(dispatcher.execute({"GET"}) == "-ERR wrong number of arguments for 'get' command\r\n");
    assert(dispatcher.execute({"EXPIRE", "session", "abc"}) == "-ERR value is not an integer or out of range\r\n");
}
