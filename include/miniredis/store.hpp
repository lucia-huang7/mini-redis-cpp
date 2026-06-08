#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
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
        SetCondition condition = SetCondition::Always;
    };

    bool set(std::string key, std::string value);
    bool set(std::string key, std::string value, SetOptions options);
    std::optional<std::string> get(const std::string& key);
    std::vector<std::optional<std::string>> mget(const std::vector<std::string>& keys);
    std::size_t exists(const std::vector<std::string>& keys);
    bool del(const std::string& key);
    bool expire(const std::string& key, std::chrono::seconds ttl);
    std::optional<long long> ttl(const std::string& key);
    long long increment(const std::string& key, long long delta);
    std::size_t remove_expired();

private:
    struct Value {
        std::string data;
        std::optional<std::chrono::steady_clock::time_point> expires_at;
    };

    bool is_expired(const Value& value) const;
    void erase_if_expired(const std::string& key);

    struct Shard {
        std::unordered_map<std::string, Value> values;
        mutable std::shared_mutex mutex;
    };

    static constexpr std::size_t kShardCount = 64;

    Shard& shard_for(const std::string& key);
    const Shard& shard_for(const std::string& key) const;

    std::array<Shard, kShardCount> shards_;
};

}  // namespace miniredis
