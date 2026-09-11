#include "miniredis/aof.hpp"
#include "miniredis/command.hpp"
#include "miniredis/durable_executor.hpp"
#include "miniredis/resp.hpp"
#include "miniredis/store.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <cerrno>
#include <algorithm>
#include <unistd.h>

void aof_append_replay_test() {
    const auto path = std::filesystem::temp_directory_path() / "miniredis_aof_test.aof";
    std::filesystem::remove(path);

    {
        miniredis::Aof aof(path);
        aof.append(miniredis::resp::array({"SET", "name", "alice"}));
        aof.append(miniredis::resp::array({"SET", "city", "chicago"}));
        aof.append(miniredis::resp::array({"DEL", "city"}));
        aof.append(miniredis::resp::array({"INCR", "visits"}));
        aof.append(miniredis::resp::array({"INCR", "visits"}));
    }

    miniredis::Store store;
    miniredis::CommandDispatcher dispatcher(store);
    miniredis::Aof aof(path);

    assert(aof.replay(dispatcher) == 5);
    assert(store.get("name") == "alice");
    assert(!store.get("city").has_value());
    assert(store.get("visits") == "2");

    std::filesystem::remove(path);
}

void aof_handles_short_writes_test() {
    const auto path = std::filesystem::temp_directory_path() / "miniredis_aof_short_write_test.aof";
    std::filesystem::remove(path);
    miniredis::AofIoHooks hooks;
    hooks.write = [](int fd, const void* data, std::size_t size) {
        return ::write(fd, data, std::min<std::size_t>(size, 3));
    };
    {
        miniredis::Aof aof(path, miniredis::AofSyncPolicy::No, std::move(hooks));
        aof.append(miniredis::resp::array({"SET", "key", "value"}));
    }
    miniredis::Store store;
    miniredis::CommandDispatcher dispatcher(store);
    miniredis::Aof replay(path);
    assert(replay.replay(dispatcher) == 1);
    assert(store.get("key") == "value");
    std::filesystem::remove(path);
}

void aof_write_failure_disables_future_writes_test() {
    const auto path = std::filesystem::temp_directory_path() / "miniredis_aof_write_failure_test.aof";
    std::filesystem::remove(path);
    miniredis::AofIoHooks hooks;
    hooks.write = [](int, const void*, std::size_t) -> ssize_t {
        errno = ENOSPC;
        return -1;
    };
    miniredis::Aof aof(path, miniredis::AofSyncPolicy::No, std::move(hooks));
    miniredis::Store store;
    miniredis::CommandDispatcher dispatcher(store);
    miniredis::DurableCommandExecutor executor(dispatcher, &aof);
    const std::vector<std::string_view> first{"SET", "key", "value"};
    const std::vector<std::string_view> second{"SET", "other", "value"};
    assert(executor.execute(first).find("-MISCONF") == 0);
    assert(executor.write_failed());
    assert(executor.execute(second).find("-MISCONF") == 0);
    assert(!store.get("other").has_value());
    std::filesystem::remove(path);
}

void aof_fsync_failure_is_reported_test() {
    const auto path = std::filesystem::temp_directory_path() / "miniredis_aof_fsync_failure_test.aof";
    std::filesystem::remove(path);
    miniredis::AofIoHooks hooks;
    hooks.fsync = [](int) {
        errno = EIO;
        return -1;
    };
    miniredis::Aof aof(path, miniredis::AofSyncPolicy::Always, std::move(hooks));
    bool failed = false;
    try {
        aof.append(miniredis::resp::array({"SET", "key", "value"}));
    } catch (const std::runtime_error&) {
        failed = true;
    }
    assert(failed);
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
