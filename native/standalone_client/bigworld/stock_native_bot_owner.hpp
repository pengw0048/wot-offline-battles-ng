#pragma once
// The worker's native Bot simulation owner, driven directly by the native battle's Bot authority.
// `runtime` is the round's Python BotRuntime: cold installation still reads its per-Bot rows.
#include "stock_native_battle.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace stock_native_bot {
class Clock;
struct Owner;
namespace owner {
using native_battle::Battle;
namespace p = stock_native_bot_python;
// Installs the persistent owner on the avatar's space; throws when the space or snapshot is unavailable.
std::shared_ptr<Owner> create(Battle& b, PyObject* runtime);
// Retires the owner idempotently; a retired owner ignores every later call.
void close(Owner& o);
// The owner still belongs to the runtime's round and native generation.
bool current(const Owner& o, PyObject* runtime);
const Clock& clock(const Owner& o);

// Prepared human input from the latest accepted snapshot.
p::Ref players(Battle& b, PyObject* runtime);
// One Bot update: the outgoing authority messages (bot_state, bot_ram, bot_observation) as Python
// rows for the LAN client senders.
p::Ref update(Owner& o, Battle& b, PyObject* runtime, double dt, double now, PyObject* players);
bool frame(Owner& o, PyObject* runtime, double now, bool force);
std::size_t prewarm(Owner& o, Battle& b, PyObject* runtime, double now);
std::size_t prewarmPlayers(Owner& o, Battle& b, PyObject* runtime, double now);
std::size_t presentPoses(Owner& o, Battle& b, PyObject* runtime, double now);
void snapshot(Owner& o, PyObject* runtime, PyObject* message);

// World events: [(sequence, event)] while current; each accepted send is acknowledged.
p::Ref worldEvents(Owner& o, Battle& b, PyObject* runtime);
bool acknowledgeWorld(Owner& o, std::uint64_t sequence);
bool worldIngress(Owner& o, PyObject* runtime, PyObject* event, double now);
p::Ref playerContacts(Owner& o, Battle& b, PyObject* runtime, PyObject* players, double now);
bool acknowledgePlayerContact(Owner& o, std::uint64_t serial, double now);
// (sequence, observations) when a batch is due.
std::optional<std::pair<std::int64_t, p::Ref>> playerEnvironment(Owner& o, Battle& b, PyObject* runtime, double dt, bool authority);
void acknowledgePlayerEnvironment(Owner& o, Battle& b, std::int64_t sequence, bool sent);
p::Ref humanRam(Owner& o, Battle& b, PyObject* runtime);

// Detached turrets. `vehicle` keys the queue by kind, engine id and network id.
bool detachedQueue(Owner& o, PyObject* runtime, const native_battle::Actor& vehicle, PyObject* entity, PyObject* state, double now);
bool detachedIngress(Owner& o, PyObject* runtime, PyObject* message, double now);
p::Ref detachedAdvance(Owner& o, Battle& b, PyObject* runtime, double now);
p::Ref detachedRows(Owner& o);

// Projectiles.
bool projectileIngress(Owner& o, PyObject* runtime, PyObject* message, double now);
bool projectileObserve(Owner& o, PyObject* runtime, PyObject* message, double now);
bool projectileEvent(Owner& o, PyObject* runtime, PyObject* event, double now);
// [(serial, kind, wire kwargs)] to send through send_projectile_<kind>.
p::Ref projectileAdvance(Owner& o, Battle& b, PyObject* runtime, double now, bool force);
bool projectileAcknowledge(Owner& o, std::uint64_t serial, bool sent);
// The next launch wave [(bot, sequence, args, kwargs)] for send_projectile_launch.
p::Ref projectileLaunches(Owner& o, PyObject* runtime, PyObject* message);
void projectileLaunchResult(Owner& o, std::int64_t bot, std::int64_t sequence, bool accepted);
// Records one local transport failure without failing the round.
void transportError(Owner& o, const std::string& message);

bool acknowledgeLaunch(Owner& o, std::int64_t bot, std::int64_t sequence);
void acknowledgeRam(Owner& o, std::int64_t player, std::int64_t sequence);
void stageTiming(Owner& o, bool enabled);
// Cumulative native boundary timings and counters.
p::Ref statistics(Owner& o);
}
}
