// Exercise both production recording-start owners over observable recorder calls.
#include "../bigworld/stock_native_battle_startup_session.cpp"
#include "../bigworld/stock_native_client_recording.cpp"

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
std::string describe(const std::exception& e) { PyErr_Clear(); return e.what(); }
namespace startup {
const GarageLoadout& garage(Battle& b) { return b.startup->garage; }
p::Ref callKw(PyObject* fn, std::initializer_list<PyObject*> args,
              std::initializer_list<std::pair<const char*, PyObject*>> keywords) {
    p::Ref positional(PyTuple_New(Py_ssize_t(args.size())));
    Py_ssize_t i = 0;
    for (auto* value : args) { Py_INCREF(value); PyTuple_SET_ITEM(positional.get(), i++, value); }
    auto named = p::dict();
    for (const auto& item : keywords) PyDict_SetItemString(named.get(), item.first, item.second);
    return p::Ref(PyObject_Call(fn, positional.get(), named.get()));
}
}
}
namespace {
namespace b = native_battle;
PyObject* startup(PyObject*, PyObject* runtime) {
    try {
        b::Battle battle;
        battle.phase = b::p::string(b::py::attr(runtime, "state").get()) == "running" ? b::Phase::Running : b::Phase::Stopped;
        battle.worker = b::p::flag(b::py::attr(runtime, "_worker_mode").get());
        battle.startup = std::make_unique<b::Startup>();
        battle.startup->client_ready_received = b::p::flag(b::py::attr(runtime, "_client_ready_received").get());
        battle.startup->battle_ready_sent = b::p::flag(b::py::attr(runtime, "_ready_sent").get());
        battle.startup->local_descriptor = b::own(b::py::attr(runtime, "_local_descriptor"));
        battle.start = b::cs::read(b::py::attr(runtime, "_start_message").get());
        battle.config = b::cs::read(b::py::attr(runtime, "_config").get());
        battle.engine.client = b::own(b::py::attr(runtime, "client"));
        battle.engine.bigworld = b::own(b::py::attr(runtime, "bigworld"));
        battle.local_player = b::p::integral(battle.engine.client.get(), "player_id");
        b::startup::ensureRecording(battle);
        Py_RETURN_NONE;
    } catch (const std::exception& e) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
PyMethodDef methods[] = {{"startup", startup, METH_O, ""}, {nullptr, nullptr, 0, nullptr}};
}
PyMODINIT_FUNC initreplay_boundary_test() {
    PyObject* module = Py_InitModule("replay_boundary_test", methods);
    if (!module) return;
    Py_InitModule("replay_boundary_test", stock_native_client_lifecycle::recording_methods());
}
