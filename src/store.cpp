#include "miniredis/store.hpp"

#include <mutex>

namespace miniredis {

bool Store::set(std::string key, std::string value) {
    std::unique_lock lock(mutex_);
    values_[std::move(key)] = Value{std::move(value), std::nullopt};
    return true;
}

std::optional<std::string> Store::get(const std::string& key) {
    std::unique_lock lock(mutex_);
    auto it = values_.find(key);
    if (it == values_.end()) {
        return std::nullopt;
    }

    if (is_expired(it->second)) {
        values_.erase(it);
        return std::nullopt;
    }

    return it->second.data;
}

bool Store::del(const std::string& key) {
    std::unique_lock lock(mutex_);
    return values_.erase(key) > 0;
}

bool Store::expire(const std::string& key, std::chrono::seconds ttl) {
    std::unique_lock lock(mutex_);
    auto it = values_.find(key);
    if (it == values_.end() || is_expired(it->second)) {
        if (it != values_.end()) {
            values_.erase(it);
        }
        return false;
    }

    it->second.expires_at = std::chrono::steady_clock::now() + ttl;
    return true;
}

std::optional<long long> Store::ttl(const std::string& key) {
    std::unique_lock lock(mutex_);
    auto it = values_.find(key);
    if (it == values_.end()) {
        return std::nullopt;
    }

    if (is_expired(it->second)) {
        values_.erase(it);
        return std::nullopt;
    }

    if (!it->second.expires_at.has_value()) {
        return -1;
    }

    const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
        *it->second.expires_at - std::chrono::steady_clock::now());
    return remaining.count();
}

bool Store::is_expired(const Value& value) const {
    return value.expires_at.has_value() && std::chrono::steady_clock::now() >= *value.expires_at;
}

}  // namespace miniredis
