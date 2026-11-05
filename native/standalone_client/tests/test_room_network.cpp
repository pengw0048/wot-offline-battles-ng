#include "../room/room_network.hpp"
#include "../room/room_json.hpp"
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <cassert>
#include <cstring>
#include <future>
#include <thread>

using namespace wot::room;
namespace {
std::string encode(const Value& message) {
    auto body = writeJson(message);
    std::string out(8, '\0'); const auto size = static_cast<std::uint32_t>(body.size());
    std::memcpy(&out[0], &size, 4); return out + body;
}
Codec codec() {
    Codec out;
    out.frameSize = [](const unsigned char* data, std::size_t size) -> std::size_t {
        if (size < 8) return 0;
        std::uint32_t n; std::memcpy(&n, data, 4); return std::size_t(n) + 8;
    };
    out.decode = [](const unsigned char* data, std::size_t size, Value& value, std::string&) {
        value = parseJson(std::string(reinterpret_cast<const char*>(data + 8), size - 8));
        if (value.get("type").text == "oversized") {
            // A compact binary Any list can expand far beyond its wire size.
            auto padding = array(); padding.array.resize(32 * 1024 * 1024 / sizeof(Value) + 1);
            value.object["padding"] = std::move(padding);
        }
        return true;
    };
    out.encode = [](const Value& value, std::string& bytes, std::string& error) {
        if (value.get("type").text == "invalid_event") {
            error = "missing required event field"; return false;
        }
        bytes = encode(value); return true;
    };
    return out;
}
Value message(const char* type) { auto v = object(); v.object["type"] = text(type); return v; }
}
int main() {
    Network network(codec());
    const auto bound = network.start("127.0.0.1", 0);
    assert(bound.port);
    auto peer = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{}; address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(bound.port));
    assert(inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
    assert(::connect(peer, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    const auto hello = encode(message("hello")), leave = encode(message("leave"));
    // Fragment the first frame and join the following frame to its tail.
    assert(::send(peer, hello.data(), 3, 0) == 3);
    const auto remainder = hello.substr(3) + leave;
    assert(::send(peer, remainder.data(), static_cast<int>(remainder.size()), 0) == static_cast<int>(remainder.size()));
#if defined(_WIN32)
    assert(::shutdown(peer, SD_SEND) == 0);
#else
    assert(::shutdown(peer, SHUT_WR) == 0);
#endif
    const auto deadline = Channel::Clock::now() + std::chrono::seconds(5);
    std::vector<Network::Received> messages;
    while (messages.size() < 2 && Channel::Clock::now() < deadline) {
        auto batch = network.take(8); messages.insert(messages.end(), batch.begin(), batch.end());
        std::this_thread::yield();
    }
    assert(messages.size() == 2);
    assert(messages[0].peer == messages[1].peer);
    assert(messages[0].message->get("type").text == "hello");
    assert(messages[1].message->get("type").text == "leave");
    const auto id = messages[0].peer;
    const auto welcome = message("welcome");
    assert(network.offer(id, freeze(welcome)) == Channel::Admission::admitted);
    network.disconnect(id, true);
    auto reply = std::async(std::launch::async, [&] {
        std::string bytes; char buffer[1024];
        while (true) {
            const auto n = ::recv(peer, buffer, sizeof(buffer), 0);
            if (n <= 0) break;
            bytes.append(buffer, static_cast<std::size_t>(n));
        }
        return bytes;
    });
    assert(reply.wait_until(deadline) == std::future_status::ready);
    assert(reply.get() == encode(welcome));
#if defined(_WIN32)
    closesocket(peer);
#else
    close(peer);
#endif
    network.takeClosed();
    auto large = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(::connect(large, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    const auto oversized = encode(message("oversized"));
    assert(::send(large, oversized.data(), static_cast<int>(oversized.size()), 0) == static_cast<int>(oversized.size()));
    std::vector<Network::Closed> ended;
    const auto bound_deadline = Channel::Clock::now() + std::chrono::seconds(5);
    while (ended.empty() && Channel::Clock::now() < bound_deadline) { ended = network.takeClosed(); std::this_thread::yield(); }
    assert(!ended.empty() && network.running());
    assert(network.take(1).empty()); // Impossible admission is never retried forever.
#if defined(_WIN32)
    closesocket(large);
#else
    close(large);
#endif
    auto malformed = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(::connect(malformed, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    assert(::send(malformed, hello.data(), static_cast<int>(hello.size()), 0) == static_cast<int>(hello.size()));
    std::vector<Network::Received> admitted;
    const auto error_deadline = Channel::Clock::now() + std::chrono::seconds(5);
    while (admitted.empty() && Channel::Clock::now() < error_deadline) {
        admitted = network.take(1); std::this_thread::yield();
    }
    assert(admitted.size() == 1);
    assert(network.offer(admitted[0].peer, freeze(message("invalid_event"))) == Channel::Admission::admitted);
    ended.clear();
    while (ended.empty() && Channel::Clock::now() < error_deadline) {
        ended = network.takeClosed(); std::this_thread::yield();
    }
    assert(ended.size() == 1 && ended[0].peer == admitted[0].peer);
    assert(ended[0].reason == "encode invalid_event: missing required event field");
    assert(network.running());
#if defined(_WIN32)
    closesocket(malformed);
#else
    close(malformed);
#endif
    network.stop();
    assert(network.offer(id, freeze(welcome)) == Channel::Admission::refused);
    // Repeated stop and a fresh start on the same object are both safe.
    network.stop();
    assert(network.start("127.0.0.1", 0).port);
    network.stop();
}
