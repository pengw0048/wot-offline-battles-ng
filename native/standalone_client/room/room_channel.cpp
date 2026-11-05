#include "room_channel.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace wot::room {
std::size_t Channel::chargeOwned(const std::shared_ptr<const Value>& owner, std::size_t budget,
                               unsigned depth, std::uint64_t& visits, std::uint64_t& hits) {
    if (depth > 128) return budget + 1;
    const auto found = sizes_.find(owner.get());
    if (found != sizes_.end() && !found->second.owner.expired() && depth <= found->second.depth) {
        ++hits; return found->second.bytes <= budget ? found->second.bytes : budget + 1;
    }
    const auto size = charge(*owner, budget, depth, visits, hits);
    if (size <= budget && sizes_.size() < 512) sizes_[owner.get()] = {owner, size, depth};
    return size;
}
std::size_t Channel::charge(const Value& raw, std::size_t budget, unsigned depth,
                          std::uint64_t& visits, std::uint64_t& hits) {
    ++visits;
    if (depth > 128 || sizeof(Value) > budget) return budget + 1;
    if (raw.reference) return chargeOwned(raw.reference, budget, depth, visits, hits);
    const auto& v = raw.resolved();
    std::size_t used = sizeof(Value);
    const auto add = [&](std::size_t n) {
        if (n > budget - used) return false;
        used += n;
        return true;
    };
    if (!add(v.text.size())) return budget + 1;
    for (const auto& child : v.array) {
        const auto size = charge(child, budget - used, depth + 1, visits, hits);
        if (!add(size)) return budget + 1;
    }
    for (const auto& child : v.object) {
        // Account for key allocation and ordered-map linkage as well as data.
        if (!add(child.first.size() + sizeof(void*) * 4)) return budget + 1;
        const auto size = charge(child.second, budget - used, depth + 1, visits, hits);
        if (!add(size)) return budget + 1;
    }
    return used;
}
Channel::Channel(std::size_t messages, std::size_t bytes)
    : max_messages_(messages), max_bytes_(bytes) {
    if (!messages || !bytes || bytes == std::numeric_limits<std::size_t>::max())
        throw std::invalid_argument("Room channel limits must be finite and positive");
}
Channel::Admission Channel::offer(std::shared_ptr<const Value> message, std::optional<std::string> key, Refusal* reason) {
    if (reason) *reason = Refusal::none;
    std::uint64_t visits = 0, hits = 0;
    std::size_t bytes = max_bytes_ + 1;
    {
        // Consumers never need this producer-only cache lock. Weak owners do
        // not extend a snapshot or static section's lifetime.
        std::lock_guard<std::mutex> lock(sizing_mutex_);
        if (sizes_.size() >= 384) for (auto it = sizes_.begin(); it != sizes_.end();) {
            if (it->second.owner.expired()) it = sizes_.erase(it); else ++it;
        }
        if (message) bytes = chargeOwned(message, max_bytes_, 0, visits, hits);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    counts_.sizing_visits += visits; counts_.sizing_cache_hits += hits;
    if (counts_.closed || !message || bytes > max_bytes_) {
        if (reason) *reason = counts_.closed ? Refusal::closed : Refusal::message_size;
        ++counts_.refused;
        return Admission::refused;
    }
    const bool replace = key && !entries_.empty() && entries_.back().key == key;
    const auto previous = replace ? entries_.back().charge : 0;
    if ((!replace && entries_.size() >= max_messages_) || bytes > max_bytes_ - (counts_.bytes - previous)) {
        if (reason) *reason = Refusal::capacity;
        ++counts_.refused;
        return Admission::refused;
    }
    const auto now = Clock::now();
    Entry entry{std::move(message), std::move(key), bytes, now, now};
    if (replace) {
        // Queue age includes the superseded interval so sustained backlog is visible.
        entry.queued = entries_.back().queued;
        entries_.back() = std::move(entry);
        ++counts_.replaced;
    } else {
        entries_.push_back(std::move(entry));
        ++counts_.admitted;
    }
    counts_.bytes = counts_.bytes - previous + bytes;
    counts_.peak_pending = std::max(counts_.peak_pending, entries_.size());
    counts_.peak_bytes = std::max(counts_.peak_bytes, counts_.bytes);
    changed_.notify_one();
    return replace ? Admission::replaced : Admission::admitted;
}
std::vector<Channel::Entry> Channel::take(std::size_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Entry> out;
    out.reserve(std::min(limit, entries_.size()));
    while (limit-- && !entries_.empty()) {
        counts_.bytes -= entries_.front().charge;
        out.push_back(std::move(entries_.front()));
        entries_.pop_front();
    }
    if (!out.empty()) changed_.notify_all();
    return out;
}
void Channel::waitForChange(Clock::time_point deadline) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!counts_.closed) changed_.wait_until(lock, deadline);
}
bool Channel::waitUntil(Clock::time_point deadline) {
    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait_until(lock, deadline, [&] { return counts_.closed || !entries_.empty(); });
    return !entries_.empty();
}
void Channel::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    counts_.closed = true;
    changed_.notify_all();
}
Channel::Metrics Channel::metrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto out = counts_;
    out.pending = entries_.size();
    if (!entries_.empty()) out.oldest_seconds = std::chrono::duration<double>(Clock::now() - entries_.front().queued).count();
    return out;
}
}
