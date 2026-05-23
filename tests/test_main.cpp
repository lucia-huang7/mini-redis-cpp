#include <iostream>

void command_dispatcher_ping_test();
void aof_append_replay_test();
void resp_encoder_test();
void store_set_get_test();

int main() {
    resp_encoder_test();
    store_set_get_test();
    command_dispatcher_ping_test();
    aof_append_replay_test();
    std::cout << "All tests passed\n";
    return 0;
}
