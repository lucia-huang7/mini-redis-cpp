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
