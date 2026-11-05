#include "room_core.hpp"
#include "room_inputs.hpp"
#include "room_bot_rows.hpp"
#include "room_lobby_policy.hpp"
#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>

namespace wot::room {
namespace {
std::string string(const Value& value, const char* name, const std::string& fallback = {}) {
    const auto& field = value.get(name).resolved();
    return field.kind == Value::Kind::Text ? field.text : fallback;
}
Value roundedField(const Value& raw, int places) {
    std::ostringstream formatted; formatted.imbue(std::locale::classic());
    formatted << std::fixed << std::setprecision(places) << raw.numeric();
    std::istringstream parsed(formatted.str()); parsed.imbue(std::locale::classic());
    double value = 0.; parsed >> value; return number(value);
}
void require(bool okay, const char* why) { if (!okay) throw std::invalid_argument(why); }
}
Core::Core(Value configuration, Value resources, Publish publish, Disconnect disconnect, Resync resync, EffectiveParamsValidator parameters)
    : rules_(state_), planner_(state_), publish_(std::move(publish)), disconnect_(std::move(disconnect)),
      resync_(std::move(resync)), random_(std::random_device{}()), parameters_(std::move(parameters)) {
    require(bool(parameters_), "Native room parameter validator is missing");
    require(intField(configuration, "schema") == 1, "Unknown room configuration schema");
    require(intField(resources, "schema") == 1, "Unknown native room data schema");
    require(string(resources, "gameplay_revision") == "abd03c4a91366f52ff6cb81d6b8b56539ea3c5c0", "Native room gameplay data mismatch");
    require(!rows(resources.get("lobby").get("map_pool")).empty(), "Native room map pool is empty");
    require(resources.get("public_player_defaults").resolved().kind == Value::Kind::Object, "Native room actor defaults are missing");
    state_.config = std::move(configuration); state_.resources = std::move(resources);
    state_.round_id = 1;
    auto& c = state_.config;
    c.object["team_classes"] = state_.resources.get("lobby").get("team_classes").resolved();
    for (auto& row : c.object["team_classes"].object) {
        row.second = row.second.resolved();
        row.second.object["order"] = row.second.get("order").resolved();
    }
    c.object["bot_tier_mode"] = text("random"); c.object["bot_skill_mode"] = text("mixed");
    c.object["battle_mode"] = text("regular"); c.object["training_bots"] = boolean(false);
    lobby::normalizeConfiguration(c, state_.resources);
    c.object["prebattle_seconds"] = state_.resources.get("timing").get("prebattle_seconds");
    c.object["battle_duration_seconds"] = state_.resources.get("timing").get("battle_duration_seconds");
    std::ostringstream identity; identity << std::hex << std::setfill('0');
    for (int n = 0; n < 4; ++n) identity << std::setw(8) << random_();
    c.object["receipt_namespace"] = text(identity.str());
    c.object["receipt_arena_prefix"] = integer(random_());
    const auto& maps = rows(state_.resources.get("lobby").get("map_pool"));
    state_.map_name = maps[std::uniform_int_distribution<std::size_t>(0, maps.size() - 1)(random_)].resolved().text;
    inputs_ = std::make_unique<Inputs>(state_, [this](std::int64_t id, const Value& previous, const Value& message) {
        rules_.actorAdvanced("player", id, previous, message);
    });
    rules_.reset(); planner_.reset();
    makeRoster();
}
Core::~Core() = default;
Value Core::teamSizes() const {
    auto result = object();
    for (int team : {1, 2}) result.object[std::to_string(team)] = integer(slots(team).size());
    return result;
}
Value Core::publicActor(const Value& actor, bool bot, bool full) const {
    const auto name = bot ? (full ? "BotRowFull" : "BotRow") : (full ? "PlayerRowFull" : "PlayerRow");
    const auto& fields = state_.resources.get("wire_records").get(name);
    if (fields.resolved().kind != Value::Kind::Array) throw std::invalid_argument("Missing native room actor field contract");
    auto out = object();
    for (const auto& field : rows(fields)) {
        const auto& name = field.resolved().text;
        if (has(actor, name)) out.object.emplace(name, actor.get(name));
    }
    if (!bot) {
        for (const char* name : {"x", "y", "z", "forward", "turn", "speed"})
            if (has(actor, name)) out.object[name] = roundedField(actor.get(name), 4);
        for (const char* name : {"yaw", "pitch", "roll", "aim_yaw", "gun_pitch"})
            if (has(actor, name)) out.object[name] = roundedField(actor.get(name), 5);
        if (has(actor, "up_cosine")) out.object["up_cosine"] = roundedField(actor.get("up_cosine"), 6);
        const auto team = intField(actor, "team"), slot = intField(actor, "slot", -1);
        const bool assigned = (team == 1 || team == 2) && slot >= 0 && slot < 15;
        out.object["spawn_x"] = number(assigned ? double(slot) * 12. : 0.);
        out.object["spawn_z"] = number(assigned ? (team == 1 ? -35. : 35.) : 0.);
        if (actor.get("display_health").resolved().kind == Value::Kind::Null) out.object["display_health"] = actor.get("health");
        if (intField(actor, "gun_checkpoint_seq") <= 0) { out.object.erase("gun_checkpoint_seq"); out.object.erase("gun_checkpoint"); }
        for (const char* name : {"critical", "ram_contact", "ram_contacts", "destructible_contacts", "destructible_contact_resolved_seqs", "destructible_contact_rejected_seqs"})
            if (!actor.get(name).truth()) out.object.erase(name);
        if (has(actor, "client_position")) out.object["world_pose"] = actor.get("client_position");
    }
    return out;
}
Value Core::lobby(const char* type) const {
    auto out = object();
    out.object["type"] = text(type); out.object["protocol"] = state_.resources.get("protocol");
    out.object["client_build"] = state_.resources.get("client_build");
    out.object["phase"] = text(state_.phase); out.object["round_id"] = integer(state_.round_id);
    out.object["state_revision"] = integer(state_.state_revision); out.object["map"] = text(state_.map_name);
    out.object["map_pool"] = state_.resources.get("lobby").get("map_pool");
    out.object["host_player_id"] = host_player_ ? integer(host_player_) : Value{};
    out.object["bot_authority_id"] = worker_ready_ ? integer(-1) : Value{};
    out.object["authority_epoch"] = integer(state_.authority_epoch);
    out.object["worker_status"] = text(worker_ready_ ? "connected" : "missing");
    out.object["worker_failure_reason"] = text("");
    out.object["team_sizes"] = teamSizes();
    out.object["team_size"] = integer(std::max(slots(1).size(), slots(2).size()));
    out.object["lineup_locked"] = boolean(std::any_of(rows(state_.config.get("bot_lineup")).begin(), rows(state_.config.get("bot_lineup")).end(), [](const Value& v) { return has(v, "enabled"); }));
    out.object["team_classes"] = state_.config.get("team_classes"); out.object["team_class_notice"] = state_.config.get("team_class_notice");
    out.object["bot_tier_mode"] = state_.config.get("bot_tier_mode"); out.object["bot_skill_mode"] = state_.config.get("bot_skill_mode");
    auto players = array();
    for (const auto& row : state_.players) if (row.second.get("connected").truth()) players.array.push_back(publicActor(row.second, false, true));
    out.object["players"] = std::move(players);
    return out;
}
Value Core::welcome(const Connection& connection) const {
    auto out = lobby("welcome");
    out.object["capabilities"] = array();
    for (const auto& cap : connection.capabilities) out.object["capabilities"].array.push_back(text(cap));
    out.object["server_capabilities"] = state_.resources.get("server_capabilities");
    auto identity = object(); identity.object["schema"] = integer(1);
    identity.object["semanticVersion"] = state_.config.get("semantic_version");
    identity.object["buildIdentity"] = state_.config.get("build_identity");
    out.object["ng_identity"] = std::move(identity);
    out.object["server_time_ms"] = integer(state_.server_time_ms);
    if (connection.sender.worker) {
        out.object["role"] = text("simulation_worker"); out.object["worker_id"] = integer(-1);
    } else if (!connection.probe) {
        const auto* actor = state_.actor("player", connection.sender.id);
        if (!actor) throw std::logic_error("Visible welcome has no admitted actor");
        out.object["player_id"] = integer(connection.sender.id);
        for (const char* name : {"name", "vehicle", "vehicle_compact_descr", "outfits", "effective_params",
                                 "team", "slot", "requested_team", "max_health"}) out.object[name] = actor->get(name);
        auto spawn = object();
        for (const char* axis : {"x", "y", "z", "yaw"}) spawn.object[axis] = actor->get(axis);
        out.object["spawn"] = std::move(spawn);
    }
    return out;
}
bool Core::flushPending() {
    for (auto& peer : peers_) flush(peer.second, peer.first);
    const auto worker = peers_.find(worker_peer);
    return worker == peers_.end() || worker->second.pending.empty();
}
void Core::flush(Connection& connection, Peer peer) {
    while (!connection.pending.empty()) {
        auto& front = connection.pending.front();
        if (!publish_(peer, front.message, front.key)) return;
        connection.pending.pop_front();
    }
}
void Core::emit(Peer peer, Value value, std::optional<std::string> key) {
    const auto found = peers_.find(peer); if (found == peers_.end()) return;
    auto& pending = found->second.pending;
    auto frozen = freeze(std::move(value));
    if (key && !pending.empty() && pending.back().key == key) pending.back().message = std::move(frozen);
    else pending.push_back({std::move(frozen), std::move(key)});
    flush(found->second, peer);
}
void Core::broadcast(Value value, std::optional<std::string> key) {
    auto frozen = freeze(std::move(value));
    for (auto& row : peers_) if (!row.second.probe) {
        auto& pending = row.second.pending;
        if (key && !pending.empty() && pending.back().key == key) pending.back().message = frozen;
        else pending.push_back({frozen, key});
        flush(row.second, row.first);
    }
}
void Core::error(Peer peer, const std::string& code, const std::string& detail) {
    auto out = object(); out.object["type"] = text("error"); out.object["code"] = text(code); out.object["message"] = text(detail);
    if (peers_.count(peer)) emit(peer, std::move(out)); else publish_(peer, freeze(std::move(out)), {});
}
void Core::hello(Peer peer, const Value& message, const std::string& address) {
    require(string(message, "type") == "hello", "First room message must be hello");
    require(message.get("protocol") == state_.resources.get("protocol"), "Room protocol mismatch");
    require(message.get("client_build") == state_.resources.get("client_build"), "Unsupported client build");
    Connection connection;
    const auto role = string(message, "role", "player");
    connection.probe = role == "probe"; connection.sender.worker = role == "simulation_worker";
    require(role == "probe" || role == "player" || role == "simulation_worker", "Unsupported room role");
    require(connection.sender.worker == (peer == worker_peer), "Only the internal worker may own simulation");
    for (const auto& cap : rows(message.get("capabilities"))) {
        require(cap.kind == Value::Kind::Text, "Invalid capability name"); connection.capabilities.insert(cap.text);
    }
    for (const auto& cap : rows(state_.resources.get("required_capabilities"))) require(connection.capabilities.count(cap.text) != 0, "Missing required room capability");
    if (connection.sender.worker) {
        require(!worker_ready_, "Internal worker already registered");
        connection.sender.id = -1; worker_ready_ = true; ++state_.authority_epoch;
    } else if (!connection.probe) {
        require(state_.phase == "waiting", "Round already started");
        require(std::count_if(state_.players.begin(), state_.players.end(), [](const auto& p) { return p.second.get("connected").truth(); }) < 30, "Room is full");
        connection.sender.id = next_player_;
        auto actor = admitPlayer(message, connection.sender.id, address);
        state_.players.emplace(connection.sender.id, std::move(actor));
        ++next_player_;
        if (!host_player_) host_player_ = connection.sender.id;
    }
    peers_.emplace(peer, std::move(connection));
    auto& admitted = peers_.at(peer);
    if (!admitted.probe && !admitted.sender.worker) {
        assignSlots(); makeRoster(); ++state_.state_revision;
        resync_(peer, admitted.sender.id);
    }
    emit(peer, welcome(admitted));
    if (!admitted.probe) broadcast(lobby());
}
void Core::receive(Peer peer, const Value& message, const std::string& address) {
    if (stopped_) return;
    try {
        require(message.resolved().kind == Value::Kind::Object, "Room message is not an object");
        const auto found = peers_.find(peer);
        if (found == peers_.end()) hello(peer, message, address); else command(peer, found->second, message);
    } catch (const std::exception& e) {
        // Recoverable operation rejection must not become a fatal worker-link
        // error. Admitted operations publish their typed terminal separately.
        if (log_) log_("NATIVE ROOM rejected peer=" + std::to_string(peer) + ": " + e.what());
        if (!peers_.count(peer)) {
            const auto* rejection = dynamic_cast<const Rejection*>(&e);
            error(peer, rejection ? rejection->code : "invalid_message", e.what());
            disconnect_(peer, true);
        }
    }
}
}
