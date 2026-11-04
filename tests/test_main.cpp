#include <iostream>

void command_dispatcher_ping_test();
void aof_append_replay_test();
void aof_replay_truncates_incomplete_tail_test();
void aof_handles_short_writes_test();
void aof_write_failure_disables_future_writes_test();
void aof_fsync_failure_is_reported_test();
void resp_encoder_test();
void store_set_get_test();
void aof_logs_blind_writes_test();
void aof_rewrite_compacts_log_test();
void aof_rewrite_under_concurrent_writes_test();
void aof_auto_rewrite_triggers_on_growth_test();
void bgrewriteaof_command_test();
void ttl_cleaner_removes_expired_keys_test();
void ttl_cleaner_stops_promptly_test();
void store_tracks_expiring_keys_test();
void active_expire_cycle_reclaims_mostly_expired_keyspace_test();
void active_expire_cycle_respects_time_budget_test();

int main() {
    resp_encoder_test();
    store_set_get_test();
    command_dispatcher_ping_test();
    aof_append_replay_test();
    aof_replay_truncates_incomplete_tail_test();
    aof_handles_short_writes_test();
    aof_write_failure_disables_future_writes_test();
    aof_fsync_failure_is_reported_test();
    aof_logs_blind_writes_test();
    aof_rewrite_compacts_log_test();
    aof_rewrite_under_concurrent_writes_test();
    aof_auto_rewrite_triggers_on_growth_test();
    bgrewriteaof_command_test();
    ttl_cleaner_removes_expired_keys_test();
    ttl_cleaner_stops_promptly_test();
    store_tracks_expiring_keys_test();
    active_expire_cycle_reclaims_mostly_expired_keyspace_test();
    active_expire_cycle_respects_time_budget_test();
    std::cout << "All tests passed\n";
    return 0;
}
