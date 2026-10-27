// Production LeveredSuspension kernels driven by oracle scenarios on stdin.
// Floats travel as IEEE-754 bit patterns; one output line per scenario.
#include "stock_battle_vehicle_levered_reference.hpp"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
using wot::StockAnimationMatrix;
using wot::StockAnimationVector;
using wot::StockLeverContact;
using wot::StockLeverState;

float f(std::uint32_t bits) { float v; std::memcpy(&v, &bits, 4); return v; }
std::uint32_t b(float v) { std::uint32_t bits; std::memcpy(&bits, &v, 4); return bits; }
std::uint32_t word() { std::uint32_t w; std::cin >> w; return w; }
float real() { return f(word()); }
void readMatrix(StockAnimationMatrix& m) { for (float& v : m) v = real(); }
void readVector(StockAnimationVector& v) { for (float& c : v) c = real(); }
void put(std::uint32_t w) { std::cout << w << ' '; }
void putf(float v) { put(b(v)); }
void putMatrix(const StockAnimationMatrix& m) { for (float v : m) putf(v); }
void putVector(const StockAnimationVector& v) { for (float c : v) putf(c); }
const wot::StockLeveredMath& math() { return referenceLeveredMath(); }

void readContact(StockLeverContact& c) {
    readVector(c.position); readVector(c.normal); c.distance = real();
}
void putContact(const StockLeverContact& c) {
    putVector(c.position); putVector(c.normal); putf(c.distance); put(c.hit);
}
// Native 0x30 lever record: cos, sin, joint, lengths, limits, flag dword.
void readLever(StockLeverState& l) {
    l.cosine = real(); l.sine = real(); readVector(l.joint); l.jointLength = real(); l.trackLength = real();
    l.limits.cos1 = real(); l.limits.sin1 = real(); l.limits.cos2 = real(); l.limits.sin2 = real();
    l.limits.forward = (word() & 0xff) != 0;
}
void putLever(const StockLeverState& l) {
    putf(l.cosine); putf(l.sine); putVector(l.joint); putf(l.jointLength); putf(l.trackLength);
    putf(l.limits.cos1); putf(l.limits.sin1); putf(l.limits.cos2); putf(l.limits.sin2);
    put(l.limits.forward ? 1 : 0);
}
wot::detail::StockLeverFrame readFrame() {
    wot::detail::StockLeverFrame frame;
    readMatrix(frame.world); readMatrix(frame.inverse);
    readVector(frame.axisZ); readVector(frame.side); readVector(frame.up);
    return frame;
}

void limits() {
    const float minimum = real(), maximum = real();
    const wot::StockLeverLimits l = wot::makeStockLeverLimits(minimum, maximum, math());
    putf(l.cos1); putf(l.sin1); putf(l.cos2); putf(l.sin2); put(l.forward ? 1 : 0);
}

void project() {
    const wot::detail::StockLeverFrame frame = readFrame();
    StockAnimationVector anchor; readVector(anchor);
    StockLeverContact c; readContact(c);
    wot::detail::projectStockLeverContact(frame, anchor, c, wot::stockLeverConeCos(math()), math());
    putVector(c.position); putVector(c.normal); putf(c.distance);
}

void solve() {
    const wot::detail::StockLeverFrame frame = readFrame();
    StockAnimationVector anchor; readVector(anchor);
    StockLeverContact c; readContact(c);
    StockLeverState lever; readLever(lever);
    wot::detail::solveStockLever(c, lever, anchor, frame, math());
    putVector(c.position); putVector(c.normal); putf(c.distance);
    putf(lever.cosine); putf(lever.sine); putVector(lever.joint);
}

void chain() {
    std::vector<StockLeverContact> contacts(word());
    for (StockLeverContact& c : contacts) readContact(c);
    const std::uint32_t i = word(), j = word();
    StockAnimationMatrix fallback, m; readMatrix(fallback); readMatrix(m);
    putMatrix(wot::detail::stockLeverChain(contacts, i, j, fallback, m, math()));
}

// build: nodes, locals, then per suspension: levers (start joint track min max), speedMul.
struct Scene {
    std::vector<StockAnimationMatrix> locals;
    std::vector<wot::StockLeveredSuspension> suspensions;
};

Scene readScene() {
    Scene scene;
    scene.locals.resize(word());
    for (StockAnimationMatrix& m : scene.locals) readMatrix(m);
    scene.suspensions.resize(word());
    for (wot::StockLeveredSuspension& s : scene.suspensions) {
        std::vector<wot::StockLeverInput> levers(word());
        for (wot::StockLeverInput& l : levers) {
            l.startNode = static_cast<int>(word()); l.jointNode = static_cast<int>(word());
            l.trackNode = static_cast<int>(word()); l.minAngle = real(); l.maxAngle = real();
        }
        const float speed = real();
        if (!wot::buildStockLeveredSuspension(s, levers, scene.locals, speed, math())) std::exit(3);
    }
    return scene;
}

void build() {
    const Scene scene = readScene();
    for (const wot::StockLeveredSuspension& s : scene.suspensions) {
        put(s.split); putf(s.rayLength); putf(s.interpolationSpeedMul); put(s.levers.size());
        for (std::size_t k = 0; k < s.levers.size(); ++k) {
            const wot::StockLeverAuthored& a = s.authored[k];
            put(a.startNode); putMatrix(a.start); put(a.jointNode); putMatrix(a.joint);
            put(a.trackNode); putMatrix(a.track);
            putLever(s.levers[k]); putVector(s.anchors[k]); putVector(s.probes[k]);
        }
    }
}

void sequence() {
    Scene scene = readScene();
    const std::uint32_t frames = word();
    for (std::uint32_t number = 0; number < frames; ++number) {
        const float dt = real();
        const bool space = word() != 0;
        std::vector<StockAnimationMatrix> worlds(scene.locals.size());
        for (StockAnimationMatrix& m : worlds) readMatrix(m);
        std::vector<StockAnimationMatrix> frame(scene.suspensions.size());
        std::vector<std::array<float, 6> > boxes(scene.suspensions.size());
        std::vector<bool> gates(scene.suspensions.size());
        for (std::size_t i = 0; i < frame.size(); ++i) {
            readMatrix(frame[i]);
            for (float& v : boxes[i]) v = real();
            gates[i] = word() != 0;
        }
        std::vector<std::uint32_t> answers(word() * 5);
        for (std::uint32_t& w : answers) w = word();
        std::size_t next = 0;
        std::vector<std::uint32_t> calls;
        std::uint32_t callCount = 0;
        for (std::size_t i = 0; i < frame.size(); ++i)
            scene.suspensions[i].history = wot::advanceStockLeveredHistory(scene.suspensions[i].history, gates[i]);
        for (std::size_t i = 0; i < frame.size(); ++i)
            wot::groundStockLevered(scene.suspensions[i], frame[i], boxes[i],
                [&](const std::array<float, 6>& box) {
                    if (!space) return;
                    ++callCount; calls.push_back(1);
                    for (float v : box) calls.push_back(b(v));
                },
                [&](const StockAnimationVector& o, const StockAnimationVector& d, float length,
                    float& distance, StockAnimationVector& normal) {
                    ++callCount; calls.push_back(2);
                    for (float v : o) calls.push_back(b(v));
                    for (float v : d) calls.push_back(b(v));
                    calls.push_back(b(length));
                    const std::uint32_t* a = &answers[5 * next++];
                    if (!a[0]) return false;
                    distance = f(a[1]);
                    normal = {{f(a[2]), f(a[3]), f(a[4])}};
                    return true;
                }, math());
        for (std::size_t i = 0; i < frame.size(); ++i)
            wot::updateStockLevered(scene.suspensions[i], dt, frame[i], math());
        std::vector<wot::StockLeveredNodeWrite> writes;
        for (std::size_t i = 0; i < frame.size(); ++i)
            wot::writeStockLeveredNodes(scene.suspensions[i], frame[i], scene.locals, worlds, writes, math());
        put(callCount);
        for (std::uint32_t w : calls) put(w);
        for (const wot::StockLeveredSuspension& s : scene.suspensions) {
            put(s.history);
            for (const StockLeverContact& c : s.contacts) putContact(c);
            put(s.smoothed.size());
            for (const StockLeverContact& c : s.smoothed) putContact(c);
            for (const StockLeverState& l : s.levers) { putf(l.cosine); putf(l.sine); putVector(l.joint); }
            for (const StockAnimationVector& p : s.probes) putVector(p);
        }
        for (const StockAnimationMatrix& m : scene.locals) putMatrix(m);
        put(writes.size());
    }
}
}

int main() {
    std::string kind;
    while (std::cin >> kind) {
        if (kind == "limits") limits();
        else if (kind == "project") project();
        else if (kind == "solve") solve();
        else if (kind == "chain") chain();
        else if (kind == "build") build();
        else if (kind == "sequence") sequence();
        else return 2;
        std::cout << '\n';
    }
    return 0;
}
