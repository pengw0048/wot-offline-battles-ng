// Run the production process shutdown with partial startup and failing stock cleanup.
#include "../bigworld/stock_native_battle_subsystems.hpp"

namespace native_battle {
// No scene is created in this partial-startup host.
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
namespace { std::unique_ptr<Battle> live; bool fail = false; std::vector<std::string> trace; }
Battle::Battle() = default;
Battle::~Battle() { trace.push_back(current() ? "unsafe-current" : "destroy"); }
Battle* current() { return live.get(); }
std::unique_ptr<Battle> retire() { return std::move(live); }
std::string describe(const std::exception& e) { return e.what(); }
void log(const std::string&) {}
namespace startup { void stopRound(bool restore) { trace.push_back(restore ? "restore" : "stop"); if (fail) throw p::Error("cleanup failed"); } }
namespace worker {
void stop(bool restore) { trace.push_back(restore ? "worker-restore" : "worker"); if (fail) throw p::Error("worker failed"); }
void releaseScriptOwners() { trace.push_back("worker-release"); }
}
namespace remote { void finaliseScriptOwners() { trace.push_back("presentation"); if (fail) throw p::Error("unsubscribe failed"); } }
namespace link { void releaseScriptOwners() { trace.push_back("link-release"); if (fail) throw p::Error("leave failed"); } }
namespace compat {
void uninstall(Battle&) { trace.push_back("hooks"); }
void releaseScriptOwners() { trace.push_back("stock-release"); }
}
PyObject* run(PyObject*, PyObject* args) {
    PyObject* held; int failures;
    if (!PyArg_ParseTuple(args, "Oi", &held, &failures)) return nullptr;
    trace.clear(); fail = failures != 0;
    live = std::make_unique<Battle>();
    live->startup = std::make_unique<Startup>();
    live->startup->retired_owners.push_back(borrow(held));
    const auto before = Py_REFCNT(held);
    startup::shutdown();
    if (current() || Py_REFCNT(held) != before - 1 || PyErr_Occurred()) {
        PyErr_SetString(PyExc_AssertionError, "shutdown retained an owner or exception"); return nullptr;
    }
    startup::shutdown(); // Shutdown remains safe after a failed or completed attempt.
    auto out = p::list();
    for (const auto& row : trace) p::append(out.get(), p::text(row));
    return out.release();
}
}
namespace {
PyMethodDef methods[] = {{"run", native_battle::run, METH_VARARGS, ""}, {nullptr, nullptr, 0, nullptr}};
}
PyMODINIT_FUNC initbattle_shutdown_test() { Py_InitModule("battle_shutdown_test", methods); }
