// Real CPython 2.7 host for the production battle scene-query sources.
// Space traversal, Water, identity and D3DX are recorded fixtures, not engine owners.
#pragma once
#include <Python.h>
#include <array>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define PY_AUTO_MODULE_FUNCTION(...)
#define BW_GUARD
#define BW_STATIC_ASSERT(expression, message) static_assert(expression, #message)

namespace BW {
using string = std::string;
template <class T> using vector = std::vector<T>;
using int32 = std::int32_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using uint = unsigned;
using SpaceID = int32;

struct Vector3 {
    float x, y, z;
    Vector3(float a = 0, float b = 0, float c = 0) : x(a), y(b), z(c) {}
    Vector3 operator-(const Vector3 & o) const { return Vector3(x - o.x, y - o.y, z - o.z); }
    bool operator==(const Vector3 & o) const { return x == o.x && y == o.y && z == o.z; }
};
struct Vector2 { float x = 0, y = 0; };
struct Vector4 {
    float x, y, z, w;
    Vector4(float a = 0, float b = 0, float c = 0, float d = 0) : x(a), y(b), z(c), w(d) {}
};
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

// Host references only. Windows uses d3dx9_43.
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
    PyObject * getObject() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    bool operator!() const { return p_ == nullptr; }
private:
    PyObject * p_;
};

template <class T> using SmartPointer = PyObjectPtr;
struct PyModuleMethodLink {
    PyModuleMethodLink(const char *, const char *, PyCFunction, const char *) {}
};
namespace Script {
int setData(PyObject * object, int & value, const char * name);
int setData(PyObject * object, PyObjectPtr & value, const char * name);
int setData(PyObject * object, Vector3 & value, const char * name);
PyObject * argCountError(const char *, int, int, ...);
int setData(PyObject * object, bool & value, const char * name);
// Math.Vector3/Vector4 are pure-Python fixtures installed by the consumer.
inline PyObject * vector(const char * type, PyObject * args) {
    if (!args) return nullptr;
    PyObject * module = PyImport_ImportModule("Math");
    PyObject * factory = module ? PyObject_GetAttrString(module, type) : nullptr;
    PyObject * result = factory ? PyObject_Call(factory, args, nullptr) : nullptr;
    Py_XDECREF(factory); Py_XDECREF(module); Py_DECREF(args);
    return result;
}
inline PyObject * getData(const Vector3 & v) { return vector("Vector3", Py_BuildValue("(fff)", v.x, v.y, v.z)); }
inline PyObject * getData(const Vector4 & v) {
    return vector("Vector4", Py_BuildValue("(ffff)", v.x, v.y, v.z, v.w));
}
}

enum {
    TRIANGLE_TERRAIN = 1 << 3,
};
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
    // Stand-in for the SDK ray test: the scripted distance, and the same
    // zero-direction rejection as 01172190 and worldtri.cpp.
    float hostDistance = 0.f;
    bool intersects(const Vector3 &, const Vector3 & dir, float & dist) const {
        if (dir.x == 0.f && dir.y == 0.f && dir.z == 0.f) return false;
        dist = hostDistance;
        return true;
    }
};
const int TRIANGLE_NOT_IN_BSP = 0xff;

struct HostIdentity { bool known = false; bool raises = false; uint32 chunkID = 0; uint32 itemIndex = 0; };
class CollisionObstacle {
public:
    Matrix matrix;
    HostIdentity identity;
    const Matrix & transform() const { return matrix; }
};
// readStockObstacleIdentity is defined by the host from HostIdentity.

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

struct CollisionVisitor { virtual ~CollisionVisitor() {} virtual bool visit(const WorldTriangle &, float) = 0; };
// The public BSPNode storage; intersectsThisNode is the unchanged SDK body.
struct BSPNode {
    uint numTriangles_ = 0, numSharedTriangles_ = 0;
    WorldTriangle * pTriangles_ = nullptr;
    WorldTriangle ** ppSharedTriangles_ = nullptr;
    bool intersectsThisNode(const Vector3 & start, const Vector3 & end, float & dist,
                            const WorldTriangle ** ppHitTriangle, CollisionVisitor * pVisitor) const;
};

struct HostVisit { SpaceID space; uint16 flags; float distance; int verdict; };
extern std::vector<HostVisit> hostVisits;
extern int hostLiveSpaces;
extern int hostRetiredNodes;
extern std::vector<bool> hostHighestLod;
extern std::vector<std::string> hostRetirements;

class ClientSpace;
class ClientSpacePtr {
public:
    ClientSpacePtr(ClientSpace * p = nullptr);
    ClientSpacePtr(const ClientSpacePtr & o);
    ~ClientSpacePtr();
    ClientSpacePtr & operator=(const ClientSpacePtr & o);
    ClientSpace * operator->() const { return p_; }
    ClientSpace * get() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    bool operator!() const { return p_ == nullptr; }
    friend bool operator==(const ClientSpacePtr & a, const ClientSpace * b) { return a.p_ == b; }
    friend bool operator==(const ClientSpace * a, const ClientSpacePtr & b) { return a == b.p_; }
private:
    ClientSpace * p_;
};

// The patched lib/space/client_space.hpp members the extracted bodies use.
class ClientSpace {
public:
    typedef uint64 GeometryMappingID;
    explicit ClientSpace(SpaceID id) : id_(id), isCleared_(false) { ++hostLiveSpaces; }
    virtual ~ClientSpace() { --hostLiveSpaces; }
    void incRef() { ++references_; }
    void decRef() { if (--references_ == 0) delete this; }
    SpaceID id() const { return id_; }
    void delMapping(GeometryMappingID mappingID);
    void clear();
    bool cleared() const;
    static bool geometryQueryActive();
    static void retireDeferredGeometry();
    AABB bounds() const { return box; }
    float collide(const Vector3 & start, const Vector3 & end, CollisionCallback & cc) const;
    float collide(const WorldTriangle & triangle, const Vector3 & translation, CollisionCallback & cc) const;
    AABB box;
protected:
    virtual void doDelMapping(GeometryMappingID mappingID) = 0;
    virtual void doClear() = 0;
    virtual float doCollide(const Vector3 & start, const Vector3 & end, CollisionCallback & cc) const = 0;
    virtual float doCollide(const WorldTriangle &, const Vector3 &, CollisionCallback &) const { return -1.f; }
private:
    SpaceID id_;
    bool isCleared_;
    int references_ = 0;
};

inline ClientSpacePtr::ClientSpacePtr(ClientSpace * p) : p_(p) { if (p_) p_->incRef(); }
inline ClientSpacePtr::ClientSpacePtr(const ClientSpacePtr & o) : ClientSpacePtr(o.p_) {}
inline ClientSpacePtr::~ClientSpacePtr() { if (p_) p_->decRef(); }
inline ClientSpacePtr & ClientSpacePtr::operator=(const ClientSpacePtr & o) {
    ClientSpace * old = p_; p_ = o.p_; if (p_) p_->incRef(); if (old) old->decRef(); return *this;
}

// Mapped geometry: one obstacle per BSP node, heap storage freed on retirement.
struct HostNode { CollisionObstacle obstacle; BSPNode node; };
class HostSpace : public ClientSpace {
public:
    explicit HostSpace(SpaceID id) : ClientSpace(id) {}
    ~HostSpace() { retireAll(); }
    std::map<GeometryMappingID, std::vector<HostNode *>> mappings;
    bool prune = true;
protected:
    void doDelMapping(GeometryMappingID mappingID) override {
        hostRetirements.push_back("delMapping");
        auto found = mappings.find(mappingID);
        if (found == mappings.end()) return;
        for (HostNode * node : found->second) retire(node);
        mappings.erase(found);
    }
    void doClear() override { hostRetirements.push_back("clear"); retireAll(); }
    float doCollide(const Vector3 & start, const Vector3 & end, CollisionCallback & cc) const override;
private:
    static void retire(HostNode * node) { delete[] node->node.pTriangles_; delete node; ++hostRetiredNodes; }
    void retireAll() {
        for (auto & mapping : mappings) for (HostNode * node : mapping.second) retire(node);
        mappings.clear();
    }
};

struct SpaceManager {
    std::map<SpaceID, ClientSpacePtr> spaces;
    static SpaceManager & instance() { static SpaceManager m; return m; }
    ClientSpace * space(SpaceID id) { auto f = spaces.find(id); return f == spaces.end() ? nullptr : f->second.get(); }
};
namespace DeprecatedSpaceHelpers {
extern ClientSpacePtr camera;
inline ClientSpacePtr cameraSpace() { return camera; }
inline void cameraSpace(const ClientSpacePtr & space) { camera = space; }
}
// Logical retirement owners called by the patched ClientSpace::clear.
inline void retireStockSpaceData(SpaceID, ClientSpace *) { hostRetirements.push_back("spaceData"); }
inline void retireStockSpaceSounds(ClientSpace *) { hostRetirements.push_back("sounds"); }

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
