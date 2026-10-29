#pragma once
#include "stock_native_player_contacts.hpp"
#include "stock_native_bot_human_snapshot.hpp"
struct _object;
namespace stock_native_player_contacts {
// Plain accepted ingress only: no descriptor producer, policy callback or
// custom Python numeric/sequence protocol executes on this hot boundary.
bool readPlayerContacts(_object* players,
    const std::map<ID,stock_native_bot::HumanDescriptor>& descriptors,
    std::vector<Request>& output,std::uint64_t& rejected);
_object* packPlayerVerdicts(const std::vector<Verdict>&);
}
