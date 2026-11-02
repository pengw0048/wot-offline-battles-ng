// Production readiness publisher, worker message builder and Python object ABI.
#include "../bigworld/stock_native_battle.cpp"
#include "stock_native_battle_link_internal.hpp"

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
void teardownAfterFailure(Battle&) { throw p::Error("unexpected readiness teardown"); }
namespace remote { p::Ref get(const Battle&, std::int64_t) { return p::retain(Py_None); } }
namespace {
link::SenderState sender;
bool transport_connected = true, queue_accepts = true;
int attempts = 0;
Value outgoing;
}
namespace link {
bool connected() { return transport_connected; }
bool authority() { return sender.ready && sender.bot_authority_id == -1 &&
    (sender.phase == "loading" || sender.phase == "battle"); }
std::string phase() { return sender.phase; }
std::optional<std::int64_t> roundId(const Battle&) { return sender.round_id; }
bool send(Battle&, const Value& message) {
    ++attempts;
    auto built = build(sender, message);
    if (built) outgoing = built->wire;
    return built && transport_connected && queue_accepts;
}
}
namespace startup {
const Value& field(const Value& value, const char* name) { return value.get(name); }
Value number(double value) { Value out; out.kind = Value::Kind::Number; out.number = value; return out; }
#include "battle_readiness.inc"
}
namespace {
PyObject* run(PyObject*, PyObject* args) {
    PyObject* client;
    int worker, radius, terminal, complete, accepts;
    if (!PyArg_ParseTuple(args, "Oiiiii", &client, &worker, &radius, &terminal, &complete, &accepts)) return nullptr;
    try {
        Battle b;
        b.worker = worker != 0;
        b.round_id = 7;
        b.phase = Phase::Running;
        b.engine.client = borrow(client);
        b.startup = std::make_unique<Startup>();
        b.roster = std::make_unique<Vehicles>();
        b.start.kind = Value::Kind::Object;
        for (const char* kind : {"player", "bot"}) {
            auto actor = std::make_shared<Actor>();
            actor->kind = kind;
            actor->engine_id = std::string(kind) == "player" ? 1 : 2;
            actor->ready = complete != 0;
            b.vehicles->actors[kind] = actor;
            auto& rows = b.start.object[std::string(kind) + "s"];
            rows.kind = Value::Kind::Array;
            rows.array.emplace_back();
        }
        b.startup->spawn = SpawnPlan();
        b.startup->spawn->bases = {{{{10., 20.}}, {{30., 40.}}}};
        if (radius) b.startup->spawn->base_radii = std::array<double, 2>{{35., 45.}};
        if (terminal) {
            b.result.kind = Value::Kind::Object;
            b.result.object["reason"].kind = Value::Kind::Text;
            b.result.object["reason"].text = "worker_disconnected";
        }
        sender = link::SenderState();
        sender.ready = p::flag(py::attr(client, "ready").get());
        sender.phase = p::string(py::attr(client, "phase").get());
        sender.round_id = p::wide(py::attr(client, "round_id").get());
        sender.bot_authority_id = -1;
        transport_connected = p::flag(py::attr(client, "connected").get());
        queue_accepts = accepts != 0;
        outgoing = Value(); attempts = 0;
        std::string error;
        try {
            startup::maybeSendBattleReady(b);
            startup::maybeSendBattleReady(b);
        } catch (const std::exception& e) { error = describe(e); }
        auto result = p::dict();
        p::set(result.get(), "sent", p::boolean(b.startup->battle_ready_sent));
        p::set(result.get(), "error", p::text(error));
        p::set(result.get(), "attempts", p::integer(attempts));
        p::set(result.get(), "outgoing", p::Ref(cs::write(outgoing)));
        p::set(result.get(), "result", p::Ref(cs::write(b.result)));
        return result.release();
    } catch (const std::exception& e) { PyErr_SetString(PyExc_RuntimeError, describe(e).c_str()); return nullptr; }
}
PyMethodDef methods[] = {{"run", run, METH_VARARGS, nullptr}, {nullptr, nullptr, 0, nullptr}};
}
}
PyMODINIT_FUNC initbattle_readiness_test() { Py_InitModule("battle_readiness_test", native_battle::methods); }
