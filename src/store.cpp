#include "miniredis/store.hpp"

#include <limits>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <utility>

namespace miniredis {

bool Store::set(std::string key, std::string value) {
    return set(std::move(key), std::move(value), SetOptions{});
}

bool Store::set(std::string key, std::string value, SetOptions options) {
    std::unique_lock lock(mutex_);
    auto it = values_.find(key);
    if (it != values_.end() && is_expired(it->second)) {
        it = values_.erase(it);
    }

    const bool exists = it != values_.end();
    if (options.condition == SetCondition::IfExists && !exists) {
        return false;
    }
    if (options.condition == SetCondition::IfNotExists && exists) {
        return false;
    }

    std::optional<std::chrono::steady_clock::time_point> expires_at;
    if (options.ttl.has_value()) {
        expires_at = std::chrono::steady_clock::now() + *options.ttl;
    }

    values_[std::move(key)] = Value{std::move(value), expires_at};
    return true;
}

std::optional<std::string> Store::get(const std::string& key) {
    {
        std::shared_lock lock(mutex_);
        auto it = values_.find(key);
        if (it == values_.end()) {
            return std::nullopt;
        }

        if (!is_expired(it->second)) {
            return it->second.data;
        }
    }

    erase_if_expired(key);
    return std::nullopt;
}

std::vector<std::optional<std::string>> Store::mget(const std::vector<std::string>& keys) {
    std::vector<std::optional<std::string>> result;
    result.reserve(keys.size());

    for (const auto& key : keys) {
        result.push_back(get(key));
    }
    return result;
}

std::size_t Store::exists(const std::vector<std::string>& keys) {
    std::size_t count = 0;
    for (const auto& key : keys) {
        if (get(key).has_value()) {
            ++count;
        }
    }
    return count;
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
    {
        std::shared_lock lock(mutex_);
        auto it = values_.find(key);
        if (it == values_.end()) {
            return std::nullopt;
        }

        if (!is_expired(it->second)) {
            if (!it->second.expires_at.has_value()) {
                return -1;
            }

            const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
                *it->second.expires_at - std::chrono::steady_clock::now());
            return remaining.count();
        }
    }

    erase_if_expired(key);
    return std::nullopt;
}

long long Store::increment(const std::string& key, long long delta) {
    std::unique_lock lock(mutex_);
    auto it = values_.find(key);
    std::optional<std::chrono::steady_clock::time_point> expires_at;

    long long current = 0;
    if (it != values_.end()) {
        if (is_expired(it->second)) {
            values_.erase(it);
        } else {
            expires_at = it->second.expires_at;
            try {
                std::size_t parsed = 0;
                current = std::stoll(it->second.data, &parsed);
                if (parsed != it->second.data.size()) {
                    throw std::invalid_argument("not an integer");
                }
            } catch (const std::exception&) {
                throw std::invalid_argument("value is not an integer or out of range");
            }
        }
    }

    if ((delta > 0 && current > std::numeric_limits<long long>::max() - delta) ||
        (delta < 0 && current < std::numeric_limits<long long>::min() - delta)) {
        throw std::out_of_range("increment or decrement would overflow");
    }

    const auto updated = current + delta;
    values_[key] = Value{std::to_string(updated), expires_at};
    return updated;
}

std::size_t Store::remove_expired() {
    std::unique_lock lock(mutex_);
    std::size_t removed = 0;

    for (auto it = values_.begin(); it != values_.end();) {
        if (is_expired(it->second)) {
            it = values_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }

    return removed;
}

bool Store::is_expired(const Value& value) const {
    return value.expires_at.has_value() && std::chrono::steady_clock::now() >= *value.expires_at;
}

void Store::erase_if_expired(const std::string& key) {
    std::unique_lock lock(mutex_);
    auto it = values_.find(key);
    if (it != values_.end() && is_expired(it->second)) {
        values_.erase(it);
    }
}

}  // namespace miniredis
