// The #1513 entity/arena binding: Vehicle properties, ClientArena updates, feedback markers and Avatar properties.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_startup_internal.hpp"

namespace native_battle {
namespace startup {
namespace binding {
namespace {
PyObject* avatar(Battle& b) {
    if (!b.engine.avatar) throw p::Error("#1513 Avatar is unavailable for the binding");
    return b.engine.avatar.get();
}
// BigWorld STRING is a Python 2 str; LAN names arrive as unicode.
p::Ref entityString(PyObject* value) {
    if (PyUnicode_Check(value)) return p::Ref(PyUnicode_AsUTF8String(value));
    return p::retain(value);
}
// The private authority registry: synthetic remotes stay reachable while unspotted or dead.
p::Ref authorityEntity(Battle& b, std::int64_t entity_id) {
    auto entity = b.entity(entity_id);
    if (entity.get() == Py_None) throw p::Error("Authority vehicle entity " + std::to_string(entity_id) + " is unavailable");
    return entity;
}
p::Ref engineEntity(Battle& b, std::int64_t entity_id) {
    auto entity = py::call(b.engine.bigworld.get(), "entity", p::integer(entity_id).get());
    if (entity.get() == Py_None) throw p::Error("Vehicle entity " + std::to_string(entity_id) + " is unavailable");
    return entity;
}
p::Ref arenaUpdate(Battle& b, const char* name) {
    return need(need(b.engine.constants.get(), "ARENA_UPDATE").get(), name);
}
void updateArena(Battle& b, const char* kind, PyObject* payload) {
    py::call(avatar(b), "updateArena", arenaUpdate(b, kind).get(), payload);
}
// setattr(avatar, name, value) and the stock set_<name>(previous) notifier.
void setAvatarProperty(Battle& b, const char* name, PyObject* value) {
    PyObject* a = avatar(b);
    auto previous = need(a, name);
    py::set(a, name, value);
    auto notifier = py::optional(a, (std::string("set_") + name).c_str());
    if (notifier.get() != Py_None) py::invoke(notifier.get(), previous.get());
}
p::Ref feedback(Battle& b) {
    return need(need(need(avatar(b), "guiSessionProvider").get(), "shared").get(), "feedback");
}
// (entity, vehicle info, gui props, feedback): the arguments of #1513's two visual signals.
struct FeedbackContext { p::Ref entity, info, props, feedback; };
FeedbackContext feedbackContext(Battle& b, std::int64_t entity_id) {
    auto entity = authorityEntity(b, entity_id);
    need(entity.get(), "proxy");
    auto provider = need(avatar(b), "guiSessionProvider");
    auto arena_dp = py::invoke(needCallable(provider.get(), "getArenaDP").get());
    auto id = p::integer(entity_id);
    auto info = py::invoke(needCallable(arena_dp.get(), "getVehicleInfo").get(), id.get());
    auto props = py::invoke(needCallable(arena_dp.get(), "getPlayerGuiProps").get(), id.get(), py::attr(info.get(), "team").get());
    return {std::move(entity), std::move(info), std::move(props), feedback(b)};
}
}

p::Ref properties(Battle& b, PyObject* compact_descr, std::int64_t team, const std::string& name) {
    auto descriptor = callKw(py::attr(b.engine.vehicles.get(), "VehicleDescr").get(), {}, {{"compactDescr", compact_descr}});
    auto pitch_limits = py::attr(need(descriptor.get(), "gun").get(), "pitchLimits");
    PyObject* absolute = PyDict_Check(pitch_limits.get()) ? PyDict_GetItemString(pitch_limits.get(), "absolute") : nullptr;
    if (!absolute) throw p::Error("VehicleDescr.gun.pitchLimits.absolute unavailable");
    auto info = p::dict();
    p::set(info.get(), "compDescr", entityString(py::call(descriptor.get(), "makeCompactDescr").get()));
    p::set(info.get(), "name", p::text(name));
    p::set(info.get(), "team", p::integer(team));
    p::set(info.get(), "prebattleID", p::integer(0));
    p::set(info.get(), "marksOnGun", p::integer(0));
    p::set(info.get(), "index", p::integer(0));
    auto& loadout = garage(b);
    p::set(info.get(), "outfit", loadout.outfit ? entityString(loadout.outfit.get()) : p::text(""));
    auto encode = need(b.engine.modules.get(), "encode_gun_angles");
    auto out = p::dict();
    p::set(out.get(), "publicInfo", info);
    p::set(out.get(), "gunAnglesPacked", py::invoke(encode.get(), p::integer(0).get(), p::integer(0).get(), absolute));
    p::set(out.get(), "health", py::attr(descriptor.get(), "maxHealth"));
    p::set(out.get(), "isCrewActive", p::boolean(true));
    p::set(out.get(), "steeringAngle", p::number(0.));
    p::set(out.get(), "isStrafing", p::boolean(false));
    p::set(out.get(), "physicsMode", py::attr(need(b.engine.constants.get(), "VEHICLE_PHYSICS_MODE").get(), "STANDARD"));
    p::set(out.get(), "siegeState", py::attr(need(b.engine.constants.get(), "VEHICLE_SIEGE_STATE").get(), "DISABLED"));
    p::set(out.get(), "engineMode", p::Ref(Py_BuildValue("(ii)", 0, 0)));
    p::set(out.get(), "damageStickers", p::list());
    p::set(out.get(), "publicStateModifiers", p::Ref(PyTuple_New(0)));
    p::set(out.get(), "stunInfo", p::number(0.));
    return out;
}

p::Ref createVehicle(Battle& b, PyObject* properties, PyObject* position, PyObject* rotation) {
    auto space = py::attr(avatar(b), "spaceID");
    return p::Ref(PyObject_CallMethod(b.engine.bigworld.get(), const_cast<char*>("createEntity"), const_cast<char*>("sOiOOO"),
                                      "Vehicle", space.get(), 0, position, rotation, properties));
}

// The exact #1513 ClientArena 18-item vehicle-list tuple, compressed.
void arenaVehicleAdded(Battle& b, std::int64_t entity_id, PyObject* properties, bool team_killer, PyObject* mission_ids,
                       std::int64_t crew_group) {
    PyObject* info = PyDict_GetItemString(properties, "publicInfo");
    if (!info) throw p::Error("Vehicle snapshot properties are required");
    PyObject* health = PyDict_GetItemString(properties, "health");
    PyObject* crew = PyDict_GetItemString(properties, "isCrewActive");
    const bool alive = (health ? asInt(health) : 0) > 0 && (!crew || p::flag(crew));
    auto id = p::integer(entity_id);
    auto missions = mission_ids ? p::Ref(PySequence_List(mission_ids)) : p::list();
    p::Ref values(Py_BuildValue("[OOOOOOOOsiOOOOiOiO]", id.get(), PyDict_GetItemString(info, "compDescr"), PyDict_GetItemString(info, "name"),
                                PyDict_GetItemString(info, "team"), alive ? Py_True : Py_False, Py_True, team_killer ? Py_True : Py_False,
                                id.get(), "", 0, PyDict_GetItemString(info, "prebattleID"), Py_False, Py_False, p::dict().get(), 0,
                                missions.get(), int(crew_group), p::dict().get()));
    updateArena(b, "VEHICLE_ADDED", compress(pickle(values.get()).get()).get());
}

void startVehicleVisual(Battle& b, std::int64_t entity_id, bool immediate) {
    auto entity = authorityEntity(b, entity_id);
    auto provider = need(avatar(b), "guiSessionProvider");
    py::call(provider.get(), "startVehicleVisual", need(entity.get(), "proxy").get(), immediate ? Py_True : Py_False);
}
void stopVehicleVisual(Battle& b, std::int64_t entity_id, bool is_player) {
    auto provider = need(avatar(b), "guiSessionProvider");
    py::call(provider.get(), "stopVehicleVisual", p::integer(entity_id).get(), is_player ? Py_True : Py_False);
}
void startVehicleMarker(Battle& b, std::int64_t entity_id) {
    auto c = feedbackContext(b, entity_id);
    py::invoke(needCallable(c.feedback.get(), "onVehicleMarkerAdded").get(), py::attr(c.entity.get(), "proxy").get(), c.info.get(), c.props.get());
}
void stopVehicleMarker(Battle& b, std::int64_t entity_id) {
    py::invoke(needCallable(feedback(b).get(), "onVehicleMarkerRemoved").get(), p::integer(entity_id).get());
}
void startVehicleMinimap(Battle& b, std::int64_t entity_id) {
    auto c = feedbackContext(b, entity_id);
    py::call(need(c.feedback.get(), "_BattleFeedbackAdaptor__visible").get(), "add", p::integer(entity_id).get());
    py::invoke(needCallable(c.feedback.get(), "onMinimapVehicleAdded").get(), py::attr(c.entity.get(), "proxy").get(), c.info.get(), c.props.get());
}
void stopVehicleMinimap(Battle& b, std::int64_t entity_id) {
    auto fb = feedback(b);
    py::call(need(fb.get(), "_BattleFeedbackAdaptor__visible").get(), "discard", p::integer(entity_id).get());
    py::invoke(needCallable(fb.get(), "onMinimapVehicleRemoved").get(), p::integer(entity_id).get());
}

// The uncompressed #1513 kill tuple, plus the killfeed/postmortem text stock skips for the own or observed vehicle.
void arenaVehicleKilled(Battle& b, std::int64_t entity_id, std::int64_t attacker_id, std::int64_t reason) {
    PyObject* a = avatar(b);
    p::Ref payload(Py_BuildValue("(LLiL)", (long long)entity_id, (long long)attacker_id, 0, (long long)reason));
    const bool own = entity_id == asInt(py::attr(a, "playerVehicleID").get());
    auto controller = py::optional(py::optional(a, "inputHandler").get(), "ctrl");
    auto alive = py::optional(a, "isVehicleAlive");
    auto observed_id = py::optional(controller.get(), "curVehicleID");
    const bool observed = alive.get() != Py_None && !p::flag(alive.get()) && observed_id.get() != Py_None && asInt(observed_id.get()) == entity_id;
    updateArena(b, "VEHICLE_KILLED", pickle(payload.get()).get());
    if (!own && !observed) return;
    auto messages = need(need(need(a, "guiSessionProvider").get(), "shared").get(), "messages");
    auto kill_info = need(messages.get(), "_BattleMessagesController__getKillInfo");
    p::Ref args(Py_BuildValue("(OLLiL)", a, (long long)entity_id, (long long)attacker_id, 0, (long long)reason));
    p::Ref info(PyObject_CallObject(kill_info.get(), args.get()));
    auto code = p::item(info.get(), 0), postfix = p::item(info.get(), 1);
    if (!own) {
        py::invoke(need(messages.get(), "onShowPlayerMessageByCode").get(), code.get(), postfix.get(), p::integer(entity_id).get(),
                   p::integer(attacker_id).get(), p::integer(0).get());
        return;
    }
    std::string death_code = p::string(code.get());
    p::Ref death_extra(p::retain(Py_None));
    auto reasons = need(b.engine.constants.get(), "ATTACK_REASON_INDICES");
    auto index = [&](const char* name, std::int64_t fallback) {
        PyObject* v = PyDict_Check(reasons.get()) ? PyDict_GetItemString(reasons.get(), name) : nullptr;
        return v ? asInt(v) : fallback;
    };
    const bool fire = reason == index("fire", 1);
    if (fire) death_code = "DEATH_FROM_FIRE";
    auto entity = authorityEntity(b, entity_id);
    if (asInt(py::attr(entity.get(), "health").get()) < 0) {
        auto extras = py::attr(py::attr(entity.get(), "typeDescriptor").get(), "extrasDict");
        replace(death_extra, py::call(extras.get(), "get", p::text("ammoBayHealth").get()));
        death_code = fire ? "DEATH_FROM_DEVICE_EXPLOSION_AT_FIRE"
                   : reason == index("world_collision", 3) ? "DEATH_FROM_DEVICE_EXPLOSION_AT_WORLD_COLLISION"
                   : "DEATH_FROM_DEVICE_EXPLOSION_AT_SHOT";
        log("AMMO DEATH PRESENT actor=" + std::to_string(entity_id) + " attacker=" + std::to_string(attacker_id) +
            " reason=" + std::to_string(reason) + " code=" + death_code);
    } else if (!attacker_id && death_code == "DEATH_FROM_SHOT" && !fire) {
        death_code = "DEATH_UNKNOWN";
    }
    p::Ref final_postfix(p::retain(postfix.get()));
    // #1513 has no UNKNOWN_SELF postmortem templates; its unassisted cause text is SELF_SUICIDE.
    for (const char* suicide : {"DEATH_FROM_WORLD_COLLISION", "DEATH_FROM_DROWNING", "DEATH_FROM_OVERTURN", "DEATH_FROM_DEATH_ZONE",
                                "DEATH_FROM_INACTIVE_CREW_AT_WORLD_COLLISION"})
        if (!attacker_id && death_code == suicide) replace(final_postfix, p::text("SELF_SUICIDE"));
    py::invoke(need(messages.get(), "onShowVehicleMessageByCode").get(), p::text(death_code).get(), final_postfix.get(),
               p::integer(attacker_id).get(), death_extra.get(), p::integer(0).get());
}

void arenaVehicleStatistics(Battle& b, std::int64_t entity_id, std::int64_t frags) {
    p::Ref payload(Py_BuildValue("(LL)", (long long)entity_id, (long long)frags));
    updateArena(b, "VEHICLE_STATISTICS", compress(pickle(payload.get()).get()).get());
}
void arenaTeamKiller(Battle& b, std::int64_t entity_id) { updateArena(b, "TEAM_KILLER", pickle(p::integer(entity_id).get()).get()); }

void avatarSelectVehicle(Battle& b, std::int64_t entity_id) { setAvatarProperty(b, "playerVehicleID", p::integer(entity_id).get()); }

// Publish the local Vehicle and bind #1513's attachment state the client-only mailbox cannot.
void avatarVehicleEntered(Battle& b) {
    PyObject* a = avatar(b);
    py::call(a, "onVehicleChanged");
    auto entity = engineEntity(b, asInt(py::attr(a, "playerVehicleID").get()));
    auto matrix = need(entity.get(), "matrix");
    auto setter = need(py::attr(a, "consistentMatrices").get(), "_ConsistentMatrices__setTarget");
    auto view_points = need(need(need(a, "guiSessionProvider").get(), "shared").get(), "viewPoints");
    auto update = needCallable(view_points.get(), "updateAttachedVehicle");
    py::invoke(setter.get(), matrix.get(), Py_False);
    py::invoke(update.get(), py::attr(a, "playerVehicleID").get());
}

void avatarClientReady(Battle& b) {
    setAvatarProperty(b, "isGunLocked", Py_False);
    setAvatarProperty(b, "ownVehicleAuxPhysicsData", p::integer(0).get());
    setAvatarProperty(b, "ownVehicleGear", p::integer(0).get());
    PyObject* a = avatar(b);
    auto entity = engineEntity(b, asInt(py::attr(a, "playerVehicleID").get()));
    auto radius = need(need(need(entity.get(), "typeDescriptor").get(), "turret").get(), "circularVisionRadius");
    auto attrs = p::dict();
    p::set(attrs.get(), "circularVisionRadius", radius);
    py::call(a, "syncVehicleAttrs", attrs.get());
}

// The six-field UINT64 #1513 WoT.unpackAuxVehiclePhysicsData reads, gear first.
std::uint64_t avatarAuxPhysics(Battle& b, const std::array<double, 6>& values, std::int64_t gear) {
    static const struct { unsigned bits; double minimum, maximum; } fields[6] = {
        {15, -kPi, kPi}, {12, -kPi / 2., kPi / 2.}, {13, -kPi, kPi}, {8, -15., 30.}, {8, -15., 30.}, {8, 0., 1.2}};
    if (gear < 0 || gear > 255) throw p::Error("Vehicle Avatar ownVehicleGear is outside integer schema");
    std::uint64_t packed = 0;
    unsigned shift = 0;
    for (unsigned i = 0; i < 6; ++i) {
        if (!std::isfinite(values[i])) throw p::Error("auxiliary vehicle physics value is not finite");
        const double ratio = std::max(0., std::min(1., (values[i] - fields[i].minimum) / (fields[i].maximum - fields[i].minimum)));
        const std::uint64_t mask = (std::uint64_t(1) << fields[i].bits) - 1;
        packed |= (std::uint64_t(std::llround(double(mask) * ratio)) & mask) << shift;
        shift += fields[i].bits;
    }
    setAvatarProperty(b, "ownVehicleGear", p::integer(gear).get());
    setAvatarProperty(b, "ownVehicleAuxPhysicsData", p::Ref(PyLong_FromUnsignedLongLong(packed)).get());
    return packed;
}

void avatarReady(Battle& b) { updateArena(b, "AVATAR_READY", pickle(py::attr(avatar(b), "playerVehicleID").get()).get()); }

void arenaPeriod(Battle& b, const char* period, double duration, std::optional<std::pair<std::int64_t, std::int64_t>> outcome) {
    auto periods = need(b.engine.constants.get(), "ARENA_PERIOD");
    const std::string name = period;
    p::Ref value = name == "prebattle" ? need(periods.get(), "PREBATTLE") : name == "battle" ? need(periods.get(), "BATTLE")
                 : name == "afterbattle" ? need(periods.get(), "AFTERBATTLE") : throw p::Error("unsupported arena period: " + name);
    p::Ref additional(p::list());
    if (name == "afterbattle") {
        if (!outcome) throw p::Error("afterbattle arena period has no round outcome");
        replace(additional, p::Ref(Py_BuildValue("(LL)", (long long)outcome->first, (long long)outcome->second)));
    } else if (outcome) throw p::Error("arena period " + name + " carries no round outcome");
    duration = std::max(0., duration);
    const double end = duration > 0. ? asFloat(py::call(b.engine.bigworld.get(), "serverTime").get()) + duration : 0.;
    p::Ref payload(Py_BuildValue("(OddO)", value.get(), end, duration, additional.get()));
    updateArena(b, "PERIOD", compress(pickle(payload.get()).get()).get());
}

void setVehiclePose(Battle& b, std::int64_t entity_id, PyObject* position, PyObject* rotation, PyObject* relax_time, PyObject* now) {
    auto entity = authorityEntity(b, entity_id);
    auto setter = py::optional(entity.get(), "set_pose");
    const bool presentation = p::flag(py::optional(entity.get(), "_offlineLANPresentation").get()) ||
                              p::flag(py::optional(entity.get(), "_offlineNativeRemote").get());
    if (!presentation || !py::callable(setter)) throw p::Error("remote vehicle has no authoritative presentation");
    py::invoke(setter.get(), position, rotation, relax_time, now);
}

bool settleVehicleMotion(Battle& b, std::int64_t entity_id, PyObject* now) {
    auto entity = authorityEntity(b, entity_id);
    auto settle = py::optional(entity.get(), "settle_motion");
    const bool presentation = p::flag(py::optional(entity.get(), "_offlineLANPresentation").get()) ||
                              p::flag(py::optional(entity.get(), "_offlineNativeRemote").get());
    if (!presentation || !py::callable(settle)) throw p::Error("remote vehicle has no authoritative motion settlement");
    return p::flag(py::invoke(settle.get(), now).get());
}

// A network world aim to the presentation setter, else the exact packed Vehicle property.
void updateVehicleAim(Battle& b, std::int64_t entity_id, double hull_yaw, double aim_yaw, double gun_pitch) {
    auto entity = authorityEntity(b, entity_id);
    auto setter = py::optional(entity.get(), "set_aim");
    const bool presentation = p::flag(py::optional(entity.get(), "_offlineLANPresentation").get()) ||
                              p::flag(py::optional(entity.get(), "_offlineNativeRemote").get());
    if (presentation && py::callable(setter)) {
        py::invoke(setter.get(), p::number(hull_yaw).get(), p::number(aim_yaw).get(), p::number(gun_pitch).get());
        return;
    }
    auto limits = need(need(need(entity.get(), "typeDescriptor").get(), "gun").get(), "pitchLimits");
    PyObject* absolute = PyDict_Check(limits.get()) ? PyDict_GetItemString(limits.get(), "absolute") : nullptr;
    if (!absolute) throw p::Error("Vehicle gun pitch limits are unavailable");
    const double relative = std::fmod(std::fmod(aim_yaw - hull_yaw + kPi, 2. * kPi) + 2. * kPi, 2. * kPi) - kPi;
    auto packed = py::invoke(need(b.engine.modules.get(), "encode_gun_angles").get(), p::number(relative).get(), p::number(gun_pitch).get(), absolute);
    const std::int64_t value = asInt(packed.get());
    if (value < 0 || value > 65535) throw p::Error("Vehicle gunAnglesPacked is outside integer schema");
    auto previous = need(entity.get(), "gunAnglesPacked");
    py::set(entity.get(), "gunAnglesPacked", packed.get());
    auto notifier = py::optional(entity.get(), "set_gunAnglesPacked");
    if (py::callable(notifier)) py::invoke(notifier.get(), previous.get());
}

// Drive Vehicle.onSiegeStateUpdated; the player takes Avatar.updateVehicleMiscStatus.
bool updateVehicleSiegeState(Battle& b, std::int64_t entity_id, std::int64_t state, double time_to_next_mode) {
    auto states = need(b.engine.constants.get(), "VEHICLE_SIEGE_STATE");
    const std::int64_t disabled = asInt(py::attr(states.get(), "DISABLED").get()), on = asInt(py::attr(states.get(), "SWITCHING_ON").get());
    const std::int64_t enabled = asInt(py::attr(states.get(), "ENABLED").get()), off = asInt(py::attr(states.get(), "SWITCHING_OFF").get());
    if (state != disabled && state != on && state != enabled && state != off) throw p::Error("Vehicle siegeState is unsupported");
    if (!std::isfinite(time_to_next_mode)) throw p::Error("Vehicle siege transition time must be finite");
    const bool switching = state == on || state == off;
    if ((switching && time_to_next_mode <= 0.) || (!switching && time_to_next_mode != 0.)) throw p::Error("Vehicle siege transition time is invalid");
    auto entity = authorityEntity(b, entity_id);
    auto descriptor = need(entity.get(), "typeDescriptor");
    if (!p::flag(py::optional(descriptor.get(), "hasSiegeMode").get())) {
        if (state != disabled) throw p::Error("Vehicle without Siege mode received an active state");
        return false;
    }
    auto previous = need(entity.get(), "siegeState");
    py::set(entity.get(), "siegeState", p::integer(state).get());
    try {
        if (entity_id == asInt(py::attr(avatar(b), "playerVehicleID").get())) {
            auto status = py::attr(need(b.engine.constants.get(), "VEHICLE_MISC_STATUS").get(), "SIEGE_MODE_STATE_CHANGED");
            p::Ref times(Py_BuildValue("(d)", time_to_next_mode));
            py::invoke(need(avatar(b), "updateVehicleMiscStatus").get(), p::integer(entity_id).get(), status.get(), p::integer(state).get(), times.get());
        } else {
            py::invoke(need(entity.get(), "onSiegeStateUpdated").get(), p::integer(state).get(), p::number(time_to_next_mode).get());
        }
    } catch (...) {
        PyObject *type, *value, *trace;
        PyErr_Fetch(&type, &value, &trace);
        PyObject_SetAttrString(entity.get(), "siegeState", previous.get());
        PyErr_Restore(type, value, trace);
        throw;
    }
    return asInt(previous.get()) != state;
}

void destroyEntity(Battle& b, std::int64_t entity_id) { py::call(b.engine.bigworld.get(), "destroyEntity", p::integer(entity_id).get()); }

// BigWorld has materialized the Vehicle in-world, the gate native consumers share.
bool isVehicleReady(Battle& b, std::int64_t entity_id) {
    PyObject* raw = PyObject_CallMethod(b.engine.bigworld.get(), const_cast<char*>("entity"), const_cast<char*>("L"), (long long)entity_id);
    if (!raw) {
        if (PyErr_ExceptionMatches(PyExc_ReferenceError)) { PyErr_Clear(); return false; }
        throw p::Error("BigWorld.entity failed");
    }
    p::Ref entity(raw);
    if (entity.get() == Py_None) return false;
    const bool in_world = p::flag(py::optional(entity.get(), "inWorld").get()) && p::flag(py::optional(entity.get(), "isStarted").get());
    if (p::flag(py::optional(entity.get(), "_offlineLANPresentation").get())) return in_world && py::optional(entity.get(), "model").get() != Py_None;
    return in_world && py::optional(entity.get(), "typeDescriptor").get() != Py_None;
}
}
}
}
