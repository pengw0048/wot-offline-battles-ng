// Exercise the real worker-link owner without starting transport threads.
#include "../bigworld/stock_native_battle_link.cpp"

namespace {
bool fail_send = false;
int host_receives = 0;
bool transport_destroyed = false, detached_at_close = false;
PyObject* held_world = nullptr;
Py_ssize_t references_at_close = 0;
std::vector<std::string> trace;
}
namespace wot::room {
struct Host::Impl {};
Host::Host(const std::string&, Codec, EffectiveParamsValidator) : impl_(new Impl) {}
Host::~Host() {
    transport_destroyed = true;
    detached_at_close = !native_battle::link::link;
    references_at_close = Py_REFCNT(held_world);
    trace.push_back("host-destroy");
}
void Host::stop() {
    trace.push_back("stop");
    if (fail_send) throw std::runtime_error("host stop failed");
}
Channel::Admission Host::send(std::shared_ptr<const Value>,std::optional<std::string>) { return Channel::Admission::admitted; }
std::vector<Channel::Entry> Host::receive(std::size_t) { ++host_receives;return {}; }
Host::Status Host::status() const { return {}; }
std::pair<Channel::Metrics,Channel::Metrics> Host::metrics() const { return {}; }
}
namespace stock_wire {
std::size_t frame_size(const unsigned char*,std::size_t) { return 0; }
bool decode_value(const unsigned char*,std::size_t,stock_native_client_state::Value&,std::string&) { return false; }
bool encode_value(const stock_native_client_state::Value&,std::string&,std::string&) { return false; }
}
namespace native_battle {
void log(const std::string&) {}
std::string describe(const std::exception& error) { return error.what(); }
namespace params { std::optional<Value> canonicalEffectiveParams(const Value& value) { return value; } }
namespace link { std::optional<Outgoing> build(SenderState&,const Value& value) { return Outgoing{value,{}}; } }
}
namespace {
PyObject* run(PyObject*, PyObject* args) {
    PyObject* world; int failure;
    if (!PyArg_ParseTuple(args, "Oi", &world, &failure)) return nullptr;
    namespace b = native_battle;
    fail_send = failure != 0;
    transport_destroyed = detached_at_close = false;
    trace.clear(); held_world = world;
    const auto before = Py_REFCNT(world);
    b::link::link = std::make_unique<b::link::Link>();
    b::link::link->bigworld = b::borrow(world);
    b::link::link->poll_id = b::own(b::p::integer(42));
    b::link::link->room = std::make_shared<wot::room::Host>("", wot::room::Codec{}, wot::room::EffectiveParamsValidator{});
    b::link::link->connected = b::link::link->running = true;
    bool failed = false;
    try { b::link::releaseScriptOwners(); }
    catch (const std::exception&) { failed = true; PyErr_Clear(); }
    if (failed != fail_send || b::link::link || !transport_destroyed || !detached_at_close ||
        references_at_close != before + 1 || Py_REFCNT(world) != before) {
        PyErr_SetString(PyExc_AssertionError, "worker link retained script/host or stopped after its script handles");
        return nullptr;
    }
    b::link::releaseScriptOwners();
    auto out = b::p::list();
    for (const auto& row : trace) b::p::append(out.get(), b::p::text(row));
    return out.release();
}
PyObject* reconnect(PyObject*,PyObject* world) {
    namespace b=native_battle;
    try {
        fail_send=false;held_world=world;host_receives=0;
        setenv("OFFLINE_LAN_0922_ROOM_HOST_CONFIG","fixture",1);
        auto connect=[&](b::link::Handler handler) { return b::link::connect(world,"",0,std::move(handler)); };
        int old_deliveries=0,new_deliveries=0;
        connect([&](const std::string&,std::shared_ptr<const b::Value>) {
            ++old_deliveries;
            b::link::releaseScriptOwners();
            connect([&](const std::string&,std::shared_ptr<const b::Value>) { ++new_deliveries; });
        });
        const auto old_generation=b::link::link->generation;
        auto old_poll=b::py::attr(world,"last");
        std::vector<b::Value> batch;
        for(const char* kind:{"error","error"}) {
            auto message=b::link::object();message.object["type"]=b::link::text(kind);batch.push_back(std::move(message));
        }
        b::link::dispatchBatch(batch,0.);
        const auto replacement=b::link::link->generation;
        if(replacement<=old_generation || old_deliveries!=1 || new_deliveries!=0)
            throw std::runtime_error("released Link reused its batch lifetime");
        b::py::invoke(old_poll.get());
        if(host_receives!=0) throw std::runtime_error("stale poll reached the reconstructed Host");
        b::link::releaseScriptOwners();
        connect(nullptr);
        if(b::link::link->generation<=replacement) throw std::runtime_error("released Link reset the process lifetime counter");
        b::link::releaseScriptOwners();
        Py_RETURN_TRUE;
    } catch(const std::exception& e) { PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr; }
}
PyMethodDef methods[] = {{"run", run, METH_VARARGS, ""}, {"reconnect",reconnect,METH_O,""}, {nullptr, nullptr, 0, nullptr}};
}
PyMODINIT_FUNC initbattle_link_shutdown_test() { Py_InitModule("battle_link_shutdown_test", methods); }
