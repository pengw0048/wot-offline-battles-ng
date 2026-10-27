// Real CPython 2.7 host for the production WGBspCollisionModel binding.
// physics2 below transcribes public BigWorld 14.4.1 (bsp.cpp, worldtri.cpp,
// worldpoly.cpp, sweep_helpers.cpp); D3DXVec3Normalize is a float32 model.
// It proves binding, schema and lifetime, not #1513 native traversal bits.
#pragma once
#include "stock_bigworld_ipc_host.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include <utility>

namespace BW {
typedef unsigned int uint32;
typedef unsigned short uint16;
typedef unsigned int uint;

struct Vector2 {
    float x, y;
    Vector2(float a = 0.f, float b = 0.f) : x(a), y(b) {}
    Vector2 operator-(const Vector2 & v) const { return Vector2(x - v.x, y - v.y); }
};

struct Vector3 {
    float x, y, z;
    Vector3() : x(0.f), y(0.f), z(0.f) {}
    Vector3(float a, float b, float c) : x(a), y(b), z(c) {}
    Vector3 operator+(const Vector3 & v) const { return Vector3(x + v.x, y + v.y, z + v.z); }
    Vector3 operator-(const Vector3 & v) const { return Vector3(x - v.x, y - v.y, z - v.z); }
    Vector3 operator-() const { return Vector3(-x, -y, -z); }
    Vector3 operator*(float s) const { return Vector3(x * s, y * s, z * s); }
    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    float dotProduct(const Vector3 & v) const { return x * v.x + y * v.y + z * v.z; }
    Vector3 crossProduct(const Vector3 & v) const {
        return Vector3(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x);
    }
    void crossProduct(const Vector3 & a, const Vector3 & b) { *this = a.crossProduct(b); }
    float lengthSquared() const { return dotProduct(*this); }
    float length() const { return std::sqrt(lengthSquared()); }
    // D3DXVec3Normalize float32 model (also used by stock_cursor_camera_oracle).
    void normalise() {
        const float n = length();
        if (n == 0.f) { x = y = z = 0.f; return; }
        x = x / n; y = y / n; z = z / n;
    }
    Vector3 unitVector() const { Vector3 r(*this); r.normalise(); return r; }
};
inline Vector3 operator*(float s, const Vector3 & v) { return v * s; }

struct Matrix {
    float m[4][4];
    static const Matrix identity;
};
const Matrix Matrix::identity = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};

inline bool almostZero(float f, float epsilon) { return -epsilon < f && f < epsilon; }

class PlaneEq {
public:
    enum ShouldNormalise { SHOULD_NORMALISE, SHOULD_NOT_NORMALISE };
    PlaneEq() : d_(0.f) {}
    PlaneEq(const Vector3 & n, float d) : normal_(n), d_(d) {}
    PlaneEq(const Vector3 & a, const Vector3 & b, const Vector3 & c, ShouldNormalise s) { init(a, b, c, s); }
    void init(const Vector3 & a, const Vector3 & b, const Vector3 & c, ShouldNormalise s) {
        normal_.crossProduct(b - a, c - a);
        if (s == SHOULD_NORMALISE) normal_.normalise();
        d_ = normal_.dotProduct(a);
    }
    float distanceTo(const Vector3 & p) const { return normal_.dotProduct(p) - d_; }
    float intersectRayHalf(const Vector3 & source, float normalDotDir) const {
        if (std::fabs(normalDotDir) <= 0.f) return 0.f;
        return (d_ - normal_.dotProduct(source)) / normalDotDir;
    }
    float intersectRayHalfNoCheck(const Vector3 & source, float inverse) const {
        return (d_ - normal_.dotProduct(source)) * inverse;
    }
    const Vector3 & normal() const { return normal_; }
    float d() const { return d_; }
private:
    Vector3 normal_;
    float d_;
};

class BoundingBox {
public:
    BoundingBox(const Vector3 & a, const Vector3 & b) : min_(a), max_(b) {}
    const Vector3 & minBounds() const { return min_; }
    const Vector3 & maxBounds() const { return max_; }
private:
    Vector3 min_, max_;
};

enum {
    TRIANGLE_NOT_IN_BSP = 0xff, TRIANGLE_NOCOLLIDE = 1 << 4, TRIANGLE_WATER = 1 << 6,
    TRIANGLE_MATERIALKIND_MASK = 0xff << 8, TRIANGLE_MATERIALKIND_SHIFT = 8
};

class WorldPolygon : public std::vector<Vector3> {
public:
    bool chop(const PlaneEq & planeEq) {
        if (size() < 3) { clear(); return false; }
        std::vector<Vector3> points;
        float lastDist = planeEq.distanceTo(front());
        for (uint32 i = 0; i < size(); ++i) {
            const Vector3 lastPoint = (*this)[i];
            if (lastDist >= 0.f) points.push_back(lastPoint);
            const Vector3 point = (*this)[(i + 1) % size()];
            const float dist = planeEq.distanceTo(point);
            if ((dist >= 0.f && lastDist < 0.f) || (dist < 0.f && lastDist >= 0.f))
                points.push_back(lastPoint + (point - lastPoint) * (-lastDist / (dist - lastDist)));
            lastDist = dist;
        }
        clear();
        for (size_t i = 0; i < points.size(); ++i)
            if (!((points[i] - points[(i + 1) % points.size()]).lengthSquared() <= 0.001f * 0.001f))
                push_back(points[i]);
        return size() >= 3;
    }
};

inline bool inside(const Vector2 & a, const Vector2 & b) { return (a.x * b.y - a.y * b.x) >= 0; }

class WorldTriangle {
public:
    typedef uint16 Flags;
    WorldTriangle() : flags_(0), padding_(0) {}
    WorldTriangle(const Vector3 & a, const Vector3 & b, const Vector3 & c, Flags f = 0) : flags_(f), padding_(0) {
        v_[0] = a; v_[1] = b; v_[2] = c;
    }
    const Vector3 & v0() const { return v_[0]; }
    const Vector3 & v1() const { return v_[1]; }
    const Vector3 & v2() const { return v_[2]; }
    const Vector3 & v(uint32 i) const { return v_[i]; }
    Flags flags() const { return flags_; }
    void flags(Flags f) { flags_ = f; }
    uint collisionFlags() const { return flags_ & 0xff; }
    uint materialKind() const { return (flags_ & TRIANGLE_MATERIALKIND_MASK) >> TRIANGLE_MATERIALKIND_SHIFT; }

    bool intersects(const Vector3 & start, const Vector3 & dir, float & length) const {
        const Vector3 edge1(v1() - v0()), edge2(v2() - v0());
        const Vector3 p(dir.crossProduct(edge2));
        const float det = edge1.dotProduct(p);
        if (almostZero(det, 0.000001f)) return false;
        const float invDet = 1.f / det;
        const Vector3 t(start - v0());
        const float u = t.dotProduct(p) * invDet;
        if (u < 0.f || 1.f < u) return false;
        const Vector3 q(t.crossProduct(edge1));
        const float v = dir.dotProduct(q) * invDet;
        if (v < 0.f || 1.f < u + v) return false;
        const float distance = edge2.dotProduct(q) * invDet;
        if (0.f < distance && distance < length) { length = distance; return true; }
        return false;
    }

    bool intersects(const WorldTriangle & triangle, const Vector3 & offset) const {
        PlaneEq planeEq(v_[0], v_[1], v_[2], PlaneEq::SHOULD_NOT_NORMALISE);
        const float unitDot = planeEq.normal().unitVector().dotProduct(offset.unitVector());
        const float ndt = planeEq.normal().length() * offset.length() * unitDot;
        float vd0 = 0, vd1 = 0, vd2 = 0;
        bool brute = false;
        if (almostZero(unitDot, 0.005f)) brute = true;
        else {
            const float indt = 1.f / ndt;
            vd0 = planeEq.intersectRayHalfNoCheck(triangle.v_[0], indt);
            vd1 = planeEq.intersectRayHalfNoCheck(triangle.v_[1], indt);
            vd2 = planeEq.intersectRayHalfNoCheck(triangle.v_[2], indt);
            if (vd0 < 0.f && vd1 < 0.f && vd2 < 0.f) return false;
            if (vd0 >= 1.f && vd1 >= 1.f && vd2 >= 1.f) return false;
            if (vd0 < 0.f || vd1 < 0.f || vd2 < 0.f || vd0 >= 1.f || vd1 >= 1.f || vd2 >= 1.f) brute = true;
        }
        if (brute) {
            WorldTriangle wt1;
            PlaneEq pe1(triangle.v0(), triangle.v1(), triangle.v2(), PlaneEq::SHOULD_NOT_NORMALISE);
            if (pe1.normal().dotProduct(offset) < 0.f) {
                pe1 = PlaneEq(-pe1.normal(), -pe1.d());
                wt1 = WorldTriangle(triangle.v2(), triangle.v1(), triangle.v0());
            } else wt1 = triangle;
            WorldPolygon wp;
            wp.push_back(v_[0]); wp.push_back(v_[1]); wp.push_back(v_[2]);
            if (!wp.chop(pe1)) return false;
            const Vector3 invNormal(-pe1.normal().x, -pe1.normal().y, -pe1.normal().z);
            PlaneEq pe2(invNormal, invNormal.dotProduct(wt1.v0() + offset));
            if (!wp.chop(pe2)) return false;
            WorldTriangle wt2(wt1.v0() + offset, wt1.v1() + offset, wt1.v2() + offset);
            PlaneEq plane;
            for (uint32 i = 0; i < 3; ++i) {
                plane.init(wt1.v(i), wt2.v(i), wt1.v((i + 1) % 3), PlaneEq::SHOULD_NOT_NORMALISE);
                if (!wp.chop(plane)) return false;
            }
            return true;
        }
        const Vector3 & norm = planeEq.normal();
        float max = std::fabs(norm.x);
        int u = 1, w = 2;
        const float temp = std::fabs(norm.y);
        if (temp > max) { max = temp; u = 0; w = 2; }
        if (std::fabs(norm.z) > max) { u = 0; w = 1; }
        const Vector3 b3[3] = {triangle.v_[0] + offset * vd0, triangle.v_[1] + offset * vd1,
                               triangle.v_[2] + offset * vd2};
        const Vector2 a[3] = {Vector2(v_[0][u], v_[0][w]), Vector2(v_[1][u], v_[1][w]), Vector2(v_[2][u], v_[2][w])};
        const Vector2 b[3] = {Vector2(b3[0][u], b3[0][w]), Vector2(b3[1][u], b3[1][w]), Vector2(b3[2][u], b3[2][w])};
        const bool signA = inside(a[1] - a[0], a[2] - a[0]);
        const bool signB = inside(b[1] - b[0], b[2] - b[0]);
        for (int i1 = 0, i2 = 2; i1 <= 2; i2 = i1, i1++) {
            const Vector2 sa = a[i1] - a[i2];
            if (inside(sa, b[0] - a[i2]) != signA && inside(sa, b[1] - a[i2]) != signA &&
                inside(sa, b[2] - a[i2]) != signA) return false;
            const Vector2 sb = b[i1] - b[i2];
            if (inside(sb, a[0] - b[i2]) != signB && inside(sb, a[1] - b[i2]) != signB &&
                inside(sb, a[2] - b[i2]) != signB) return false;
        }
        return true;
    }
private:
    Vector3 v_[3];
    Flags flags_;
    uint16 padding_;
};
static_assert(sizeof(WorldTriangle) == 40, "BSPv2 triangle ABI");

struct SceneObject {};
class CollisionObstacle {
public:
    CollisionObstacle(const Matrix * t, const Matrix * i, const SceneObject &, uint32 = 0) : t_(*t), i_(*i) {}
private:
    const Matrix & t_;
    const Matrix & i_;
};
const int COLLIDE_STOP = 0, COLLIDE_BEFORE = 1, COLLIDE_AFTER = 2, COLLIDE_ALL = 3;
class CollisionCallback {
public:
    virtual ~CollisionCallback() {}
    int visit(const CollisionObstacle & o, const WorldTriangle & t, float d) { return (*this)(o, t, d); }
    static CollisionCallback s_default;
    virtual int operator()(const CollisionObstacle &, const WorldTriangle &, float) { return COLLIDE_STOP; }
};
CollisionCallback CollisionCallback::s_default;
class CollisionState {
public:
    explicit CollisionState(CollisionCallback & cc)
        : cc_(cc), onlyLess_(false), onlyMore_(false), sTravel_(0.f), eTravel_(0.f), dist_(-1.f) {}
    CollisionCallback & cc_;
    bool onlyLess_, onlyMore_;
    float sTravel_, eTravel_, dist_;
};
class CollisionVisitor {
public:
    virtual ~CollisionVisitor() {}
    virtual bool visit(const WorldTriangle & hitTriangle, float dist) = 0;
};
namespace SweepHelpers {
class BSPSweepVisitor : public CollisionVisitor {
public:
    BSPSweepVisitor(const CollisionObstacle & ob, CollisionState & cs) : ob_(ob), cs_(cs), stop_(false) {}
    bool shouldStop() const { return stop_; }
private:
    virtual bool visit(const WorldTriangle & hitTriangle, float rd) {
        const float ndist = cs_.sTravel_ + (cs_.eTravel_ - cs_.sTravel_) * rd;
        if (cs_.onlyLess_ && ndist > cs_.dist_) return true;
        if (cs_.onlyMore_ && ndist < cs_.dist_) return false;
        const int say = cs_.cc_.visit(ob_, hitTriangle, ndist);
        if ((say & 3) != 3) cs_.dist_ = ndist;
        if (!say) { stop_ = true; return true; }
        cs_.onlyLess_ = !(say & 2);
        cs_.onlyMore_ = !(say & 1);
        return cs_.onlyLess_;
    }
    const CollisionObstacle & ob_;
    CollisionState & cs_;
    bool stop_;
};
}

// Public BSPv2 current-format loader and both query traversals.
class BSPTree {
public:
    struct Node {
        int front, back;
        PlaneEq plane;
        std::vector<const WorldTriangle *> triangles;
    };
    static BSPTree * load(const std::string & bytes) {
        auto u32 = [&](size_t at) {
            if (at + 4 > bytes.size()) throw std::runtime_error("Truncated BSPv2");
            std::uint32_t v; std::memcpy(&v, bytes.data() + at, 4); return v;
        };
        if (u32(0) != 0x02505342U || u32(4) != 16 || u32(8) != 40 || u32(12) != 40)
            throw std::runtime_error("Unsupported BSPv2 header");
        const std::uint32_t triangles = u32(16), nodes = u32(20), shared = u32(24);
        BSPTree * tree = new BSPTree();
        tree->triangles_.resize(triangles);
        if (60 + size_t(triangles) * 40 + size_t(shared) * 4 + size_t(nodes) * 40 > bytes.size())
            throw std::runtime_error("Truncated BSPv2 arrays");
        std::memcpy(tree->triangles_.data(), bytes.data() + 60, size_t(triangles) * 40);
        size_t at = 60 + size_t(triangles) * 40;
        std::vector<std::int32_t> sharedIndices(shared);
        for (std::uint32_t i = 0; i < shared; ++i, at += 4) sharedIndices[i] = std::int32_t(u32(at));
        tree->nodes_.resize(nodes);
        for (std::uint32_t i = 0; i < nodes; ++i, at += 40) {
            Node & node = tree->nodes_[i];
            node.front = std::int32_t(u32(at)); node.back = std::int32_t(u32(at + 4));
            float plane[4]; std::memcpy(plane, bytes.data() + at + 8, 16);
            node.plane = PlaneEq(Vector3(plane[0], plane[1], plane[2]), plane[3]);
            const std::int32_t first = std::int32_t(u32(at + 24)), count = std::int32_t(u32(at + 28));
            const std::int32_t sharedFirst = std::int32_t(u32(at + 32)), sharedCount = std::int32_t(u32(at + 36));
            for (std::int32_t t = 0; t < count; ++t) node.triangles.push_back(&tree->triangles_.at(first + t));
            for (std::int32_t t = 0; t < sharedCount; ++t)
                node.triangles.push_back(&tree->triangles_.at(sharedIndices.at(sharedFirst + t)));
        }
        return tree;
    }
    uint numNodes() const { return uint(nodes_.size()); }
    uint numTriangles() const { return uint(triangles_.size()); }
    const std::vector<WorldTriangle> & triangles() const { return triangles_; }
    void remapFlags(const std::vector<WorldTriangle::Flags> & map) {
        for (WorldTriangle & t : triangles_) t.flags(map[std::uint32_t(t.flags()) % map.size()]);
    }

    bool intersects(const Vector3 & start, const Vector3 & end, float & dist, const WorldTriangle ** pp,
                    CollisionVisitor * visitor) const {
        const WorldTriangle * hit = NULL;
        if (!pp) pp = &hit;
        const float origDist = dist;
        const WorldTriangle * origHT = *pp;
        const Vector3 delta = end - start;
        const float tolerancePct = 0.01f / delta.length();
        struct Stack { int node, eBack; float sDist, eDist; };
        std::vector<Stack> stack(1, Stack{0, -1, 0.f, 1.f});
        while (!stack.empty()) {
            const Stack cur = stack.back(); stack.pop_back();
            const Node & node = nodes_[cur.node];
            const float sDist = cur.sDist, eDist = cur.eDist;
            float iDist = 0.f;
            int sBack, eBack;
            const int sides[2] = {node.front, node.back};
            if (node.front < 0 && node.back < 0) { sBack = -2; eBack = -2; }
            else if (cur.eBack == -1) {
                const float sOut = node.plane.distanceTo(start + delta * (sDist - tolerancePct));
                const float eOut = node.plane.distanceTo(start + delta * (eDist + tolerancePct));
                sBack = int(sOut < 0.f); eBack = int(eOut < 0.f);
                const int startSide = sides[sBack];
                if (sBack == eBack) {
                    if (std::fabs(sOut) < 0.01f || std::fabs(eOut) < 0.01f)
                        stack.push_back(Stack{cur.node, !sBack, sDist, eDist});
                    if (startSide >= 0) stack.push_back(Stack{startSide, -1, sDist, eDist});
                    continue;
                }
                iDist = node.plane.intersectRayHalf(start, node.plane.normal().dotProduct(delta));
                if (startSide >= 0) {
                    stack.push_back(Stack{cur.node, eBack, sDist, eDist});
                    stack.push_back(Stack{startSide, -1, sDist, iDist});
                    continue;
                }
            } else {
                sBack = eBack = cur.eBack;
                iDist = node.plane.intersectRayHalf(start, node.plane.normal().dotProduct(delta));
            }
            if (thisNode(node, start, end, dist, pp, visitor) && dist <= cur.eDist + tolerancePct) return true;
            dist = origDist; *pp = origHT;
            if (eBack >= 0 && sides[eBack] >= 0) stack.push_back(Stack{sides[eBack], -1, iDist, eDist});
        }
        return false;
    }

    bool intersects(const WorldTriangle & triangle, const Vector3 & translation, CollisionVisitor * visitor) const {
        return sweep(0, triangle, translation, visitor);
    }

private:
    bool thisNode(const Node & node, const Vector3 & start, const Vector3 & end, float & dist,
                  const WorldTriangle ** pp, CollisionVisitor * visitor) const {
        bool result = false;
        const Vector3 direction(end - start);
        for (const WorldTriangle * candidate : node.triangles) {
            if (candidate->collisionFlags() == TRIANGLE_NOT_IN_BSP) continue;
            const float original = dist;
            if (candidate->intersects(start, direction, dist) && (!visitor || visitor->visit(*candidate, dist))) {
                result = true; *pp = candidate;
            } else dist = original;
        }
        return result;
    }
    bool sweepThisNode(const Node & node, const WorldTriangle & t, const Vector3 & o, CollisionVisitor * v) const {
        for (const WorldTriangle * candidate : node.triangles)
            if (candidate->collisionFlags() != TRIANGLE_NOT_IN_BSP && candidate->intersects(t, o) &&
                (!v || v->visit(*candidate, 0.f))) return true;
        return false;
    }
    bool sweep(int index, const WorldTriangle & t, const Vector3 & o, CollisionVisitor * v) const {
        const Node & node = nodes_[index];
        if (node.front < 0 && node.back < 0) return sweepThisNode(node, t, o, v);
        const float d[6] = {node.plane.distanceTo(t.v0()), node.plane.distanceTo(t.v1()), node.plane.distanceTo(t.v2()),
                            node.plane.distanceTo(t.v0() + o), node.plane.distanceTo(t.v1() + o),
                            node.plane.distanceTo(t.v2() + o)};
        const float lo = std::min(std::min(std::min(d[0], d[1]), d[2]), std::min(std::min(d[3], d[4]), d[5]));
        const float hi = std::max(std::max(std::max(d[0], d[1]), d[2]), std::max(std::max(d[3], d[4]), d[5]));
        if (lo < 0.01f && hi > -0.01f && sweepThisNode(node, t, o, v)) return true;
        if (node.back >= 0 && lo < 0.01f && sweep(node.back, t, o, v)) return true;
        if (node.front >= 0 && hi > -0.01f && sweep(node.front, t, o, v)) return true;
        return false;
    }
    std::vector<WorldTriangle> triangles_;
    std::vector<Node> nodes_;
};

// Intrusive SmartPointer over an incRef/decRef pair, like cstdmf.
inline void incRef(PyObject * o) { Py_INCREF(o); }
inline void decRef(PyObject * o) { Py_DECREF(o); }
template <class T> class SmartPointer {
public:
    SmartPointer(T * p = NULL) : p_(p) { if (p_) incRef(p_); }
    SmartPointer(const SmartPointer & o) : p_(o.p_) { if (p_) incRef(p_); }
    ~SmartPointer() { if (p_) decRef(p_); }
    SmartPointer & operator=(const SmartPointer & o) {
        T * old = p_; p_ = o.p_; if (p_) incRef(p_); if (old) decRef(old); return *this;
    }
    T * get() const { return p_; }
    T * operator->() const { return p_; }
    bool hasObject() const { return p_ != NULL; }
    explicit operator bool() const { return p_ != NULL; }
    bool operator!() const { return p_ == NULL; }
    bool operator==(const SmartPointer & o) const { return p_ == o.p_; }
private:
    T * p_;
};
typedef SmartPointer<PyObject> PyObjectPtr;

// Host Model cache: Model::get( name ) shares one instance per resource while
// referenced, like ModelMap. The Python loader is the resource reader.
class Model;
typedef SmartPointer<Model> ModelPtr;
PyObject * & hostModelLoader();
std::map<std::string, Model *> & hostModels();
class Model {
public:
    static ModelPtr get(const std::string & name);
    const BSPTree * decompose() const { return bsp_; }
    const BoundingBox & boundingBox() const { return box_; }
    const std::string & resourceID() const { return name_; }
    int refs_ = 0;
    Model(const std::string & name, const BoundingBox & box, BSPTree * bsp) : name_(name), box_(box), bsp_(bsp) {}
    ~Model() { hostModels().erase(name_); delete bsp_; }
private:
    std::string name_;
    BoundingBox box_;
    BSPTree * bsp_;
};
inline void incRef(Model * m) { ++m->refs_; }
inline void decRef(Model * m) { if (--m->refs_ == 0) delete m; }

class SuperModel {
public:
    explicit SuperModel(const std::vector<std::string> & names) : isOK_(true) {
        for (const std::string & name : names) {
            ModelPtr model = Model::get(name);
            if (!model) { isOK_ = false; continue; }
            models_.push_back(model);
        }
    }
    int nModels() const { return int(models_.size()); }
    ModelPtr topModel(int i) { return models_[i]; }
    bool isOK() const { return isOK_; }
private:
    std::vector<ModelPtr> models_;
    bool isOK_;
};

// BigWorld.Model( *names ): enough PyModel for setModel and its Python callers.
class PyModel : public PyObjectPlus {
    Py_Header(PyModel, PyObjectPlus)
public:
    explicit PyModel(const std::vector<std::string> & names) : PyObjectPlus(&s_type_), superModel_(names),
        matrix_(Py_None), sources_(PyTuple_New(Py_ssize_t(names.size()))) {
        Py_INCREF(matrix_);
        for (size_t i = 0; i < names.size(); ++i)
            PyTuple_SET_ITEM(sources_, Py_ssize_t(i), PyString_FromString(names[i].c_str()));
    }
    ~PyModel() { Py_DECREF(matrix_); Py_DECREF(sources_); Py_XDECREF(dict_); }
    // Entity scripts also set castsShadow and addMotor on the stock PyModel.
    static Py_ssize_t dictOffset() {
        PyModel * p = static_cast<PyModel *>(PyObject_Malloc(sizeof(PyModel)));
        const Py_ssize_t offset = reinterpret_cast<char *>(&p->dict_) - reinterpret_cast<char *>(static_cast<PyObject *>(p));
        PyObject_Free(p);
        return offset;
    }
    SuperModel * pSuperModel() { return &superModel_; }
    static PyObject * _pyNew(PyObject *, PyObject * args, PyObject *) {
        std::vector<std::string> names;
        for (Py_ssize_t i = 0; i < PyTuple_Size(args); ++i) {
            if (!PyString_Check(PyTuple_GET_ITEM(args, i))) { PyErr_SetString(PyExc_TypeError, "names"); return NULL; }
            names.push_back(PyString_AsString(PyTuple_GET_ITEM(args, i)));
        }
        return new PyModel(names);
    }
    static PyObject * getMatrix(PyObject * self, void *) { PyObject * m = getSelf(self)->matrix_; Py_INCREF(m); return m; }
    static int setMatrix(PyObject * self, PyObject * v, void *) {
        Py_INCREF(v); Py_DECREF(getSelf(self)->matrix_); getSelf(self)->matrix_ = v; return 0;
    }
    static PyObject * getSources(PyObject * self, void *) { PyObject * s = getSelf(self)->sources_; Py_INCREF(s); return s; }
private:
    SuperModel superModel_;
    PyObject * matrix_;
    PyObject * sources_;
    PyObject * dict_ = NULL;
};
inline void incRef(PyModel * o) { Py_INCREF(o); }
inline void decRef(PyModel * o) { Py_DECREF(o); }

PyObject * hostMathVector3(const Vector3 & v);

namespace Script {
inline PyObject * argCountError(const char * fn, int optas, int allas, ...) {
    std::string types;
    va_list values;
    va_start(values, allas);
    for (int i = 0; i < allas; ++i) {
        if (i > 0) types += ", ";
        const char * type = va_arg(values, const char *);
        size_t length = std::strlen(type);
        if (length > 13 && !std::strncmp(type, "SmartPointer<", 13)) types.append(type + 13, length - 14);
        else if (length > 3 && !std::strncmp(type + length - 3, "Ptr", 3)) types.append(type, length - 3);
        else { while (length > 0 && type[length - 1] == '*') --length; types.append(type, length); }
    }
    va_end(values);
    if (allas == 0) PyErr_Format(PyExc_TypeError, "%s() takes no arguments.", fn);
    else if (optas == allas) {
        const char * plural = allas != 1 ? "s" : "";
        PyErr_Format(PyExc_TypeError, "%s() expects %d argument%s of type%s %s", fn, allas, plural, plural,
                     types.c_str());
    } else PyErr_Format(PyExc_TypeError, "%s() expects between %d and %d arguments of types %s", fn, optas,
                        allas, types.c_str());
    return NULL;
}
inline int setData(PyObject * o, uint & value, const char * name) {
    if (!PyInt_Check(o) && !PyLong_Check(o)) {
        PyErr_Format(PyExc_TypeError, "%s must be set to an int.", name); return -1;
    }
    value = uint(PyLong_Check(o) ? PyLong_AsUnsignedLongMask(o) : (unsigned long)PyInt_AsLong(o));
    return 0;
}
inline int setData(PyObject * o, bool & value, const char * name) {
    const int truth = PyObject_IsTrue(o);
    if (truth < 0) { PyErr_Format(PyExc_TypeError, "%s must be set to a bool.", name); return -1; }
    value = truth != 0;
    return 0;
}
// Math.Vector3 or a 3-sequence of numbers, like script_math.cpp.
inline int setData(PyObject * o, Vector3 & value, const char * name) {
    float c[3];
    PyObject * sequence = PySequence_Check(o) ? PySequence_Fast(o, "") : NULL;
    if (!sequence) {
        PyErr_Clear();
        PyObject * xyz[3] = {PyObject_GetAttrString(o, "x"), PyObject_GetAttrString(o, "y"),
                             PyObject_GetAttrString(o, "z")};
        bool ok = xyz[0] && xyz[1] && xyz[2];
        for (int i = 0; i < 3; ++i) {
            if (ok) { c[i] = float(PyFloat_AsDouble(xyz[i])); ok = !PyErr_Occurred(); }
            Py_XDECREF(xyz[i]);
        }
        if (!ok) { PyErr_Clear(); PyErr_Format(PyExc_TypeError, "%s must be set to a Vector3", name); return -1; }
    } else {
        bool ok = PySequence_Fast_GET_SIZE(sequence) == 3;
        for (int i = 0; ok && i < 3; ++i) {
            c[i] = float(PyFloat_AsDouble(PySequence_Fast_GET_ITEM(sequence, i)));
            ok = !PyErr_Occurred();
        }
        Py_DECREF(sequence);
        if (!ok) { PyErr_Clear(); PyErr_Format(PyExc_TypeError, "%s must be set to a Vector3", name); return -1; }
    }
    value = Vector3(c[0], c[1], c[2]);
    return 0;
}
inline int setData(PyObject * o, SmartPointer<PyModel> & value, const char * name) {
    if (o == Py_None) { value = SmartPointer<PyModel>(); return 0; }
    if (!PyModel::Check(o)) { PyErr_Format(PyExc_TypeError, "%s must be set to a PyModel or None", name); return -1; }
    value = SmartPointer<PyModel>(static_cast<PyModel *>(static_cast<PyObjectPlus *>(o)));
    return 0;
}
inline int setData(PyObject * o, PyObjectPtr & value, const char *) {
    value = PyObjectPtr(o == Py_None ? NULL : o);
    return 0;
}
inline PyObject * getData(bool value) { return PyBool_FromLong(value); }
inline PyObject * getData(float value) { return PyFloat_FromDouble(value); }
inline PyObject * getData(int value) { return PyInt_FromLong(value); }
inline PyObject * getData(const Vector3 & value) { return hostMathVector3(value); }
}
}
#ifndef Py_RETURN_NONE
#define Py_RETURN_NONE return Py_INCREF(Py_None), Py_None
#endif
using namespace BW;
