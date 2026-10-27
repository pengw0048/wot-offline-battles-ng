// Runs the production WGBspCollisionModel core on hex float32 inputs for the
// original-instruction oracle. One request per line, one reply per line.
#include "stock_bsp_collision_model_host.hpp"
#include "stock_bsp_collision_model.hpp"

#include <cstdio>
#include <iostream>
#include <sstream>

std::vector<HostCall> hostCalls;
HostResults hostResults;

namespace {
using wot::stock_bsp::Point;
float word(std::istream & in) {
    std::uint32_t bits;
    in >> std::hex >> bits;
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}
std::uint32_t bits(float value) {
    std::uint32_t result;
    std::memcpy(&result, &value, 4);
    return result;
}
Point point(std::istream & in) {
    const float x = word(in), y = word(in), z = word(in);
    return Point{x, y, z};
}
void out(std::ostream & o, const Point & p) { o << ' ' << bits(p.x) << ' ' << bits(p.y) << ' ' << bits(p.z); }
}

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string op;
        in >> op;
        std::ostringstream reply;
        reply << std::hex;
        if (op == "closest") {
            const Point p = point(in), a = point(in), b = point(in), c = point(in);
            out(reply, wot::stock_bsp::closestPoint(p, a, b, c));
        } else if (op == "less") {
            wot::stock_bsp::SegmentHit h[2];
            for (auto & hit : h) {
                in >> std::hex >> hit.materialKind;
                hit.distance = word(in);
                hit.normal = point(in);
            }
            reply << ' ' << int(wot::stock_bsp::segmentHitLess(h[0], h[1]));
        } else if (op == "segment") {
            // CollideSegmentTriangleCollector over the public WorldTriangle test.
            const Point v0 = point(in), v1 = point(in), v2 = point(in), s = point(in), d = point(in);
            std::uint32_t flags, stop;
            in >> std::hex >> flags >> stop;
            const BW::WorldTriangle triangle(BW::Vector3(v0.x, v0.y, v0.z), BW::Vector3(v1.x, v1.y, v1.z),
                                             BW::Vector3(v2.x, v2.y, v2.z), BW::WorldTriangle::Flags(flags));
            float distance = 1e10f;
            if (flags & 0x50 || !triangle.intersects(BW::Vector3(s.x, s.y, s.z), BW::Vector3(d.x, d.y, d.z), distance))
                reply << " 3 -";
            else {
                reply << ' ' << (stop ? 0 : 3) << ' ' << triangle.materialKind() << ' ' << bits(distance);
                out(reply, wot::stock_bsp::rawNormal(v0, v1, v2));
            }
        } else if (op == "sphere") {
            const Point v0 = point(in), v1 = point(in), v2 = point(in), c = point(in);
            const float r2 = word(in);
            std::uint32_t flags, outside, count;
            in >> std::hex >> flags >> outside >> count;
            std::vector<wot::stock_bsp::SphereHit> hits(count);
            for (auto & hit : hits) {
                hit.point = point(in);
                hit.normal = point(in);
                hit.distanceSquared = word(in);
                in >> std::hex >> hit.materialKind;
            }
            if (!(flags & 0x50) && !(outside && wot::stock_bsp::sphereCentreBehind(c, v0, v1, v2)))
                wot::stock_bsp::collectSphereHit(hits, c, r2, v0, v1, v2, (flags >> 8) & 0xff);
            reply << ' ' << hits.size();
            for (const auto & hit : hits) {
                out(reply, hit.point);
                out(reply, hit.normal);
                reply << ' ' << bits(hit.distanceSquared) << ' ' << hit.materialKind;
            }
        } else if (op == "faced") {
            Point n = point(in);
            const Point d = point(in);
            const float cosine = wot::stock_bsp::facedCosine(n, d);
            out(reply, n);
            reply << ' ' << bits(cosine);
        } else if (op == "radius") {
            const Point low = point(in), high = point(in);
            reply << ' ' << bits(std::sqrt(wot::stock_bsp::boundingRadiusSquared(low, high)));
        } else if (op == "sweep") {
            const Point c = point(in);
            const float r = word(in);
            Point first[3], second[3], extent;
            wot::stock_bsp::sphereSweep(c, r, first, second, extent);
            for (const Point & p : first) out(reply, p);
            for (const Point & p : second) out(reply, p);
            out(reply, extent);
            reply << ' ' << bits(r * r);
        }
        std::cout << reply.str() << std::endl;
    }
    return 0;
}
