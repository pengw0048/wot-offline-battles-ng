// Production PyTimedWarplaneMotor kernel driven by oracle scenarios on stdin.
// Floats travel as IEEE-754 bit patterns; one output line per operation.
#include "stock_timed_warplane_motor_reference.hpp"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

namespace {
float f(std::uint32_t bits) { float v; std::memcpy(&v, &bits, 4); return v; }
std::uint32_t b(float v) { std::uint32_t bits; std::memcpy(&bits, &v, 4); return bits; }
float readFloat() { std::uint32_t bits; std::cin >> bits; return f(bits); }
wot::StockWarplanePoint readPoint() {
    wot::StockWarplanePoint p;
    for (float& v : p.position) v = readFloat();
    for (float& v : p.velocity) v = readFloat();
    p.time = readFloat();
    return p;
}
void state(const wot::StockTimedWarplane& s) {
    std::cout << b(s.roll) << ' ';
    for (float v : s.transform) std::cout << b(v) << ' ';
    std::cout << b(s.startTime) << ' ' << b(s.elapsed) << ' ';
    for (float v : s.position) std::cout << b(v) << ' ';
    for (float v : s.velocity) std::cout << b(v) << ' ';
    std::cout << s.segment << ' ' << b(s.clock) << ' ' << s.points.size() << ' ';
    for (const auto& p : s.points) {
        for (float v : p.position) std::cout << b(v) << ' ';
        for (float v : p.velocity) std::cout << b(v) << ' ';
        std::cout << b(p.time) << ' ';
    }
}
}

int main() {
    std::string kind;
    wot::StockTimedWarplane s;
    const wot::StockWarplaneMath& math = referenceWarplaneMath();
    while (std::cin >> kind) {
        if (kind == "init") {
            s = wot::StockTimedWarplane();
            s.rollSpeed = readFloat();
            const float now = readFloat();
            const wot::StockWarplanePoint first = readPoint();
            const wot::StockWarplanePoint second = readPoint();
            wot::initStockTimedWarplane(s, now, first, second, math);
        } else if (kind == "add") {
            std::cout << wot::addStockTimedWarplanePoint(s, readPoint()) << ' ';
        } else if (kind == "rev") {
            const float now = readFloat(), dt = readFloat();
            std::string calls;
            int count = 0;
            wot::advanceStockTimedWarplane(s, now, dt, [&](std::uint32_t index, const wot::StockWarplaneVector& p,
                    const wot::StockWarplaneVector& v) {
                ++count;
                calls += std::to_string(index);
                for (float x : p) calls += ' ' + std::to_string(b(x));
                for (float x : v) calls += ' ' + std::to_string(b(x));
                calls += ' ';
            }, math);
            std::cout << count << ' ' << calls;
        } else return 2;
        state(s);
        std::cout << '\n';
    }
    return 0;
}
