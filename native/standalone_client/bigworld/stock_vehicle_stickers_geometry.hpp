#pragma once

#include "math/boundbox.hpp"
#include "math/matrix.hpp"
#include "moo/vertex_formats.hpp"
#include "physics2/worldtri.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

BW_BEGIN_NAMESPACE
namespace StockStickerGeometry
{
typedef Moo::VertexXYZNUV Vertex;
inline Vector3 normalised(Vector3 value)
{
    D3DXVec3Normalize(&value, &value);
    return value;
}
inline Vector3 transformed(const Vector3& value, const Matrix& matrix)
{
    D3DXVECTOR4 result;
    D3DXVec3Transform(&result, &value, &matrix);
    return Vector3(result.x, result.y, result.z);
}

struct Box
{
    Vector3 world[8], local[8];
    Matrix toWorld, toLocal;
    BoundingBox bounds;
};

// Original 95a190. The public Matrix::invert/normalise implementation can use
// a different float grouping; the original calls these D3DX boundaries.
inline Box box(const Vector3& start, const Vector3& end, const Vector2& size,
    const Vector3& up)
{
    Box result;
    const Vector3 z = normalised(end - start);
    const Vector3 x = normalised(up.crossProduct(z));
    const Vector3 y = z.crossProduct(x);
    const float halfWidth = size.x * .5f, halfHeight = size.y * .5f;
    const Vector3 extent = end - start;
    result.world[0] = start - x * halfWidth + y * halfHeight;
    result.world[1] = start + x * halfWidth + y * halfHeight;
    result.world[2] = start + x * halfWidth - y * halfHeight;
    result.world[3] = start - x * halfWidth - y * halfHeight;
    for (unsigned index = 0; index != 4; ++index)
        result.world[index + 4] = result.world[index] + extent;
    result.toWorld = Matrix(Vector4(x.x, x.y, x.z, 0.f),
        Vector4(y.x, y.y, y.z, 0.f), Vector4(z.x, z.y, z.z, 0.f),
        Vector4(start.x, start.y, start.z, 1.f));
    result.toLocal = result.toWorld;
    D3DXMatrixInverse(&result.toLocal, NULL, &result.toWorld);
    for (unsigned index = 0; index != 8; ++index)
    {
        result.local[index] = transformed(result.world[index], result.toLocal);
        result.bounds.addBounds(result.local[index]);
    }
    return result;
}

// Original 959e70, including the degenerate and segment-endpoint guards.
inline bool intersect(const Vector3& start, const Vector3& end,
    const Vector3& a, const Vector3& b, const Vector3& c, Vector3& point)
{
    const Vector3 u = b - a, v = c - a, n = u.crossProduct(v);
    if (n.lengthSquared() < .00001f) return false;
    const Vector3 direction = end - start;
    const float divisor = n.dotProduct(direction);
    if (std::fabs(divisor) < .00001f) return false;
    const float fraction = ((a.y - start.y) * n.y + (a.x - start.x) * n.x +
        (a.z - start.z) * n.z) / divisor;
    if (fraction < 0.f || fraction > 1.f) return false;
    point = start + direction * fraction;
    const float uu = u.x * u.x + u.y * u.y + u.z * u.z;
    const float uv = v.y * u.y + u.x * v.x + u.z * v.z;
    const float vv = v.y * v.y + v.x * v.x + v.z * v.z;
    const Vector3 w = point - a;
    const float wu = w.dotProduct(u), wv = w.dotProduct(v);
    const float reciprocal = 1.f / (uv * uv - vv * uu);
    const float s = (wv * uv - wu * vv) * reciprocal;
    if (s < 0.f || s > 1.f) return false;
    const float t = (wu * uv - wv * uu) * reciprocal;
    return t >= 0.f && s + t <= 1.f;
}

inline void sort(std::vector<Vector3>& points)
{
    if (points.empty()) return;
    Vector3 center(0.f, 0.f, 0.f);
    for (const auto& point : points) center += point;
    center *= 1.f / static_cast<float>(points.size());
    struct Ordered { Vector3 point; float angle; };
    std::vector<Ordered> ordered;
    for (const auto& point : points)
    {
        const Vector3 relative = point - center;
        float angle = std::acos(relative.x /
            std::sqrt(relative.x * relative.x + relative.y * relative.y));
        if (relative.y < 0.f) angle = 6.2831855f - angle;
        ordered.push_back({point, angle});
    }
    std::sort(ordered.begin(), ordered.end(), [](const Ordered& a, const Ordered& b)
        { return a.angle > b.angle; });
    points.clear();
    for (const auto& item : ordered)
    {
        bool equal = !points.empty();
        // 970d70 compares open intervals in this order, rather than rounding
        // a component difference before comparing its absolute value.
        if (equal) for (unsigned axis = 0; axis != 3; ++axis)
            equal = equal && points.back()[axis] + .0001f > item.point[axis] &&
                item.point[axis] > points.back()[axis] - .0001f;
        if (!equal) points.push_back(item.point);
    }
}

inline Vector2 uv(const Box& bounds, const Vector3& point)
{
    Vector2 result;
    for (unsigned index = 0; index != 2; ++index)
    {
        Vector3 axis = bounds.world[index == 0 ? 1 : 3] - bounds.world[0];
        const float length = std::sqrt(axis.lengthSquared());
        axis = normalised(axis);
        const float value = (axis.dotProduct(point) - axis.dotProduct(bounds.world[0])) / length;
        result[index] = value < 0.f ? 0.f : (value > 1.f ? 1.f : value);
    }
    return result;
}

// Original 956bf0 intersects all three triangle edges and all twelve box
// edges, then triangulates the ordered polygon. The result reports whether
// clipping reached the far face, which drives the original depth retries.
inline bool clip(const Box& bounds, const std::vector<WorldTriangle>& triangles,
    std::vector<Vertex>& output)
{
    static const unsigned edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    bool farFace = false;
    output.clear();
    for (const auto& triangle : triangles)
    {
        const Vector3 points[3] = {transformed(triangle.v0(), bounds.toLocal),
            transformed(triangle.v1(), bounds.toLocal), transformed(triangle.v2(), bounds.toLocal)};
        std::vector<Vector3> polygon;
        for (unsigned index = 0; index != 3; ++index)
        {
            Vector3 first = points[index], second = points[(index + 1) % 3];
            if (!bounds.bounds.clip(first, second)) continue;
            polygon.push_back(first); polygon.push_back(second);
            const float limit = bounds.bounds.maxBounds().z;
            if ((points[index].z > limit && first.z <= limit) ||
                (points[(index + 1) % 3].z > limit && second.z <= limit)) farFace = true;
        }
        for (unsigned index = 0; index != 12; ++index)
        {
            Vector3 point;
            if (intersect(bounds.local[edges[index][0]], bounds.local[edges[index][1]],
                points[0], points[1], points[2], point))
            {
                polygon.push_back(point);
                if (index >= 4 && index < 8) farFace = true;
            }
        }
        sort(polygon);
        for (auto& point : polygon) point = transformed(point, bounds.toWorld);
        for (size_t index = 1; index + 1 < polygon.size(); ++index)
        {
            const Vector3 normal = normalised((polygon[index] - polygon[0]).crossProduct(
                polygon[index + 1] - polygon[0]));
            for (const auto& point : {polygon[0], polygon[index], polygon[index + 1]})
            {
                Vertex vertex;
                vertex.pos_ = point;
                vertex.normal_ = normal;
                vertex.uv_ = uv(bounds, point);
                output.push_back(vertex);
            }
        }
    }
    return farFace;
}

// Original 95b130 rotates about the segment midpoint, preserving its length.
inline void rotate(Vector3& start, Vector3& end, const Vector3& normal, float angle)
{
    const Vector3 center = (end + start) * .5f;
    Vector3 direction = end - center;
    const float length = std::sqrt(direction.lengthSquared());
    direction = normalised(direction);
    if (length == 0.f) return;
    const Vector3 perpendicular = normalised(direction * direction.dotProduct(normal) - normal);
    const Vector3 half = (direction * std::cos(angle) + perpendicular * std::sin(angle)) * length;
    start = center - half;
    end = center + half;
}

inline void finish(std::vector<Vertex>& vertices)
{
    for (auto& vertex : vertices) vertex.pos_ += vertex.normal_ * .0025f;
    for (size_t index = 0; index + 1 < vertices.size(); ++index)
        for (size_t next = index + 1; next < vertices.size(); ++next)
            if (vertices[index].pos_ != vertices[next].pos_ &&
                (vertices[index].pos_ - vertices[next].pos_).lengthSquared() < .0001f)
                vertices[next].pos_ = vertices[index].pos_;
}
} // namespace StockStickerGeometry
BW_END_NAMESPACE
