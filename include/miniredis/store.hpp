#pragma once

#include <chrono>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace miniredis {

class Store {
public:
    bool set(std::string key, std::string value);
    std::optional<std::string> get(const std::string& key);
    bool del(const std::string& key);
    bool expire(const std::string& key, std::chrono::seconds ttl);
    std::optional<long long> ttl(const std::string& key);

private:
    struct Value {
        std::string data;
        std::optional<std::chrono::steady_clock::time_point> expires_at;
    };

    bool is_expired(const Value& value) const;

    std::unordered_map<std::string, Value> values_;
    mutable std::shared_mutex mutex_;
};

}  // namespace miniredis
