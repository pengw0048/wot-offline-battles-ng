// LAN battle transport owned by native threads: connect, frame receive and send queue.
#include "stock_wire_net.hpp"
#include "stock_wire_codec.hpp"
#include "stock_wire_replica_store.hpp"
#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace stock_wire_net {
namespace {
#if defined(_WIN32)
using Socket = SOCKET;
const Socket invalid_socket = INVALID_SOCKET;
void closeSocket(Socket s) { closesocket(s); }
#else
using Socket = int;
const Socket invalid_socket = -1;
void closeSocket(Socket s) { ::close(s); }
#endif
using Clock = std::chrono::steady_clock;
const std::size_t max_frame = 4u * 1024u * 1024u;
const std::size_t max_send_bytes = 8u * 1024u * 1024u;
const char* capsule_name = "offline_client_wire_native.Connection";

enum class State { connecting, connected, closed, failed };

// One received frame; replica snapshots are already decoded and merged on the receive thread.
struct Received {
    std::string frame;
    Clock::time_point arrival;
    std::shared_ptr<const stock_native_client_state::Value> snapshot;
    std::string resync;
};

struct Shared {
    std::mutex mutex;
    std::condition_variable wake;
    State state = State::connecting;
    std::string error;
    Socket socket = invalid_socket;
    std::deque<std::string> outgoing;
    std::size_t outgoing_bytes = 0;
    std::deque<Received> frames;
    bool closing = false;
    double flush_seconds = 0.5;

    void finish(State terminal, const std::string& why) {
        std::lock_guard<std::mutex> lock(mutex);
        if (state == State::closed || state == State::failed) return;
        state = terminal;
        error = why;
        wake.notify_all();
    }
};

bool connectWithTimeout(Shared& shared, const std::string& host, int port, double timeout, std::string& error) {
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || !found) {
        error = "cannot resolve server address";
        return false;
    }
    Socket s = ::socket(found->ai_family, found->ai_socktype, found->ai_protocol);
    if (s == invalid_socket) { freeaddrinfo(found); error = "cannot create socket"; return false; }
#if defined(_WIN32)
    u_long nonblocking = 1;
    ioctlsocket(s, FIONBIO, &nonblocking);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    int status = ::connect(s, found->ai_addr, static_cast<int>(found->ai_addrlen));
    freeaddrinfo(found);
    bool ok = status == 0;
    if (!ok) {
        fd_set writable, failed;
        FD_ZERO(&writable); FD_SET(s, &writable);
        FD_ZERO(&failed); FD_SET(s, &failed);
        timeval limit;
        limit.tv_sec = static_cast<long>(timeout);
        limit.tv_usec = static_cast<long>((timeout - static_cast<double>(limit.tv_sec)) * 1e6);
        if (select(static_cast<int>(s + 1), nullptr, &writable, &failed, &limit) > 0 && FD_ISSET(s, &writable)) {
            int code = 0;
            socklen_t length = sizeof(code);
            ok = getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&code), &length) == 0 && code == 0;
        }
    }
    if (!ok) { closeSocket(s); error = "cannot connect to the server"; return false; }
#if defined(_WIN32)
    nonblocking = 0;
    ioctlsocket(s, FIONBIO, &nonblocking);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) & ~O_NONBLOCK);
#endif
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
    std::lock_guard<std::mutex> lock(shared.mutex);
    if (shared.closing) { closeSocket(s); error = "closed before connect"; return false; }
    shared.socket = s;
    shared.state = State::connected;
    shared.wake.notify_all();
    return true;
}

void sender(std::shared_ptr<Shared> shared) {
    std::unique_lock<std::mutex> lock(shared->mutex);
    Clock::time_point deadline;
    bool draining = false;
    for (;;) {
        shared->wake.wait(lock, [&] {
            return shared->state != State::connecting && (!shared->outgoing.empty() || shared->closing ||
                                                         shared->state != State::connected);
        });
        if (shared->state != State::connected) return;
        if (shared->closing && !draining) { draining = true; deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(shared->flush_seconds)); }
        if (shared->outgoing.empty() || (draining && Clock::now() > deadline)) {
            if (draining) {
                // Leave is queued before close; shut down only after it is written.
#if defined(_WIN32)
                shutdown(shared->socket, SD_BOTH);
#else
                shutdown(shared->socket, SHUT_RDWR);
#endif
                return;
            }
            continue;
        }
        std::string payload;
        payload.swap(shared->outgoing.front());
        shared->outgoing.pop_front();
        shared->outgoing_bytes -= payload.size();
        Socket s = shared->socket;
        lock.unlock();
        std::size_t offset = 0;
        bool ok = true;
        while (offset < payload.size()) {
            int sent = ::send(s, payload.data() + offset, static_cast<int>(payload.size() - offset), 0);
            if (sent <= 0) { ok = false; break; }
            offset += static_cast<std::size_t>(sent);
        }
        lock.lock();
        if (!ok) {
            lock.unlock();
            shared->finish(State::failed, "server closed the connection");
            return;
        }
    }
}

void receiver(std::shared_ptr<Shared> shared, std::string host, int port, double timeout) {
    std::string error;
    if (!connectWithTimeout(*shared, host, port, timeout, error)) {
        shared->finish(State::failed, error);
        return;
    }
    Socket s;
    { std::lock_guard<std::mutex> lock(shared->mutex); s = shared->socket; }
    std::string buffer;
    char chunk[65536];
    stock_wire::ReplicaStore store;
    const uint16_t replica = stock_wire::message_id("replica_snapshot");
    for (;;) {
        int got = ::recv(s, chunk, sizeof(chunk), 0);
        if (got <= 0) {
            std::lock_guard<std::mutex> lock(shared->mutex);
            if (shared->state == State::connected) {
                if (got == 0 || shared->closing) shared->state = State::closed;
                else { shared->state = State::failed; shared->error = "server closed the connection"; }
            }
            shared->wake.notify_all();
            return;
        }
        buffer.append(chunk, static_cast<std::size_t>(got));
        Clock::time_point now = Clock::now();
        std::size_t used = 0;
        std::vector<Received> ready;
        while (buffer.size() - used >= stock_wire::frame_header_bytes) {
            std::size_t size = stock_wire::frame_size(reinterpret_cast<const unsigned char*>(buffer.data() + used),
                                                      buffer.size() - used);
            if (size > max_frame) { shared->finish(State::failed, "battle frame exceeds the wire limit"); return; }
            if (buffer.size() - used < size) break;
            Received item{std::string(buffer, used, size), now, {}, {}};
            used += size;
            const auto* data = reinterpret_cast<const unsigned char*>(item.frame.data());
            if (stock_wire::frame_message(data, item.frame.size()) == replica) {
                // A delta that does not fit the merged rows asks the server for a full one.
                stock_native_client_state::Value message;
                try {
                    if (!stock_wire::decode_value(data, item.frame.size(), message, item.resync)) throw std::runtime_error(item.resync);
                    item.snapshot = store.apply(std::move(message));
                } catch (const std::exception& error) {
                    item.resync = error.what();
                    item.snapshot.reset();
                }
            }
            ready.push_back(std::move(item));
        }
        buffer.erase(0, used);
        if (!ready.empty()) {
            std::lock_guard<std::mutex> lock(shared->mutex);
            for (auto& item : ready) shared->frames.push_back(std::move(item));
        }
    }
}

struct Connection {
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    void close() {
        std::lock_guard<std::mutex> lock(shared->mutex);
        shared->closing = true;
        shared->wake.notify_all();
    }
    ~Connection() { close(); }
};

void destroy(PyObject* capsule) { delete static_cast<Connection*>(PyCapsule_GetPointer(capsule, capsule_name)); }
Connection* connection(PyObject* capsule) { return static_cast<Connection*>(PyCapsule_GetPointer(capsule, capsule_name)); }

void startup() {
#if defined(_WIN32)
    static bool started = false;
    if (!started) { WSADATA data; WSAStartup(MAKEWORD(2, 2), &data); started = true; }
#endif
}

void runThreads(const std::shared_ptr<Shared>& shared, const std::string& host, int port, double timeout) {
    std::thread([shared, host, port, timeout] {
        receiver(shared, host, port, timeout);
        Socket s;
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            s = shared->socket;
            shared->socket = invalid_socket;
            if (shared->state == State::connected) shared->state = State::closed;
            shared->wake.notify_all();
        }
        if (s != invalid_socket) closeSocket(s);
    }).detach();
    std::thread([shared] { sender(shared); }).detach();
}

PyObject* openPython(PyObject*, PyObject* args) {
    const char* host; int port; double timeout;
    if (!PyArg_ParseTuple(args, "sid", &host, &port, &timeout)) return nullptr;
    startup();
    auto* value = new Connection();
    runThreads(value->shared, host, port, timeout);
    return PyCapsule_New(value, capsule_name, destroy);
}

PyObject* sendPython(PyObject*, PyObject* args) {
    PyObject *capsule, *payload; const void* data; Py_ssize_t size;
    if (!PyArg_ParseTuple(args, "OO", &capsule, &payload)) return nullptr;
    Connection* value = connection(capsule);
    if (!value || PyObject_AsReadBuffer(payload, &data, &size) < 0) return nullptr;
    Shared& shared = *value->shared;
    std::lock_guard<std::mutex> lock(shared.mutex);
    if (shared.closing || shared.state == State::closed || shared.state == State::failed ||
        shared.outgoing_bytes + static_cast<std::size_t>(size) > max_send_bytes)
        Py_RETURN_FALSE;
    shared.outgoing.emplace_back(static_cast<const char*>(data), static_cast<std::size_t>(size));
    shared.outgoing_bytes += static_cast<std::size_t>(size);
    shared.wake.notify_all();
    Py_RETURN_TRUE;
}

// ([(frame, age_seconds, native), ...], state, error); age converts to the caller's clock.
// native is a merged replica snapshot capsule, a resync reason string, or None.
PyObject* receivePython(PyObject*, PyObject* args) {
    PyObject* capsule; int limit = 256;
    if (!PyArg_ParseTuple(args, "O|i", &capsule, &limit)) return nullptr;
    Connection* value = connection(capsule);
    if (!value) return nullptr;
    std::deque<Received> taken;
    State state; std::string error;
    {
        std::lock_guard<std::mutex> lock(value->shared->mutex);
        auto& frames = value->shared->frames;
        std::size_t count = std::min(frames.size(), static_cast<std::size_t>(limit > 0 ? limit : 0));
        for (std::size_t i = 0; i < count; ++i) { taken.push_back(std::move(frames.front())); frames.pop_front(); }
        // A terminal state is reported only after every received frame was taken.
        state = frames.empty() ? value->shared->state : State::connected;
        error = value->shared->error;
    }
    Clock::time_point now = Clock::now();
    PyObject* list = PyList_New(static_cast<Py_ssize_t>(taken.size()));
    if (!list) return nullptr;
    Py_ssize_t index = 0;
    for (auto& item : taken) {
        double age = std::chrono::duration<double>(now - item.arrival).count();
        PyObject* bytes = PyString_FromStringAndSize(item.frame.data(), static_cast<Py_ssize_t>(item.frame.size()));
        PyObject* native = item.snapshot ? stock_wire::snapshot_capsule(item.snapshot)
                         : !item.resync.empty() ? PyString_FromString(item.resync.c_str()) : (Py_INCREF(Py_None), Py_None);
        PyObject* pair = bytes && native ? Py_BuildValue("(NdN)", bytes, age, native) : nullptr;
        if (!pair) { Py_XDECREF(bytes); Py_XDECREF(native); }
        if (!pair) { Py_DECREF(list); return nullptr; }
        PyList_SET_ITEM(list, index++, pair);
    }
    const char* name = state == State::connecting ? "connecting" : state == State::connected ? "connected"
                     : state == State::closed ? "closed" : "failed";
    return Py_BuildValue("(Nss)", list, name, error.c_str());
}

PyObject* closePython(PyObject*, PyObject* args) {
    PyObject* capsule; double flush = 0.5;
    if (!PyArg_ParseTuple(args, "O|d", &capsule, &flush)) return nullptr;
    Connection* value = connection(capsule);
    if (!value) return nullptr;
    { std::lock_guard<std::mutex> lock(value->shared->mutex); value->shared->flush_seconds = flush; }
    value->close();
    Py_RETURN_NONE;
}
}

PyMethodDef* methods() {
    static PyMethodDef table[] = {
        {"net_open", openPython, METH_VARARGS, "Connect natively and start the receive and send threads."},
        {"net_send", sendPython, METH_VARARGS, "Queue one encoded frame; False once the transport ended or is full."},
        {"net_receive", receivePython, METH_VARARGS, "Take received frames with their ages and the transport state."},
        {"net_close", closePython, METH_VARARGS, "Flush queued frames briefly, then close the transport."},
        {nullptr, nullptr, 0, nullptr}};
    return table;
}

}
