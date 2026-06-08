#include "miniredis/store.hpp"

#include <cassert>
#include <chrono>
#include <stdexcept>
#include <thread>

void store_set_get_test() {
    miniredis::Store store;
    store.set("name", "alice");
    assert(store.get("name") == "alice");

    miniredis::Store::SetOptions if_not_exists;
    if_not_exists.condition = miniredis::Store::SetCondition::IfNotExists;
    assert(!store.set("name", "bob", if_not_exists));
    assert(store.get("name") == "alice");

    miniredis::Store::SetOptions if_exists;
    if_exists.condition = miniredis::Store::SetCondition::IfExists;
    assert(store.set("name", "bob", if_exists));
    assert(store.get("name") == "bob");
    assert(!store.set("missing", "value", if_exists));

    assert(store.exists({"name", "missing", "name"}) == 2);
    const auto values = store.mget({"name", "missing"});
    assert(values.size() == 2);
    assert(values[0] == "bob");
    assert(!values[1].has_value());

    assert(store.increment("counter", 1) == 1);
    assert(store.increment("counter", -1) == 0);
    store.set("bad-counter", "abc");
    try {
        store.increment("bad-counter", 1);
        assert(false);
    } catch (const std::invalid_argument&) {
    }

    store.set("temp", "value");
    assert(store.expire("temp", std::chrono::seconds(0)));
    assert(store.remove_expired() == 1);
    assert(!store.get("temp").has_value());

    miniredis::Store::SetOptions short_ttl;
    short_ttl.ttl = std::chrono::milliseconds(1);
    store.set("short", "value", short_ttl);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(!store.get("short").has_value());
}
