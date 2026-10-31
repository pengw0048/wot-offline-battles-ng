#pragma once
// Native LAN battle transport: connect, receive and send on background threads.
#include <Python.h>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace stock_native_client_state { struct Value; }

namespace stock_wire_net {
PyMethodDef* methods();

// A connection owned by native code: its receive thread decodes every frame.
struct Native;
// One received message, its age in seconds, or the decode failure reason when `message` is empty.
struct Frame {
    std::shared_ptr<const stock_native_client_state::Value> message;
    double age;
    std::string error;
};
// "connecting", "connected", "closed" or "failed" (reported only after every frame was taken).
struct Status {
    std::string state, error;
};
enum class Queued { queued, replaced, refused };
std::shared_ptr<Native> open(const std::string& host, int port, double timeout);
// Queue one encoded frame; a keyed frame replaces the newest unsent frame with the same key.
Queued send(Native& native, std::string payload, const std::string& type, const std::optional<std::string>& key,
            std::size_t max_messages, std::size_t max_bytes);
Status receive(Native& native, std::size_t limit, std::vector<Frame>& out);
// Flush queued frames briefly, then close.
void close(Native& native, double flush);
}
