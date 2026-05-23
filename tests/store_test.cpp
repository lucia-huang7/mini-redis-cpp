#include "miniredis/store.hpp"

#include <cassert>
#include <chrono>
#include <thread>

void store_set_get_test() {
    miniredis::Store store;
    store.set("name", "alice");
    assert(store.get("name") == "alice");

    store.set("temp", "value");
    assert(store.expire("temp", std::chrono::seconds(0)));
    assert(store.remove_expired() == 1);
    assert(!store.get("temp").has_value());
}
