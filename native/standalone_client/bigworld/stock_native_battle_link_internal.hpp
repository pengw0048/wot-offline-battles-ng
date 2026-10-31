#pragma once
// The worker link's sender state and outgoing message builder, shared by the transport and the senders.
#include "stock_native_battle_link.hpp"
#include <set>

namespace native_battle {
namespace link {
// What the worker's LAN client knows when it builds an outgoing message; the transport keeps it current.
struct SenderState {
    bool ready = false;   // welcome accepted
    std::string phase;    // waiting | loading | battle
    std::optional<std::int64_t> round_id, authority_epoch, bot_authority_id;
    std::set<std::string> capabilities, server_capabilities;
    // Sender-owned bookkeeping (per-round sequences and the like) follows; reset when round_id changes.
};
// The exact wire message to encode. A coalesced message replaces the newest queued unsent message with an equal key
// unless another keyed message or a bot_ram_report sits between them.
struct Outgoing {
    Value wire;
    std::optional<std::string> coalesce;
};
// Builds one link::send message as the former LAN client sender did; nullopt when that sender refused it.
std::optional<Outgoing> build(SenderState& s, const Value& message);
}
}
