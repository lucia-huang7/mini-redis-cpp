#include "miniredis/aof.hpp"
#include "miniredis/command.hpp"
#include "miniredis/resp.hpp"
#include "miniredis/store.hpp"

#include <cassert>
#include <filesystem>

void aof_append_replay_test() {
    const auto path = std::filesystem::temp_directory_path() / "miniredis_aof_test.aof";
    std::filesystem::remove(path);

    {
        miniredis::Aof aof(path);
        aof.append(miniredis::resp::array({"SET", "name", "alice"}));
        aof.append(miniredis::resp::array({"SET", "city", "chicago"}));
        aof.append(miniredis::resp::array({"DEL", "city"}));
    }

    miniredis::Store store;
    miniredis::CommandDispatcher dispatcher(store);
    miniredis::Aof aof(path);

    assert(aof.replay(dispatcher) == 3);
    assert(store.get("name") == "alice");
    assert(!store.get("city").has_value());

    std::filesystem::remove(path);
}
