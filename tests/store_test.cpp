#include "miniredis/store.hpp"

#include <cassert>

void store_set_get_test() {
    miniredis::Store store;
    store.set("name", "alice");
    assert(store.get("name") == "alice");
}
