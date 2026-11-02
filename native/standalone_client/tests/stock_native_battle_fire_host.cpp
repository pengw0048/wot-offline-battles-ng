// Drive the production fire-effect boundary with actual native damage ownership.
#include "../bigworld/stock_native_battle_subsystems.hpp"
#include "../bigworld/stock_native_battle_startup.hpp"

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
Critical::Critical() = default;
namespace critical {
@DAMAGE@
}
namespace vehicles {
@SYNC_FIRE@
}
}
namespace {
namespace b = native_battle;
PyObject* sync(PyObject*, PyObject* args) {
    try {
        PyObject *entity, *states, *override;
        int phase;
        if (!PyArg_ParseTuple(args, "OOOi", &entity, &states, &override, &phase)) return nullptr;
        b::Battle battle;
        battle.phase = b::Phase(phase);
        if (states != Py_None) {
            battle.critical = std::make_unique<b::Critical>();
            Py_ssize_t position = 0;
            PyObject *key, *value;
            while (PyDict_Next(states, &position, &key, &value))
                battle.critical->vehicles[b::p::wide(key)].fire = b::p::flag(value);
        }
        return b::p::boolean(b::vehicles::syncFireEffect(battle, entity,
            override == Py_None ? std::nullopt : std::optional<bool>(b::p::flag(override)))).release();
    } catch (const std::exception& error) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, error.what());
        return nullptr;
    }
}
PyMethodDef methods[] = {{"sync", sync, METH_VARARGS, ""}, {nullptr, nullptr, 0, nullptr}};
}
PyMODINIT_FUNC initbattle_fire_test() { Py_InitModule("battle_fire_test", methods); }
