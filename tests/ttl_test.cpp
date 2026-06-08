#include "miniredis/store.hpp"
#include "miniredis/ttl.hpp"

#include <cassert>
#include <chrono>
#include <thread>

void ttl_cleaner_removes_expired_keys_test() {
    miniredis::Store store;
    store.set("temp", "value");
    assert(store.expire("temp", std::chrono::seconds(0)));

    miniredis::TtlCleaner cleaner(store, std::chrono::milliseconds(10));
    cleaner.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cleaner.stop();

    assert(!store.get("temp").has_value());
}
