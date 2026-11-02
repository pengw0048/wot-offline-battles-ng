// The local player's critical lineage: owner-CAS track repair, server revision reconciliation and the repair HUD.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_critical.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_client_critical.hpp"
#include "stock_native_client_drive_python.hpp"
#include <algorithm>
#include <cmath>

namespace native_battle {
// Provided by other subsystems.
namespace gun {
double activeRepairBonus(Battle& b);
void publishEquipment(Battle& b, double now);
}
namespace vehicles {
bool damageInfoServiceable(Battle& b);
bool showDamageInfo(Battle& b, std::int64_t engine_id, int damage_index, int extra_index, std::int64_t attacker_id);
}

namespace drive = stock_native_client_drive;
namespace core = stock_native_client_critical;

struct LocalCriticalOwner {
    std::shared_ptr<cs::State> state;
    std::shared_ptr<Actor> actor;
    std::uint64_t epoch = 0, descriptor_revision = 0, visual_sequence = 0;
    Object entity, descriptor;
    std::shared_ptr<drive::Status> status;
    std::shared_ptr<core::Owner> model;
    bool closed = false, busy = false;
    int progress_status = 0, repair_info = 0;
    std::int64_t visual_base = -1;
    std::map<std::string, int> extra_indexes;
    std::map<std::string, double> maxima;
    // Stock presentation edges retried until a call succeeds.
    struct Edge { std::string kind; bool left = false; int status = 0, code = 0, index = 0, extra = 0; double seconds = 0.; };
    std::map<std::string, Edge> pending;
    std::vector<std::string> order;
};

Critical::Critical() = default;
Critical::~Critical() = default;

namespace critical {
namespace {
using V = Value;
V text(const std::string& s) { V v; v.kind = V::Kind::Text; v.text = s; return v; }
V number(double n) { V v; v.kind = V::Kind::Number; v.number = n; return v; }
V object() { V v; v.kind = V::Kind::Object; return v; }
V array() { V v; v.kind = V::Kind::Array; return v; }
bool has(const V& v, const char* name) { const auto& r = v.resolved(); return r.kind == V::Kind::Object && r.object.count(name); }
bool track(const std::string& name) { return name == "leftTrackHealth" || name == "rightTrackHealth"; }
V& writable(V& state) {
    if (state.reference) { V copy = *state.reference; state = std::move(copy); }
    if (state.kind != V::Kind::Object) state.kind = V::Kind::Object;
    return state;
}

// The stock descriptor extra index Avatar uses for one device.
int extraIndex(PyObject* descriptor, const std::string& name) {
    auto extras = p::value(descriptor, "extras"), mapping = p::value(descriptor, "extrasDict");
    auto selected = mapping.get() != Py_None ? p::value(mapping.get(), name.c_str()) : p::retain(Py_None);
    auto matches = [&](PyObject* extra) {
        auto named = p::value(extra, "name");
        return extra == selected.get() || p::string(named.get()) == name;
    };
    if (PyDict_Check(extras.get())) {
        Py_ssize_t cursor = 0;
        PyObject *key, *value;
        while (PyDict_Next(extras.get(), &cursor, &key, &value)) if (matches(value)) return int(p::wide(key));
    } else if (extras.get() != Py_None) {
        for (Py_ssize_t i = 0; i < PySequence_Size(extras.get()); ++i) {
            auto value = p::item(extras.get(), i);
            if (matches(value.get())) return int(i);
        }
    }
    auto index = p::value(selected.get(), "index");
    const int found = index.get() == Py_None ? 0 : int(p::wide(index.get()));
    if (found <= 0) throw p::Error("Native critical descriptor extra is unavailable: " + name);
    return found;
}

bool active(Battle& b, const LocalCriticalOwner& o) {
    if (o.closed || o.state->retired || o.state->epoch != o.epoch) return false;
    const auto& a = *o.actor;
    if (a.tombstone || a.missing || !a.ready) return false;
    const auto at = o.state->actors.find(a.key);
    if (at == o.state->actors.end() || at->second != o.actor) return false;
    return b.entity(a.engine_id).get() == o.entity.get();
}

void close(LocalCriticalOwner& o) {
    o.closed = true;
    if (o.status) o.status->trackOverlay({});
}

// Mirrors the owner's lineage into Critical so a later owner and the non-native paths continue it.
void mirror(Battle& b, LocalCriticalOwner& o) {
    const auto& s = o.model->state();
    auto& c = *b.critical;
    c.base_revision = s.base_revision;
    c.next_sequence = s.next_sequence;
    c.server_revision = s.server_revision;
    c.owned = s.owned;
    c.report = s.report;
    c.next_report_time = s.next_report_time;
    if (o.visual_base != s.base_revision) { o.pending.clear(); o.order.clear(); o.visual_base = s.base_revision; }
}

void metadata(Battle& b, LocalCriticalOwner& o) {
    auto raw = py::attr(o.entity.get(), "typeDescriptor");
    if (raw.get() == Py_None) throw p::Error("Native critical descriptor is unavailable");
    if (o.descriptor.get() == raw.get() && o.descriptor_revision == o.actor->descriptor_revision) return;
    core::Metadata m;
    auto misc = p::value(raw.get(), "miscAttrs");
    m.repair_speed_factor = p::numeric(misc.get(), "repairSpeedFactor", 1.);
    for (const char* name : {"engineHealth", "ammoBayHealth", "fuelTankHealth", "radioHealth", "leftTrackHealth", "rightTrackHealth",
                             "gunHealth", "turretRotatorHealth", "surveyingDeviceHealth"}) {
        const auto pool = stock_native_bot_descriptor::devicePool(raw.get(), name);
        if (pool.maximum) m.maxima[name] = *pool.maximum;
        if (pool.regeneration) m.regeneration[name] = *pool.regeneration;
        if (pool.maximum || pool.regeneration) m.extra_indexes[name] = extraIndex(raw.get(), name);
    }
    auto statuses = p::value(b.engine.constants.get(), "VEHICLE_MISC_STATUS");
    auto progress = p::value(statuses.get(), "DESTROYED_DEVICE_IS_REPAIRING");
    if (progress.get() == Py_None) throw p::Error("Native repair-progress status is unavailable");
    o.progress_status = int(p::wide(progress.get()));
    auto indices = p::value(b.engine.constants.get(), "DAMAGE_INFO_INDICES");
    auto repaired = p::value(indices.get(), "DEVICE_REPAIRED_TO_CRITICAL");
    if (repaired.get() == Py_None) throw p::Error("Native repaired-track damage-info code is unavailable");
    o.repair_info = int(p::wide(repaired.get()));
    o.extra_indexes = m.extra_indexes;
    o.maxima = m.maxima;
    if (o.model) o.model->metadata(std::move(m));
    else {
        const auto& c = *b.critical;
        core::State seed;
        seed.base_revision = c.base_revision;
        seed.next_sequence = c.next_sequence;
        seed.server_revision = c.server_revision;
        seed.owned = c.owned;
        seed.next_report_time = c.next_report_time;
        seed.report = c.report;
        o.model = std::make_shared<core::Owner>(std::move(m), o.status->critical(), std::move(seed), o.status->casualties());
        o.status->trackOverlay(o.model);
        auto current = o.status->critical();
        o.model->reconcile(*o.actor, current);
        o.status->localTracks(current);
    }
    o.descriptor = own(std::move(raw));
    o.descriptor_revision = o.actor->descriptor_revision;
}

LocalCriticalOwner& ensure(Battle& b) {
    auto& c = *b.critical;
    if (c.local && active(b, *c.local)) return *c.local;
    if (c.local) { close(*c.local); c.local.reset(); }
    auto state = b.vehicles;
    auto at = state->actors.find("player:" + std::to_string(state->local_player_id));
    if (state->retired || at == state->actors.end() || !at->second->local || !at->second->ready)
        throw p::Error("Native local critical actor is unavailable");
    auto owner = std::make_unique<LocalCriticalOwner>();
    owner->state = state;
    owner->actor = at->second;
    owner->epoch = state->epoch;
    owner->entity = own(b.entity(at->second->engine_id));
    if (owner->entity.get() == Py_None) throw p::Error("Native local critical entity retired");
    drive::synchronizeStatus(b, b.clock());
    owner->status = drive::localStatus(*owner->actor);
    if (!owner->status) throw p::Error("Native local critical status is unavailable");
    c.local = std::move(owner);
    metadata(b, *c.local);
    return *c.local;
}

// The local VehicleDamage follows the owner's two track fields.
void mirrorTracks(Battle& b, LocalCriticalOwner& o) {
    auto& d = b.critical->vehicles[o.actor->engine_id];
    for (const char* name : {"leftTrackHealth", "rightTrackHealth"}) {
        const auto at = o.model->tracks().devices.find(name);
        if (at == o.model->tracks().devices.end()) continue;
        const auto& device = at->second;
        if (device.hp) d.devices[name] = *device.hp;
        if (device.destroyed) d.destroyed.insert(name); else d.destroyed.erase(name);
        if (device.critical) d.critical.insert(name); else d.critical.erase(name);
    }
}

void payload(Battle& b, LocalCriticalOwner& o, const core::Effects& effects) {
    if (!effects.payload) return;
    mirrorTracks(b, o);
    V canonical = *effects.payload;
    canonical.object["events"].array.clear();
    auto& presented = o.actor->presented;
    presented.critical = canonical;
    writable(presented.state).object["critical"] = canonical;
}

void queue(LocalCriticalOwner& o, const std::string& key, LocalCriticalOwner::Edge edge) {
    if (!o.pending.count(key)) o.order.push_back(key);
    o.pending[key] = std::move(edge);
}

void visuals(LocalCriticalOwner& o, const core::Effects& effects) {
    const auto* events = effects.payload ? &effects.payload->get("events").resolved().array : nullptr;
    if (events)
        for (const auto& event : *events) {
            const auto& name = event.get("name").resolved().text;
            if (event.get("kind").resolved().text != "device" || event.get("old_state").resolved().text != "destroyed" || !track(name)) continue;
            LocalCriticalOwner::Edge edge;
            edge.kind = "crash";
            edge.left = name == "leftTrackHealth";
            queue(o, "crash:" + name, edge);
        }
    for (const auto& progress : effects.progress) {
        LocalCriticalOwner::Edge edge;
        edge.kind = "progress";
        edge.status = o.progress_status;
        edge.code = progress.extra_index | (progress.percentage << 8);
        edge.seconds = progress.seconds_left;
        queue(o, "progress:" + progress.name, edge);
    }
    if (events)
        for (const auto& event : *events) {
            const auto& name = event.get("name").resolved().text;
            if (event.get("kind").resolved().text != "device") continue;
            if (!o.model->tracks().devices.count(name)) continue;
            const auto extra = o.extra_indexes.find(name);
            if (extra == o.extra_indexes.end() || extra->second <= 0) continue;
            LocalCriticalOwner::Edge edge;
            edge.kind = "repaired";
            edge.index = o.repair_info;
            edge.extra = extra->second;
            queue(o, "repaired:" + std::to_string(++o.visual_sequence) + ":" + name, edge);
        }
}

// Sends the pending stock edges in order; a failed edge stays pending for the next tick.
void emit(Battle& b, LocalCriticalOwner& o) {
    const std::int64_t id = o.actor->engine_id;
    auto order = o.order;
    for (const auto& key : order) {
        if (!active(b, o)) return;
        const auto at = o.pending.find(key);
        if (at == o.pending.end()) continue;
        const auto& edge = at->second;
        try {
            if (edge.kind == "crash") {
                auto appearance = py::optional(o.entity.get(), "appearance");
                auto callback = py::optional(appearance.get(), "delCrashedTrack");
                if (py::callable(callback)) py::invoke(callback.get(), edge.left ? Py_True : Py_False);
            } else if (edge.kind == "progress") {
                p::Ref seconds(Py_BuildValue("(d)", edge.seconds));
                py::call(b.engine.avatar.get(), "updateVehicleMiscStatus", p::integer(id).get(), p::integer(edge.status).get(),
                         p::integer(edge.code).get(), seconds.get());
            } else if (edge.kind == "repaired") {
                if (!vehicles::damageInfoServiceable(b) || !vehicles::showDamageInfo(b, id, edge.index, edge.extra, id)) continue;
            }
            o.pending.erase(at);
        } catch (const std::exception& e) {
            b.warn("native track repair presentation", describe(e), false);
        }
    }
    o.order.erase(std::remove_if(o.order.begin(), o.order.end(), [&](const std::string& key) { return !o.pending.count(key); }), o.order.end());
}

struct Busy {
    LocalCriticalOwner& owner;
    explicit Busy(LocalCriticalOwner& o) : owner(o) { owner.busy = true; }
    ~Busy() { owner.busy = false; }
};

bool nativeOwner(const Battle& b, const Actor& vehicle) { return b.running() && !b.worker && !b.replay && vehicle.local; }

// The owner-CAS path of the local vehicle.
bool reconcileNative(Battle& b, Actor& vehicle, const V& source, V* canonical) {
    auto& o = ensure(b);
    for (const char* key : {"critical_revision", "critical_base_revision", "critical_ack_seq"})
        if (!has(source, key)) throw p::Error("Native player critical state has no revision contract");
    if (o.actor.get() != &vehicle) return false;
    drive::synchronizeStatus(b, b.clock());
    metadata(b, o);
    const auto& s = source.resolved();
    const auto revision = std::int64_t(s.get("critical_revision").numeric()), base = std::int64_t(s.get("critical_base_revision").numeric()),
               ack = std::int64_t(s.get("critical_ack_seq").numeric());
    auto& presented = vehicle.presented;
    const std::int64_t previous = presented.critical_revision;
    if (revision < previous) return false;
    auto decoded = canonical ? drive::decodeCritical(*canonical, o.status->metadata()) : o.status->critical();
    o.model->accept(revision, base, ack, decoded);
    o.status->localTracks(decoded);
    mirror(b, o);
    presented.critical_revision = revision;
    presented.critical_base_revision = base;
    presented.critical_ack_seq = ack;
    if (canonical && o.model->state().owned) {
        // The owning client advances only tracks; server-owned gun, engine, fire and crew stay canonical.
        auto rows = array();
        for (const auto& row : canonical->resolved().get("devices").resolved().array)
            if (!track(row.get("name").resolved().text)) rows.array.push_back(row);
        std::set<std::string> destroyed;
        for (const auto& name : canonical->resolved().get("destroyed").resolved().array)
            if (!track(name.resolved().text)) destroyed.insert(name.resolved().text);
        for (const char* name : {"leftTrackHealth", "rightTrackHealth"}) {
            const auto at = decoded.devices.find(name);
            if (at == decoded.devices.end()) continue;
            const auto& d = at->second;
            auto row = object();
            row.object["name"] = text(name);
            const double hp = d.hp.value_or(0.);
            row.object["hp"] = number(hp);
            const auto maximum = o.maxima.find(name);
            row.object["max_hp"] = number(maximum == o.maxima.end() ? d.wire_maximum.value_or(d.maximum.value_or(std::max(1., hp))) : maximum->second);
            row.object["state"] = text(d.destroyed ? "destroyed" : d.critical ? "critical" : "normal");
            rows.array.push_back(std::move(row));
            if (d.destroyed) destroyed.insert(name);
        }
        auto& out = writable(*canonical);
        out.object["devices"] = std::move(rows);
        auto names = array();
        for (const auto& name : destroyed) names.array.push_back(text(name));
        out.object["destroyed"] = std::move(names);
    }
    return revision > previous;
}
}

bool acknowledgeLocalDamageReport(Battle& b, std::int64_t base_revision, std::int64_t ack_seq, std::int64_t server_revision) {
    auto& c = *b.critical;
    base_revision = std::max<std::int64_t>(0, base_revision);
    ack_seq = std::max<std::int64_t>(0, ack_seq);
    server_revision = std::max<std::int64_t>(0, server_revision);
    if (server_revision < c.server_revision) return false;
    c.server_revision = server_revision;
    if (base_revision != c.base_revision) {
        c.base_revision = base_revision;
        c.next_sequence = 0;
        c.owned = false;
        c.report = V();
        return false;
    }
    const auto& report = c.report.resolved();
    if (report.kind == V::Kind::Null || ack_seq < std::int64_t(report.get("critical_seq").numeric(0.))) return false;
    const auto& tracks = report.get("tracks").resolved();
    bool complete = tracks.truth();
    for (const auto& row : tracks.array) complete = complete && row.get("state").resolved().text == "critical";
    c.report = V();
    c.owned = !complete;
    return true;
}

// BattleRuntime._queue_local_damage_report at the local death edge.
void localDeath(Battle& b) {
    if (!b.critical) return;
    auto& c = *b.critical;
    if (c.report.resolved().get("tracks").truth()) return;
    c.report = V();
    c.owned = false;
    if (c.local && c.local->model) c.local->model->release();
}

bool reconcileAuthority(Battle& b, Actor& vehicle, const V& source, V* canonical) {
    if (nativeOwner(b, vehicle)) return reconcileNative(b, vehicle, source, canonical);
    if (vehicle.kind != "player") return true;
    for (const char* key : {"critical_revision", "critical_base_revision", "critical_ack_seq"})
        if (!has(source, key)) throw p::Error("#1513 player critical state has no revision contract");
    const auto& s = source.resolved();
    const auto revision = std::max<std::int64_t>(0, std::int64_t(s.get("critical_revision").numeric()));
    const auto base = std::max<std::int64_t>(0, std::int64_t(s.get("critical_base_revision").numeric()));
    const auto ack = std::max<std::int64_t>(0, std::int64_t(s.get("critical_ack_seq").numeric()));
    auto& presented = vehicle.presented;
    const std::int64_t previous = presented.critical_revision;
    if (revision < previous) return false;
    // The server omits an undamaged player's empty critical object. Establish its native
    // initial state only on the first accepted zero-revision contract; never repair a lost owner later.
    if (!canonical && previous < 0 && s.get("critical_revision").numeric(-1.) == 0. &&
        s.get("critical_base_revision").numeric(-1.) == 0. && s.get("critical_ack_seq").numeric(-1.) == 0. &&
        s.get("critical").resolved().kind == V::Kind::Null && b.critical)
        b.critical->vehicles.emplace(vehicle.engine_id, VehicleDamage{});
    presented.critical_revision = revision;
    presented.critical_base_revision = base;
    presented.critical_ack_seq = ack;
    if (!vehicle.local) return revision > previous;
    auto& c = *b.critical;
    const V pending = c.report;
    const bool acknowledged = acknowledgeLocalDamageReport(b, base, ack, revision);
    const bool canonical_object = canonical && canonical->resolved().kind == V::Kind::Object;
    if (acknowledged && c.owned && pending.resolved().kind == V::Kind::Object && canonical_object) {
        std::set<std::string> pending_tracks;
        for (const auto& row : pending.resolved().get("tracks").resolved().array)
            if (row.resolved().kind == V::Kind::Object) pending_tracks.insert(row.get("name").resolved().text);
        std::map<std::string, std::string> states;
        for (const auto& row : canonical->resolved().get("devices").resolved().array)
            if (row.resolved().kind == V::Kind::Object) states[row.get("name").resolved().text] = row.get("state").resolved().text;
        std::set<std::string> destroyed;
        for (const auto& name : canonical->resolved().get("destroyed").resolved().array) destroyed.insert(name.resolved().text);
        bool repaired = !pending_tracks.empty();
        for (const auto& name : pending_tracks) {
            const auto state = states.find(name);
            repaired = repaired && state != states.end() && (state->second == "normal" || state->second == "critical") && !destroyed.count(name);
        }
        if (repaired) c.owned = false;
    }
    if (c.owned && base == c.base_revision && canonical_object) {
        const auto& local = presented.critical.resolved();
        auto rows = array();
        for (const auto& row : canonical->resolved().get("devices").resolved().array)
            if (!track(row.get("name").resolved().text)) rows.array.push_back(row);
        for (const auto& row : local.get("devices").resolved().array)
            if (track(row.get("name").resolved().text)) rows.array.push_back(row);
        std::set<std::string> destroyed;
        for (const auto& name : canonical->resolved().get("destroyed").resolved().array)
            if (!track(name.resolved().text)) destroyed.insert(name.resolved().text);
        for (const auto& name : local.get("destroyed").resolved().array)
            if (track(name.resolved().text)) destroyed.insert(name.resolved().text);
        auto& out = writable(*canonical);
        out.object["devices"] = std::move(rows);
        auto names = array();
        for (const auto& name : destroyed) names.array.push_back(text(name));
        out.object["destroyed"] = std::move(names);
    }
    return revision > previous;
}

// BattleRuntime._tick_critical_states(dt) through the native owner: track repair and the repair HUD.
void tick(Battle& b, double dt) {
    if (dt <= 0. || !b.running() || b.worker || b.replay) return;
    try {
        auto& o = ensure(b);
        if (o.busy) return;
        Busy guard(o);
        const double now = b.clock();
        drive::synchronizeStatus(b, now);
        if (!active(b, o)) throw p::Error("Native critical owner retired during status ingress");
        metadata(b, o);
        const auto& state = o.actor->state.resolved();
        const bool eligible = state.object.count("alive")
            ? state.get("alive").truth() && std::int64_t(state.object.count("health") ? state.get("health").numeric() : 1.) > 0
            : o.actor->alive;
        if (!eligible) return;
        const auto& repair = state.get("effective_params").get("loadout").get("repair_factor").resolved();
        if (repair.kind == V::Kind::Null) throw p::Error("Native local repair factor is unavailable");
        const double factor = std::max(0., repair.numeric(1.)) * (1. + gun::activeRepairBonus(b));
        const auto effects = o.model->advance(o.status->critical(), dt, now, true, factor, o.status->casualties());
        o.status->localTracks(o.model->tracks());
        mirror(b, o);
        payload(b, o, effects);
        visuals(o, effects);
        emit(b, o);
        if (!active(b, o)) return;
        gun::publishEquipment(b, now);
    } catch (const std::exception& e) {
        b.warn("native local track repair", describe(e), false);
    }
}

void retire(Battle& b) {
    if (!b.critical || !b.critical->local) return;
    close(*b.critical->local);
    b.critical->local.reset();
}
}
}
