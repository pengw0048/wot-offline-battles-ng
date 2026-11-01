// Exercise the production combat presenter and native marker identity owner together.
#include "../bigworld/stock_native_battle_subsystems.hpp"
#include "../bigworld/stock_native_battle_compat_internal.hpp"

namespace native_battle {
// These subsystems stay null in this presenter-only host; no native scene is created.
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
Actor* Battle::vehicle(const std::string&) const { return nullptr; }
void Battle::warn(const std::string& feature, const std::string&, bool disable) {
    if (disable) disabled_features.insert(feature);
}
std::string describe(const std::exception& e) { PyErr_Clear(); return e.what(); }
void log(const std::string&) {}
namespace local { p::Ref bodyPose(Battle&) { return p::retain(Py_None); } }
namespace visibility { void resetSpottingTimer(Battle&) {} }
}

namespace {
namespace b = native_battle;
b::Battle* active = nullptr;
PyObject* result(PyObject*, PyObject* args) {
    try {
        PyObject *avatar, *constants, *events;
        int worker;
        if (!PyArg_ParseTuple(args, "OOOi", &avatar, &constants, &events, &worker)) return nullptr;
        b::Battle battle;
        battle.phase = b::Phase::Running;
        battle.live = true;
        battle.worker = worker;
        battle.engine.avatar = b::borrow(avatar);
        battle.engine.constants = b::borrow(constants);
        battle.feedback = std::make_unique<b::Feedback>();
        for (const auto& event : b::cs::read(events).array) b::feedback::battleResult(battle, event);
        auto out = b::p::dict();
        b::p::set(out.get(), "notified", b::p::boolean(battle.feedback->round_finished_notified));
        b::p::set(out.get(), "live", b::p::boolean(battle.live));
        b::p::set(out.get(), "result", b::p::Ref(b::cs::write(battle.result)));
        return out.release();
    } catch (const std::exception& e) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
PyObject* postmortemId(PyObject*, PyObject*) {
    return b::p::integer(active && active->compat ? active->compat->postmortem_vehicle_id : 0).release();
}
PyObject* postmortem(PyObject*, PyObject* args) {
    try {
        PyObject *avatar, *modules, *bigworld, *legacy, *state;
        long long previous_id = 77;
        if (!PyArg_ParseTuple(args, "OOOOO|L", &avatar, &modules, &bigworld, &legacy, &state, &previous_id)) return nullptr;
        b::Battle battle;
        battle.phase = b::Phase::Running;
        battle.engine.avatar = b::borrow(avatar);
        battle.engine.modules = b::borrow(modules);
        battle.engine.math = b::own(b::py::attr(modules, "math"));
        battle.engine.bigworld = b::borrow(bigworld);
        battle.engine.compatibility = b::borrow(legacy);
        battle.engine.server = b::own(b::py::attr(modules, "server"));
        battle.engine.client = b::own(b::py::attr(modules, "client"));
        battle.compat = std::make_unique<b::Compat>();
        battle.compat->active = true;
        battle.compat->postmortem_vehicle_id = previous_id;
        battle.feedback = std::make_unique<b::Feedback>();
        battle.vehicles = std::make_shared<b::cs::State>();
        auto target = std::make_shared<b::Actor>();
        target->engine_id = 92;
        target->ready = true;
        target->presented.state = b::cs::read(state);
        battle.vehicles->actors["bot:2"] = target;
        b::compat::stock().bigworld = b::borrow(bigworld);
        active = &battle;
        bool switched = false;
        std::string error;
        try { switched = b::feedback::switchPostmortemViewpoint(battle, false, 92); }
        catch (const std::exception& e) { error = e.what(); PyErr_Clear(); }
        auto result = b::p::dict();
        b::p::set(result.get(), "switched", b::p::boolean(switched));
        b::p::set(result.get(), "error", b::p::text(error));
        b::p::set(result.get(), "vehicle", b::p::integer(battle.compat->postmortem_vehicle_id));
        b::p::set(result.get(), "spectated", b::p::integer(battle.feedback->spectated_engine_id.value_or(0)));
        active = nullptr;
        b::compat::stock().bigworld = b::Object();
        return result.release();
    } catch (const std::exception& e) {
        active = nullptr;
        b::compat::stock().bigworld = b::Object();
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
PyObject* combat(PyObject*, PyObject* args) {
    try {
        PyObject *avatar, *modules, *bigworld, *legacy, *plugin, *event;
        long long expected;
        int local, target_local, same_team, visible, worker;
        if (!PyArg_ParseTuple(args, "OOOOOOLiiiii", &avatar, &modules, &bigworld, &legacy, &plugin, &event, &expected,
                              &local, &target_local, &same_team, &visible, &worker)) return nullptr;
        b::Battle battle;
        battle.worker = worker;
        battle.engine.avatar = b::borrow(avatar);
        battle.engine.modules = b::borrow(modules);
        battle.engine.bigworld = b::borrow(bigworld);
        battle.engine.compatibility = b::borrow(legacy);
        battle.engine.constants = b::own(b::py::attr(modules, "constants"));
        battle.compat = std::make_unique<b::Compat>();
        battle.compat->player_vehicle_id = expected;
        battle.compat->marker_plugins[plugin] = b::borrow(plugin);
        battle.feedback = std::make_unique<b::Feedback>();
        b::compat::stock().runtime = b::borrow(modules);
        b::Actor attacker, target;
        attacker.local = local;
        attacker.engine_id = 91;
        target.local = target_local;
        target.engine_id = 92;
        auto state = b::p::dict();
        b::p::set(state.get(), "team", b::p::integer(1));
        attacker.presented.state = b::cs::read(state.get());
        b::p::set(state.get(), "team", b::p::integer(same_team ? 1 : 2));
        target.presented.state = b::cs::read(state.get());
        target.presented.spot_marker_visible = visible;
        const bool shown = b::feedback::combat(battle, b::cs::read(event), target, attacker, 0);
        b::compat::stock().runtime = b::Object();
        return b::p::boolean(shown).release();
    } catch (const std::exception& e) {
        b::compat::stock().runtime = b::Object();
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
PyMethodDef methods[] = {{"combat", combat, METH_VARARGS, ""}, {"postmortem", postmortem, METH_VARARGS, ""},
                         {"postmortem_id", postmortemId, METH_NOARGS, ""}, {"result", result, METH_VARARGS, ""},
                         {nullptr, nullptr, 0, nullptr}};
}
PyMODINIT_FUNC initbattle_feedback_test() { Py_InitModule("battle_feedback_test", methods); }
