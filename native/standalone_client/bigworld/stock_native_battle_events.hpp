#pragma once
// Ordered combat events: the accepted journal, combat application and its stock feedback.
#include "stock_native_battle.hpp"
#include <deque>
#include <unordered_set>

namespace native_battle {
// Membership over the most recent ids only: event ids arrive in order, and the client runs in a 2 GB address space.
class RecentIds {
    std::size_t limit_;
    std::unordered_set<std::string> ids_;
    std::deque<std::string> order_;
public:
    explicit RecentIds(std::size_t limit = 8192) : limit_(limit ? limit : 1) {}
    bool add(const std::string& id);
    bool contains(const std::string& id) const { return ids_.count(id) != 0; }
    std::size_t size() const { return ids_.size(); }
};

struct Events {
    std::vector<Value> journal;
    RecentIds accepted, applied;
    std::optional<std::int64_t> local_last_attacker;
    std::size_t effect_reports = 0, hit_impulse_reports = 0;
};

namespace events {
// The LAN `events` message; false when the round no longer accepts it.
bool receive(Battle& b, const Value& message);
// Applies journaled events whose vehicles are ready; true when the journal emptied.
bool drain(Battle& b);
// Whether a journaled combat event still targets this vehicle.
bool pendingCombatFor(const Battle& b, const Actor& vehicle);
// Whether any journaled event names this vehicle key.
bool references(const Battle& b, const std::string& key);
// A local kill reached the stock arena: the kill contract diagnostics and session statistics.
void reportLocalKill(Battle& b, const Actor& victim, std::int64_t attacker, std::int64_t reason);
}
}
