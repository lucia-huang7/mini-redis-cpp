#include "miniredis/store.hpp"
#include "miniredis/ttl.hpp"

#include <cassert>
#include <chrono>
#include <random>
#include <string>
#include <thread>

void ttl_cleaner_removes_expired_keys_test() {
    miniredis::Store store;
    store.set("temp", "value");
    assert(store.expire("temp", std::chrono::seconds(0)));
    assert(store.size() == 1);

    miniredis::TtlCleaner cleaner(store, std::chrono::milliseconds(10));
    cleaner.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cleaner.stop();

    // Checked through size() rather than get(), which would expire lazily.
    assert(store.size() == 0);
    assert(cleaner.removed_total() == 1);
}

void ttl_cleaner_stops_promptly_test() {
    miniredis::Store store;
    miniredis::TtlCleaner cleaner(store, std::chrono::seconds(10));
    cleaner.start();
    const auto started = std::chrono::steady_clock::now();
    cleaner.stop();
    assert(std::chrono::steady_clock::now() - started < std::chrono::seconds(1));
}

void store_tracks_expiring_keys_test() {
    miniredis::Store store;
    store.set("plain", "value");
    assert(store.expiring_size() == 0);

    miniredis::Store::SetOptions with_ttl;
    with_ttl.ttl = std::chrono::seconds(60);
    store.set("a", "1", with_ttl);
    store.set("b", "2", with_ttl);
    store.set("c", "3", with_ttl);
    assert(store.expiring_size() == 3);

    // Overwrite without TTL clears the expiration, as in Redis.
    store.set("a", "plain-now");
    assert(store.expiring_size() == 2);
    assert(store.ttl("a") == -1);

    assert(store.expire("plain", std::chrono::seconds(60)));
    assert(store.expiring_size() == 3);

    // INCR keeps the TTL and its index slot.
    store.set("counter", "5", with_ttl);
    assert(store.increment("counter", 1) == 6);
    assert(store.expiring_size() == 4);
    assert(store.ttl("counter") > 0);

    assert(store.del("b"));
    assert(store.expiring_size() == 3);
    assert(store.del("plain"));
    assert(store.del("counter"));
    assert(store.del("c"));
    assert(store.expiring_size() == 0);
    assert(store.size() == 1);
}

void active_expire_cycle_reclaims_mostly_expired_keyspace_test() {
    miniredis::Store store;
    miniredis::Store::SetOptions expired;
    expired.ttl = std::chrono::milliseconds(1);
    miniredis::Store::SetOptions live;
    live.ttl = std::chrono::hours(1);

    for (int i = 0; i < 20000; ++i) {
        store.set("dead:" + std::to_string(i), "v", expired);
    }
    for (int i = 0; i < 200; ++i) {
        store.set("live:" + std::to_string(i), "v", live);
        store.set("plain:" + std::to_string(i), "v");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    std::mt19937_64 rng(42);
    miniredis::Store::ActiveExpireConfig config;
    config.time_budget = std::chrono::seconds(10);
    std::size_t removed = 0;
    for (int cycle = 0; cycle < 20 && removed < 20000; ++cycle) {
        removed += store.active_expire_cycle(rng, config);
    }

    // Sampling keeps going while most samples are expired, so a keyspace that is
    // ~99% expired drains to the point where samples stop finding expired keys.
    assert(removed >= 19000);
    assert(store.get("live:7") == "v");
    assert(store.get("plain:7") == "v");
    assert(store.size() == 400 + (20000 - removed));
}

void active_expire_cycle_respects_time_budget_test() {
    miniredis::Store store;
    miniredis::Store::SetOptions expired;
    expired.ttl = std::chrono::milliseconds(1);
    for (int i = 0; i < 200000; ++i) {
        store.set("k" + std::to_string(i), "v", expired);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    std::mt19937_64 rng(7);
    miniredis::Store::ActiveExpireConfig config;
    config.time_budget = std::chrono::microseconds(500);
    const auto started = std::chrono::steady_clock::now();
    const auto removed = store.active_expire_cycle(rng, config);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    assert(removed > 0);
    assert(removed < 200000);
    // Budget is checked between 20-key rounds, so overshoot is a single round.
    // Sanitizer builds are slow, hence the generous bound.
    assert(elapsed < std::chrono::milliseconds(200));
}
