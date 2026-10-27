// Engine fixture for the BQ scene-query TU, BP's scene adapter and BP's core.
// Space traversal is geometric over z-planes; identity, Water fields and D3DX
// are fixtures. The production query, filter and water owners are unchanged.
#pragma once
#include <Python.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define PY_AUTO_MODULE_FUNCTION(...)

namespace BW {
using string = std::string;
using int32 = std::int32_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using SpaceID = int32;

struct Vector3 {
    float x, y, z;
    Vector3(float a = 0, float b = 0, float c = 0) : x(a), y(b), z(c) {}
    Vector3 operator-(const Vector3 & o) const { return Vector3(x - o.x, y - o.y, z - o.z); }
    bool operator==(const Vector3 & o) const { return x == o.x && y == o.y && z == o.z; }
};
struct Vector4 {
    float x, y, z, w;
    Vector4(float a = 0, float b = 0, float c = 0, float d = 0) : x(a), y(b), z(c), w(d) {}
};
struct Vector2 { float x = 0, y = 0; };
struct Matrix {
    float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    const Vector3 & applyToOrigin() const { return *reinterpret_cast<const Vector3 *>(m[3]); }
};
struct BoundingBox {
    Vector3 minimum, maximum;
    const Vector3 & minBounds() const { return minimum; }
    const Vector3 & maxBounds() const { return maximum; }
};
using AABB = BoundingBox;

inline Vector3 * D3DXVec3Normalize(Vector3 * out, const Vector3 * v) {
    const float length = std::sqrt(v->x * v->x + v->y * v->y + v->z * v->z);
    *out = length > 0.f ? Vector3(v->x / length, v->y / length, v->z / length) : Vector3();
    return out;
}
inline Vector4 * D3DXVec3Transform(Vector4 * out, const Vector3 * v, const Matrix * m) {
    *out = Vector4(v->x * m->m[0][0] + v->y * m->m[1][0] + v->z * m->m[2][0] + m->m[3][0],
                   v->x * m->m[0][1] + v->y * m->m[1][1] + v->z * m->m[2][1] + m->m[3][1],
                   v->x * m->m[0][2] + v->y * m->m[1][2] + v->z * m->m[2][2] + m->m[3][2],
                   v->x * m->m[0][3] + v->y * m->m[1][3] + v->z * m->m[2][3] + m->m[3][3]);
    return out;
}
inline Vector3 * D3DXVec3TransformNormal(Vector3 * out, const Vector3 * v, const Matrix * m) {
    *out = Vector3(v->x * m->m[0][0] + v->y * m->m[1][0] + v->z * m->m[2][0],
                   v->x * m->m[0][1] + v->y * m->m[1][1] + v->z * m->m[2][1],
                   v->x * m->m[0][2] + v->y * m->m[1][2] + v->z * m->m[2][2]);
    return out;
}

class PyObjectPtr {
public:
    enum Steal { STEAL_REFERENCE };
    PyObjectPtr(PyObject * p = nullptr) : p_(p) { Py_XINCREF(p_); }
    PyObjectPtr(PyObject * p, Steal) : p_(p) {}
    PyObjectPtr(const PyObjectPtr & o) : p_(o.p_) { Py_XINCREF(p_); }
    ~PyObjectPtr() { Py_XDECREF(p_); }
    PyObjectPtr & operator=(const PyObjectPtr & o) {
        PyObject * old = p_; p_ = o.p_; Py_XINCREF(p_); Py_XDECREF(old); return *this;
    }
    PyObject * get() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    bool operator!() const { return p_ == nullptr; }
private:
    PyObject * p_;
};

namespace Script {
// The Python wrappers are not called by this host.
inline PyObject * getData(const Vector3 &) { Py_RETURN_NONE; }
inline PyObject * getData(const Vector4 &) { Py_RETURN_NONE; }
}

enum { TRIANGLE_TERRAIN = 1 << 3 };
struct WorldTriangle {
    typedef uint16 Flags;
    Vector3 v[3];
    Flags flags_ = 0;
    const Vector3 & v0() const { return v[0]; }
    const Vector3 & v1() const { return v[1]; }
    const Vector3 & v2() const { return v[2]; }
    Flags flags() const { return flags_; }
    uint32 collisionFlags() const { return flags_ & 0xff; }
    uint32 materialKind() const { return (flags_ >> 8) & 0xff; }
};

struct HostIdentity { bool known = false; uint32 chunkID = 0; uint32 itemIndex = 0; };
class CollisionObstacle {
public:
    Matrix matrix;
    HostIdentity identity;
    const Matrix & transform() const { return matrix; }
};

const int COLLIDE_STOP = 0;
const int COLLIDE_BEFORE = 1;
const int COLLIDE_AFTER = 2;
const int COLLIDE_ALL = COLLIDE_BEFORE | COLLIDE_AFTER;
class CollisionCallback {
public:
    virtual ~CollisionCallback() {}
    virtual bool useHighestAvailableLod() { return false; }
    virtual int operator()(const CollisionObstacle &, const WorldTriangle &, float) = 0;
};

// A rectangle in the plane z = const, |x| <= halfWidth, y in [bottom, top].
struct HostPlane {
    float z, halfWidth = 100.f, bottom = -100.f, top = 100.f;
    CollisionObstacle obstacle;
    WorldTriangle triangle;
};
struct HostVisit { float z; int verdict; };

class ClientSpace {
public:
    explicit ClientSpace(SpaceID id) : id_(id) {}
    void incRef() { ++references_; }
    void decRef() { if (--references_ == 0) delete this; }
    SpaceID id() const { return id_; }
    bool cleared() const { return cleared_; }
    void clear() { cleared_ = true; planes.clear(); }
    AABB bounds() const { return AABB(); }
    // Reports every crossed plane in increasing distance; COLLIDE_BEFORE prunes
    // farther ones, as the SDK CollisionState does.
    float collide(const Vector3 & start, const Vector3 & end, CollisionCallback & cc) {
        ++collides;
        lods.push_back(cc.useHighestAvailableLod());
        const float dz = end.z - start.z;
        const Vector3 d = end - start;
        const float length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        std::vector<std::pair<float, const HostPlane *>> crossed;
        const std::vector<HostPlane> visit = planes;
        for (const HostPlane & plane : visit) {
            if (dz == 0.f) continue;
            const float t = (plane.z - start.z) / dz;
            if (t < 0.f || t > 1.f) continue;
            const float x = start.x + d.x * t, y = start.y + d.y * t;
            if (std::fabs(x) > plane.halfWidth || y < plane.bottom || y > plane.top) continue;
            crossed.push_back(std::make_pair(t * length, &plane));
        }
        std::stable_sort(crossed.begin(), crossed.end(),
            [](const std::pair<float, const HostPlane *> & a, const std::pair<float, const HostPlane *> & b)
            { return a.first < b.first; });
        float limit = -1.f;
        for (const auto & entry : crossed) {
            if (limit >= 0.f && entry.first > limit) continue;
            const int verdict = cc(entry.second->obstacle, entry.second->triangle, entry.first);
            visits.push_back(HostVisit{entry.second->z, verdict});
            if (verdict == COLLIDE_STOP) break;
            if (verdict == COLLIDE_BEFORE) limit = entry.first;
        }
        return limit;
    }
    std::vector<HostPlane> planes;
    std::vector<HostVisit> visits;
    std::vector<bool> lods;
    int collides = 0;
private:
    SpaceID id_;
    int references_ = 0;
    bool cleared_ = false;
};

class ClientSpacePtr {
public:
    ClientSpacePtr(ClientSpace * p = nullptr) : p_(p) { if (p_) p_->incRef(); }
    ClientSpacePtr(const ClientSpacePtr & o) : ClientSpacePtr(o.p_) {}
    ~ClientSpacePtr() { if (p_) p_->decRef(); }
    ClientSpacePtr & operator=(const ClientSpacePtr & o) {
        ClientSpace * old = p_; p_ = o.p_; if (p_) p_->incRef(); if (old) old->decRef(); return *this;
    }
    ClientSpace * operator->() const { return p_; }
    ClientSpace * get() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    bool operator!() const { return p_ == nullptr; }
private:
    ClientSpace * p_;
};

struct SpaceManager {
    std::map<SpaceID, ClientSpacePtr> spaces;
    static SpaceManager & instance() { static SpaceManager m; return m; }
    ClientSpace * space(SpaceID id) { auto f = spaces.find(id); return f == spaces.end() ? nullptr : f->second.get(); }
};
namespace DeprecatedSpaceHelpers {
inline ClientSpacePtr cameraSpace() { return ClientSpacePtr(); }
}

class Water {
public:
    struct WaterState { Vector2 size_; float tessellation_ = 1.f; bool useEdgeAlpha_ = false; };
    float stockGroundRayDistance(const Vector3 & start, const Vector3 & end) const;
    float stockWaterRayDistance(const Vector3 & start, const Vector3 & end, bool edgeAlpha) const;
    const Vector3 & position() const { return transform_.applyToOrigin(); }
    WaterState state_;
    Matrix transform_, invTransform_;
    uint32 gridSizeX_ = 0, gridSizeZ_ = 0;
    std::vector<uint32> wAlpha_;
    BoundingBox bbActual_;
    bool initialised_ = true, visible_ = true;
};
struct Waters : std::vector<Water *> { static Waters & instance() { static Waters w; return w; } };
}

using BW::D3DXVec3Normalize;
using BW::D3DXVec3Transform;
using BW::D3DXVec3TransformNormal;
using std::sqrtf;
