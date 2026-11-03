#pragma once
#include "stock_native_client_state.hpp"
#include <cstdint>
#include <optional>

namespace native_battle { namespace link {
// The visible client's timestamp is opaque to the worker's clock domain.
inline stock_native_client_state::Value workerProbeReply(const stock_native_client_state::Value& request,
    std::int64_t player,std::int64_t sequence,std::optional<double> frame_ms) {
    using V=stock_native_client_state::Value;
    V pong;pong.kind=V::Kind::Object;
    V type;type.kind=V::Kind::Text;type.text="worker_pong";pong.object["type"]=type;
    V id;id.kind=V::Kind::Integer;id.integer=player;pong.object["player_id"]=id;
    id.integer=sequence;pong.object["seq"]=id;
    pong.object["client_time"]=request.get("client_time");
    pong.object["round_id"]=request.get("round_id");pong.object["authority_epoch"]=request.get("authority_epoch");
    V frame;if(frame_ms){frame.kind=V::Kind::Number;frame.number=*frame_ms;}pong.object["frame_ms"]=frame;
    return pong;
}
}}
