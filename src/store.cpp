#include "miniredis/store.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <utility>

namespace miniredis {
namespace {

using SteadyClock = std::chrono::steady_clock;

long long unix_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

SteadyClock::time_point steady_deadline_from_unix_ms(long long unix_ms) {
    const auto steady_now = SteadyClock::now();
    const auto wall_now_ms = unix_now_ms();
    if (unix_ms <= wall_now_ms) {
        return steady_now;
    }

    const auto remaining = std::chrono::milliseconds(unix_ms - wall_now_ms);
    const auto maximum_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        SteadyClock::time_point::max() - steady_now);
    if (remaining >= maximum_ms) {
        return SteadyClock::time_point::max();
    }
    return steady_now + remaining;
}

long long unix_ms_from_steady_deadline(SteadyClock::time_point deadline) {
    const auto steady_now = SteadyClock::now();
    const auto wall_now_ms = unix_now_ms();
    if (deadline <= steady_now) {
        return wall_now_ms;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - steady_now);
    if (remaining.count() > std::numeric_limits<long long>::max() - wall_now_ms) {
        return std::numeric_limits<long long>::max();
    }
    return wall_now_ms + remaining.count();
}

}  // namespace

bool Store::set(std::string key, std::string value) {
    return set(std::move(key), std::move(value), SetOptions{});
}

bool Store::set(std::string key, std::string value, SetOptions options) {
    auto& shard = shard_for(key);
    std::unique_lock lock(shard.mutex);
    auto it = shard.values.find(key);
    if (it != shard.values.end() && is_expired(it->second)) {
        it = erase_entry(shard, it);
        it = shard.values.end();
    }

    const bool exists = it != shard.values.end();
    if (options.condition == SetCondition::IfExists && !exists) {
        return false;
    }
    if (options.condition == SetCondition::IfNotExists && exists) {
        return false;
    }

    std::optional<SteadyClock::time_point> expires_at;
    if (options.ttl.has_value()) {
        expires_at = SteadyClock::now() + *options.ttl;
    } else if (options.expires_at_unix_ms.has_value()) {
        expires_at = steady_deadline_from_unix_ms(*options.expires_at_unix_ms);
    }

    if (!exists) {
        it = shard.values.try_emplace(std::move(key)).first;
    }
    it->second.data = std::move(value);
    set_expiration(shard, *it, expires_at);
    return true;
}

std::optional<std::string> Store::get(const std::string& key) {
    {
        const auto& shard = shard_for(key);
        std::shared_lock lock(shard.mutex);
        auto it = shard.values.find(key);
        if (it == shard.values.end()) {
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
    auto& shard = shard_for(key);
    std::unique_lock lock(shard.mutex);
    auto it = shard.values.find(key);
    if (it == shard.values.end()) {
        return false;
    }
    const bool was_live = !is_expired(it->second);
    erase_entry(shard, it);
    return was_live;
}

bool Store::expire(const std::string& key, std::chrono::seconds ttl) {
    auto& shard = shard_for(key);
    std::unique_lock lock(shard.mutex);
    auto it = shard.values.find(key);
    if (it == shard.values.end()) {
        return false;
    }
    if (is_expired(it->second)) {
        erase_entry(shard, it);
        return false;
    }

    set_expiration(shard, *it, SteadyClock::now() + ttl);
    return true;
}

bool Store::expire_at(const std::string& key, long long unix_ms) {
    auto& shard = shard_for(key);
    std::unique_lock lock(shard.mutex);
    auto it = shard.values.find(key);
    if (it == shard.values.end()) {
        return false;
    }
    if (is_expired(it->second)) {
        erase_entry(shard, it);
        return false;
    }

    set_expiration(shard, *it, steady_deadline_from_unix_ms(unix_ms));
    return true;
}

std::optional<long long> Store::ttl(const std::string& key) {
    {
        const auto& shard = shard_for(key);
        std::shared_lock lock(shard.mutex);
        auto it = shard.values.find(key);
        if (it == shard.values.end()) {
            return std::nullopt;
        }

        if (!is_expired(it->second)) {
            if (!it->second.expires_at.has_value()) {
                return -1;
            }

            const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
                *it->second.expires_at - SteadyClock::now());
            return remaining.count();
        }
    }

    erase_if_expired(key);
    return std::nullopt;
}

long long Store::increment(const std::string& key, long long delta) {
    auto& shard = shard_for(key);
    std::unique_lock lock(shard.mutex);
    auto it = shard.values.find(key);
    if (it != shard.values.end() && is_expired(it->second)) {
        erase_entry(shard, it);
        it = shard.values.end();
    }

    long long current = 0;
    if (it != shard.values.end()) {
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

    if ((delta > 0 && current > std::numeric_limits<long long>::max() - delta) ||
        (delta < 0 && current < std::numeric_limits<long long>::min() - delta)) {
        throw std::out_of_range("increment or decrement would overflow");
    }

    const auto updated = current + delta;
    if (it == shard.values.end()) {
        it = shard.values.try_emplace(key).first;
    }
    // The value changes in place, so an existing TTL and its index entry are kept.
    it->second.data = std::to_string(updated);
    return updated;
}

std::optional<std::optional<long long>> Store::deadline_unix_ms(const std::string& key) const {
    const auto& shard = shard_for(key);
    std::shared_lock lock(shard.mutex);
    const auto it = shard.values.find(key);
    if (it == shard.values.end()) {
        return std::nullopt;
    }
    if (!it->second.expires_at.has_value()) {
        return std::optional<long long>{};
    }
    return std::optional<long long>{unix_ms_from_steady_deadline(*it->second.expires_at)};
}

std::size_t Store::remove_expired() {
    std::size_t removed = 0;
    const auto now = SteadyClock::now();

    for (auto& shard : shards_) {
        std::unique_lock lock(shard.mutex);
        for (auto it = shard.values.begin(); it != shard.values.end();) {
            if (is_expired(it->second, now)) {
                it = erase_entry(shard, it);
                ++removed;
            } else {
                ++it;
            }
        }
    }

    return removed;
}

std::size_t Store::active_expire_cycle(std::mt19937_64& rng, const ActiveExpireConfig& config) {
    const auto started = SteadyClock::now();
    const auto samples_per_round = std::max<std::size_t>(1, config.samples_per_round);
    std::size_t removed = 0;

    for (std::size_t visited = 0; visited < kShardCount; ++visited) {
        auto& shard = shards_[expire_cursor_];
        expire_cursor_ = (expire_cursor_ + 1) % kShardCount;

        while (true) {
            std::size_t sampled = 0;
            std::size_t expired = 0;
            {
                std::unique_lock lock(shard.mutex);
                const auto now = SteadyClock::now();
                while (sampled < samples_per_round && !shard.expiring.empty()) {
                    const auto index = static_cast<std::size_t>(rng() % shard.expiring.size());
                    Entry* entry = shard.expiring[index];
                    ++sampled;
                    if (is_expired(entry->second, now)) {
                        erase_entry(shard, shard.values.find(entry->first));
                        ++expired;
                    }
                }
            }
            removed += expired;

            if (sampled == 0 ||
                static_cast<double>(expired) <= config.repeat_threshold * static_cast<double>(sampled)) {
                break;
            }
            if (SteadyClock::now() - started >= config.time_budget) {
                return removed;
            }
        }

        if (SteadyClock::now() - started >= config.time_budget) {
            break;
        }
    }
    return removed;
}

std::vector<Store::SnapshotEntry> Store::snapshot_shard(std::size_t shard_index) const {
    const auto& shard = shards_.at(shard_index);
    std::shared_lock lock(shard.mutex);
    const auto now = SteadyClock::now();

    std::vector<SnapshotEntry> entries;
    entries.reserve(shard.values.size());
    for (const auto& [key, value] : shard.values) {
        if (is_expired(value, now)) {
            continue;
        }
        std::optional<long long> deadline;
        if (value.expires_at.has_value()) {
            deadline = unix_ms_from_steady_deadline(*value.expires_at);
        }
        entries.push_back(SnapshotEntry{key, value.data, deadline});
    }
    return entries;
}

std::size_t Store::size() const {
    std::size_t total = 0;
    for (const auto& shard : shards_) {
        std::shared_lock lock(shard.mutex);
        total += shard.values.size();
    }
    return total;
}

std::size_t Store::expiring_size() const {
    std::size_t total = 0;
    for (const auto& shard : shards_) {
        std::shared_lock lock(shard.mutex);
        total += shard.expiring.size();
    }
    return total;
}

bool Store::is_expired(const Value& value, SteadyClock::time_point now) {
    return value.expires_at.has_value() && now >= *value.expires_at;
}

bool Store::is_expired(const Value& value) {
    return is_expired(value, SteadyClock::now());
}

void Store::set_expiration(
    Shard& shard,
    Entry& entry,
    std::optional<SteadyClock::time_point> expires_at) {
    auto& value = entry.second;
    value.expires_at = expires_at;
    if (expires_at.has_value()) {
        if (value.expiring_index == kNotTracked) {
            value.expiring_index = shard.expiring.size();
            shard.expiring.push_back(&entry);
        }
    } else {
        untrack(shard, value);
    }
}

void Store::untrack(Shard& shard, Value& value) {
    if (value.expiring_index == kNotTracked) {
        return;
    }
    const auto index = value.expiring_index;
    Entry* last = shard.expiring.back();
    shard.expiring[index] = last;
    last->second.expiring_index = index;
    shard.expiring.pop_back();
    value.expiring_index = kNotTracked;
}

Store::Map::iterator Store::erase_entry(Shard& shard, Map::iterator it) {
    untrack(shard, it->second);
    return shard.values.erase(it);
}

void Store::erase_if_expired(const std::string& key) {
    auto& shard = shard_for(key);
    std::unique_lock lock(shard.mutex);
    auto it = shard.values.find(key);
    if (it != shard.values.end() && is_expired(it->second)) {
        erase_entry(shard, it);
    }
}

Store::Shard& Store::shard_for(const std::string& key) {
    return shards_[std::hash<std::string>{}(key) % kShardCount];
}

const Store::Shard& Store::shard_for(const std::string& key) const {
    return shards_[std::hash<std::string>{}(key) % kShardCount];
}

}  // namespace miniredis
