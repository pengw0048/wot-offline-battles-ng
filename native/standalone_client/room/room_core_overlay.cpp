#include "room_core.hpp"

namespace wot::room {
void Core::overlay(Peer peer, const Value& message) {
    const auto& archive = state_.resources.get("vehicle_overlay");
    if (message.get("type").resolved().text == "vehicle_overlay_query") {
        if (has(archive, "manifest")) { emit(peer, archive.get("manifest")); return; }
        auto payload = object(); payload.object["type"] = text("vehicle_overlay_manifest");
        payload.object["present"] = boolean(false); payload.object["digest"] = text("");
        payload.object["profile"] = text(""); payload.object["manifest"] = Value(); payload.object["members"] = array();
        emit(peer, std::move(payload)); return;
    }
    const auto& member = message.get("sourceMember").resolved();
    if (member.kind == Value::Kind::Text && has(archive.get("members"), member.text)) {
        emit(peer, archive.get("members").get(member.text)); return;
    }
    error(peer, "unknown_member", "unknown vehicle overlay member");
}
}
