// Stop the real receiver at raw enqueue, then order detach/footer without a GIL change.
#include "../bigworld/stock_wire_replay.hpp"
namespace stock_wire_replay {
bool controlledRecordFrame(Writer&, const std::string&, double);
}
#define record_frame controlledRecordFrame
#include "../bigworld/stock_wire_net.cpp"
#undef record_frame

namespace {
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false, completed = false;
    bool detaching = false, footer_written = false, stop_peer = false;
};
Gate* active_gate = nullptr;
}
namespace stock_wire_replay {
bool controlledRecordFrame(Writer& writer, const std::string& frame, double t) {
    Gate* gate = active_gate;
    if (gate) {
        std::unique_lock<std::mutex> lock(gate->mutex);
        gate->entered = true;
        gate->changed.notify_all();
        gate->changed.wait(lock, [&] { return gate->released; });
    }
    const bool result = record_frame(writer, frame, t);
    if (gate) {
        std::lock_guard<std::mutex> lock(gate->mutex);
        gate->completed = true;
        gate->changed.notify_all();
    }
    return result;
}
}
namespace {
PyObject* detachRace(PyObject*, PyObject* args) {
    PyObject *writer, *payload, *footer;
    if (!PyArg_ParseTuple(args, "OOO", &writer, &payload, &footer)) return nullptr;
    char* bytes = nullptr; Py_ssize_t length = 0;
    if (PyString_AsStringAndSize(payload, &bytes, &length) < 0) return nullptr;
    const std::string frame(bytes, static_cast<std::size_t>(length));
    auto* connection = new stock_wire_net::Connection();
    PyObject* capsule = PyCapsule_New(connection, stock_wire_net::capsule_name, stock_wire_net::destroy);
    if (!capsule) { delete connection; return nullptr; }
    auto shared = connection->shared;
    PyObject* attach_args = Py_BuildValue("OOd", capsule, writer, 0.);
    PyObject* attached = stock_wire_net::recordPython(nullptr, attach_args);
    Py_DECREF(attach_args);
    if (!attached) { Py_DECREF(capsule); return nullptr; }
    Py_DECREF(attached);

    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size = sizeof(address);
    if (listener < 0 || ::bind(listener, reinterpret_cast<sockaddr*>(&address), size) != 0 ||
        ::listen(listener, 1) != 0 || ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) != 0) {
        if (listener >= 0) ::close(listener);
        Py_DECREF(capsule); PyErr_SetString(PyExc_RuntimeError, "loopback listener failed"); return nullptr;
    }
    Gate gate;
    active_gate = &gate;
    std::thread peer([&] {
        const int socket = ::accept(listener, nullptr, nullptr);
        std::size_t sent = 0;
        while (sent < frame.size()) {
            const auto count = ::send(socket, frame.data() + sent, frame.size() - sent, 0);
            if (count <= 0) break;
            sent += static_cast<std::size_t>(count);
        }
        {
            std::unique_lock<std::mutex> lock(gate.mutex);
            gate.changed.wait(lock, [&] { return gate.stop_peer; });
        }
        ::shutdown(socket, SHUT_RDWR); ::close(socket);
    });
    // No sender thread is needed: only the production receiver and main
    // thread can own Shared::mutex, so try_lock identifies this exact boundary.
    std::thread receiver([&] { stock_wire_net::receiver(shared, "127.0.0.1", ntohs(address.sin_port), 3.); });
    {
        std::unique_lock<std::mutex> lock(gate.mutex);
        gate.changed.wait(lock, [&] { return gate.entered; });
    }
    const bool escaped = shared->mutex.try_lock();
    std::size_t published = 0;
    if (escaped) { published = shared->frames.size(); shared->mutex.unlock(); }
    std::thread controller([&] {
        std::unique_lock<std::mutex> lock(gate.mutex);
        gate.changed.wait(lock, [&] { return gate.detaching; });
        // The old implementation allows footer first. A receiver holding the
        // connection lock must enqueue before detach can return to Python.
        if (escaped) gate.changed.wait(lock, [&] { return gate.footer_written; });
        gate.released = true;
        gate.changed.notify_all();
        gate.changed.wait(lock, [&] { return gate.completed; });
    });
    {
        std::lock_guard<std::mutex> lock(gate.mutex);
        gate.detaching = true; gate.changed.notify_all();
    }
    // Keep the GIL exactly as production net_record does. The controller and
    // receiver use only C++ state, so neither needs it to release this call.
    PyObject* detach_args = Py_BuildValue("OO", capsule, Py_None);
    PyObject* detached = stock_wire_net::recordPython(nullptr, detach_args);
    Py_DECREF(detach_args);
    PyObject* count = detached ? PyObject_CallFunctionObjArgs(footer, writer, nullptr) : nullptr;
    Py_XDECREF(detached);
    {
        std::lock_guard<std::mutex> lock(gate.mutex);
        gate.footer_written = true; gate.changed.notify_all();
    }
    controller.join();
    {
        std::lock_guard<std::mutex> lock(gate.mutex);
        gate.stop_peer = true; gate.changed.notify_all();
    }
    peer.join(); receiver.join();
    if (shared->socket != stock_wire_net::invalid_socket) stock_wire_net::closeSocket(shared->socket);
    shared->socket = stock_wire_net::invalid_socket;
    ::close(listener);
    active_gate = nullptr;
    Py_DECREF(capsule);
    if (!count) return nullptr;
    return Py_BuildValue("{s:O,s:k,s:N}", "lease_escaped", escaped ? Py_True : Py_False,
                         "published_before_enqueue", static_cast<unsigned long>(published), "footer_count", count);
}
PyMethodDef extra[] = {{"detach_race", detachRace, METH_VARARGS, ""}, {nullptr, nullptr, 0, nullptr}};
}
PyMODINIT_FUNC initoffline_client_wire_native() {
    static std::vector<PyMethodDef> methods;
    for (PyMethodDef* table : {stock_wire::methods(), stock_wire_replay::methods(), extra})
        for (PyMethodDef* method = table; method->ml_name; ++method) methods.push_back(*method);
    methods.push_back({nullptr, nullptr, 0, nullptr});
    Py_InitModule("offline_client_wire_native", methods.data());
}
