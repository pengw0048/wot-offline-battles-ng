#pragma once
// Bot authority on the hidden worker: simulation step, manifests, outgoing Bot messages, poses and tracks.
#include "stock_native_battle.hpp"

namespace native_battle {
struct BotAuthority {};

namespace bots {
// Per frame while live: artillery arcs, the Bot simulation step and its outgoing messages.
void update(Battle& b, double now, double dt);
// Applies the accepted authority pose set; returns the presented Bot count.
std::size_t presentPoses(Battle& b, double now);
// Hidden worker: drains the transport; false when the round ended inside it.
bool drainWorkerTransport(Battle& b);
void retryManifest(Battle& b, double now);
void workerProbe(Battle& b);
// Remote Bot track feed for a replica-presented Bot.
void tracks(Battle& b, Actor& vehicle, double now);
}
}
