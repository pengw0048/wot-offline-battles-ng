#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace wot {
struct StockShadowPoint2 { float x, y; };
struct StockShadowRectangle2 { StockShadowPoint2 center, axis0, axis1; };

// #1513 9cded0: PCA seed followed by the gift-wrapped hull's edge normals.
// The returned axes include their half lengths. Keep strict area comparison,
// the native degeneracy thresholds, longest-axis order and canonical sign.
inline StockShadowRectangle2 stock_shadow_rectangle(
    const StockShadowPoint2 * points, unsigned count, float minimumHalfExtent = .001f)
{
    StockShadowPoint2 mean = points[0];
    for (unsigned i = 1; i < count; ++i) { mean.x += points[i].x; mean.y += points[i].y; }
    mean.x *= 1.f / count; mean.y *= 1.f / count;
    float xx = 0.f, xy = 0.f, yy = 0.f;
    for (unsigned i = 0; i < count; ++i) {
        const float x = points[i].x - mean.x, y = points[i].y - mean.y;
        xx += x*x; xy += x*y; yy += y*y;
    }
    const float trace = xx + yy;
    const float discriminant = trace*trace - 4.f*(xx*yy-xy*xy);
    const float root = discriminant > 0.f ? std::sqrt(discriminant) : 0.f;
    const float eigenvalue = std::max((trace+root)*.5f, (trace-root)*.5f);
    StockShadowPoint2 seed = {1.f, 0.f};
    if (std::fabs(xy) > .00001f) seed = {eigenvalue-yy, xy};
    const auto normalise = [](StockShadowPoint2 value) {
        const float length = std::sqrt(value.x*value.x+value.y*value.y);
        if (length != 0.f) { value.x /= length; value.y /= length; }
        return value;
    };
    seed = normalise(seed);
    unsigned start = 0;
    for (unsigned i = 1; i < count; ++i) {
        const float x = points[start].x - points[i].x;
        if (std::fabs(x) < 1.e-10f ? points[start].y > points[i].y : x > 0.f) start = i;
    }
    std::vector<StockShadowPoint2> hull;
    StockShadowPoint2 previous = {0.f, -1.f};
    unsigned current = start;
    for (unsigned step = 0; step < count; ++step) {
        float bestDot = -std::numeric_limits<float>::max();
        int next = -1;
        StockShadowPoint2 direction = previous;
        for (unsigned offset = 1; offset < count; ++offset) {
            const unsigned candidate = (current+offset)%count;
            StockShadowPoint2 delta = {points[candidate].x-points[current].x,
                                      points[candidate].y-points[current].y};
            if (delta.x*delta.x+delta.y*delta.y <= 1.e-10f) continue;
            delta = normalise(delta);
            const float dot = delta.x*previous.x+delta.y*previous.y;
            if (dot > bestDot) { bestDot = dot; next = int(candidate); direction = delta; }
        }
        if (next < 0) { hull.clear(); break; }
        current = unsigned(next); previous = direction;
        hull.push_back(points[current]);
        const float x = points[current].x-points[start].x, y = points[current].y-points[start].y;
        if (x*x+y*y <= 1.e-10f) break;
        if (step+1 == count) hull.clear();
    }
    StockShadowRectangle2 result = {};
    float bestArea = std::numeric_limits<float>::max();
    StockShadowPoint2 axis = seed;
    for (unsigned candidate = 0; candidate <= hull.size(); ++candidate) {
        const StockShadowPoint2 perpendicular = {-axis.y, axis.x};
        float minX = std::numeric_limits<float>::max(), minY = minX;
        float maxX = -minX, maxY = -minX;
        for (unsigned i = 0; i < count; ++i) {
            const float x = points[i].x*axis.x+points[i].y*axis.y;
            const float y = points[i].x*perpendicular.x+points[i].y*perpendicular.y;
            minX = std::min(minX,x); maxX = std::max(maxX,x);
            minY = std::min(minY,y); maxY = std::max(maxY,y);
        }
        const float width = maxX-minX, height = maxY-minY, area = width*height;
        if (bestArea > area) {
            bestArea = area;
            const float cx = (minX+maxX)*.5f, cy = (minY+maxY)*.5f;
            result.center = {cx*axis.x+cy*perpendicular.x, cx*axis.y+cy*perpendicular.y};
            const float halfX = std::max(width*.5f,minimumHalfExtent);
            const float halfY = std::max(height*.5f,minimumHalfExtent);
            result.axis0 = {axis.x*halfX,axis.y*halfX};
            result.axis1 = {perpendicular.x*halfY,perpendicular.y*halfY};
            if (height > width) std::swap(result.axis0,result.axis1);
        }
        if (candidate < hull.size()) {
            const StockShadowPoint2 a = hull[candidate], b = hull[(candidate+1)%hull.size()];
            axis = normalise({b.y-a.y,a.x-b.x});
        }
    }
    const float dominant = std::fabs(result.axis0.x) > std::fabs(result.axis0.y) ? result.axis0.x : result.axis0.y;
    if (dominant < 0.f) {
        result.axis0.x = -result.axis0.x; result.axis0.y = -result.axis0.y;
        result.axis1.x = -result.axis1.x; result.axis1.y = -result.axis1.y;
    }
    return result;
}
} // namespace wot
