#include "miniredis/aof.hpp"
#include "miniredis/command.hpp"
#include "miniredis/resp.hpp"
#include "miniredis/store.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>

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

void aof_replay_truncates_incomplete_tail_test() {
    const auto path = std::filesystem::temp_directory_path() / "miniredis_aof_corrupt_test.aof";
    std::filesystem::remove(path);

    const auto valid = miniredis::resp::array({"SET", "name", "alice"});
    {
        miniredis::Aof aof(path);
        aof.append(valid);
    }

    {
        std::ofstream file(path, std::ios::binary | std::ios::app);
        file << "*3\r\n$3\r\nSET\r\n$4\r\ncity\r\n$";
    }

    miniredis::Store store;
    miniredis::CommandDispatcher dispatcher(store);
    miniredis::Aof aof(path);

    assert(aof.replay(dispatcher) == 1);
    assert(store.get("name") == "alice");
    assert(!store.get("city").has_value());
    assert(std::filesystem::file_size(path) == valid.size());

    std::filesystem::remove(path);
}
