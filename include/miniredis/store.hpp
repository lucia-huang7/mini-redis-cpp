#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace miniredis {

class Store {
public:
    enum class SetCondition {
        Always,
        IfExists,
        IfNotExists,
    };

    struct SetOptions {
        std::optional<std::chrono::milliseconds> ttl;
        std::optional<long long> expires_at_unix_ms;
        SetCondition condition = SetCondition::Always;
    };

    struct SnapshotEntry {
        std::string key;
        std::string value;
        std::optional<long long> expires_at_unix_ms;
    };

    struct ActiveExpireConfig {
        std::size_t samples_per_round = 20;
        // Keep sampling a shard while more than this fraction of a round was expired.
        double repeat_threshold = 0.25;
        std::chrono::microseconds time_budget{25000};
    };

    static constexpr std::size_t kShardCount = 64;

    bool set(std::string key, std::string value);
    bool set(std::string key, std::string value, SetOptions options);
    std::optional<std::string> get(const std::string& key);
    std::vector<std::optional<std::string>> mget(const std::vector<std::string>& keys);
    std::size_t exists(const std::vector<std::string>& keys);
    bool del(const std::string& key);
    bool expire(const std::string& key, std::chrono::seconds ttl);
    bool expire_at(const std::string& key, long long unix_ms);
    std::optional<long long> ttl(const std::string& key);
    long long increment(const std::string& key, long long delta);

    // Physical presence and absolute deadline of a key, ignoring logical expiry.
    // Outer nullopt: key is not stored. Inner nullopt: key has no expiration.
    std::optional<std::optional<long long>> deadline_unix_ms(const std::string& key) const;

    // Full scan; used by tests and tools. The background cleaner uses active_expire_cycle.
    std::size_t remove_expired();

    // Redis-style probabilistic expiration: sample keys that carry a TTL and keep
    // sampling a shard only while the expired fraction stays above the threshold.
    std::size_t active_expire_cycle(std::mt19937_64& rng, const ActiveExpireConfig& config);
    std::size_t active_expire_cycle(std::mt19937_64& rng) {
        return active_expire_cycle(rng, ActiveExpireConfig{});
    }

    std::vector<SnapshotEntry> snapshot_shard(std::size_t shard_index) const;

    std::size_t size() const;
    std::size_t expiring_size() const;

private:
    static constexpr std::size_t kNotTracked = std::numeric_limits<std::size_t>::max();

    struct Value {
        std::string data;
        std::optional<std::chrono::steady_clock::time_point> expires_at;
        std::size_t expiring_index = kNotTracked;
    };

    using Map = std::unordered_map<std::string, Value>;
    using Entry = Map::value_type;

    struct Shard {
        Map values;
        // Entries that carry a TTL. unordered_map nodes are stable across rehash,
        // so raw entry pointers stay valid until the entry itself is erased.
        std::vector<Entry*> expiring;
        mutable std::shared_mutex mutex;
    };

    static bool is_expired(const Value& value, std::chrono::steady_clock::time_point now);
    static bool is_expired(const Value& value);
    static void set_expiration(
        Shard& shard,
        Entry& entry,
        std::optional<std::chrono::steady_clock::time_point> expires_at);
    static void untrack(Shard& shard, Value& value);
    static Map::iterator erase_entry(Shard& shard, Map::iterator it);
    void erase_if_expired(const std::string& key);

    Shard& shard_for(const std::string& key);
    const Shard& shard_for(const std::string& key) const;

    std::array<Shard, kShardCount> shards_;
    std::size_t expire_cursor_ = 0;
};

}  // namespace miniredis
