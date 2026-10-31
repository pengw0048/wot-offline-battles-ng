// The battle world: destructible round state and events, contacts, ram Bot history, detached-turret death edges.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_world.hpp"
#include "stock_native_battle_world_imports.hpp"
#include "stock_native_battle_startup_needs.hpp"
#include "stock_native_battle_world_parts.hpp"
#include "stock_native_battle_local.hpp"
#include "stock_native_detached_turret_snapshot.hpp"
#include <algorithm>
#include <cmath>

namespace native_battle {
World::World() = default;
World::~World() = default;

namespace world {
namespace {
using Kind = Value::Kind;
constexpr std::size_t max_active_turrets = 12;
constexpr std::size_t failure_text_limit = 160;

World& state(Battle& b) {
    if (!b.world) throw p::Error("the native battle world is not started");
    return *b.world;
}
// Python int(value) for wire numbers; nullopt where int() would raise.
std::optional<std::int64_t> integer(const Value& raw) {
    const auto& v = raw.resolved();
    switch (v.kind) {
    case Kind::Integer: return v.integer;
    case Kind::Boolean: return std::int64_t(v.boolean);
    case Kind::Number:
        if (!std::isfinite(v.number) || std::abs(v.number) >= 9.2e18) return std::nullopt;
        return std::int64_t(std::trunc(v.number));
    case Kind::Text:
        try {
            std::size_t used = 0;
            const auto n = std::stoll(v.text, &used);
            return used == v.text.size() ? std::optional<std::int64_t>(n) : std::nullopt;
        } catch (...) { return std::nullopt; }
    default: return std::nullopt;
    }
}
// A plain wire number; anything else reads as zero (the native receipt reader's contract).
double plain(const Value& raw) {
    const auto& v = raw.resolved();
    if (v.kind == Kind::Integer) return double(v.integer);
    if (v.kind == Kind::Number) return v.number;
    return 0.;
}
Value number(double v) { Value out; out.kind = Kind::Number; out.number = v; return out; }
Value boolean(bool v) { Value out; out.kind = Kind::Boolean; out.boolean = v; return out; }
Value text(std::string v) { Value out; out.kind = Kind::Text; out.text = std::move(v); return out; }
Value owned(const Value& v) { return v.reference ? owned(*v.reference) : v; }
// An owned, writable object (shared accepted payloads are immutable).
Value& writable(Value& v) {
    if (v.reference) v = owned(v);
    if (v.kind == Kind::Null) v.kind = Kind::Object;
    return v;
}
// One single-line diagnostic, bounded for logs and the wire.
std::string bounded(const std::string& raw) {
    std::string out;
    bool space = false;
    for (char c : raw) {
        if (std::isspace(static_cast<unsigned char>(c))) { space = !out.empty(); continue; }
        if (space) { out += ' '; space = false; }
        out += c;
    }
    if (out.empty()) out = "RuntimeError";
    return out.substr(0, failure_text_limit);
}
bool truth(PyObject* v) { return p::flag(v); }

void eraseTimeline(RamBotHistory& ram, std::int64_t revision, std::int64_t time_us, const std::map<std::int64_t, Value>& bots) {
    const auto key = std::make_pair(time_us, revision);
    for (const auto& row : bots) {
        const auto found = ram.timelines.find(row.first);
        if (found == ram.timelines.end()) continue;
        auto& timeline = found->second;
        const auto at = std::lower_bound(timeline.begin(), timeline.end(), key);
        if (at != timeline.end() && *at == key) timeline.erase(at);
        if (timeline.empty()) ram.timelines.erase(found);
    }
}

// The player's `effective_params` and contact list arrive exactly as one admitted worker contact.
bool exactKeys(const Value& object, std::initializer_list<const char*> names) {
    const auto& v = object.resolved();
    if (v.kind != Kind::Object || v.object.size() != names.size()) return false;
    for (const char* name : names) if (!v.object.count(name)) return false;
    return true;
}
}

void disableDestructibles(Battle& b, const std::string& error) {
    auto& w = state(b);
    // Native crushing, catalog contacts and catalog shots stop for the rest of the round on this peer.
    w.catalog.reset();
    withdrawCatalog(b);
    b.warn("destructible interactions", error);
}

bool observeDestructiblesDisabled(Battle& b, const Value& message) {
    const auto& m = message.resolved();
    const auto& flag = m.get("destructibles_disabled").resolved();
    if (m.kind != Kind::Object || flag.kind != Kind::Boolean || !flag.boolean) return false;
    auto& w = state(b);
    auto& start = writable(b.start);
    start.object["destructibles_disabled"] = boolean(true);
    const auto& raw = m.get("destructibles_disabled_reason").resolved();
    const std::string reason = bounded(raw.kind == Kind::Text && !raw.text.empty() ? raw.text : "server disabled destructible interactions");
    start.object["destructibles_disabled_reason"] = text(reason);
    w.destructibles_disabled = true;
    w.destructibles_disabled_reason = reason;
    if (b.featureEnabled("destructible interactions"))
        disableDestructibles(b, "RuntimeError: LAN server disabled destructible interactions: " + reason);
    return true;
}

bool applyDestructibleState(Battle& b, const Value& events) {
    const auto& rows = events.resolved();
    if (rows.kind != Kind::Array) throw p::Error("canonical destructible state is malformed");
    bool changed = false;
    for (const auto& event : rows.array) {
        if (event.resolved().kind != Kind::Object) throw p::Error("canonical destructible event is malformed");
        changed = ingressDestructible(b, event) || changed;
    }
    return changed;
}

bool rememberRamBotSnapshot(Battle& b, const Value& snapshot) {
    // A visible client's native peers and trigger ledgers retain the accepted receipts themselves.
    if (!b.worker && !b.replay) return false;
    const auto& s = snapshot.resolved();
    if (s.kind != Kind::Object || !b.bots) return false;
    const auto revision = integer(s.get("bot_state_revision")), time_us = integer(s.get("bot_state_time_us"));
    if (!revision || !time_us || *revision < 0 || *time_us < 0) return false;
    std::map<std::int64_t, Value> bots;
    const auto& rows = s.get("bots").resolved();
    if (rows.kind == Kind::Array) {
        for (const auto& raw : rows.array) {
            const auto& row = raw.resolved();
            if (row.kind != Kind::Object || row.get("id").resolved().kind == Kind::Null) continue;
            const auto id = integer(row.get("id"));
            if (!id) continue;
            // Pose fields come from this wire revision; descriptor-derived fields may come from the newer local state.
            Value body;
            body.kind = Kind::Object;
            if (const Value* current = bots::currentState(b, *id)) {
                const auto& now = current->resolved();
                for (const char* name : {"mass", "collision_shape", "ram_profile", "vehicle", "team"})
                    if (now.object.count(name)) body.object[name] = now.get(name);
            }
            for (const auto& field : row.object) body.object[field.first] = field.second;
            bots[*id] = std::move(body);
        }
    }
    auto& ram = state(b).ram;
    const auto previous = ram.revisions.find(*revision);
    if (previous != ram.revisions.end()) eraseTimeline(ram, *revision, previous->second.time_us, previous->second.bots);
    else ram.order.push_back(*revision);
    auto& entry = ram.revisions[*revision];
    entry.time_us = *time_us;
    entry.bots = std::move(bots);
    const auto key = std::make_pair(*time_us, *revision);
    for (const auto& row : entry.bots) {
        auto& timeline = ram.timelines[row.first];
        timeline.insert(std::upper_bound(timeline.begin(), timeline.end(), key), key);
    }
    // The server admits receipts up to 255 revisions behind; keep headroom on both sides of a skipped revision.
    while (ram.order.size() > RamBotHistory::limit) {
        const auto expired = ram.order.front();
        ram.order.pop_front();
        const auto found = ram.revisions.find(expired);
        if (found == ram.revisions.end()) continue;
        eraseTimeline(ram, expired, found->second.time_us, found->second.bots);
        ram.revisions.erase(found);
    }
    return true;
}

std::optional<Value> ramBotState(const Battle& b, std::int64_t bot_id, std::int64_t revision, std::int64_t time_us) {
    if (!b.world) return std::nullopt;
    const auto& ram = b.world->ram;
    const auto timeline = ram.timelines.find(bot_id);
    if (timeline == ram.timelines.end()) return std::nullopt;
    struct Sample { std::int64_t time, revision; const Value* state; };
    std::vector<Sample> samples;
    for (const auto& entry : timeline->second) {
        const auto found = ram.revisions.find(entry.second);
        const Value* body = nullptr;
        if (found != ram.revisions.end()) {
            const auto bot = found->second.bots.find(bot_id);
            if (bot != found->second.bots.end()) body = &bot->second;
        }
        samples.push_back({entry.first, entry.second, body});
    }
    const auto key = std::make_pair(time_us, revision);
    const auto lower = std::lower_bound(samples.begin(), samples.end(), key,
                                        [](const Sample& a, const std::pair<std::int64_t, std::int64_t>& k) { return std::make_pair(a.time, a.revision) < k; });
    const auto upper = std::upper_bound(samples.begin(), samples.end(), key,
                                        [](const std::pair<std::int64_t, std::int64_t>& k, const Sample& a) { return k < std::make_pair(a.time, a.revision); });
    if (lower == samples.end() || upper == samples.begin()) return std::nullopt;
    const std::size_t li = std::size_t(upper - samples.begin()) - 1, ri = std::size_t(lower - samples.begin());
    const auto& l = samples[li];
    const auto& r = samples[ri];
    if (l.time > time_us || l.revision > revision || r.time < time_us || r.revision < revision || !l.state || !r.state ||
        l.state->resolved().kind != Kind::Object || r.state->resolved().kind != Kind::Object)
        return std::nullopt;
    const auto& left = l.state->resolved();
    const auto& right = r.state->resolved();
    Value out = owned(left);
    double span = double(r.time - l.time);
    if (span > 0.) {
        const double progress = std::max(0., std::min(1., double(time_us - l.time) / span));
        for (const char* name : {"x", "y", "z", "pitch", "roll", "aim_yaw", "gun_pitch", "yaw"}) {
            if (!left.object.count(name) || !right.object.count(name)) continue;
            const double a = plain(left.get(name));
            double delta = plain(right.get(name)) - a;
            if (std::string(name) == "yaw") {
                const double pi = 3.14159265358979323846;
                delta = std::fmod(delta + pi, 2 * pi);
                if (delta < 0) delta += 2 * pi;
                delta -= pi;
            }
            out.object[name] = number(a + delta * progress);
        }
        if (progress >= 1.) out.object["alive"] = boolean(right.get("alive").truth(true));
    }
    const Sample* before = &l;
    const Sample* after = &r;
    if (span == 0. && samples.size() >= 2) {
        before = &samples[li ? li - 1 : li];
        after = &samples[li ? li : li + 1];
        span = double(after->time - before->time);
    }
    for (const char* axis : {"x", "y", "z"}) {
        const double v = span > 0. && before->state && after->state
            ? (plain(after->state->resolved().get(axis)) - plain(before->state->resolved().get(axis))) * 1000000. / span : 0.;
        out.object[std::string("ram_v") + axis] = number(v);
    }
    // Contact motion rows (21 wide): the newest sequence per actor at or before the receipt time.
    std::map<std::int64_t, const Value*> motion;
    for (const Value* list : {l.time == r.time ? nullptr : &left.get("ram_motion"), &right.get("ram_motion")}) {
        if (!list || list->resolved().kind != Kind::Array) continue;
        for (const auto& raw : list->resolved().array) {
            const auto& row = raw.resolved();
            if (row.kind != Kind::Array || row.array.size() != 21) continue;
            const auto& id = row.array[0].resolved();
            const auto& seq = row.array[1].resolved();
            const auto& stamp = row.array[2].resolved();
            auto whole = [](const Value& v, std::int64_t& out) {
                if (v.kind == Kind::Integer) { out = v.integer; return true; }
                if (v.kind == Kind::Number && std::isfinite(v.number) && std::abs(v.number) <= 9007199254740991.) { out = std::int64_t(v.number); return true; }
                return false;
            };
            std::int64_t actor = 0, sequence = 0, at = 0;
            if (!whole(id, actor) || !whole(seq, sequence) || !whole(stamp, at) || at > time_us) continue;
            const auto old = motion.find(actor);
            std::int64_t prior = 0;
            if (old == motion.end() || (whole(old->second->resolved().array[1].resolved(), prior) && sequence > prior)) motion[actor] = &row;
        }
    }
    Value rows;
    rows.kind = Kind::Array;
    for (const auto& row : motion) rows.array.push_back(owned(*row.second));
    out.object["ram_motion"] = std::move(rows);
    return out;
}

void destructibleContact(Battle& b, const Value& message) {
    if (!b.worker || !b.running()) return;
    const auto& m = message.resolved();
    static const std::set<std::string> required{"type", "protocol", "round_id", "authority_epoch", "player"};
    static const std::set<std::string> transport{"_client_received_time", "_client_dispatch_delay"};
    bool shape = m.kind == Kind::Object;
    if (shape) {
        for (const auto& name : required) shape = shape && m.object.count(name);
        for (const auto& field : m.object) shape = shape && (required.count(field.first) || transport.count(field.first));
    }
    if (!shape) throw p::Error("worker player destructible contact is malformed");
    const auto& player = m.get("player").resolved();
    const auto& contacts = player.get("destructible_contacts").resolved();
    if (!exactKeys(player, {"id", "vehicle", "vehicle_compact_descr", "effective_params", "destructible_contacts"}) ||
        player.get("effective_params").resolved().kind != Kind::Object || contacts.kind != Kind::Array || contacts.array.size() != 1)
        throw p::Error("worker player destructible contact body is malformed");
    const auto epoch = integer(m.get("authority_epoch")), player_id = integer(player.get("id"));
    if (!epoch || !player_id) throw p::Error("worker player destructible contact has invalid values");
    const auto local_epoch = p::wide(py::attr(b.engine.client.get(), "authority_epoch").get());
    if (m.get("round_id") != b.start.get("round_id") || *epoch != local_epoch || *player_id <= 0)
        throw p::Error("worker player destructible contact violates its contract");
    Value players;
    players.kind = Kind::Array;
    players.array.push_back(owned(player));
    bots::playerContacts(b, players, b.clock());
}

void destructibleContactResult(Battle& b, const Value& message) {
    const auto& m = message.resolved();
    if (b.worker || m.kind != Kind::Object) return;
    static const std::set<std::string> required{"type", "round_id", "contact_seq", "accepted"};
    static const std::set<std::string> pose{"x", "y", "z", "yaw"};
    std::set<std::string> fields;
    for (const auto& field : m.object)
        if (field.first != "_client_received_time" && field.first != "_client_dispatch_delay") fields.insert(field.first);
    std::set<std::string> with_pose(required);
    with_pose.insert(pose.begin(), pose.end());
    const auto& accepted = m.get("accepted").resolved();
    if ((fields != required && fields != with_pose) || m.get("type").resolved().text != "player_destructible_contact_result" ||
        m.get("round_id") != b.start.get("round_id") || accepted.kind != Kind::Boolean || accepted.boolean)
        return;
    const auto sequence = integer(m.get("contact_seq"));
    if (!sequence || *sequence <= 0) return;
    std::optional<std::pair<Point, double>> server_pose;
    if (fields == with_pose) {
        double values[4];
        unsigned i = 0;
        for (const char* name : {"x", "y", "z", "yaw"}) {
            const auto& v = m.get(name).resolved();
            if (v.kind != Kind::Integer && v.kind != Kind::Number && v.kind != Kind::Boolean) return;
            values[i] = v.kind == Kind::Integer ? double(v.integer) : v.kind == Kind::Boolean ? double(v.boolean) : v.number;
            if (!std::isfinite(values[i])) return;
            ++i;
        }
        server_pose = std::make_pair(Point{values[0], values[1], values[2]}, values[3]);
    }
    local::destructibleRejection(b, *sequence, server_pose);
}

std::int64_t ammoBaySpecialHealth(Battle& b, PyObject* entity) {
    auto special = py::attr(b.engine.constants.get(), "SPECIAL_VEHICLE_HEALTH");
    const bool marked = truth(py::optional(entity, "isTurretMarkedForDetachment").get());
    return p::wide(py::attr(special.get(), marked ? "TURRET_DETACHED" : "AMMO_BAY_DESTROYED").get());
}

std::int64_t markTurretDetached(Battle& b, PyObject* entity) {
    // Vehicle.isTurretDetached is IS_TURRET_DETACHED(health) plus the private confirmation flag; idempotent.
    if (truth(py::optional(entity, "isTurretDetached").get())) {
        auto health = py::optional(entity, "health");
        return health.get() == Py_None ? 0 : p::wide(health.get());
    }
    auto special = py::attr(b.engine.constants.get(), "SPECIAL_VEHICLE_HEALTH");
    auto detached = py::attr(special.get(), "TURRET_DETACHED");
    py::set(entity, "health", detached.get());
    py::set(entity, "_Vehicle__turretDetachmentConfirmed", Py_True);
    return p::wide(detached.get());
}

bool detachTurret(Battle& b, Actor& vehicle, PyObject* entity, const Value* state) {
    const bool changed = !truth(py::optional(entity, "isTurretDetached").get());
    markTurretDetached(b, entity);
    // The worker freezes the server-accepted debris flight every peer replays.
    if (b.worker) bots::detachedQueue(b, vehicle, entity, state);
    return changed;
}

bool detachLateAmmoTurret(Battle& b, Actor& vehicle, PyObject* entity, const Value* state) {
    if (!detachTurret(b, vehicle, entity, state)) return false;
    if (b.worker) return true;
    // Rebuild the visible wreck without its turret once: the damage-state data, then confirmTurretDetachment.
    auto appearance = py::optional(entity, "appearance");
    auto damage = py::optional(appearance.get(), "damageState");
    auto update = py::optional(damage.get(), "update");
    auto confirm = py::optional(entity, "confirmTurretDetachment");
    auto water = py::optional(appearance.get(), "waterSensor");
    if (!py::callable(update) || !py::callable(confirm) || water.get() == Py_None) return true;
    b.feature("late ammo-bay wreck refresh", [&] {
        auto health = py::attr(entity, "health");
        auto crew = py::attr(entity, "isCrewActive");
        auto under = py::attr(water.get(), "isUnderWater");
        py::invoke(update.get(), health.get(), crew.get(), under.get());
        py::invoke(confirm.get());
    }, false);
    return true;
}

bool wreckPrewarmReady(Battle& b, double vehicle_ready_deadline) {
    if (remote::wreckPrewarmPending(b) <= 0) return true;
    if (vehicle_ready_deadline <= 0. || b.clock() < vehicle_ready_deadline) return false;
    remote::abandonWreckPrewarm(b);
    return true;
}

bool reconcileDetachedTurrets(Battle& b, const Value& message) {
    if (bots::enabled(b)) {
        if (const auto accepted = bots::detachedIngress(b, message)) return *accepted;
    }
    const auto& m = message.resolved();
    if (m.kind != Kind::Object || m.get("round_id") != b.start.get("round_id")) return false;
    const auto& rows = m.get("detached_turrets").resolved();
    if (rows.kind != Kind::Array) return false;
    auto& turrets = state(b).turrets;
    bool changed = false;
    for (std::size_t i = 0; i < rows.array.size() && i < max_active_turrets; ++i) {
        stock_native_detached_turret::Row row;
        p::Ref raw(cs::write(rows.array[i]));
        if (!stock_native_detached_turret::readRow(raw.get(), row)) { PyErr_Clear(); continue; }
        const std::string key = std::string(row.owner.human ? "player:" : "bot:") + std::to_string(row.owner.id);
        const auto previous = turrets.rows.find(key);
        if (previous != turrets.rows.end() && row.motion_sequence <= previous->second.motion_sequence) continue;
        if (previous == turrets.rows.end()) {
            if (turrets.rows.size() >= max_active_turrets) break;
            turrets.order.push_back(key);
        }
        turrets.rows[key] = std::move(row);
        turrets.geometry.erase(key);
        changed = true;
    }
    if (changed && !b.worker) presentTurretRows(b);
    return changed;
}

void detachedTurrets(Battle& b, double now) {
    if (!b.worker) { advanceTurretPresentation(b, now); return; }
    if (!bots::enabled(b)) throw p::Error("worker detached turrets need the native Bot runtime");
    bots::detachedAdvance(b, now);
}

void prewarm(Battle& b, double now) {
    auto& w = state(b);
    const bool countdown = !b.live && b.prebattle_deadline >= 0.;
    const bool enabled = !w.destructibles_disabled && b.featureEnabled("destructible interactions");
    if (enabled && (countdown || (b.worker && b.live))) {
        try {
            if (bots::enabled(b)) bots::prewarmPlayers(b, now);
            // Register countdown tree identities around the player's own spawn.
            else if (!b.worker && b.local && b.localVehicle()) prewarmTrees(b, b.local->position, b.local->yaw);
        } catch (const std::exception&) {
            // One not-yet-streamed native item is local to this pose; the next frame retries.
            PyErr_Clear();
        }
    }
    try {
        if (bots::enabled(b)) bots::prewarmBots(b, now);
    } catch (const std::exception& e) {
        b.warn("bot destructible prewarm", describe(e));
    }
}

void start(Battle& b, const std::string& config_dir) {
    b.world = std::make_unique<World>();
    auto& w = *b.world;
    try {
        loadFoliage(b, config_dir);
    } catch (const std::exception& e) {
        w.foliage.reset();
        b.warn("foliage camouflage", describe(e));
    }
    const auto& map = b.config.get("map").resolved().text;
    try {
        w.catalog = loadDestructibleCatalog(config_dir, map);
        if (!w.catalog) throw p::Error("validated destructible catalog is unavailable for " + shortMapName(map));
    } catch (const std::exception& e) {
        disableDestructibles(b, describe(e));
    }
    // The server's round-wide optional-feature decision in the start message.
    observeDestructiblesDisabled(b, b.start);
}

void retire(Battle& b) {
    if (!b.world) return;
    retireTurretPresentation(b);
    b.world->turrets.obstacles.clear();
    closeSpace(b);
    b.world.reset();
}

bool keepNavigationSurface(const Surface& surface) {
    // 71..85 are original destructible materials; 87..100 are damaged replacements that stay solid.
    return surface.material < 71 || surface.material >= 86;
}

const stock_native_bot_turret::Obstacles* turretObstacles(const Battle& b) {
    if (b.worker || !b.world) return nullptr;
    return &b.world->turrets.obstacles;
}
}
}
