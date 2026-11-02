// Exercise the outline producer and the production target-lock input hooks together.
#include "../bigworld/stock_native_battle_visibility_targeting.cpp"
#include "../bigworld/stock_native_battle_compat_internal.hpp"

namespace native_battle { namespace compat {
PyObject* avatarAutoAim(PyObject*, PyObject*);
PyObject* arcadeKeyEvent(PyObject*, PyObject*);
PyObject* sniperKeyEvent(PyObject*, PyObject*);
} }
namespace native_battle { namespace remote_hooks { PyObject* initialize(); } }
namespace stock_native_client_query {
PyObject* registeredVehicle(PyObject*, std::int64_t) { return nullptr; }
}

namespace native_battle {
namespace { Battle* active_battle = nullptr; }
// These subsystems are never allocated by this input-only host.
struct LocalCriticalOwner {};
struct DestructibleCatalog {};
struct FallenFoliage {};
struct RemoteVehicle {};
struct ReplicaBinding {};
class ReloadSound {};
Visibility::Visibility() = default;
Visibility::~Visibility() = default;
World::~World() = default;
Remote::~Remote() = default;
Critical::~Critical() = default;
Projectiles::~Projectiles() = default;
LocalGun::~LocalGun() = default;
Battle::Battle() = default;
Battle::~Battle() = default;
Battle* current() { return active_battle; }
std::string describe(const std::exception& error) { return error.what(); }
namespace visibility {
VisibilityCore* core(Battle& battle) { return battle.visibility ? battle.visibility->core.get() : nullptr; }
bool enginePresent(std::int64_t, PyObject*) { return false; }
}
}

namespace {
namespace b = native_battle;
PyObject* exercise(PyObject*, PyObject* args) {
    try {
        PyObject *avatar, *control, *candidate, *modules;
        if (!PyArg_ParseTuple(args, "OOOO", &avatar, &control, &candidate, &modules)) return nullptr;
        b::Battle battle;
        battle.compat = std::make_unique<b::Compat>();
        battle.compat->active = true;
        battle.engine.avatar = b::borrow(avatar);
        battle.engine.compatibility = b::own(b::py::attr(modules, "legacy"));
        b::active_battle = &battle;
        struct Guard {
            ~Guard() {
                b::active_battle = nullptr;
                b::compat::stock() = b::compat::Stock{};
                b::compat::mutableScopes() = b::compat::Scopes{};
            }
        } guard;
        auto& stock = b::compat::stock();
        stock.bigworld = b::own(b::py::attr(modules, "bigworld"));
        stock.runtime = b::borrow(modules);
        stock.avatar_getattribute = b::own(b::py::attr(modules, "getattribute"));
        stock.avatar_auto_aim = b::own(b::py::attr(modules, "auto_aim"));
        stock.arcade_key_event = b::own(b::py::attr(modules, "arcade"));
        stock.sniper_key_event = b::own(b::py::attr(modules, "sniper"));
        stock.codes[std::size_t(b::compat::Code::ArcadeHandleKeyEvent)] = b::own(b::py::attr(stock.arcade_key_event.get(), "func_code"));
        stock.codes[std::size_t(b::compat::Code::SniperHandleKeyEvent)] = b::own(b::py::attr(stock.sniper_key_event.get(), "func_code"));
        auto aim = b::compat::hook(b::compat::avatarAutoAim, "autoAim");
        b::py::set(avatar, "autoAim", aim.get());
        // This is the same producer reached by both outline add and clear.
        b::visibility::candidate(battle, candidate);
        auto run = b::py::attr(modules, "run");
        auto arcade = b::compat::hook(b::compat::arcadeKeyEvent, "arcade");
        auto sniper = b::compat::hook(b::compat::sniperKeyEvent, "sniper");
        auto team = b::compat::teamCommandTarget(battle);
        b::py::invoke(run.get(), avatar, control, candidate, arcade.get(), sniper.get(), team.get());
        // Target retirement must read the same round flag and originals as admission.
        b::py::set(avatar, "_PlayerAvatar__autoAimVehID", b::p::integer(123).get());
        if (b::visibility::releaseTargetLock(battle, 999) || !b::visibility::releaseTargetLock(battle, 123) ||
            b::visibility::releaseTargetLock(battle, 123))
            throw b::p::Error("target-specific release did not preserve lock identity");
        battle.visibility = std::make_unique<b::Visibility>();
        battle.visibility->core = std::make_unique<b::VisibilityCore>();
        battle.vehicles = std::make_shared<b::cs::State>();
        b::py::set(avatar, "_PlayerAvatar__autoAimVehID", b::p::integer(456).get());
        b::visibility::validateTargetLock(battle);
        if (b::p::integral(avatar, "_PlayerAvatar__autoAimVehID") != 0)
            throw b::p::Error("lost target stayed locked on a stale legacy battle flag");
        b::visibility::candidate(battle, Py_None);
        if (battle.compat->target_lock_candidate || b::compat::teamCommandTarget(battle).get() != Py_None)
            throw b::p::Error("cleared outline retained a native target");
        Py_RETURN_NONE;
    } catch (const std::exception& e) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
PyObject* remoteHooks(PyObject*, PyObject* args) {
    try {
        PyObject *legacy, *vehicle, *avatar, *appearance, *run;
        if (!PyArg_ParseTuple(args, "OOOOO", &legacy, &vehicle, &avatar, &appearance, &run)) return nullptr;
        b::Battle battle;
        battle.compat = std::make_unique<b::Compat>();
        battle.compat->active = true;
        b::active_battle = &battle;
        struct Guard {
            ~Guard() {
                b::remote::clearOverlays();
                b::active_battle = nullptr;
                b::compat::stock() = b::compat::Stock{};
                b::compat::mutableScopes() = b::compat::Scopes{};
            }
        } guard;
        auto& stock = b::compat::stock();
        stock.vehicle_getattribute = b::own(b::py::attr(legacy, "_original_vehicle_getattribute"));
        stock.codes[std::size_t(b::compat::Code::CompoundModelsRefresh)] = b::own(b::py::attr(run, "func_code"));
        b::compat::mutableScopes().refreshing_models = appearance;
        auto position = b::p::text("current position"), matrix = b::p::text("current matrix");
        b::remote::OverlayPose pose{position.get(), .3, matrix.get(), 9., .2};
        b::remote::publishOverlay(vehicle, pose);
        b::py::invoke(run, legacy, vehicle, avatar, appearance, Py_True);
        battle.compat->active = false;
        b::py::set(legacy, "_battle_active", Py_True);
        b::py::invoke(run, legacy, vehicle, avatar, appearance, Py_False);
        Py_RETURN_NONE;
    } catch (const std::exception& e) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
PyMethodDef methods[] = {{"exercise", exercise, METH_VARARGS, ""}, {"remote_hooks", remoteHooks, METH_VARARGS, ""}, {nullptr, nullptr, 0, nullptr}};
}
PyMODINIT_FUNC initbattle_target_lock_test() {
    if (!b::remote_hooks::initialize()) return;
    Py_InitModule("battle_target_lock_test", methods);
}
