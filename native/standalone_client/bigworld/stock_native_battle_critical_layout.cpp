// The interior hit layout of one installed vehicle configuration: decoded Console meshes registered to the exact
// PC components, else the retained archetype primitives, validated against each component's hit-tester bounds.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_critical.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace native_battle {
namespace critical {
namespace {
namespace step = wot::offline_bot_step;
using step::InternalLayout;
using step::InternalMesh;
using step::InternalNode;
using step::InternalPrimitive;
using step::InternalShape;
using step::InternalTarget;
using Vec = std::array<double, 3>;
struct Bounds { Vec minimum{}, maximum{}; };

const char* const parents[] = {"hull", "turret", "gun", "chassis"};
const char* const module_targets[] = {"engine", "ammoBay", "gun", "turretRotator", "leftTrack", "rightTrack",
                                      "surveyingDevice", "radio", "fuelTank"};
const std::map<std::string, std::string> module_extras = {
    {"engine", "engineHealth"}, {"ammoBay", "ammoBayHealth"}, {"gun", "gunHealth"}, {"turretRotator", "turretRotatorHealth"},
    {"leftTrack", "leftTrackHealth"}, {"rightTrack", "rightTrackHealth"}, {"surveyingDevice", "surveyingDeviceHealth"},
    {"radio", "radioHealth"}, {"fuelTank", "fuelTankHealth"}};
constexpr int layout_key = 16;
constexpr std::size_t cache_limit = 128;

double asNumber(PyObject* v, double fallback = 0.) {
    if (!v || v == Py_None) return fallback;
    PyObject* f = PyNumber_Float(v);
    if (!f) { PyErr_Clear(); return fallback; }
    const double out = PyFloat_AS_DOUBLE(f);
    Py_DECREF(f);
    return out;
}
std::string asText(PyObject* v) {
    if (!v) return {};
    PyObject* s = PyObject_Str(v);
    if (!s) { PyErr_Clear(); return {}; }
    p::Ref held(s);
    return std::string(PyString_AsString(s), std::size_t(PyString_Size(s)));
}
// The pinned _value(value, key): dict.get or getattr with a None default.
p::Ref value(PyObject* o, const char* key) {
    if (!o || o == Py_None) return p::retain(Py_None);
    return p::value(o, key);
}
std::optional<Vec> vector(PyObject* v) {
    if (!v || v == Py_None) return {};
    auto x = p::value(v, "x"), y = p::value(v, "y"), z = p::value(v, "z");
    if (x.get() != Py_None && y.get() != Py_None && z.get() != Py_None)
        return Vec{asNumber(x.get()), asNumber(y.get()), asNumber(z.get())};
    if (!PySequence_Check(v) || PySequence_Size(v) < 3) { PyErr_Clear(); return {}; }
    Vec out{};
    for (int i = 0; i < 3; ++i) {
        PyObject* item = PySequence_GetItem(v, i);
        if (!item) { PyErr_Clear(); return {}; }
        PyObject* f = PyNumber_Float(item);
        Py_DECREF(item);
        if (!f) { PyErr_Clear(); return {}; }
        out[i] = PyFloat_AS_DOUBLE(f);
        Py_DECREF(f);
    }
    return out;
}
std::optional<Bounds> componentBounds(PyObject* descriptor, const char* parent) {
    PyObject* component = PyObject_GetAttrString(descriptor, parent);
    if (!component) { PyErr_Clear(); return {}; }
    p::Ref held(component);
    auto tester = value(component, "hitTester");
    PyObject* bbox = tester.get() != Py_None ? PyObject_GetAttrString(tester.get(), "bbox") : nullptr;
    if (!bbox) { PyErr_Clear(); return {}; }
    p::Ref box(bbox);
    if (bbox == Py_None || !PySequence_Check(bbox) || PySequence_Size(bbox) < 2) { PyErr_Clear(); return {}; }
    PyObject* low = PySequence_GetItem(bbox, 0);
    PyObject* high = PySequence_GetItem(bbox, 1);
    auto minimum = vector(low), maximum = vector(high);
    Py_XDECREF(low); Py_XDECREF(high);
    PyErr_Clear();
    if (!minimum || !maximum) return {};
    for (int a = 0; a < 3; ++a) if ((*maximum)[a] - (*minimum)[a] <= .0001) return {};
    return Bounds{*minimum, *maximum};
}
std::string componentIdentity(PyObject* component) {
    return asText(value(component, "name").get()) + "|" + asText(value(component, "id").get()) + "|" + asText(value(component, "compactDescr").get());
}
std::string vehicleName(PyObject* descriptor) {
    auto type = value(descriptor, "type");
    auto name = value(type.get(), "name");
    return name.get() == Py_None ? std::string() : asText(name.get());
}
std::string fingerprint(PyObject* descriptor) {
    std::string out = vehicleName(descriptor);
    for (const char* name : {"chassis", "hull", "turret", "gun", "engine", "fuelTank", "radio"}) {
        PyObject* component = PyObject_GetAttrString(descriptor, name);
        if (!component) { PyErr_Clear(); out += std::string(";") + name + "=missing"; continue; }
        p::Ref held(component);
        out += std::string(";") + name + "=" + componentIdentity(component);
    }
    return out;
}
std::string normalName(const std::string& text) {
    std::string out;
    for (unsigned char c : text) if (std::isalnum(c)) out.push_back(char(std::tolower(c)));
    return out;
}
std::optional<std::pair<std::string, std::string>> profileKey(const std::string& vehicle) {
    const auto colon = vehicle.find(':');
    if (colon == std::string::npos) return {};
    static const std::map<std::string, std::string> aliases = {{"russian", "ussr"}, {"german", "germany"}, {"american", "usa"},
                                                               {"french", "france"}, {"british", "uk"}, {"chinese", "china"}};
    auto nation = normalName(vehicle.substr(0, colon));
    const auto alias = aliases.find(nation);
    if (alias != aliases.end()) nation = alias->second;
    return std::make_pair(nation, normalName(vehicle.substr(colon + 1)));
}

// The generated interior tables are resources; their modules are imported once.
PyObject* dataModule(const char* name) {
    static std::map<std::string, PyObject*> modules;
    auto& slot = modules[name];
    if (!slot) {
        slot = PyImport_ImportModule((std::string("gui.mods.offline_lan_0922.") + name).c_str());
        if (!slot) { PyErr_Clear(); slot = Py_None; Py_INCREF(slot); }
    }
    return slot == Py_None ? nullptr : slot;
}
p::Ref keyTuple(const std::pair<std::string, std::string>& key) {
    return p::Ref(Py_BuildValue("(s#s#)", key.first.data(), Py_ssize_t(key.first.size()), key.second.data(), Py_ssize_t(key.second.size())));
}
PyObject* lookup(PyObject* mapping, PyObject* key) {
    if (!mapping || !PyDict_Check(mapping)) return nullptr;
    return PyDict_GetItem(mapping, key);
}
std::vector<std::vector<std::string>> roleRows(PyObject* rows) {
    std::vector<std::vector<std::string>> out;
    if (!rows || !PySequence_Check(rows)) return out;
    for (Py_ssize_t i = 0; i < PySequence_Size(rows); ++i) {
        auto row = p::item(rows, i);
        std::vector<std::string> roles;
        if (PySequence_Check(row.get()))
            for (Py_ssize_t j = 0; j < PySequence_Size(row.get()); ++j) roles.push_back(asText(p::item(row.get(), j).get()));
        out.push_back(std::move(roles));
    }
    return out;
}

struct Crew { std::string entity; std::vector<std::string> roles; };
struct Candidate {
    std::string entity, kind, parent, zone_id, shape_hint;
    Vec center_fractions{}, half_fractions{};
    PyObject* mesh = nullptr;     // decoded geometry dict, borrowed from the resident table
};
struct Fit { Vec center{}, half{}; std::vector<InternalPrimitive> primitives; };

std::string zoneShapeHint(const std::optional<std::pair<std::string, std::string>>& key, const std::string& entity, const std::string& kind,
                          const std::string& zone_id) {
    const std::string fallback = kind == "crew" ? "crew_capsule_head" : "box";
    PyObject* profiles = dataModule("internal_layout_profiles");
    if (!profiles) return fallback;
    if (key) {
        p::Ref triple(Py_BuildValue("(s#s#s#)", key->first.data(), Py_ssize_t(key->first.size()), key->second.data(),
                                    Py_ssize_t(key->second.size()), zone_id.data(), Py_ssize_t(zone_id.size())));
        auto overrides = p::value(profiles, "PROFILE_ZONE_SHAPE_OVERRIDES");
        if (PyObject* hint = lookup(overrides.get(), triple.get())) return asText(hint);
    }
    if (kind == "crew") return "crew_capsule_head";
    auto zones = p::value(profiles, "_MODULE_ZONE_SHAPES");
    if (PyObject* hint = PyDict_Check(zones.get()) ? PyDict_GetItemString(zones.get(), zone_id.c_str()) : nullptr) return asText(hint);
    auto defaults = p::value(profiles, "_ENTITY_DEFAULT_SHAPES");
    if (PyObject* hint = PyDict_Check(defaults.get()) ? PyDict_GetItemString(defaults.get(), entity.c_str()) : nullptr) return asText(hint);
    return "box";
}

// ---- decoded meshes (internal_mesh.decode / prepare) -------------------------------------------------------

std::pair<std::vector<Vec>, std::vector<std::array<std::uint32_t, 3>>> decodeMesh(PyObject* payload) {
    static PyObject* base64 = PyImport_ImportModule("base64");
    static PyObject* zlib = PyImport_ImportModule("zlib");
    if (!base64 || !zlib) throw p::Error("mesh codecs are unavailable");
    p::Ref raw(PyObject_CallMethod(base64, const_cast<char*>("b64decode"), const_cast<char*>("O"), payload));
    p::Ref blob(PyObject_CallMethod(zlib, const_cast<char*>("decompress"), const_cast<char*>("O"), raw.get()));
    const char* data = PyString_AsString(blob.get());
    const std::size_t size = std::size_t(PyString_Size(blob.get()));
    std::uint32_t nv = 0, nt = 0;
    if (size < 12 || std::memcmp(data, "IM01", 4) != 0) throw p::Error("invalid indexed interior mesh payload");
    std::memcpy(&nv, data + 4, 4);
    std::memcpy(&nt, data + 8, 4);
    if (size != 12 + std::size_t(nv) * 24 + std::size_t(nt) * 12) throw p::Error("invalid indexed interior mesh payload");
    std::vector<Vec> vertices(nv);
    std::vector<std::array<std::uint32_t, 3>> triangles(nt);
    std::memcpy(vertices.data(), data + 12, std::size_t(nv) * 24);
    std::memcpy(triangles.data(), data + 12 + std::size_t(nv) * 24, std::size_t(nt) * 12);
    return {std::move(vertices), std::move(triangles)};
}
Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec cross(const Vec& a, const Vec& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
double dot(const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// Builds the BVH preorder into mesh.nodes: (low, high, left, right, faces) with eight faces per leaf.
int tree(InternalMesh& mesh, std::vector<int> indices) {
    Vec low{}, high{};
    bool first = true;
    for (int j : indices)
        for (int i : mesh.triangles[std::size_t(j)])
            for (int a = 0; a < 3; ++a) {
                const double v = mesh.vertices[std::size_t(i)][a];
                if (first || v < low[a]) low[a] = v;
                if (first || v > high[a]) high[a] = v;
                if (a == 2) first = false;
            }
    const int index = int(mesh.nodes.size());
    mesh.nodes.emplace_back();
    mesh.nodes[std::size_t(index)].minimum = low;
    mesh.nodes[std::size_t(index)].maximum = high;
    if (indices.size() <= 8) { mesh.nodes[std::size_t(index)].triangles = std::move(indices); return index; }
    int axis = 0;
    for (int a = 1; a < 3; ++a) if (high[a] - low[a] > high[axis] - low[axis]) axis = a;
    auto key = [&](int j) {
        const auto& f = mesh.triangles[std::size_t(j)];
        return mesh.vertices[std::size_t(f[0])][axis] + mesh.vertices[std::size_t(f[1])][axis] + mesh.vertices[std::size_t(f[2])][axis];
    };
    std::stable_sort(indices.begin(), indices.end(), [&](int x, int y) { return key(x) < key(y); });
    const auto mid = indices.size() / 2;
    std::vector<int> left(indices.begin(), indices.begin() + std::ptrdiff_t(mid)), right(indices.begin() + std::ptrdiff_t(mid), indices.end());
    const int l = tree(mesh, std::move(left));
    const int r = tree(mesh, std::move(right));
    mesh.nodes[std::size_t(index)].left = l;
    mesh.nodes[std::size_t(index)].right = r;
    return index;
}
// Orients a closed manifold piece for inside tests without changing its faces; empty when open.
std::vector<int> orientations(const std::vector<std::array<int, 3>>& faces) {
    std::map<std::pair<int, int>, std::vector<std::pair<int, bool>>> edges;
    for (std::size_t j = 0; j < faces.size(); ++j)
        for (int k = 0; k < 3; ++k) {
            const int a = faces[j][std::size_t(k)], b = faces[j][std::size_t((k + 1) % 3)];
            edges[{std::min(a, b), std::max(a, b)}].push_back({int(j), a < b});
        }
    for (const auto& edge : edges) if (edge.second.size() != 2) return {};
    std::vector<std::vector<std::pair<int, int>>> links(faces.size());
    for (const auto& edge : edges) {
        const auto& x = edge.second[0];
        const auto& y = edge.second[1];
        const int relation = x.second == y.second ? -1 : 1;
        links[std::size_t(x.first)].push_back({y.first, relation});
        links[std::size_t(y.first)].push_back({x.first, relation});
    }
    std::vector<int> signs(faces.size(), 0);
    signs[0] = 1;
    std::vector<int> stack{0};
    while (!stack.empty()) {
        const int j = stack.back();
        stack.pop_back();
        for (const auto& link : links[std::size_t(j)]) {
            const int expected = signs[std::size_t(j)] * link.second;
            if (signs[std::size_t(link.first)]) { if (signs[std::size_t(link.first)] != expected) return {}; }
            else { signs[std::size_t(link.first)] = expected; stack.push_back(link.first); }
        }
    }
    return signs;
}
// One accelerated mesh per edge-connected piece, never a hull (internal_mesh.prepare).
std::vector<InternalPrimitive> prepareMesh(const std::vector<Vec>& raw_vertices, const std::vector<std::array<std::uint32_t, 3>>& triangles,
                                           const std::string& primitive_id) {
    if (raw_vertices.empty() || triangles.empty()) throw p::Error("empty interior mesh");
    for (const auto& v : raw_vertices)
        for (double c : v) if (!(-60. <= c && c <= 60.)) throw p::Error("invalid interior mesh coordinate");
    // Exactly identical seam vertices share topology; nearby vertices are deliberately not welded.
    std::vector<Vec> unique;
    std::vector<int> remap;
    std::map<Vec, int> canonical;
    for (const auto& v : raw_vertices) {
        auto at = canonical.find(v);
        if (at == canonical.end()) { at = canonical.emplace(v, int(unique.size())).first; unique.push_back(v); }
        remap.push_back(at->second);
    }
    std::vector<std::array<int, 3>> faces;
    for (const auto& f : triangles) {
        for (auto i : f) if (i >= raw_vertices.size()) throw p::Error("invalid interior triangle index");
        const std::array<int, 3> face{remap[f[0]], remap[f[1]], remap[f[2]]};
        const auto normal = cross(sub(unique[std::size_t(face[1])], unique[std::size_t(face[0])]),
                                  sub(unique[std::size_t(face[2])], unique[std::size_t(face[0])]));
        if (dot(normal, normal) == 0.) continue;
        faces.push_back(face);
    }
    if (faces.empty()) throw p::Error("interior mesh has no nondegenerate triangle");
    std::map<std::pair<int, int>, std::vector<int>> edges;
    for (std::size_t j = 0; j < faces.size(); ++j)
        for (int k = 0; k < 3; ++k) {
            const int a = faces[j][std::size_t(k)], b = faces[j][std::size_t((k + 1) % 3)];
            edges[{std::min(a, b), std::max(a, b)}].push_back(int(j));
        }
    std::vector<std::set<int>> adjacency(faces.size());
    for (const auto& linked : edges) for (int j : linked.second) adjacency[std::size_t(j)].insert(linked.second.begin(), linked.second.end());
    std::set<int> remaining;
    for (std::size_t j = 0; j < faces.size(); ++j) remaining.insert(int(j));
    std::vector<InternalPrimitive> result;
    while (!remaining.empty()) {
        std::set<int> component;
        std::vector<int> stack{*remaining.begin()};
        while (!stack.empty()) {
            const int j = stack.back();
            stack.pop_back();
            if (!component.insert(j).second) continue;
            for (int n : adjacency[std::size_t(j)]) if (!component.count(n)) stack.push_back(n);
        }
        for (int j : component) remaining.erase(j);
        std::set<int> vertex_ids;
        for (int j : component) for (int i : faces[std::size_t(j)]) vertex_ids.insert(i);
        std::map<int, int> mapping;
        InternalPrimitive primitive;
        primitive.shape = InternalShape::mesh;
        auto& mesh = primitive.mesh;
        for (int i : vertex_ids) { mapping[i] = int(mesh.vertices.size()); mesh.vertices.push_back(unique[std::size_t(i)]); }
        std::vector<std::array<int, 3>> piece;
        for (int j : component) {
            const auto& f = faces[std::size_t(j)];
            piece.push_back({mapping[f[0]], mapping[f[1]], mapping[f[2]]});
        }
        mesh.orientations = orientations(piece);
        mesh.closed = !mesh.orientations.empty();
        mesh.triangles = piece;
        std::vector<int> indices(piece.size());
        for (std::size_t j = 0; j < piece.size(); ++j) indices[j] = int(j);
        tree(mesh, std::move(indices));
        primitive.minimum = mesh.nodes[0].minimum;
        primitive.maximum = mesh.nodes[0].maximum;
        for (int a = 0; a < 3; ++a) {
            primitive.center[a] = (primitive.minimum[a] + primitive.maximum[a]) * .5;
            primitive.half_extents[a] = (primitive.maximum[a] - primitive.minimum[a]) * .5;
        }
        primitive.radii = primitive.half_extents;
        primitive.has_center = primitive.has_half_extents = true;
        primitive.id = primitive_id + ":" + std::to_string(result.size());
        result.push_back(std::move(primitive));
    }
    return result;
}

// Requires the exact PC model variant and its unscaled component frame.
bool meshMatchesComponent(PyObject* descriptor, const std::string& parent, const Bounds& bounds, PyObject* geometry) {
    auto component = value(descriptor, parent.c_str());
    auto models = value(component.get(), "models");
    auto path = value(models.get(), "undamaged");
    std::string text = path.get() == Py_None ? std::string() : asText(path.get());
    if (text.empty()) text.clear();
    std::replace(text.begin(), text.end(), '\\', '/');
    text = text.substr(text.rfind('/') == std::string::npos ? 0 : text.rfind('/') + 1);
    text = text.substr(0, text.find('.'));
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (asText(p::value(geometry, "part").get()) != text) return false;
    auto reference = p::value(geometry, "reference_bounds");
    for (int side = 0; side < 2; ++side) {
        auto row = p::item(reference.get(), side);
        for (int axis = 0; axis < 3; ++axis) {
            const double expected = side ? bounds.maximum[std::size_t(axis)] : bounds.minimum[std::size_t(axis)];
            if (std::abs(expected - asNumber(p::item(row.get(), axis).get())) > .002) return false;
        }
    }
    return true;
}
Fit fitMesh(PyObject* descriptor, const Candidate& c, const Bounds& bounds) {
    if (!meshMatchesComponent(descriptor, c.parent, bounds, c.mesh)) throw p::Error("installed component mesh mismatch");
    auto decoded = decodeMesh(p::value(c.mesh, "payload").get());
    Fit fit;
    fit.primitives = prepareMesh(decoded.first, decoded.second, c.zone_id);
    Vec minimum = fit.primitives[0].minimum, maximum = fit.primitives[0].maximum;
    for (const auto& primitive : fit.primitives)
        for (int a = 0; a < 3; ++a) { minimum[a] = std::min(minimum[a], primitive.minimum[a]); maximum[a] = std::max(maximum[a], primitive.maximum[a]); }
    for (int a = 0; a < 3; ++a) { fit.center[a] = (minimum[a] + maximum[a]) * .5; fit.half[a] = (maximum[a] - minimum[a]) * .5; }
    return fit;
}

// ---- archetype primitives (internal_geometry.fit_target) ---------------------------------------------------

constexpr double pi = 3.14159265358979323846;
double radians(double degrees) { return degrees * pi / 180.; }
std::pair<Vec, Vec> rotatedBounds(const Vec& center, Vec half, double yaw_degrees) {
    for (auto& h : half) h = std::max(0., h);
    const double yaw = radians(yaw_degrees), c = std::abs(std::cos(yaw)), s = std::abs(std::sin(yaw));
    const Vec extents{half[0] * c + half[2] * s, half[1], half[0] * s + half[2] * c};
    return {Vec{center[0] - extents[0], center[1] - extents[1], center[2] - extents[2]},
            Vec{center[0] + extents[0], center[1] + extents[1], center[2] + extents[2]}};
}
InternalPrimitive box(const Vec& center, const Vec& half, const std::string& id, double yaw = 0.) {
    InternalPrimitive out;
    out.shape = InternalShape::box;
    out.id = id;
    out.center = center;
    out.half_extents = out.radii = half;
    out.yaw_degrees = yaw;
    std::tie(out.minimum, out.maximum) = rotatedBounds(center, half, yaw);
    out.has_center = out.has_half_extents = true;
    return out;
}
InternalPrimitive sphere(const Vec& center, double radius, const std::string& id) {
    radius = std::max(.001, radius);
    InternalPrimitive out;
    out.shape = InternalShape::sphere;
    out.id = id;
    out.center = center;
    out.radius = radius;
    out.half_extents = out.radii = Vec{radius, radius, radius};
    for (int a = 0; a < 3; ++a) { out.minimum[a] = center[a] - radius; out.maximum[a] = center[a] + radius; }
    out.has_center = out.has_half_extents = true;
    return out;
}
InternalPrimitive ellipsoid(const Vec& center, Vec radii, const std::string& id, double yaw = 0.) {
    for (auto& r : radii) r = std::max(.001, r);
    InternalPrimitive out;
    out.shape = InternalShape::ellipsoid;
    out.id = id;
    out.center = center;
    out.radii = out.half_extents = radii;
    out.yaw_degrees = yaw;
    std::tie(out.minimum, out.maximum) = rotatedBounds(center, radii, yaw);
    out.has_center = out.has_half_extents = true;
    return out;
}
InternalPrimitive capsule(const Vec& center, double radius, double half_length, int axis, const std::string& id, double yaw = 0.) {
    radius = std::max(.001, radius);
    half_length = std::max(0., half_length);
    Vec half{radius, radius, radius};
    half[std::size_t(axis)] = half_length + radius;
    InternalPrimitive out;
    out.shape = InternalShape::capsule;
    out.id = id;
    out.center = center;
    out.radius = radius;
    out.half_length = half_length;
    out.axis = axis;
    out.half_extents = out.radii = half;
    out.yaw_degrees = yaw;
    std::tie(out.minimum, out.maximum) = rotatedBounds(center, half, yaw);
    out.has_center = out.has_half_extents = true;
    return out;
}
// axis < 0 selects the longest half extent (first on ties).
InternalPrimitive capsuleFromHalf(const Vec& center, Vec half, const std::string& id, int axis = -1, double yaw = 0., double factor = .88) {
    for (auto& h : half) h = std::max(.015, h);
    if (axis < 0) { axis = 0; for (int a = 1; a < 3; ++a) if (half[std::size_t(a)] > half[std::size_t(axis)]) axis = a; }
    double perpendicular = 1.e300;
    for (int a = 0; a < 3; ++a) if (a != axis) perpendicular = std::min(perpendicular, half[std::size_t(a)]);
    const double radius = std::max(.015, perpendicular * factor);
    const double axis_half = std::max(radius, half[std::size_t(axis)] * .96);
    return capsule(center, radius, std::max(0., axis_half - radius), axis, id, yaw);
}
std::vector<InternalPrimitive> crewPrimitives(const Vec& c, const Vec& h, const std::string& zone) {
    const Vec body_center{c[0], c[1] - h[1] * .10, c[2]}, body_half{h[0] * .82, h[1] * .72, h[2] * .82};
    const double head = std::max(.055, std::min({h[0] * .62, h[2] * .62, h[1] * .20}));
    return {capsuleFromHalf(body_center, body_half, zone + ":body", 1, 0., .92), sphere(Vec{c[0], c[1] + h[1] * .68, c[2]}, head, zone + ":head")};
}
std::vector<InternalPrimitive> ringPrimitives(const Vec& c, const Vec& h, const std::string& zone) {
    const double tube = std::max(.025, std::min({h[1] * .92, h[0] * .14, h[2] * .14}));
    const double rx = std::max(tube * 1.25, h[0] - tube), rz = std::max(tube * 1.25, h[2] - tube);
    std::vector<InternalPrimitive> out;
    for (int index = 0; index < 12; ++index) {
        const double a0 = pi * 2. * index / 12., a1 = pi * 2. * (index + 1) / 12.;
        const Vec first{c[0] + std::cos(a0) * rx, c[1], c[2] + std::sin(a0) * rz}, second{c[0] + std::cos(a1) * rx, c[1], c[2] + std::sin(a1) * rz};
        const Vec mid{(first[0] + second[0]) * .5, (first[1] + second[1]) * .5, (first[2] + second[2]) * .5};
        const double dx = second[0] - first[0], dz = second[2] - first[2];
        char id[16];
        std::snprintf(id, sizeof id, ":ring%02d", index);
        out.push_back(capsule(mid, tube, std::sqrt(dx * dx + dz * dz) * .5, 0, zone + id, std::atan2(dz, dx) * 180. / pi));
    }
    return out;
}
std::vector<InternalPrimitive> traversePrimitives(const Vec& c, const Vec& h, const std::string& zone) {
    const Vec actuator{std::max(.03, h[0] * .20), std::max(.03, h[1] * .58), std::max(.03, h[2] * .40)};
    const double offset = std::max(0., h[0] - actuator[0]);
    return {capsuleFromHalf(c, Vec{std::max(.04, h[0] * .54), h[1], std::max(.04, h[2] * .74)}, zone + ":gun_mount", 2, 0., .86),
            capsuleFromHalf(Vec{c[0] - offset, c[1], c[2]}, actuator, zone + ":left_actuator", 2, 0., .82),
            capsuleFromHalf(Vec{c[0] + offset, c[1], c[2]}, actuator, zone + ":right_actuator", 2, 0., .82)};
}
std::vector<InternalPrimitive> ammoPrimitives(const Vec& c, const Vec& h, const std::string& zone) {
    std::vector<InternalPrimitive> out;
    int index = 0;
    for (double offset : {-.62, 0., .62}) {
        if (h[0] >= h[2]) out.push_back(box(Vec{c[0] + h[0] * offset, c[1], c[2]}, Vec{h[0] / 3.2, h[1], h[2] * .82}, zone + ":row" + std::to_string(index)));
        else out.push_back(box(Vec{c[0], c[1], c[2] + h[2] * offset}, Vec{h[0] * .82, h[1], h[2] / 3.2}, zone + ":row" + std::to_string(index)));
        ++index;
    }
    return out;
}
std::vector<InternalPrimitive> enginePrimitives(const Vec& c, const Vec& h, const std::string& zone) {
    return {box(Vec{c[0], c[1] - h[1] * .10, c[2]}, Vec{h[0] * .82, h[1] * .78, h[2] * .72}, zone + ":block"),
            box(Vec{c[0], c[1] + h[1] * .58, c[2]}, Vec{h[0] * .62, h[1] * .30, h[2] * .62}, zone + ":upper")};
}
std::vector<InternalPrimitive> buildPrimitives(const std::string& entity, const std::string& kind, const Vec& c, const Vec& h,
                                               const std::string& zone, bool fixed, const std::string& hint) {
    if (kind == "crew" || hint == "crew_capsule_head") return crewPrimitives(c, h, zone);
    if (hint == "elliptic_ring_capsules") return fixed ? traversePrimitives(c, h, zone) : ringPrimitives(c, h, zone);
    if (hint == "gun_traverse_capsules") return traversePrimitives(c, h, zone);
    if (hint == "ammo_rack_boxes" || entity == "ammoBay") return ammoPrimitives(c, h, zone);
    if (hint == "engine_compound_box" || entity == "engine") return enginePrimitives(c, h, zone);
    if (hint == "capsule_longest") return {capsuleFromHalf(c, h, zone + ":body")};
    if (hint == "capsule_x") return {capsuleFromHalf(c, h, zone + ":body", 0)};
    if (hint == "capsule_y") return {capsuleFromHalf(c, h, zone + ":body", 1)};
    if (hint == "capsule_z" || entity == "gun") return {capsuleFromHalf(c, h, zone + ":body", 2)};
    if (hint == "sphere") return {sphere(c, std::min({h[0], h[1], h[2]}), zone + ":body")};
    if (hint == "ellipsoid") return {ellipsoid(c, Vec{std::max(.015, h[0] * .94), std::max(.015, h[1] * .94), std::max(.015, h[2] * .94)}, zone + ":body")};
    return {box(c, h, zone + ":body")};
}
bool fixedFightingCompartment(PyObject* descriptor) {
    auto type = value(descriptor, "type");
    auto tags = value(type.get(), "tags");
    bool spg = false;
    if (tags.get() != Py_None && PySequence_Check(tags.get())) {
        p::Ref listed(PySequence_List(tags.get()));
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(listed.get()); ++i) {
            const auto tag = asText(PyList_GET_ITEM(listed.get(), i));
            spg = spg || tag == "AT-SPG" || tag == "SPG";
        }
    }
    PyErr_Clear();
    if (!spg) return false;
    auto turret = value(descriptor, "turret");
    auto limits = value(turret.get(), "yawLimits");
    if (limits.get() == Py_None) return false;
    if (!PySequence_Check(limits.get()) || PySequence_Size(limits.get()) < 2) { PyErr_Clear(); return true; }
    const double left = asNumber(p::item(limits.get(), 0).get()), right = asNumber(p::item(limits.get(), 1).get());
    return std::max(0., right - left) < radians(270.);
}
std::optional<Vec> physicalHalfCap(PyObject* descriptor, const std::string& entity, const std::string& kind, std::string zone) {
    std::transform(zone.begin(), zone.end(), zone.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    auto in = [&](const char* word) { return zone.find(word) != std::string::npos; };
    if (kind == "crew") return Vec{.26, .55, .32};
    if (entity == "surveyingDevice") {
        if (in("sight") || in("driver") || in("viewport") || in("visor")) return Vec{.12, .08, .14};
        if (in("roof") || in("cupola")) return Vec{.14, .10, .16};
        return Vec{.16, .11, .18};
    }
    if (entity == "radio") return Vec{.30, .23, .34};
    if (entity == "turretRotator") return fixedFightingCompartment(descriptor) ? Vec{.50, .24, .52} : Vec{.78, .24, .78};
    if (entity == "engine") return Vec{.95, .72, 1.10};
    if (entity == "ammoBay") return Vec{.78, .52, .92};
    if (entity == "fuelTank") return Vec{.50, .50, .86};
    if (entity == "gun") return Vec{.25, .25, 1.25};
    return {};
}
Fit fitTarget(PyObject* descriptor, const Candidate& c, const Bounds& bounds) {
    Vec span{}, anchor{}, desired{};
    for (int a = 0; a < 3; ++a) {
        span[a] = bounds.maximum[a] - bounds.minimum[a];
        anchor[a] = bounds.minimum[a] + span[a] * std::min(.98, std::max(.02, c.center_fractions[a]));
    }
    const auto cap = physicalHalfCap(descriptor, c.entity, c.kind, c.zone_id);
    for (int a = 0; a < 3; ++a) {
        const double profile = std::max(.02, std::min(span[a] * .40, span[a] * c.half_fractions[a]));
        desired[a] = cap ? std::max(.02, std::min({profile, (*cap)[a], span[a] * .40})) : profile;
    }
    Fit fit;
    for (int a = 0; a < 3; ++a) {
        fit.half[a] = std::min(desired[a], span[a] * .40);
        fit.center[a] = std::min(bounds.maximum[a] - fit.half[a], std::max(bounds.minimum[a] + fit.half[a], anchor[a]));
    }
    const bool fixed = fixedFightingCompartment(descriptor);
    std::string hint = c.shape_hint.empty() ? "box" : c.shape_hint;
    if (c.entity == "turretRotator" && fixed && hint == "elliptic_ring_capsules") hint = "gun_traverse_capsules";
    fit.primitives = buildPrimitives(c.entity, c.kind, fit.center, fit.half, c.zone_id, fixed, hint);
    return fit;
}

// The six axis rays from just outside the parent bounds to the target (validate_layout).
bool rayResolves(const Bounds& parent, const Vec& target_center, const std::vector<InternalPrimitive>& primitives) {
    Vec center = target_center;
    for (const auto& primitive : primitives)
        if (primitive.shape == InternalShape::mesh) {
            const auto& face = primitive.mesh.triangles.front();
            for (int a = 0; a < 3; ++a)
                center[a] = (primitive.mesh.vertices[std::size_t(face[0])][a] + primitive.mesh.vertices[std::size_t(face[1])][a] +
                             primitive.mesh.vertices[std::size_t(face[2])][a]) / 3.;
            break;
        }
    for (int axis = 0; axis < 3; ++axis) {
        const double margin = std::max(.001, (parent.maximum[axis] - parent.minimum[axis]) * .001);
        for (double origin : {parent.minimum[axis] - margin, parent.maximum[axis] + margin}) {
            Vec start = center;
            start[axis] = origin;
            for (const auto& primitive : primitives)
                if (!step::internal_intervals(primitive, start, center).empty()) return true;
        }
    }
    return false;
}

struct Built { bool valid = false; InternalLayout layout; std::vector<std::string> errors, missing; };

Built build(PyObject* descriptor, const std::string& vehicle, const std::map<std::string, Bounds>& bounds) {
    Built out;
    auto& errors = out.errors;
    // Crew entities: commander, driver, gunner1, loader1, ... in crewRoles order.
    std::vector<Crew> crew;
    {
        auto type = value(descriptor, "type");
        PyObject* roles = type.get() != Py_None ? PyObject_GetAttrString(type.get(), "crewRoles") : nullptr;
        if (!roles) { PyErr_Clear(); errors.push_back("missing_vehicle_type_crew_roles"); }
        else {
            p::Ref held(roles);
            std::map<std::string, int> next = {{"gunner", 1}, {"loader", 1}, {"radioman", 1}};
            const auto rows = roleRows(roles);
            for (std::size_t i = 0; i < rows.size(); ++i) {
                if (rows[i].empty()) { errors.push_back("invalid_crew_role_" + std::to_string(i)); continue; }
                auto counter = next.find(rows[i][0]);
                crew.push_back({counter == next.end() ? rows[i][0] : rows[i][0] + std::to_string(counter->second++), rows[i]});
            }
        }
    }
    const auto key = profileKey(vehicle);
    PyObject* console = dataModule("internal_layout_console");
    PyObject* profiles = dataModule("internal_layout_profiles");
    auto console_layouts = console ? p::value(console, "CONSOLE_LAYOUTS_0922") : p::retain(Py_None);
    PyObject* decoded = nullptr;
    PyObject* authored = nullptr;
    std::optional<std::pair<std::string, std::string>> authored_key = key;
    if (key) {
        auto k = keyTuple(*key);
        decoded = lookup(console_layouts.get(), k.get());
        if (profiles) {
            auto table = p::value(profiles, "PROFILES");
            authored = lookup(table.get(), k.get());
            if (!authored) {
                auto aliases = p::value(profiles, "PROFILE_ALIASES_0922");
                if (PyObject* alias = lookup(aliases.get(), k.get())) {
                    if ((authored = lookup(table.get(), alias)))
                        authored_key = std::make_pair(asText(p::item(alias, 0).get()), asText(p::item(alias, 1).get()));
                }
            }
        }
    }
    const auto profile_key = decoded ? key : authored_key;
    PyObject* profile = decoded ? decoded : authored;
    const bool decoded_geometry = decoded != nullptr;
    if (console && (!PyDict_Check(console_layouts.get()) || asNumber(p::value(console, "DECODED_COUNT").get(), -1.) != double(PyDict_Size(console_layouts.get()))))
        errors.push_back("decoded_layout_table_invalid");
    if (!profiles) errors.push_back("compiled_profile_module_unavailable");
    else {
        auto table = p::value(profiles, "PROFILES");
        const double key_version = asNumber(p::value(profiles, "LAYOUT_PROFILE_KEY").get(), 0.);
        if (asNumber(p::value(profiles, "PROFILE_COUNT").get(), 0.) != 251. || !PyDict_Check(table.get()) || PyDict_Size(table.get()) != 251)
            errors.push_back("compiled_profile_count_invalid");
        else if (key_version != 1. && key_version != 2. && key_version != 3. && key_version != 4.)
            errors.push_back("compiled_profile_invalid");
    }
    if (!profile) errors.push_back("compiled_vehicle_profile_missing:" + (profile_key ? profile_key->first + "," + profile_key->second : vehicle));
    // Record layout: decoded (class, tier, crew roles, unmodelled, module zones, crew zones);
    // authored (source id, class, tier, confidence, crew roles, module zones, crew zones).
    PyObject* crew_roles = nullptr;
    PyObject* module_zones = nullptr;
    PyObject* crew_zones = nullptr;
    p::Ref held_roles(p::retain(Py_None)), held_modules(p::retain(Py_None)), held_crew(p::retain(Py_None));
    if (profile) {
        replace(held_roles, p::item(profile, decoded_geometry ? 2 : 4));
        replace(held_modules, p::item(profile, decoded_geometry ? 4 : 5));
        replace(held_crew, p::item(profile, decoded_geometry ? 5 : 6));
        crew_roles = held_roles.get();
        module_zones = held_modules.get();
        crew_zones = held_crew.get();
        std::vector<std::vector<std::string>> active;
        for (const auto& member : crew) active.push_back(member.roles);
        if (roleRows(crew_roles) != active) errors.push_back("crew_roles_profile_mismatch");
    }
    // Official collision bindings: an exact extra on a component material wins over any zone.
    std::map<std::string, std::string> expected_extras;
    for (const auto& item : module_extras) expected_extras[item.second] = item.first;
    for (const auto& member : crew) expected_extras[member.entity + "Health"] = member.entity;
    std::set<std::string> official;
    for (const char* parent : parents) {
        auto component = value(descriptor, parent);
        auto materials = value(component.get(), "materials");
        if (materials.get() == Py_None) continue;
        p::Ref rows(PyDict_Check(materials.get()) ? PyDict_Values(materials.get()) : PySequence_List(materials.get()));
        if (!rows.get()) { PyErr_Clear(); continue; }
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(rows.get()); ++i) {
            auto extra = value(PyList_GET_ITEM(rows.get(), i), "extra");
            auto name = value(extra.get(), "name");
            const auto found = expected_extras.find(name.get() == Py_None ? std::string() : asText(name.get()));
            if (found != expected_extras.end()) official.insert(found->second);
        }
    }
    std::set<std::string> unmodelled;
    std::vector<Candidate> candidates;
    if (decoded_geometry) {
        auto meshCandidate = [&](const std::string& entity, const std::string& kind, const std::string& parent, const std::string& zone,
                                 PyObject* geometry) -> std::optional<Candidate> {
            if (official.count(entity)) return {};
            if (!PyDict_Check(geometry) || !p::value(geometry, "part").get() || !p::flag(p::value(geometry, "part").get())) return {};
            const auto parent_bounds = bounds.find(parent);
            if (parent_bounds == bounds.end()) return {};
            try {
                if (!meshMatchesComponent(descriptor, parent, parent_bounds->second, geometry)) return {};
            } catch (const std::exception&) { PyErr_Clear(); return {}; }
            Candidate c;
            c.entity = entity; c.kind = kind; c.parent = parent; c.zone_id = zone; c.mesh = geometry; c.shape_hint = "indexed_mesh";
            return c;
        };
        for (Py_ssize_t i = 0; module_zones && i < PySequence_Size(module_zones); ++i) {
            auto zone = p::item(module_zones, i);
            auto c = meshCandidate(asText(p::item(zone.get(), 0).get()), "module", asText(p::item(zone.get(), 1).get()), asText(p::item(zone.get(), 2).get()),
                                   p::item(zone.get(), 3).get());
            if (c) candidates.push_back(*c);
        }
        for (std::size_t index = 0; index < crew.size(); ++index) {
            if (!crew_zones || Py_ssize_t(index) >= PySequence_Size(crew_zones)) continue;
            auto alternatives = p::item(crew_zones, Py_ssize_t(index));
            std::vector<Candidate> matches;
            for (Py_ssize_t j = 0; alternatives.get() != Py_None && j < PySequence_Size(alternatives.get()); ++j) {
                auto zone = p::item(alternatives.get(), j);
                auto c = meshCandidate(crew[index].entity, "crew", asText(p::item(zone.get(), 0).get()), asText(p::item(zone.get(), 1).get()),
                                       p::item(zone.get(), 2).get());
                if (c) matches.push_back(*c);
            }
            if (matches.size() == 1) candidates.push_back(matches.front());
        }
        std::set<std::string> located, expected;
        for (const auto& c : candidates) located.insert(c.entity);
        for (const char* entity : module_targets) expected.insert(entity);
        for (const char* entity : {"leftTrack", "rightTrack", "gun"}) expected.erase(entity);
        for (const auto& member : crew) expected.insert(member.entity);
        for (const auto& entity : expected) if (!located.count(entity) && !official.count(entity)) unmodelled.insert(entity);
    } else if (profile) {
        auto fractions = [](PyObject* row) { return Vec{asNumber(p::item(row, 0).get()), asNumber(p::item(row, 1).get()), asNumber(p::item(row, 2).get())}; };
        for (Py_ssize_t i = 0; i < PySequence_Size(module_zones); ++i) {
            auto zone = p::item(module_zones, i);
            Candidate c;
            c.entity = asText(p::item(zone.get(), 0).get());
            if (official.count(c.entity)) continue;
            c.kind = "module";
            c.parent = asText(p::item(zone.get(), 1).get());
            c.zone_id = asText(p::item(zone.get(), 2).get());
            c.center_fractions = fractions(p::item(zone.get(), 3).get());
            c.half_fractions = fractions(p::item(zone.get(), 4).get());
            c.shape_hint = zoneShapeHint(profile_key, c.entity, c.kind, c.zone_id);
            candidates.push_back(c);
        }
        for (std::size_t index = 0; index < crew.size(); ++index) {
            if (Py_ssize_t(index) >= PySequence_Size(crew_zones)) { errors.push_back("compiled_crew_zone_missing:" + std::to_string(index)); continue; }
            if (official.count(crew[index].entity)) continue;
            auto zone = p::item(crew_zones, Py_ssize_t(index));
            if (zone.get() == Py_None) { unmodelled.insert(crew[index].entity); continue; }
            Candidate c;
            c.entity = crew[index].entity;
            c.kind = "crew";
            c.parent = asText(p::item(zone.get(), 0).get());
            c.zone_id = asText(p::item(zone.get(), 1).get());
            c.center_fractions = fractions(p::item(zone.get(), 2).get());
            c.half_fractions = fractions(p::item(zone.get(), 3).get());
            c.shape_hint = zoneShapeHint(profile_key, c.entity, c.kind, c.zone_id);
            candidates.push_back(c);
        }
    }
    if (profile && !official.count("gun") && bounds.count("gun")) {
        Candidate c;
        c.entity = "gun"; c.kind = "module"; c.parent = "gun"; c.zone_id = "single_installed_gun_model";
        c.center_fractions = Vec{.5, .5, .5};
        c.half_fractions = Vec{.40, .40, .40};
        c.shape_hint = zoneShapeHint(profile_key, "gun", "module", c.zone_id);
        candidates.push_back(c);
    }
    std::set<std::string> candidate_entities;
    for (const auto& c : candidates) candidate_entities.insert(c.entity);
    std::vector<std::string> expected_entities(std::begin(module_targets), std::end(module_targets));
    for (const auto& member : crew) expected_entities.push_back(member.entity);
    std::set<std::string> profiled;
    for (const auto& entity : expected_entities) {
        if (official.count(entity)) continue;
        if (candidate_entities.count(entity)) { profiled.insert(entity); continue; }
        if (entity == "leftTrack" || entity == "rightTrack" || unmodelled.count(entity)) continue;
        errors.push_back("geometry_source_missing:" + entity);
    }
    std::set<std::string> required;
    for (const auto& c : candidates) required.insert(c.parent);
    for (const auto& parent : required) if (!bounds.count(parent)) errors.push_back(parent + ":missing_hit_tester_bbox");
    // Targets; a decoded mesh that cannot be installed leaves its entity unmodelled.
    struct Fitted { Candidate candidate; Fit fit; };
    std::vector<Fitted> targets;
    std::set<std::string> failed;
    for (const auto& c : candidates) {
        const auto parent = bounds.find(c.parent);
        if (parent == bounds.end()) continue;
        if (c.mesh) {
            try { targets.push_back({c, fitMesh(descriptor, c, parent->second)}); }
            catch (const std::exception&) { PyErr_Clear(); failed.insert(c.entity); }
        } else {
            targets.push_back({c, fitTarget(descriptor, c, parent->second)});
        }
    }
    std::set<std::string> target_entities;
    for (const auto& t : targets) target_entities.insert(t.candidate.entity);
    for (const auto& entity : failed) if (!target_entities.count(entity)) profiled.erase(entity);
    // validate_layout
    auto& missing = out.missing;
    auto note = [&](const std::string& item) { if (std::find(missing.begin(), missing.end(), item) == missing.end()) missing.push_back(item); };
    for (const auto& entity : profiled) if (!target_entities.count(entity)) note("geometry:" + entity);
    std::set<std::string> validated;
    for (const auto& t : targets) {
        if (t.fit.primitives.empty()) { note("primitives:" + t.candidate.entity); continue; }
        if (!rayResolves(bounds.at(t.candidate.parent), t.fit.center, t.fit.primitives)) {
            note("ray_misses_target:" + t.candidate.entity + ":" + t.candidate.zone_id);
            continue;
        }
        validated.insert(t.candidate.entity);
    }
    for (const auto& entity : target_entities) if (!validated.count(entity)) note("unvalidated_entity:" + entity);
    out.valid = errors.empty() && missing.empty();
    out.layout.valid = out.valid;
    if (out.valid)
        for (auto& t : targets) {
            InternalTarget target;
            target.parent = t.candidate.parent;
            target.entity = t.candidate.entity;
            target.zone_id = t.candidate.zone_id;
            target.primitives = std::move(t.fit.primitives);
            out.layout.targets.push_back(std::move(target));
        }
    return out;
}

std::map<std::string, std::shared_ptr<const Built>>& cache() { static std::map<std::string, std::shared_ptr<const Built>> value; return value; }

std::string join(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& item : items) out += (out.empty() ? "" : ",") + item;
    return out.empty() ? "-" : out;
}
bool profilesEnabled() {
    PyObject* constants = PyImport_ImportModule("_constants");
    if (!constants) { PyErr_Clear(); return true; }
    p::Ref held(constants);
    auto options = p::value(constants, "CONFIG_OPTIONS");
    PyObject* flag = PyDict_Check(options.get()) ? PyDict_GetItemString(options.get(), "internal_layout_profiles") : nullptr;
    if (!flag) return true;
    const int on = PyObject_IsTrue(flag);
    if (on < 0) { PyErr_Clear(); return true; }
    return on != 0;
}
}

// critical_damage._offh_internal_layout: the valid layout of this configuration, else an empty invalid one.
InternalLayout internalLayout(PyObject* descriptor) {
    if (!descriptor || descriptor == Py_None || !profilesEnabled()) return {};
    try {
        const auto vehicle = vehicleName(descriptor);
        const auto print = fingerprint(descriptor);
        std::map<std::string, Bounds> bounds;
        std::string key = vehicle + "\n" + print + "\n" + std::to_string(layout_key);
        for (const char* parent : parents) {
            const auto found = componentBounds(descriptor, parent);
            if (found) bounds[parent] = *found;
            char text[160] = "-";
            if (found)
                std::snprintf(text, sizeof text, "%.17g,%.17g,%.17g,%.17g,%.17g,%.17g", found->minimum[0], found->minimum[1], found->minimum[2],
                              found->maximum[0], found->maximum[1], found->maximum[2]);
            key += std::string("\n") + parent + "=" + text;
        }
        auto& layouts = cache();
        auto at = layouts.find(key);
        if (at == layouts.end()) {
            auto built = std::make_shared<const Built>(build(descriptor, vehicle, bounds));
            if (layouts.size() >= cache_limit) layouts.clear();
            at = layouts.emplace(key, built).first;
            if (!built->valid) {
                static std::set<std::string> reported;
                if (reported.insert(print + "\n" + join(built->errors)).second)
                    log("MODULE layout unavailable vehicle=" + vehicle + " errors=" + join(built->errors) + " missing=" + join(built->missing));
            }
        }
        return at->second->valid ? at->second->layout : InternalLayout{};
    } catch (const std::exception&) {
        PyErr_Clear();
        return {};
    }
}

// critical_damage.prewarm_layout: build the layout only once every native component bound exists.
bool prewarmLayout(PyObject* descriptor) {
    if (!descriptor || descriptor == Py_None) return false;
    for (const char* parent : parents) if (!componentBounds(descriptor, parent)) return false;
    internalLayout(descriptor);
    return true;
}
}
}
