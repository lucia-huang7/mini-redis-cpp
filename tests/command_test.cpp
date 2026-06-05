#include "miniredis/command.hpp"
#include "miniredis/store.hpp"

#include <cassert>

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
    assert(dispatcher.execute({"EXPIRE", "missing", "10"}) == ":0\r\n");

    assert(dispatcher.execute({"GET"}) == "-ERR wrong number of arguments for 'get' command\r\n");
    assert(dispatcher.execute({"EXPIRE", "session", "abc"}) == "-ERR value is not an integer or out of range\r\n");
}
