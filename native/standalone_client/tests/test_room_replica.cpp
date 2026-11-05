#include "../room/room_replica.hpp"
#include <cassert>
using namespace wot::room;
Value image(std::int64_t tick) {
    auto v = object(); v.object["type"] = text("snapshot"); v.object["round_id"] = integer(1);
    v.object["authority_epoch"] = integer(1); v.object["server_tick"] = integer(tick);
    v.object["players"] = array(); v.object["bots"] = array();
    for (const char* key : {"bot_manifest", "bot_orders", "destructibles"}) v.object[key] = array();
    auto meta = object();
    for (const char* key : {"manifest_revision", "order_revision", "destructible_revision"}) meta.object[key] = integer(1);
    for (const char* key : {"manifest_refresh_ticks", "order_refresh_ticks", "destructible_refresh_ticks"}) meta.object[key] = integer(30);
    v.object["_room_sections"] = std::move(meta); return v;
}
int main() {
    ReplicaEncoder encoder;
    auto first = encoder.prepare(image(1), 1);
    assert(first.wire.get("full").truth() && has(first.wire, "bot_manifest") && !has(first.wire, "_room_sections"));
    // Merely preparing/admitting a frame cannot advance a baseline.
    auto unsent = encoder.prepare(image(2), 1); assert(unsent.wire.get("full").truth());
    encoder.commit(std::move(first));
    auto delta = encoder.prepare(image(2), 1);
    assert(!delta.wire.get("full").truth());
    for (const char* key : {"bot_manifest", "bot_orders", "destructibles"}) assert(!has(delta.wire, key));
    encoder.commit(std::move(delta));
    assert(!has(encoder.prepare(image(30), 1).wire, "bot_manifest"));
    assert(has(encoder.prepare(image(31), 1).wire, "bot_manifest"));
    auto changed = image(3); auto order = object(); order.object["id"] = integer(4); order.object["shell_index"] = integer(2);
    changed.object["bot_orders"].array.push_back(order);
    auto sent = encoder.prepare(changed, 1); assert(has(sent.wire, "bot_orders")); encoder.commit(std::move(sent));
    changed.object["server_tick"] = integer(4); assert(!has(encoder.prepare(changed, 1).wire, "bot_orders"));
    changed.object["bot_orders"].array[0].object["shell_index"] = integer(1);
    assert(has(encoder.prepare(changed, 1).wire, "bot_orders"));
    changed.object["authority_epoch"] = integer(2); assert(encoder.prepare(changed, 1).wire.get("full").truth());
    assert(encoder.prepare(image(4), 1, true).wire.get("full").truth());
    encoder.reset(); assert(encoder.prepare(image(4), 1).wire.get("full").truth());
}
