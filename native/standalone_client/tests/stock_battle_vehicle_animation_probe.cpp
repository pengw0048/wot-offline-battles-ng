// Production battle animation kernels driven by oracle scenarios on stdin.
// Floats travel as IEEE-754 bit patterns; one output line per scenario.
#include "stock_battle_vehicle_animation_reference.hpp"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

namespace {
float f(std::uint32_t bits) { float v; std::memcpy(&v, &bits, 4); return v; }
std::uint32_t b(float v) { std::uint32_t bits; std::memcpy(&bits, &v, 4); return bits; }
float readFloat() { std::uint32_t bits; std::cin >> bits; return f(bits); }
std::uint32_t readWord() { std::uint32_t word; std::cin >> word; return word; }
void readMatrix(wot::StockAnimationMatrix& m) { for (float& v : m) v = readFloat(); }
void put(float v) { std::cout << b(v) << ' '; }
void putMatrix(const wot::StockAnimationMatrix& m) { for (float v : m) put(v); }

// Native field order +0x04..+0x140 (see the oracle's SWING_FLOATS).
float* swingFields(wot::StockSwingingState& s, int index) {
    if (index < 16) return &s.basis[index];
    if (index < 32) return &s.output[index - 16];
    float* tail[] = {&s.accelPeriod, &s.accelDirection, &s.lodSetting, &s.maxMovementSpeed, &s.roll[0],
        &s.shotSensitivity, &s.pitch[0], &s.pitch[1], &s.pitch[2], &s.pitch[3], &s.pitch[4], &s.pitch[5],
        &s.roll[1], &s.roll[2], &s.roll[3], &s.roll[4], &s.roll[5], &s.roll[6],
        &s.position[0], &s.position[1], &s.position[2], &s.velocity[0], &s.velocity[1], &s.velocity[2],
        &s.acceleration[0], &s.acceleration[1], &s.acceleration[2], &s.impulse[0], &s.impulse[1], &s.impulse[2],
        &s.pitchAngle, &s.rollAngle, &s.pitchVelocity, &s.rollVelocity};
    return tail[index - 32];
}
const int SWING_FLOATS = 66;

void swing() {
    wot::StockSwingingState state;
    for (int i = 0; i < SWING_FLOATS; ++i) *swingFields(state, i) = readFloat();
    state.reset = readWord() != 0;
    const std::uint32_t steps = readWord();
    for (std::uint32_t step = 0; step < steps; ++step) {
        const float elapsed = readFloat(), lod = readFloat();
        wot::StockAnimationMatrix world, compensation;
        const bool hasWorld = readWord() != 0; readMatrix(world);
        const bool hasCompensation = readWord() != 0; readMatrix(compensation);
        const bool hasImpulse = readWord() != 0;
        wot::StockAnimationVector direction; for (float& v : direction) v = readFloat();
        const float impulse = readFloat();
        if (hasImpulse) wot::receiveStockSwingingImpulse(state, direction, impulse);
        const bool changed = wot::advanceStockSwinging(state, elapsed, lod,
            [&](wot::StockAnimationMatrix& m) { if (hasWorld) m = world; return hasWorld; },
            [&](wot::StockAnimationMatrix& m) { if (hasCompensation) m = compensation; return hasCompensation; },
            referenceAnimationMath());
        std::cout << (changed ? 1 : 0) << ' ';
        for (int i = 0; i < SWING_FLOATS; ++i) put(*swingFields(state, i));
        std::cout << (state.reset ? 1 : 0) << ' ';
    }
}

void linked() {
    wot::StockLinkedNodesGroup group;
    group.knots = readWord(); group.rows = readWord();
    group.table.resize((group.knots + 1) * group.rows);
    for (float& v : group.table) v = readFloat();
    wot::StockAnimationMatrix master; readMatrix(master);
    const std::uint32_t slaves = readWord();
    std::cout << (wot::interpolateStockLinkedNodes(group,
        wot::stockLinkedNodesMasterAngle(master, referenceAnimationMath())) ? 1 : 0) << ' ';
    for (float v : group.table) put(v);
    const wot::StockAnimationMath& math = referenceAnimationMath();
    for (std::uint32_t slave = 0; slave < slaves; ++slave) {
        wot::StockAnimationMatrix authored; readMatrix(authored);
        putMatrix(math.multiply(math.rotationX(wot::stockLinkedNodesSlaveAngle(group, slave + 1)), authored));
    }
}

void sound() {
    wot::StockSuspensionSoundState s;
    s.lodSetting = readFloat(); s.angleLimit = readFloat(); s.angle = readFloat();
    s.angularSpeed = readFloat(); s.startAngle = readFloat();
    s.motion = static_cast<int>(readWord()); s.vehicleState = static_cast<std::uint16_t>(readWord());
    const float lod = readFloat(), elapsed = readFloat();
    wot::StockAnimationMatrix body, vehicle;
    const bool hasBody = readWord() != 0; readMatrix(body);
    const bool hasVehicle = readWord() != 0; readMatrix(vehicle);
    wot::StockSuspensionSoundTransition t;
    if (s.vehicleState == 2 && wot::stockSuspensionSoundLodAllows(lod, s.lodSetting))
        t = wot::advanceStockSuspensionSound(s, elapsed, [&](float& value) {
            if (!hasBody || !hasVehicle) return false;
            value = wot::stockSuspensionSoundAngle(body, vehicle, referenceAnimationMath());
            return true; });
    put(s.angle); put(s.angularSpeed); put(s.startAngle);
    std::cout << s.motion << ' ' << (t.changed ? 1 : 0) << ' ' << t.stop << ' ' << t.start << ' ';
}
}

int main() {
    std::string kind;
    while (std::cin >> kind) {
        if (kind == "swing") swing();
        else if (kind == "linked") linked();
        else if (kind == "sound") sound();
        else return 2;
        std::cout << '\n';
    }
    return 0;
}
