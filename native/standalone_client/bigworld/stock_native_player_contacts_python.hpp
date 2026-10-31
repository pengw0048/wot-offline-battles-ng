#pragma once
#include "stock_native_player_contacts.hpp"
#include "stock_native_bot_human_snapshot.hpp"
#include "stock_native_client_state.hpp"
namespace stock_native_player_contacts {
// Plain accepted ingress only: no descriptor producer or policy callback executes on this hot boundary.
bool readPlayerContacts(const stock_native_client_state::Value& players,
    const std::map<ID,stock_native_bot::HumanDescriptor>& descriptors,
    std::vector<Request>& output,std::uint64_t& rejected);
// [{serial, player_id, seq, accepted, token}] for each verdict.
stock_native_client_state::Value packPlayerVerdicts(const std::vector<Verdict>&);
}
