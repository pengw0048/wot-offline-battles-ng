// Apply real combat events and stock feedback over failing optional engine effects.
#include "../bigworld/stock_native_battle_subsystems.hpp"
#include "../bigworld/stock_native_battle_events_internal.hpp"
#include "../bigworld/stock_native_battle_vehicles_internal.hpp"
#include "../bigworld/stock_native_battle_compat_internal.hpp"

namespace native_battle {
struct VisibilityCore {};
struct LocalCriticalOwner {};
struct RemoteVehicle {};
struct ReplicaBinding {};
struct FallenFoliage {};
struct DestructibleCatalog {};
class ReloadSound {};
Visibility::~Visibility() = default;
Critical::~Critical() = default;
Remote::~Remote() = default;
World::~World() = default;
LocalGun::~LocalGun() = default;
Projectiles::~Projectiles() = default;
Battle::Battle() = default;
Battle::~Battle() = default;
p::Ref Battle::entity(std::int64_t id) const { return py::call(engine.bigworld.get(), "entity", p::integer(id).get()); }
Actor* Battle::vehicle(const std::string& key) const { return vehicles->find(key); }
void Battle::warn(const std::string& feature, const std::string& reason, bool disable) {
    if (disable) disabled_features.insert(feature);
    py::call(engine.modules.get(), "warning", p::text(feature).get(), p::text(reason).get());
}
std::string describe(const std::exception& e) {
    std::string result = e.what();
    if (PyErr_Occurred()) {
        PyObject *type, *value, *trace;
        PyErr_Fetch(&type, &value, &trace);
        if (value) { p::Ref text(PyObject_Str(value)); if (text.get()) result += ": " + p::string(text.get()); }
        Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(trace);
    }
    return result;
}
void log(const std::string&) {}
namespace projectiles { bool missingAttackerAllowed(Battle&, const Value&) { return false; } }
namespace feedback {
bool combatHit(Battle& b, const Value& event, Actor&, Actor&) {
    // The native effect boundary may raise before combatHit reaches its own
    // TerrainEffects guard (descriptor lookup, vectors, or hit-direction UI).
    p::Ref packed(cs::write(event));
    py::call(b.engine.modules.get(), "hit", packed.get());
    return true;
}
}
namespace vehicles {
std::int64_t attackReason(Battle&, const char*, std::int64_t fallback) { return fallback; }
bool suppressPostmortemKiller(Battle&, const Actor&, const Value&, std::int64_t) { return false; }
bool reconcileCriticalAuthority(Battle&, Actor&, const Value&, Value*) { return true; }
bool presentCritical(Battle&, Actor&, const Value&, std::int64_t) { return true; }
void applyHealth(Battle& b, Actor& target, Value& state, const HealthEdge&) {
    auto entity = b.entity(target.engine_id);
    py::set(entity.get(), "health", p::Ref(cs::write(state.get("health"))).get());
    py::call(b.engine.modules.get(), "health", p::integer(target.engine_id).get(), p::Ref(cs::write(state.get("health"))).get());
}
}
namespace critical { Value applyPayload(Battle&, PyObject*, const Value&) { throw p::Error("critical payload rejected"); } }
namespace gun { void edge(Battle&) {} void descriptorChanged(Battle&, PyObject*) {} }
}

namespace {
namespace b = native_battle;
PyObject* exercise(PyObject*, PyObject* args) {
    try {
        PyObject *avatar, *modules, *world, *plugin, *events;
        int incoming;
        if (!PyArg_ParseTuple(args, "OOOOOi", &avatar, &modules, &world, &plugin, &events, &incoming)) return nullptr;
        b::Battle battle;
        battle.engine.avatar = b::borrow(avatar);
        battle.engine.modules = b::borrow(modules);
        battle.engine.bigworld = b::borrow(world);
        battle.engine.constants = b::own(b::py::attr(modules, "constants"));
        battle.compat = std::make_unique<b::Compat>();
        battle.compat->player_vehicle_id = 91;
        battle.compat->marker_plugins[plugin] = b::borrow(plugin);
        battle.feedback = std::make_unique<b::Feedback>();
        battle.events = std::make_unique<b::Events>();
        battle.vehicles = std::make_shared<b::cs::State>();
        for (int i = 0; i < 2; ++i) {
            auto actor = std::make_shared<b::Actor>();
            actor->key = i ? "bot:2" : "player:1";
            actor->engine_id = 91 + i;
            actor->local = i == 0;
            actor->presented.spot_marker_visible = true;
            auto state = b::p::dict();
            b::p::set(state.get(), "team", b::p::integer(i + 1));
            b::p::set(state.get(), "health", b::p::integer(500));
            b::p::set(state.get(), "alive", b::p::boolean(true));
            actor->presented.state = b::cs::read(state.get());
            battle.vehicles->actors[actor->key] = actor;
        }
        b::compat::stock().runtime = b::borrow(modules);
        struct Guard { ~Guard() { b::compat::stock() = b::compat::Stock{}; } } guard;
        auto errors = b::p::list();
        for (const auto& event : b::cs::read(events).array) {
            try { b::events::applyCombat(battle, event, true); }
            catch (const std::exception& error) { b::p::append(errors.get(), b::p::text(b::describe(error))); }
        }
        auto result = b::p::dict();
        b::p::set(result.get(), "errors", errors);
        b::p::set(result.get(), "disabled", b::p::boolean(!battle.disabled_features.empty()));
        const auto* target = battle.vehicle(incoming ? "player:1" : "bot:2");
        b::p::set(result.get(), "health", b::p::Ref(b::cs::write(target->presented.state.get("health"))));
        return result.release();
    } catch (const std::exception& error) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, error.what());
        return nullptr;
    }
}
PyMethodDef methods[] = {{"exercise", exercise, METH_VARARGS, ""}, {nullptr, nullptr, 0, nullptr}};
}
PyMODINIT_FUNC initbattle_combat_event_test() { Py_InitModule("battle_combat_event_test", methods); }
