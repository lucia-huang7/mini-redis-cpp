#include "miniredis/aof.hpp"
#include "miniredis/command.hpp"
#include "miniredis/durable_executor.hpp"
#include "miniredis/resp.hpp"
#include "miniredis/store.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
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
    miniredis::DurableCommandExecutor executor(store, dispatcher, &aof);
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

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

std::string run(miniredis::DurableCommandExecutor& executor, const std::vector<std::string>& command) {
    std::vector<std::string_view> views(command.begin(), command.end());
    return executor.execute(views);
}

miniredis::AofRewritePolicy manual_rewrite_only() {
    miniredis::AofRewritePolicy policy;
    policy.min_bytes = 0;
    return policy;
}

}  // namespace

void aof_logs_blind_writes_test() {
    const auto path = std::filesystem::temp_directory_path() / "miniredis_aof_blind_write_test.aof";
    std::filesystem::remove(path);
    {
        miniredis::Aof aof(path, miniredis::AofSyncPolicy::No);
        miniredis::Store store;
        miniredis::CommandDispatcher dispatcher(store);
        miniredis::DurableCommandExecutor executor(store, dispatcher, &aof, manual_rewrite_only());

        assert(run(executor, {"SET", "k", "v", "NX"}) == "+OK\r\n");
        assert(run(executor, {"SET", "k", "w", "NX"}) == "$-1\r\n");
        assert(run(executor, {"SET", "counter", "10", "EX", "100"}) == "+OK\r\n");
        assert(run(executor, {"INCR", "counter"}) == ":11\r\n");
        assert(run(executor, {"DECR", "plain"}) == ":-1\r\n");
    }

    const auto contents = read_file(path);
    assert(contents.find("$2\r\nNX\r\n") == std::string::npos);
    assert(contents.find("INCR") == std::string::npos);
    assert(contents.find("DECR") == std::string::npos);
    assert(contents.find(miniredis::resp::array({"SET", "plain", "-1"})) != std::string::npos);

    miniredis::Store store;
    miniredis::CommandDispatcher dispatcher(store);
    miniredis::Aof aof(path);
    assert(aof.replay(dispatcher) == 4);
    assert(store.get("k") == "v");
    assert(store.get("counter") == "11");
    const auto ttl = store.ttl("counter");
    assert(ttl.has_value() && *ttl > 90 && *ttl <= 100);
    assert(store.get("plain") == "-1");
    std::filesystem::remove(path);
}

void aof_rewrite_compacts_log_test() {
    const auto path = std::filesystem::temp_directory_path() / "miniredis_aof_rewrite_test.aof";
    std::filesystem::remove(path);
    {
        miniredis::Aof aof(path, miniredis::AofSyncPolicy::No);
        miniredis::Store store;
        miniredis::CommandDispatcher dispatcher(store);
        miniredis::DurableCommandExecutor executor(store, dispatcher, &aof, manual_rewrite_only());

        for (int i = 0; i < 1000; ++i) {
            run(executor, {"SET", "hot", std::to_string(i)});
            run(executor, {"SET", "gone", "x"});
            run(executor, {"DEL", "gone"});
        }
        run(executor, {"SET", "ttl-key", "v", "PX", "60000"});
        const auto before = aof.size();

        assert(executor.rewrite_aof());
        assert(executor.completed_rewrites() == 1);
        assert(aof.size() < before / 100);

        // The reopened file keeps accepting appends.
        run(executor, {"SET", "after", "rewrite"});
    }

    const auto contents = read_file(path);
    assert(contents.find("gone") == std::string::npos);
    assert(contents.find("PXAT") != std::string::npos);
    assert(!std::filesystem::exists(std::filesystem::path(path.string() + ".rewrite.tmp")));

    miniredis::Store store;
    miniredis::CommandDispatcher dispatcher(store);
    miniredis::Aof aof(path);
    assert(aof.replay(dispatcher) == 3);
    assert(store.get("hot") == "999");
    assert(!store.get("gone").has_value());
    assert(store.get("after") == "rewrite");
    const auto ttl = store.ttl("ttl-key");
    assert(ttl.has_value() && *ttl > 50 && *ttl <= 60);
    std::filesystem::remove(path);
}

void aof_rewrite_under_concurrent_writes_test() {
    const auto path = std::filesystem::temp_directory_path() / "miniredis_aof_rewrite_concurrent_test.aof";
    std::filesystem::remove(path);
    constexpr int kWriters = 4;
    constexpr int kKeys = 200;
    std::vector<std::string> expected(kKeys * kWriters);

    {
        miniredis::Aof aof(path, miniredis::AofSyncPolicy::No);
        miniredis::Store store;
        miniredis::CommandDispatcher dispatcher(store);
        miniredis::DurableCommandExecutor executor(store, dispatcher, &aof, manual_rewrite_only());

        std::atomic<bool> stop{false};
        std::vector<std::thread> writers;
        for (int w = 0; w < kWriters; ++w) {
            writers.emplace_back([&, w] {
                for (int round = 0; !stop.load() || round < 50; ++round) {
                    for (int k = 0; k < kKeys; ++k) {
                        const auto key = "w" + std::to_string(w) + ":" + std::to_string(k);
                        if ((round + k) % 7 == 0) {
                            run(executor, {"DEL", key});
                        } else if ((round + k) % 5 == 0) {
                            run(executor, {"INCR", "counter:" + std::to_string(w)});
                        } else {
                            run(executor, {"SET", key, std::to_string(round)});
                        }
                    }
                }
            });
        }

        int rewrites = 0;
        for (int i = 0; i < 5; ++i) {
            if (executor.rewrite_aof()) {
                ++rewrites;
            }
        }
        stop.store(true);
        for (auto& writer : writers) {
            writer.join();
        }
        assert(rewrites == 5);

        for (int w = 0; w < kWriters; ++w) {
            for (int k = 0; k < kKeys; ++k) {
                const auto value = store.get("w" + std::to_string(w) + ":" + std::to_string(k));
                expected[w * kKeys + k] = value.value_or("<missing>");
            }
            expected.push_back(store.get("counter:" + std::to_string(w)).value_or("<missing>"));
        }
    }

    miniredis::Store store;
    miniredis::CommandDispatcher dispatcher(store);
    miniredis::Aof aof(path);
    aof.replay(dispatcher);
    for (int w = 0; w < kWriters; ++w) {
        for (int k = 0; k < kKeys; ++k) {
            const auto value = store.get("w" + std::to_string(w) + ":" + std::to_string(k));
            assert(value.value_or("<missing>") == expected[w * kKeys + k]);
        }
        assert(store.get("counter:" + std::to_string(w)).value_or("<missing>") ==
               expected[kKeys * kWriters + w]);
    }
    std::filesystem::remove(path);
}

void aof_auto_rewrite_triggers_on_growth_test() {
    const auto path = std::filesystem::temp_directory_path() / "miniredis_aof_auto_rewrite_test.aof";
    std::filesystem::remove(path);
    {
        miniredis::Aof aof(path, miniredis::AofSyncPolicy::No);
        miniredis::Store store;
        miniredis::CommandDispatcher dispatcher(store);
        miniredis::AofRewritePolicy policy;
        policy.min_bytes = 4096;
        policy.growth_percent = 100;
        miniredis::DurableCommandExecutor executor(store, dispatcher, &aof, policy);

        std::size_t uncompacted_bytes = 0;
        for (int i = 0; i < 2000; ++i) {
            run(executor, {"SET", "same-key", std::to_string(i)});
            uncompacted_bytes += miniredis::resp::array({"SET", "same-key", std::to_string(i)}).size();
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (executor.rewrite_in_progress() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(executor.completed_rewrites() >= 1);
        assert(aof.size() < uncompacted_bytes / 2);
    }

    miniredis::Store store;
    miniredis::CommandDispatcher dispatcher(store);
    miniredis::Aof aof(path);
    aof.replay(dispatcher);
    assert(store.get("same-key") == "1999");
    std::filesystem::remove(path);
}

void bgrewriteaof_command_test() {
    miniredis::Store store;
    miniredis::CommandDispatcher dispatcher(store);
    miniredis::DurableCommandExecutor without_aof(store, dispatcher, nullptr);
    assert(run(without_aof, {"BGREWRITEAOF"}) == "-ERR AOF is disabled\r\n");

    const auto path = std::filesystem::temp_directory_path() / "miniredis_bgrewriteaof_test.aof";
    std::filesystem::remove(path);
    {
        miniredis::Aof aof(path, miniredis::AofSyncPolicy::No);
        miniredis::DurableCommandExecutor executor(store, dispatcher, &aof, manual_rewrite_only());
        run(executor, {"SET", "k", "v"});
        assert(run(executor, {"bgrewriteaof"}) == "+Background append only file rewriting started\r\n");
    }
    std::filesystem::remove(path);
}
