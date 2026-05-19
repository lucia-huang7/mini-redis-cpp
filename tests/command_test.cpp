#include "miniredis/command.hpp"
#include "miniredis/store.hpp"

#include <cassert>

void command_dispatcher_ping_test() {
    miniredis::Store store;
    miniredis::CommandDispatcher dispatcher(store);
    assert(dispatcher.execute({"PING"}) == "+PONG\r\n");
}
