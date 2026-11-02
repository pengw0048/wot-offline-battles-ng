// The complete production worker and generation-fenced native callbacks, with
// only engine/transport creation replaced by bounded lifecycle boundaries.
#include "../bigworld/stock_native_battle.cpp"
#include "../bigworld/stock_native_battle_bots_worker.cpp"

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
namespace {
int starts = 0, cleanups = 0, disconnects = 0, received = 0, drops = 0;
bool linked = false;
Object test_world;
}
namespace startup { void teardownAfterFailure(Battle&); }
void teardownAfterFailure(Battle& b) { startup::teardownAfterFailure(b); }
namespace remote { p::Ref get(const Battle&, std::int64_t) { return p::retain(Py_None); } }
namespace bots {
bool readyForDrawOff(const Battle&) { return true; }
p::Ref controlSnapshot(const Battle&) { return p::dict(); }
}
namespace link {
bool connect(PyObject*, const std::string&, int, Handler) { linked = true; return true; }
bool connected() { return linked; }
bool authority() { return linked; }
void dropAuthority() { ++drops; linked = false; }
void disconnect(bool) { ++disconnects; linked = false; }
std::string phase() { return "waiting"; }
std::optional<std::int64_t> botAuthorityId() { return linked ? std::optional<std::int64_t>(-1) : std::nullopt; }
bool send(Battle&, const Value&) { return linked; }
}
namespace compat {
void restoreLobbyAccount(Battle&) { py::call(test_world.get(), "restoreLobbyAccount"); }
void disconnect(Battle&) { py::call(test_world.get(), "disconnect"); }
}
namespace startup {
void finishRecording(Battle&, const char*) {}
void cleanup(Battle& b) {
    ++cleanups;
    b.startup->retired_owners.push_back(borrow(test_world.get()));
}
p::Ref module(const char* name) { return p::Ref(PyImport_ImportModule(name)); }
const Value& field(const Value& value, const char* name) { return value.get(name); }
p::Ref toPython(const Value& value) { return p::Ref(cs::write(value)); }
void reportFailure(Battle&, bool);
void scheduleLobbyRestore(Battle&, bool, bool);
// Actual startup::stop and scheduleLobbyRestore, extracted without edits.
#include "worker_session_startup.inc"
bool startRound(PyObject* config, const Value& message, std::string& error) {
    if (const Battle* previous = current()) {
        if ((previous->phase != Phase::Idle && previous->phase != Phase::Stopped && previous->phase != Phase::Failed) ||
            (previous->startup && previous->startup->restore_pending)) {
            error = "native startup rejected a live or restoring predecessor";
            return false;
        }
    }
    auto b = std::make_unique<Battle>();
    b->worker = true;
    b->phase = Phase::Loading;
    b->round_id = message.get("round_id").integer;
    b->start = message;
    b->config = cs::read(config);
    b->startup = std::make_unique<Startup>();
    b->engine.bigworld = borrow(test_world.get());
    install(std::move(b));
    ++starts;
    return true;
}
void stopRound(bool restore) { if (Battle* b = current()) stop(*b, restore); }
void receive(Battle&, const std::string&, const std::shared_ptr<const Value>&) { ++received; }
}
namespace {
PyObject* initialise(PyObject*, PyObject* args) {
    PyObject *world, *ready, *config;
    if (!PyArg_ParseTuple(args, "OOO", &world, &ready, &config)) return nullptr;
    try {
        worker::stop(false);
        worker::releaseScriptOwners();
        retire().reset();
        starts = cleanups = disconnects = received = drops = 0;
        test_world = borrow(world);
        worker::install(config, ready, world);
        return p::boolean(worker::start()).release();
    } catch (const std::exception& e) { PyErr_SetString(PyExc_RuntimeError, describe(e).c_str()); return nullptr; }
}
PyObject* event(PyObject*, PyObject* args) {
    const char* kind; PyObject* raw;
    if (!PyArg_ParseTuple(args, "sO", &kind, &raw)) return nullptr;
    try { worker::onEvent(kind, std::make_shared<const Value>(cs::read(raw))); Py_RETURN_NONE; }
    catch (const std::exception& e) { PyErr_SetString(PyExc_RuntimeError, describe(e).c_str()); return nullptr; }
}
PyObject* phase(PyObject*, PyObject* args) {
    const char* value;
    if (!PyArg_ParseTuple(args, "s", &value)) return nullptr;
    if (!current()) { PyErr_SetString(PyExc_AssertionError, "no native round"); return nullptr; }
    current()->phase = std::string(value) == "running" ? Phase::Running : Phase::Loading;
    Py_RETURN_NONE;
}
PyObject* fail(PyObject*, PyObject*) {
    if (current()) current()->fail("test runtime failure");
    Py_RETURN_NONE;
}
PyObject* shutdown(PyObject*, PyObject*) {
    worker::stop(false);
    worker::releaseScriptOwners();
    retire().reset();
    test_world = Object();
    Py_RETURN_NONE;
}
PyObject* details(PyObject*, PyObject*) {
    auto out = p::dict();
    auto& s = *worker::session;
    p::set(out.get(), "state", p::text(s.state));
    p::set(out.get(), "active", s.active_round ? p::integer(*s.active_round) : p::retain(Py_None));
    p::set(out.get(), "pending", s.pending_start ? p::Ref(cs::write(s.pending_start->get("round_id"))) : p::retain(Py_None));
    p::set(out.get(), "deadline", p::number(s.pending_deadline));
    p::set(out.get(), "linked", p::boolean(s.linked && linked));
    p::set(out.get(), "starts", p::integer(starts));
    p::set(out.get(), "cleanups", p::integer(cleanups));
    p::set(out.get(), "disconnects", p::integer(disconnects));
    p::set(out.get(), "received", p::integer(received));
    p::set(out.get(), "drops", p::integer(drops));
    Battle* b = current();
    p::set(out.get(), "round", b ? p::integer(b->round_id) : p::retain(Py_None));
    p::set(out.get(), "restore_pending", p::boolean(b && b->startup && b->startup->restore_pending));
    p::set(out.get(), "retained_owners", p::integer(b && b->startup ? b->startup->retired_owners.size() : 0));
    return out.release();
}
PyMethodDef entries[] = {
    {"initialise", initialise, METH_VARARGS, nullptr},
    {"event", event, METH_VARARGS, nullptr},
    {"phase", phase, METH_VARARGS, nullptr},
    {"fail", fail, METH_NOARGS, nullptr},
    {"shutdown", shutdown, METH_NOARGS, nullptr},
    {"details", details, METH_NOARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}};
}
}
PyMODINIT_FUNC initworker_session_test() { Py_InitModule("worker_session_test", native_battle::entries); }
