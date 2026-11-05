#include "room_host.hpp"
#include "room_core.hpp"
#include "room_json.hpp"
#include "room_perf.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace wot::room {
namespace {
std::string readFile(const std::string& path, std::size_t limit) {
    std::ifstream stream(std::filesystem::u8path(path), std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read native room data: " + path);
    stream.seekg(0, std::ios::end); const auto size = stream.tellg();
    if (size < 0 || std::uint64_t(size) > limit) throw std::runtime_error("Native room data exceeds its bound");
    stream.seekg(0); std::string out(static_cast<std::size_t>(size), '\0');
    if (size && !stream.read(&out[0], size)) throw std::runtime_error("Native room data read failed");
    return out;
}
bool persist(const std::string& target, const Value& receipts) {
    const auto path = std::filesystem::u8path(target), temporary = std::filesystem::u8path(target + ".tmp");
    try {
        auto document = object(); document.object["schema"] = integer(1); document.object["receipts"] = receipts;
        const auto bytes = writeJson(document);
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream || !stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size())) || !stream.flush()) return false;
        }
#if defined(_WIN32)
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return false;
#else
        std::filesystem::rename(temporary, path);
#endif
        return true;
    } catch (const std::exception&) {
        std::error_code ignored; std::filesystem::remove(temporary, ignored); return false;
    }
}
}
struct Host::Impl {
    explicit Impl(Value configuration, Codec codec)
        : config(std::move(configuration)), network(std::move(codec)), incoming(512, 64 * 1024 * 1024),
          outgoing(512, 64 * 1024 * 1024) {}
    Value config;
    std::shared_ptr<const Value> data;
    Network network;
    Channel incoming, outgoing;
    std::unique_ptr<Core> core;
    std::thread thread;
    std::atomic<bool> running{false}, ready{false}, stopped{false};
    mutable std::mutex mutex;
    std::string error;
    struct Resources {
        std::int64_t round, epoch;
        Value data;
        std::shared_ptr<const strategy::GraphData> graph;
    };
    std::deque<Resources> resources;
    std::deque<Network::Received> staged_network;
    void log(const std::string& message) {
        const auto path = config.get("log_path").text;
        if (path.empty()) return;
        std::ofstream file(std::filesystem::u8path(path), std::ios::app | std::ios::binary);
        if (file) file << message << '\n';
    }
    void loop() {
        using Clock = Channel::Clock;
        const auto origin = Clock::now();
        auto next = origin + std::chrono::microseconds(33333), next_stop_check = origin;
        auto last_report = origin; auto last_cpu = threadCpuMilliseconds();
        Timing worker_time, player_time, tick_time, worker_age, lateness;
        std::string last_network_report;
        try {
            while (running) {
                if (!network.running()) throw std::runtime_error("Native room network thread stopped: " + network.diagnostic());
                core->clock(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - origin).count());
                std::deque<Resources> updates;
                { std::lock_guard<std::mutex> lock(mutex); updates.swap(resources); }
                for (auto& update : updates) core->configureRound(update.round, update.epoch, std::move(update.data), std::move(update.graph));
                bool blocked = !core->flushPending();
                for (unsigned consumed = 0; !blocked && consumed < 64; ++consumed) {
                    auto entries = incoming.take(1); if (entries.empty()) break;
                    auto& entry = entries.front();
                    worker_age.add(std::chrono::duration<double, std::milli>(Clock::now() - entry.queued).count());
                    const auto started = Clock::now(); core->receive(Core::worker_peer, *entry.message); worker_time.elapsed(started);
                    blocked = !core->flushPending();
                }
                if (!blocked && staged_network.empty()) {
                    // At most one admitted message per peer is held outside
                    // its channel. Retain the batch if the engine backs up.
                    for (auto& entry : network.take(1)) staged_network.push_back(std::move(entry));
                }
                while (!blocked && !staged_network.empty()) {
                    const auto entry = std::move(staged_network.front()); staged_network.pop_front();
                    const auto started = Clock::now(); core->receive(entry.peer, *entry.message, entry.address); player_time.elapsed(started);
                    blocked = !core->flushPending();
                }
                if (!blocked && staged_network.empty()) for (const auto& closed : network.takeClosed()) {
                    log("NATIVE ROOM peer=" + std::to_string(closed.peer) + " closed: " + closed.reason);
                    core->disconnected(closed.peer); network.disconnect(closed.peer, true);
                }
                auto now = Clock::now();
                // Preserve every fixed step, but yield to messages between
                // bounded catch-up slices. Overload cannot starve receipts.
                unsigned steps = 0;
                while (running && !blocked && now >= next && steps++ < 4) {
                    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - origin).count();
                    lateness.add(std::chrono::duration<double, std::milli>(now - next).count());
                    const auto started = Clock::now(); core->advance(1. / 30., elapsed); tick_time.elapsed(started);
                    // Rational tick boundaries avoid accumulating 1 us drift.
                    const auto count = std::chrono::duration_cast<std::chrono::microseconds>(next - origin).count();
                    const auto ordinal = (count * 30 + 500000) / 1000000 + 1;
                    next = origin + std::chrono::microseconds(ordinal * 1000000 / 30);
                    now = Clock::now(); blocked = !core->flushPending();
                }
                ready = core->workerReady();
                if (now >= next_stop_check) {
                    next_stop_check = now + std::chrono::milliseconds(100);
                    const auto& stop = config.get("stop_file").text;
                    if (!stop.empty() && std::filesystem::exists(std::filesystem::u8path(stop))) running = false;
                }
                if (now - last_report >= std::chrono::seconds(5)) {
                    const auto cpu = threadCpuMilliseconds();
                    const auto elapsed = std::chrono::duration<double, std::milli>(now - last_report).count();
                    const auto rx = incoming.metrics(), tx = outgoing.metrics();
                    std::ostringstream out; out << "PERF room tid=" << threadIdentity() << " cpu_core_pct=" << 100. * (cpu - last_cpu) / elapsed
                        << " tick=" << core->state().server_tick << " rx_pending=" << rx.pending << " tx_pending=" << tx.pending
                        << " rx_tree_bytes=" << rx.bytes << " tx_tree_bytes=" << tx.bytes
                        << " rx_oldest_ms=" << rx.oldest_seconds * 1000. << " tx_oldest_ms=" << tx.oldest_seconds * 1000.
                        << " rx_peak=" << rx.peak_pending << " tx_peak=" << tx.peak_pending
                        << " rx_refused=" << rx.refused << " tx_refused=" << tx.refused
                        << " tx_size_visits=" << tx.sizing_visits << " tx_size_cache_hits=" << tx.sizing_cache_hits << " "
                        << worker_time.report("worker") << " " << player_time.report("player") << " "
                        << tick_time.report("tick") << " " << worker_age.report("worker_age") << " " << lateness.report("tick_late");
                    log(out.str()); const auto net = network.diagnostic();
                    if (!net.empty() && net != last_network_report) { log(net); last_network_report = net; }
                    worker_time = {}; player_time = {}; tick_time = {}; worker_age = {}; lateness = {};
                    last_cpu = cpu; last_report = now;
                }
                if (blocked) outgoing.waitForChange(Clock::now() + std::chrono::milliseconds(2));
                else incoming.waitUntil(std::min(next, Clock::now() + std::chrono::milliseconds(2)));
            }
            core->stop();
        } catch (const std::exception& e) {
            { std::lock_guard<std::mutex> lock(mutex); error = e.what(); }
            log("NATIVE ROOM stopped: " + std::string(e.what()));
        }
        ready = false; running = false;
        network.stop(); outgoing.close(); incoming.close(); stopped = true;
    }
};
Host::Host(const std::string& path, Codec codec, EffectiveParamsValidator parameters) {
    auto configuration = parseJson(readFile(path, 1024 * 1024));
    if (intField(configuration, "schema") != 1) throw std::invalid_argument("Unknown native room host configuration");
    auto resources = parseJson(readFile(configuration.get("resources_path").text, 16 * 1024 * 1024));
    const auto& overlay_path = configuration.get("vehicle_overlay_path").text;
    if (!overlay_path.empty()) {
        auto overlay = parseJson(readFile(overlay_path, 128 * 1024 * 1024));
        if (intField(overlay, "schema") != 1) throw std::invalid_argument("Unknown native room vehicle overlay schema");
        Value shared; shared.kind = Value::Kind::Shared; shared.reference = freeze(std::move(overlay));
        resources.object["vehicle_overlay"] = std::move(shared);
    }
    impl_.reset(new Impl(std::move(configuration), std::move(codec)));
    auto& i = *impl_;
    i.data = freeze(std::move(resources));
    i.core = std::make_unique<Core>(i.config, objectView(i.data), [&i](Core::Peer peer, std::shared_ptr<const Value> message, std::optional<std::string> key) {
        if (peer == Core::worker_peer) return i.outgoing.offer(std::move(message), std::move(key)) != Channel::Admission::refused;
        if (i.network.offer(peer, std::move(message), std::move(key)) == Channel::Admission::refused) {
            // TCP pressure is already buffered by the bounded native channel.
            // A full/closed endpoint is terminal for that peer, just as the
            // source reliable outbox was. Never grow a second unbounded queue.
            i.log("NATIVE ROOM output admission refused peer=" + std::to_string(peer));
            i.network.disconnect(peer, false);
        }
        return true;
    }, [&i](Core::Peer peer, bool flush) { if (peer != Core::worker_peer) i.network.disconnect(peer, flush); },
       [&i](Core::Peer peer, std::int64_t player) { i.network.setPlayer(peer, player); i.network.resync(peer); }, std::move(parameters));
    i.core->logger([&i](const std::string& message) { i.log(message); });
    const auto receipt_path = i.config.get("receipts_path").text;
    if (receipt_path.empty()) throw std::invalid_argument("Native room receipt path is missing");
    if (std::filesystem::exists(std::filesystem::u8path(receipt_path))) {
        try {
            const auto receipts = parseJson(readFile(receipt_path, 64 * 1024 * 1024));
            if (intField(receipts, "schema") != 1) throw std::invalid_argument("Unknown native room receipt schema");
            i.core->rules().restoreReceipts(receipts.get("receipts"));
        } catch (const std::exception& error) {
            i.log("NATIVE ROOM ignored invalid persisted receipts: " + std::string(error.what()));
        }
    }
    i.core->rules().receiptStore([&i, receipt_path](const Value& receipts) {
        const bool saved = persist(receipt_path, receipts);
        if (!saved) i.log("NATIVE ROOM receipt persistence failed");
        return saved;
    });
    const auto port = intField(i.config, "port", -1);
    if (port < 1 || port > 65535) throw std::invalid_argument("Native room port is invalid");
    i.network.start(i.config.get("bind_address").text, static_cast<unsigned>(port));
    i.log("NATIVE ROOM listener started port=" + std::to_string(port));
    i.running = true; i.thread = std::thread([&i] { i.loop(); });
}
Host::~Host() { stop(); }
Channel::Admission Host::send(std::shared_ptr<const Value> message, std::optional<std::string> key) {
    return impl_->incoming.offer(std::move(message), std::move(key));
}
std::vector<Channel::Entry> Host::receive(std::size_t limit) { return impl_->outgoing.take(limit); }
Host::Status Host::status() const {
    std::lock_guard<std::mutex> lock(impl_->mutex); return {impl_->ready, impl_->stopped, impl_->error};
}
std::pair<Channel::Metrics, Channel::Metrics> Host::metrics() const { return {impl_->incoming.metrics(), impl_->outgoing.metrics()}; }
std::shared_ptr<const Value> Host::resources() const { return impl_->data; }
void Host::configureRound(std::int64_t round, std::int64_t epoch, Value resources, std::shared_ptr<const strategy::GraphData> graph) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    // The engine creates at most one preparation per loading round. Replace
    // an unpublished version only within that exact round/authority scope.
    for (auto& old : impl_->resources) if (old.round == round && old.epoch == epoch) {
        old.data = std::move(resources); old.graph = std::move(graph); return;
    }
    impl_->resources.push_back({round, epoch, std::move(resources), std::move(graph)});
}
void Host::stop() {
    if (!impl_) return;
    impl_->running = false; impl_->incoming.close();
    if (impl_->thread.joinable()) impl_->thread.join();
    impl_->network.stop();
}
}
