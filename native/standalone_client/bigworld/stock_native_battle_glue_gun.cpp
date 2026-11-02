// Integration glue: the calls other subsystems make into the local gun.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_startup_needs.hpp"
#include "stock_native_battle_pending.hpp"
#include "stock_native_battle_feedback_internal.hpp"
#include "stock_native_battle_visibility_core.hpp"
#include "stock_native_client_weapon_runtime.hpp"
#include "stock_native_client_weapon_python.hpp"
#include <cmath>
#include <random>

namespace native_battle {
namespace weapon = stock_native_client_weapon_runtime;
namespace {
stock_native_client_weapon::Gun* nativeGun(const Battle& b) {
    return b.gun && b.gun->gun ? stock_native_client_weapon::gun(b.gun->gun.get()) : nullptr;
}
p::Ref integers(const std::vector<int>& values) {
    p::Ref list(PyList_New(Py_ssize_t(values.size())));
    for (std::size_t i = 0; i < values.size(); ++i) PyList_SET_ITEM(list.get(), Py_ssize_t(i), p::integer(values[i]).release());
    return list;
}
// tuple(g.get('ammo', ())) of a recorded gun row.
std::vector<double> ammoOf(const Value& gun) {
    std::vector<double> out;
    const auto& ammo = gun.get("ammo").resolved();
    if (ammo.kind == Value::Kind::Array)
        for (const auto& n : ammo.array) out.push_back(n.numeric());
    return out;
}
struct ReloadPublication { double left, duration; std::string reason; };
// replay_presentation.ReloadEdges.observe: one HUD edge per recorded reload cycle, never per render frame.
std::optional<ReloadPublication> observeReplayReload(LocalGun::ReplayReload& e, const Value& gun, double stamp, double now, bool live) {
    const auto& g = gun.resolved();
    if (g.kind != Value::Kind::Object || !g.object.count("reload_time")) return std::nullopt;
    const auto previous = e.previous;
    const bool was_live = e.live;
    e.live = live;
    e.previous = g;
    const double raw = std::max(0., g.get("reload_time").numeric());
    const double duration = std::max(0., g.get("reload_duration").numeric(0.));
    if (!live) {
        // Initial publication is quiet; the ammo controller performs it directly.
        e.deadline.reset();
        e.positive = false;
        return std::nullopt;
    }
    const bool positive = raw > 0.;
    std::string reason;
    if (positive) {
        if (!was_live || !previous || !e.positive) reason = "start";
        else {
            const auto& p = previous->resolved();
            const bool changed_shell = g.get("shot_index").resolved() != p.get("shot_index").resolved();
            const bool ammo_changed = ammoOf(g) != ammoOf(p);
            const bool clip_changed = g.get("clip").resolved() != p.get("clip").resolved();
            const bool duration_changed = std::fabs(duration - p.get("reload_duration").numeric(0.)) > 1e-5;
            const bool restarted = raw > p.get("reload_time").numeric(0.) + 0.15;
            if (changed_shell || ammo_changed || clip_changed || duration_changed || restarted) reason = "cycle_change";
        }
    } else if (e.positive) {
        reason = "complete";
    }
    // Purely monotonic countdown samples do not reset the native reloadEffect.
    e.positive = positive;
    if (reason.empty()) return std::nullopt;
    double left = 0.;
    if (positive) {
        // A late positive record is not a completion; its recorded zero successor owns that edge.
        left = std::max(1e-6, std::max(0., raw - std::max(0., now - stamp)));
        e.deadline = stamp + raw;
        ++e.starts;
    } else {
        e.deadline.reset();
        ++e.completions;
    }
    ++e.notifications;
    return ReloadPublication{left, duration, reason};
}

// ---- change_vehicle_setting ---------------------------------------------------------------------------------
std::optional<std::int64_t> settingCode(Battle& b, const char* name) {
    auto settings = py::attr(b.engine.constants.get(), "VEHICLE_SETTING");
    auto code = py::optional(settings.get(), name);
    if (code.get() == Py_None) return std::nullopt;
    return p::wide(code.get());
}
// BattleRuntime._defer_gun_setting: replayed in order against the settled gun once the launch or burst ends.
void deferSetting(DeferredSettings& pending, const stock_native_client_weapon::Gun& g, std::int64_t code, std::int64_t value) {
    if (!pending.present) {
        pending.present = true;
        pending.pending_index = g.pending;
    }
    pending.rows.emplace_back(code, value);
}
DeferredSettings* pendingFire(Battle& b, bool burst_only) {
    if (b.gun->burst) return &b.gun->burst->deferred;
    if (!burst_only && b.gun->intent) return &b.gun->intent->deferred;
    return nullptr;
}
// The pinned _publish_loaded_shell_change: close an active cycle, republish ammunition, start the new cycle.
void publishLoadedShellChange(Battle& b, const stock_native_client_weapon::Gun& g, double previous_reload, double previous_duration) {
    const double now = b.clock();
    if (previous_reload > 0.) gun::publishKind(b, "reload", now, true, 0., previous_duration);
    gun::publishKind(b, "ammo", now, true);
    gun::publishKind(b, "reload", now, true, g.reload_time, g.reload_duration);
    local::sendCurrent(b);
}
// BattleRuntime._roll_loader_intuition: each conscious loader with the perk rolls its own chance.
bool rollLoaderIntuition(Battle& b) {
    if (b.worker) return false;
    if (b.startup->effective_params.resolved().kind == Value::Kind::Null) throw p::Error("player effective parameters are unavailable for intuition");
    static std::mt19937_64 random{std::random_device{}()};
    std::uniform_real_distribution<double> uniform(0., 1.);
    const auto chances = startup::skillCount(b, "loader_intuition");
    for (std::int64_t i = 0; i < chances; ++i)
        if (uniform(random) < params::INTUITION_CHANCE) return true;
    return false;
}
// The stock intuition notification; #1513 indexes floatArgs[0] before it dispatches the status.
void presentLoaderIntuition(Battle& b) {
    b.feature("loader intuition notification", [&] {
        auto status = py::optional(py::optional(b.engine.constants.get(), "VEHICLE_MISC_STATUS").get(), "LOADER_INTUITION_WAS_USED");
        auto callback = py::optional(b.engine.avatar.get(), "updateVehicleMiscStatus");
        if (status.get() == Py_None || !py::callable(callback) || !b.engine.server) return;
        p::Ref args(Py_BuildValue("(d)", 0.));
        py::invoke(callback.get(), py::attr(b.engine.server.get(), "vehicle_id").get(), status.get(), p::integer(0).get(), args.get());
    }, false);
}
// BattleRuntime._switch_current_shell
bool switchCurrentShell(Battle& b, stock_native_client_weapon::Gun& g, int index) {
    gun::edge(b);
    const double previous_reload = g.reload_time, previous_duration = g.reload_duration;
    const bool instant = g.config.clip_size <= 1 && g.config.burst_count <= 1 && rollLoaderIntuition(b);
    if (!g.select(index, instant)) return false;
    if (!b.replay) g.rescale(weapon::reloadFactor(b));
    publishLoadedShellChange(b, g, previous_reload, previous_duration);
    if (instant) presentLoaderIntuition(b);
    return true;
}
// BattleRuntime._reload_partial_clip_now
bool reloadPartialClipNow(Battle& b, stock_native_client_weapon::Gun& g) {
    if (b.replay) return false;
    gun::edge(b);
    const double previous_reload = g.reload_time, previous_duration = g.reload_duration;
    if (!g.partialReload()) return false;
    g.rescale(weapon::reloadFactor(b));
    publishLoadedShellChange(b, g, previous_reload, previous_duration);
    return true;
}
// BattleRuntime._critical_name_from_extra_index: the descriptor extra a kit targets.
std::optional<std::string> criticalNameFromExtraIndex(PyObject* descriptor, std::int64_t extra_index) {
    auto extras = py::optional(descriptor, "extras");
    p::Ref rows(p::retain(Py_None));
    if (py::callable(py::optional(extras.get(), "items"))) replace(rows, py::call(extras.get(), "items"));
    else if (p::flag(extras.get())) {
        p::Ref listed(PySequence_List(extras.get()));
        p::Ref pairs(PyList_New(0));
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(listed.get()); ++i)
            p::append(pairs.get(), p::Ref(Py_BuildValue("(nO)", i, PyList_GET_ITEM(listed.get(), i))));
        replace(rows, std::move(pairs));
    }
    if (rows.get() == Py_None) return std::nullopt;
    p::Ref items(PySequence_List(rows.get()));
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(items.get()); ++i) {
        PyObject* row = PyList_GET_ITEM(items.get(), i);
        PyObject* index = PyNumber_Int(PySequence_Fast_GET_ITEM(p::Ref(PySequence_Fast(row, "extra row")).get(), 0));
        if (!index) { PyErr_Clear(); continue; }
        const auto matches = PyInt_AsLong(index) == extra_index;
        Py_DECREF(index);
        if (!matches) continue;
        auto name = py::optional(p::item(row, 1).get(), "name");
        return p::flag(name.get()) ? p::string(p::Ref(PyObject_Str(name.get())).get()) : std::string();
    }
    return std::nullopt;
}
// BattleRuntime._activate_equipment: one ordered trigger to the server without a local critical verdict.
bool activateEquipment(Battle& b, PyObject* raw) {
    PyObject* parsed = PyNumber_Int(raw);
    if (!parsed) { PyErr_Clear(); return false; }
    const std::int64_t activation_code = p::wide(p::Ref(parsed).get());
    if (!b.params || !b.params->equipment) return false;
    const std::int64_t equipment_id = activation_code & 65535, extra_index = std::max<std::int64_t>(0, activation_code >> 16);
    const params::EquipmentState* equipment = nullptr;
    for (const auto& row : *b.params->equipment)
        if (row.contract.id == equipment_id) { equipment = &row; break; }
    if (!equipment || !b.localVehicle() || !b.engine.server) return false;
    const auto& kind = equipment->contract.kind;
    std::optional<std::string> selected;
    if (!equipment->contract.repair_all && (kind == "repairkit" || kind == "medkit")) {
        auto entity = b.entity(p::wide(py::attr(b.engine.server.get(), "vehicle_id").get()));
        auto descriptor = py::optional(entity.get(), "typeDescriptor");
        if (descriptor.get() == Py_None) return false;
        selected = criticalNameFromExtraIndex(descriptor.get(), extra_index);
    }
    if (kind == "medkit" && selected && selected->size() >= 6 && selected->compare(selected->size() - 6, 6, "Health") == 0)
        selected->resize(selected->size() - 6);
    auto sender = py::optional(b.engine.client.get(), "send_equipment_intent");
    if (!py::callable(sender)) return false;
    auto keywords = p::dict();
    p::set(keywords.get(), "activation_code", p::integer(activation_code));
    p::set(keywords.get(), "selected", kind == "rpm_limiter" || !selected ? p::retain(Py_None) : p::text(*selected));
    p::set(keywords.get(), "requested_active", kind == "rpm_limiter" ? p::boolean(extra_index != 0) : p::retain(Py_None));
    p::Ref args(PyTuple_Pack(1, p::integer(equipment_id).get()));
    p::Ref sequence(PyObject_Call(sender.get(), args.get(), keywords.get()));
    return sequence.get() != Py_None;
}
}

namespace gun {
// Creates the player's native gun from the accepted loadout and the frozen garage ammunition.
void start(Battle& b, PyObject* descriptor) {
    if (b.worker) return;
    if (!b.gun) b.gun = std::make_unique<LocalGun>();
    const auto accepted = params::acceptedLocalEffectiveParams(b);
    p::Ref loadout(p::retain(Py_None));
    const auto& garage = startup::garage(b);
    if (accepted.resolved().kind != Value::Kind::Null) {
        const auto& row = accepted.get("loadout");
        if (row.resolved().kind != Value::Kind::Object) throw p::Error("player effective loadout is unavailable");
        replace(loadout, p::Ref(cs::write(row)));
    } else {
        // The garage snapshot remains a startup fallback when no server-accepted row exists.
        PyObject* crew = garage.crew && p::flag(garage.crew.get()) ? garage.crew.get() : nullptr;
        std::optional<params::CrewSkills> skills;
        if (crew) skills = params::crewSkillNames(crew);
        auto factors_ref = params::localFactors(b, descriptor);
        std::optional<params::Factors> factors;
        if (factors_ref.get() != Py_None) { factors.emplace(); factors->values = cs::read(factors_ref.get()); }
        const auto modifiers = params::modifiers(descriptor, garage.equipments ? garage.equipments.get() : nullptr, skills ? &*skills : nullptr,
                                                 factors ? &*factors : nullptr);
        replace(loadout, p::Ref(cs::write(modifiers.toValue())));
    }
    p::Ref layout(p::retain(Py_None));
    if (garage.shells_known) {
        replace(layout, p::dict());
        for (const auto& shell : garage.shells)
            if (PyDict_SetItem(layout.get(), p::integer(shell.first).get(), p::integer(shell.second).get()) < 0) throw p::Error("ammo layout");
    }
    p::Ref order(PyTuple_New(Py_ssize_t(garage.shell_order.size())));
    for (std::size_t i = 0; i < garage.shell_order.size(); ++i) PyTuple_SET_ITEM(order.get(), Py_ssize_t(i), p::integer(garage.shell_order[i]).release());
    p::Ref args(PyTuple_Pack(2, descriptor, loadout.get()));
    auto keywords = p::dict();
    p::set(keywords.get(), "ammo_layout", std::move(layout));
    p::set(keywords.get(), "shell_order", std::move(order));
    p::Ref created(stock_native_client_weapon::create(nullptr, args.get(), keywords.get()));
    if (b.replay) {
        // A replay plays the recorded vehicle; its module values must match the current resource profile.
        auto presentation = p::Ref(PyImport_ImportModule("gui.mods.offline_lan_0922.replay_presentation"));
        p::Ref snapshot(cs::write(accepted));
        py::call(presentation.get(), "verify_descriptor", descriptor, snapshot.get());
        auto header = py::attr(py::attr(b.engine.client.get(), "reader").get(), "header");
        auto original = py::call(header.get(), "get", p::text("local_descriptor_contract").get());
        if (original.get() != Py_None) {
            auto contract = py::call(presentation.get(), "descriptor_contract", descriptor);
            if (!p::flag(py::call(presentation.get(), "_equal_values", original.get(), contract.get()).get()))
                throw p::Error("recorded vehicle/module values differ from the current resource profile");
        }
    }
    bind(b, created.get(), b.clock());
}

// The accepted equipment ledger: the live gun runtime while running, the cold restore while loading and in replay.
void restoreEquipment(Battle& b, const Value& snapshot, bool present) {
    if (b.worker || !b.gun) return;
    if (b.running() && !b.replay) { equipmentSnapshot(b, snapshot, present); return; }
    params::restoreEquipmentCold(b, snapshot, present);
}

// BattleRuntime._set_gun_locked: a replay's saved pose owns the gun, so period edges never release it.
void setLocked(Battle& b, bool locked) {
    if (b.worker || !b.engine.avatar) return;
    auto rotator = py::optional(b.engine.avatar.get(), "gunRotator");
    auto lock = py::optional(rotator.get(), "lock");
    if (!py::callable(lock)) throw p::Error("#1513 gun lock boundary is unavailable");
    PyObject* value = locked || b.replay ? Py_True : Py_False;
    py::set(b.engine.avatar.get(), "isGunLocked", value);
    py::invoke(lock.get(), value);
}

void publishReload(Battle& b, bool force) {
    auto* g = nativeGun(b);
    if (!g || !b.engine.server) return;
    publishKind(b, "reload", b.clock(), force, g->reload_time, g->reload_duration);
}

// Keeps one 10 Hz gun clock scheduled.
void ensureTicking(Battle& b) {
    if (b.worker || !b.gun || b.gun->ammo_callback) return;
    ammoTick(b);
}

double activeRepairBonus(Battle& b) { return weapon::activeRepairBonus(b); }
void publishEquipment(Battle& b, double now) { publishKind(b, "equipment", now); }

// The pinned _local_skill_count: conscious carriers in the accepted round snapshot.
int livingSkillCount(Battle& b, const std::string& skill) { return int(startup::skillCount(b, skill.c_str())); }

std::optional<std::pair<std::int64_t, std::int64_t>> localBurst(const Battle& b) {
    if (!b.gun || !b.gun->intent || !b.gun->intent->burst || !b.gun->burst) return std::nullopt;
    return std::make_pair(std::int64_t(b.gun->burst->committed), std::int64_t(b.gun->burst->count));
}

// A Siege or critical edge changed the mounted descriptor or its factors: adopt the mode law, then close the
// gun timeline the caller opened with edge() and donate a changed law to the server.
void descriptorChanged(Battle& b, PyObject* entity) {
    if (b.worker || !b.gun || !b.gun->gun) return;
    auto descriptor = py::attr(entity, "typeDescriptor");
    const bool changed = p::flag(py::call(b.gun->gun.get(), "adopt_descriptor", descriptor.get()).get());
    if (b.replay || !b.gun->last_tick) return;
    weapon::advanceWeapon(b, b.clock());
    if (changed) local::sendCurrent(b);
}

// A stun edge: the advanced gun rescales its reload and republishes targeting with the new factors.
void stunChanged(Battle& b, PyObject*) {
    if (b.worker || b.replay || !b.gun || !b.gun->gun || !b.gun->last_tick) return;
    weapon::advanceWeapon(b, b.clock());
}

// offline_replay.local_state's gun row.
p::Ref replayState(Battle& b) {
    const auto* g = nativeGun(b);
    if (!g) return p::retain(Py_None);
    auto row = p::dict();
    p::set(row.get(), "ammo", integers(g->ammunition));
    p::set(row.get(), "shot_index", p::integer(g->shot));
    p::set(row.get(), "clip", p::integer(g->clip));
    p::set(row.get(), "reload_time", p::number(g->reload_time));
    p::set(row.get(), "reload_duration", p::number(g->reload_duration));
    p::set(row.get(), "load_started", p::boolean(g->load_started));
    p::set(row.get(), "pending_index", g->pending ? p::integer(*g->pending) : p::retain(Py_None));
    p::set(row.get(), "dispersion", p::number(g->dispersion));
    return row;
}

// BattleRuntime.apply_replay_local: the validated row becomes the replay's local presentation source.
void applyReplayLocal(Battle& b, PyObject* message) {
    if (!b.gun) return;
    b.gun->replay_local = cs::read(message);
    applyReplayAmmo(b, b.gun->replay_local);
}

// BattleRuntime._apply_replay_ammo: the recorded gun owns ammunition and reload; HUD edges follow recorded cycles.
bool applyReplayAmmo(Battle& b, const Value& sample) {
    auto* g = nativeGun(b);
    if (!b.replay || !g || !b.engine.server) return false;
    const auto& gun = sample.get("gun").resolved();
    if (gun.kind == Value::Kind::Object) {
        for (const auto& item : gun.object) {
            const auto& name = item.first;
            const auto& v = item.second.resolved();
            if (name == "ammo") {
                g->ammunition.clear();
                for (const auto& n : v.array) g->ammunition.push_back(int(n.numeric()));
            } else if (name == "shot_index") g->shot = int(v.numeric());
            else if (name == "clip") g->clip = int(v.numeric());
            else if (name == "reload_time") g->reload_time = v.numeric();
            else if (name == "reload_duration") g->reload_duration = v.numeric();
            else if (name == "dispersion") g->dispersion = v.numeric();
            else if (name == "load_started") g->load_started = v.truth();
            else if (name == "pending_index") {
                if (v.kind == Value::Kind::Null) g->pending.reset();
                else g->pending = int(v.numeric());
            }
        }
    }
    const double stamp = sample.get("_replay_t").numeric(0.);
    double now = stamp;
    auto presentation_time = py::optional(b.engine.client.get(), "presentation_time");
    if (py::callable(presentation_time)) now = p::scalar(py::invoke(presentation_time.get()).get());
    const auto publication = observeReplayReload(b.gun->replay_reload, gun, stamp, now, b.live);
    struct Publishing {
        LocalGun& gun;
        explicit Publishing(LocalGun& g) : gun(g) { gun.replay_publishing = true; }
        ~Publishing() { gun.replay_publishing = false; }
    } publishing(*b.gun);
    publishKind(b, "ammo", now);
    params::presentEquipment(b, b.clock());
    if (publication) publishKind(b, "reload", now, true, publication->left, publication->duration);
    return true;
}

// BattleRuntime.change_vehicle_setting without Siege (the local owner handles SIEGE_MODE_ENABLED).
bool changeSetting(Battle& b, int code, PyObject* value) {
    if (code == settingCode(b, "ACTIVATE_EQUIPMENT")) return activateEquipment(b, value);
    auto* g = nativeGun(b);
    if (code == settingCode(b, "RELOAD_PARTIAL_CLIP")) {
        if (!g) return false;
        if (auto* pending = pendingFire(b, false)) {
            if (g->config.clip_size <= 1 || g->ammunition.empty()) return false;
            deferSetting(*pending, *g, code, 0);
            return true;
        }
        return reloadPartialClipNow(b, *g);
    }
    const auto current_shells = settingCode(b, "CURRENT_SHELLS"), next_shells = settingCode(b, "NEXT_SHELLS");
    if ((code != current_shells && code != next_shells) || !g) return false;
    PyObject* parsed = PyNumber_Long(value);
    if (!parsed) { PyErr_Clear(); return false; }
    const std::int64_t wanted = p::wide(p::Ref(parsed).get());
    auto shots = py::attr(b.gun->gun.get(), "shots");
    for (Py_ssize_t i = 0; i < p::size(shots.get()); ++i) {
        auto shell = py::optional(p::item(shots.get(), i).get(), "shell");
        auto compact = PyDict_Check(shell.get()) ? p::value(shell.get(), "compactDescr") : py::optional(shell.get(), "compactDescr");
        if ((compact.get() == Py_None ? 0 : p::wide(compact.get())) != wanted) continue;
        const int index = int(i);
        if (b.gun->burst) {
            deferSetting(b.gun->burst->deferred, *g, code, index);
            return true;
        }
        if (code == current_shells) {
            if (b.gun->intent) {
                // The physical round was frozen at the trigger edge; queue the requested shell for the shot boundary.
                edge(b);
                const double previous_reload = g->reload_time, previous_duration = g->reload_duration;
                const auto previous_selection = std::make_pair(g->shot, g->pending);
                deferSetting(b.gun->intent->deferred, *g, code, index);
                if (g->request(index)) {
                    if (!b.replay) g->rescale(weapon::reloadFactor(b));
                    publishLoadedShellChange(b, *g, previous_reload, previous_duration);
                } else if (previous_selection != std::make_pair(g->shot, g->pending)) {
                    local::sendCurrent(b);
                }
                return true;
            }
            switchCurrentShell(b, *g, index);
            return true;
        }
        edge(b);
        const double previous_reload = g->reload_time, previous_duration = g->reload_duration;
        const auto previous_selection = std::make_pair(g->shot, g->pending);
        if (b.gun->intent && b.gun->intent->deferred.present) deferSetting(b.gun->intent->deferred, *g, code, index);
        if (g->request(index)) {
            if (!b.replay) g->rescale(weapon::reloadFactor(b));
            publishLoadedShellChange(b, *g, previous_reload, previous_duration);
        } else if (previous_selection != std::make_pair(g->shot, g->pending)) {
            // NEXT_SHELLS is a real ordered input: donate it so the authority promotes the same shell.
            local::sendCurrent(b);
        }
        return true;
    }
    return false;
}
}
}
