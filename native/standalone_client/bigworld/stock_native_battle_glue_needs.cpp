// Integration glue: what the projectile owner needs from feedback, startup, local, visibility and the worker Bots.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_feedback_internal.hpp"
#include <cmath>
#include <cstdio>

namespace native_battle {
namespace {
// The pinned _number: a finite float, else 0.
double coordinate(PyObject* value) {
    PyObject* number = value ? PyNumber_Float(value) : nullptr;
    if (!number) { PyErr_Clear(); return 0.; }
    const double v = PyFloat_AS_DOUBLE(number);
    Py_DECREF(number);
    return std::isfinite(v) ? v : 0.;
}
// The pinned _xyz: a dict's x/y/z, a 3-sequence, or a Vector3's attributes.
Point xyz(PyObject* value) {
    Point out{};
    const char* axes[] = {"x", "y", "z"};
    if (PyDict_Check(value)) {
        for (int i = 0; i < 3; ++i) {
            PyObject* item = PyDict_GetItemString(value, axes[i]);
            out[i] = item && item != Py_None ? coordinate(item) : 0.;
        }
        return out;
    }
    if (PySequence_Check(value) && PySequence_Size(value) >= 3) {
        for (int i = 0; i < 3; ++i) out[i] = coordinate(p::item(value, i).get());
        return out;
    }
    PyErr_Clear();
    for (int i = 0; i < 3; ++i) {
        auto item = py::optional(value, axes[i]);
        out[i] = item.get() == Py_None ? 0. : coordinate(item.get());
    }
    return out;
}
std::string repr(PyObject* value) {
    p::Ref text(PyObject_Repr(value ? value : Py_None));
    return p::string(text.get());
}
std::string formatXyz(PyObject* value) {
    try {
        const auto v = xyz(value);
        char line[96];
        std::snprintf(line, sizeof(line), "(%.2f, %.2f, %.2f)", v[0], v[1], v[2]);
        return line;
    } catch (const std::exception&) {
        PyErr_Clear();
        return repr(value);
    }
}
std::string str(const Value& value) {
    p::Ref raw(cs::write(value));
    p::Ref text(PyObject_Str(raw.get()));
    return p::string(text.get());
}
constexpr std::size_t effect_report_limit = 12;
}

namespace needs {
// The pinned _show_shot for an own launch presented after Avatar.shoot (update_state defaults to True).
void showShot(Battle& b, const Value& event, bool local) { feedback::showShot(b, event, local); }

// BattleRuntime._report_effect: a bounded EFFECT diagnostic per round.
void reportEffect(Battle& b, const std::string& kind, PyObject* material, PyObject* effects_index, PyObject* where, PyObject* direction,
                  const Value* event) {
    auto& s = *b.projectiles;
    if (s.effect_reports >= effect_report_limit) return;
    ++s.effect_reports;
    static const Value empty;
    const Value& row = event && event->resolved().truth() ? event->resolved() : empty;
    const auto target = feedback::entityKey(row, false);
    log("EFFECT " + kind + " material=" + repr(material) + " index=" + repr(effects_index) + " at=" + formatXyz(where) +
        " dir=" + formatXyz(direction) + " event=" + str(row.get("event_id")) + " projectile=" + str(row.get("projectile_id")) +
        " target=" + (target ? *target : std::string("None")) + " damage=" + str(row.get("damage")) + " result=" + str(row.get("shot_result")));
}

// BattleRuntime._surface_effect_material: one EFFECT_MATERIALS name, or None.
p::Ref surfaceEffectMaterial(Battle& b, PyObject* impact) {
    const auto material = feedback::surfaceMaterial(b, xyz(impact));
    return material ? p::text(*material) : p::retain(Py_None);
}

p::Ref impactSoundIdentity(Battle& b, const Actor& target, std::int64_t attacker, PyObject* direction, double damage) {
    return feedback::impactSoundIdentity(b, target, attacker, direction, damage);
}

p::Ref resolveDescriptor(Battle& b, const std::string& vehicle) { return startup::descriptorFor(b, vehicle); }

// The local survival warning levels (0 safe, 1 caution, 2 danger) that gate a trigger.
int drownLevel(const Battle& b) { return local::warningLevels(b)[0]; }
int overturnLevel(const Battle& b) { return local::warningLevels(b)[1]; }

// native_client_drive.prepare: the visible player's native world owner.
void prepareDrive(Battle& b) { local::prepare(b); }
// native_client_query.bind: the cursor/projectile query owner.
bool bindQuery(Battle& b) {
    visibility::bind(b);
    return visibility::queryOwner(b) != nullptr;
}
// offline_client_transport_native.send_current after a published shot.
void sendCurrentInput(Battle& b) { local::sendCurrent(b); }

// native_bot_runtime.projectile_*: None until the worker's native Bot owner exists.
std::optional<bool> botProjectileObserve(Battle& b, const Value& message, double now) {
    if (!b.bots || !b.bots->owner) return std::nullopt;
    p::Ref raw(cs::write(message));
    return bots::projectileObserve(b, raw.get(), now);
}
std::optional<bool> botProjectileIngress(Battle& b, const Value& message, double now) {
    if (!b.bots || !b.bots->owner) return std::nullopt;
    p::Ref raw(cs::write(message));
    return bots::projectileIngress(b, raw.get(), now);
}
bool botProjectileEvent(Battle& b, const Value& event, double now) {
    p::Ref raw(cs::write(event));
    return bots::projectileEvent(b, raw.get(), now);
}
std::size_t botProjectileAdvance(Battle& b, double now, bool force) { return bots::projectileAdvance(b, now, force); }
bool botAuthority(const Battle& b) { return bots::isAuthority(b); }

// The pinned _confirm_bot_projectile_launch: acknowledge only server-admitted Bot launches, in Bot order.
void botConfirmLaunch(Battle& b, const Value& normalized) {
    const auto& n = normalized.resolved();
    if (n.kind != Value::Kind::Object || n.get("shooter_kind").resolved().text != "bot" || !b.bots || !bots::isAuthority(b)) return;
    const auto& bot_raw = n.get("shooter_id").resolved();
    const auto& seq_raw = n.get("shot_seq").resolved();
    if (bot_raw.kind != Value::Kind::Integer || seq_raw.kind != Value::Kind::Integer)
        throw p::Error("canonical bot projectile identity is invalid");
    const std::int64_t bot_id = bot_raw.integer, shot_seq = seq_raw.integer;
    auto& s = *b.projectiles;
    std::int64_t confirmed = s.bot_fire_seen.count(bot_id) ? s.bot_fire_seen[bot_id] : 0;
    if (shot_seq <= confirmed) return;
    auto& pending = s.bot_fire_confirmations[bot_id];
    pending.insert(shot_seq);
    while (pending.count(confirmed + 1)) {
        if (!bots::acknowledgeLaunch(b, bot_id, confirmed + 1)) throw p::Error("canonical bot projectile acknowledgement is out of order");
        pending.erase(confirmed + 1);
        ++confirmed;
    }
    if (confirmed > (s.bot_fire_seen.count(bot_id) ? s.bot_fire_seen[bot_id] : 0)) s.bot_fire_seen[bot_id] = confirmed;
    if (pending.empty()) s.bot_fire_confirmations.erase(bot_id);
}
}
}
