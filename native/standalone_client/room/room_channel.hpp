#pragma once
#include "room_value.hpp"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace wot::room {
// The engine and room exchange immutable native values. No serialization or
// interpreter work occurs on this boundary. A key may supersede only the last
// queued entry with the same key; an intervening event is always a barrier.
class Channel {
public:
    using Clock = std::chrono::steady_clock;
    enum class Admission { admitted, replaced, refused };
    enum class Refusal { none, capacity, message_size, closed };
    struct Entry {
        std::shared_ptr<const Value> message;
        std::optional<std::string> key;
        std::size_t charge = 0;
        Clock::time_point queued, received;
    };
    struct Metrics {
        std::size_t pending = 0, bytes = 0, peak_pending = 0, peak_bytes = 0;
        std::uint64_t admitted = 0, replaced = 0, refused = 0;
        std::uint64_t sizing_visits = 0, sizing_cache_hits = 0;
        double oldest_seconds = 0.;
        bool closed = false;
    };
    explicit Channel(std::size_t messages, std::size_t bytes);
    Admission offer(std::shared_ptr<const Value>, std::optional<std::string> key = {}, Refusal* reason = nullptr);
    std::vector<Entry> take(std::size_t limit);
    bool waitUntil(Clock::time_point deadline);
    void waitForChange(Clock::time_point deadline);
    void close();
    Metrics metrics() const;
private:
    const std::size_t max_messages_, max_bytes_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Entry> entries_;
    Metrics counts_;
    struct CachedSize { std::weak_ptr<const Value> owner; std::size_t bytes; unsigned depth; };
    std::mutex sizing_mutex_;
    std::unordered_map<const Value*, CachedSize> sizes_;
    std::size_t charge(const Value&, std::size_t budget, unsigned depth,
                       std::uint64_t& visits, std::uint64_t& hits);
    std::size_t chargeOwned(const std::shared_ptr<const Value>&, std::size_t budget,
                            unsigned depth, std::uint64_t& visits, std::uint64_t& hits);
};
}
