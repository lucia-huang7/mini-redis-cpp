#include <iostream>

void command_dispatcher_ping_test();
void aof_append_replay_test();
void aof_replay_truncates_incomplete_tail_test();
void aof_handles_short_writes_test();
void aof_write_failure_disables_future_writes_test();
void aof_fsync_failure_is_reported_test();
void resp_encoder_test();
void store_set_get_test();
void thread_pool_runs_enqueued_tasks_test();
void thread_pool_survives_task_exception_test();
void thread_pool_rejects_tasks_when_queue_is_full_test();
void ttl_cleaner_removes_expired_keys_test();

int main() {
    resp_encoder_test();
    store_set_get_test();
    command_dispatcher_ping_test();
    aof_append_replay_test();
    aof_replay_truncates_incomplete_tail_test();
    aof_handles_short_writes_test();
    aof_write_failure_disables_future_writes_test();
    aof_fsync_failure_is_reported_test();
    thread_pool_runs_enqueued_tasks_test();
    thread_pool_survives_task_exception_test();
    thread_pool_rejects_tasks_when_queue_is_full_test();
    ttl_cleaner_removes_expired_keys_test();
    std::cout << "All tests passed\n";
    return 0;
}
