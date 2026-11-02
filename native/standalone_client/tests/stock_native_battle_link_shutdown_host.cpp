// Exercise the real worker-link owner without starting transport threads.
#include "../bigworld/stock_native_battle_link.cpp"

namespace {
bool fail_send = false;
bool transport_destroyed = false, detached_at_close = false;
PyObject* held_world = nullptr;
Py_ssize_t references_at_close = 0;
std::vector<std::string> trace;
}
namespace stock_wire_net {
struct Native {
    ~Native() {
        // Production Connection::~Connection signals closing even if explicit close was skipped.
        transport_destroyed = true;
        detached_at_close = !native_battle::link::link;
        references_at_close = Py_REFCNT(held_world);
        trace.push_back("transport-destroy");
    }
};
Queued send(Native&, std::string, const std::string&, const std::optional<std::string>&, std::size_t, std::size_t) {
    trace.push_back("leave");
    if (fail_send) throw std::runtime_error("leave failed");
    return Queued::queued;
}
void close(Native&, double flush) {
    if (flush != 0.) throw std::runtime_error("shutdown transport attempted to flush");
    trace.push_back("close");
}
}
namespace stock_wire {
bool encode_value(const stock_native_client_state::Value&, std::string&, std::string&) { return true; }
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
    b::link::link->net = std::make_shared<stock_wire_net::Native>();
    b::link::link->connected = b::link::link->running = true;
    bool failed = false;
    try { b::link::releaseScriptOwners(); }
    catch (const std::exception&) { failed = true; PyErr_Clear(); }
    if (failed != fail_send || b::link::link || !transport_destroyed || !detached_at_close ||
        references_at_close != before + 1 || Py_REFCNT(world) != before) {
        PyErr_SetString(PyExc_AssertionError, "worker link retained script/transport or closed after its script handles");
        return nullptr;
    }
    b::link::releaseScriptOwners();
    auto out = b::p::list();
    for (const auto& row : trace) b::p::append(out.get(), b::p::text(row));
    return out.release();
}
PyMethodDef methods[] = {{"run", run, METH_VARARGS, ""}, {nullptr, nullptr, 0, nullptr}};
}
PyMODINIT_FUNC initbattle_link_shutdown_test() { Py_InitModule("battle_link_shutdown_test", methods); }
