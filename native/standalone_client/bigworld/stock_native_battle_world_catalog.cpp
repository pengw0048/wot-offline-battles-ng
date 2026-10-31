// The baked #1513 destructible catalog of one map, validated once at round start.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_world_parts.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace native_battle {
namespace world {
namespace {
using Catalog = DestructibleCatalog;
using Kind = Catalog::Kind;
constexpr const char* format_name = "offline-lan-0922-destructible-catalog";
constexpr std::int64_t format_version = 9;
constexpr int structure_material_min = 73, structure_material_max = 86;
const std::set<std::string> supported_maps{
    "01_karelia", "02_malinovka", "04_himmelsdorf", "05_prohorovka", "06_ensk", "07_lakeville", "08_ruinberg",
    "10_hills", "11_murovanka", "13_erlenberg", "14_siegfried_line", "17_munchen", "18_cliff", "19_monastery",
    "22_slough", "23_westfeld", "28_desert", "29_el_hallouf", "31_airfield", "33_fjord", "34_redshire", "35_steppes",
    "36_fishing_bay", "37_caucasus", "38_mannerheim_line", "44_north_america", "45_north_america", "47_canada_a",
    "59_asia_great_wall", "63_tundra", "73_asia_korea", "83_kharkiv", "84_winter", "86_himmelsdorf_winter",
    "92_stalingrad", "95_lost_city", "100_thepit", "101_dday", "103_ruinberg_winter", "112_eiffel_tower_ctf", "114_czech"};

[[noreturn]] void invalid(const char* what) { throw p::Error(what); }
PyObject* get(PyObject* mapping, const char* name) {
    PyObject* v = PyDict_Check(mapping) ? PyDict_GetItemString(mapping, name) : nullptr;
    return v ? v : Py_None;
}
// Python `type(v) in (int, long)`: bool is excluded.
bool exactInteger(PyObject* v) { return PyInt_CheckExact(v) || PyLong_CheckExact(v); }
std::int64_t integerValue(PyObject* v) {
    const auto out = PyLong_AsLongLong(v);
    if (out == -1 && PyErr_Occurred()) { PyErr_Clear(); invalid("destructible catalog integer is out of range"); }
    return out;
}
// Python int(value), raising the given error where int() would.
std::int64_t coerced(PyObject* v, const char* what) {
    PyObject* number = v == Py_None ? nullptr : PyNumber_Int(v);
    if (!number) { PyErr_Clear(); invalid(what); }
    p::Ref held(number);
    return integerValue(number);
}
// Python float(value) that is neither NaN nor infinite.
bool finiteNumber(PyObject* v, double& out) {
    PyObject* number = v == Py_None ? nullptr : PyNumber_Float(v);
    if (!number) { PyErr_Clear(); return false; }
    out = PyFloat_AS_DOUBLE(number);
    Py_DECREF(number);
    return std::isfinite(out);
}
bool isText(PyObject* v) { return PyString_Check(v) || PyUnicode_Check(v); }
std::string utf8(PyObject* v) { return p::string(v); }
std::vector<PyObject*> listItems(PyObject* v) {
    std::vector<PyObject*> out;
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(v); ++i) out.push_back(PyList_GET_ITEM(v, i));
    return out;
}
Catalog::Signature signatureOf(PyObject* row) {
    Catalog::Signature out{};
    for (Py_ssize_t i = 0; i < 12; ++i) out[std::size_t(i)] = integerValue(PyList_GET_ITEM(row, i));
    return out;
}
bool integers(PyObject* row, Py_ssize_t first, Py_ssize_t last) {
    for (Py_ssize_t i = first; i < last; ++i) if (!exactInteger(PyList_GET_ITEM(row, i))) return false;
    return true;
}

void checkManifest(const std::string& directory, const std::string& map) {
    const auto path = directory + "/manifest.json";
    if (!exists(path)) invalid("destructible catalog manifest is missing");
    auto manifest = readJson(path);
    if (!PyDict_Check(manifest.get())) invalid("destructible catalog manifest is incompatible");
    const auto version = coerced(PyDict_GetItemString(manifest.get(), "version") ? get(manifest.get(), "version") : p::integer(-1).get(),
                                 "destructible catalog manifest is incompatible");
    const auto quantization = coerced(get(manifest.get(), "locator_quantization"), "destructible catalog manifest is incompatible");
    PyObject* format = get(manifest.get(), "format");
    if (!isText(format) || utf8(format) != std::string(format_name) + "-manifest" || version != format_version ||
        quantization != Catalog::quantization)
        invalid("destructible catalog manifest is incompatible");
    PyObject* records = get(manifest.get(), "maps");
    if (!PyList_Check(records)) invalid("destructible catalog manifest record list is invalid");
    PyObject* selected = nullptr;
    for (PyObject* record : listItems(records)) {
        if (!PyDict_Check(record)) continue;
        PyObject* name = get(record, "map");
        if (shortMapName(name == Py_None ? std::string() : p::string(p::Ref(PyObject_Str(name)).get())) != map) continue;
        if (selected) invalid("destructible catalog manifest record is duplicated");
        selected = record;
    }
    if (!selected) invalid("destructible catalog manifest has no record for this map");
    PyObject* file = get(selected, "file");
    const std::string filename = file == Py_None ? std::string() : p::string(p::Ref(PyObject_Str(file)).get());
    if (!supported_maps.count(map) || filename != map + ".json") invalid("destructible catalog manifest record is invalid");
    if (!exists(directory + "/" + filename)) invalid("destructible catalog is unavailable");
}

void readResources(PyObject* data, Catalog& out) {
    PyObject* resources = get(data, "resources");
    if (!PyDict_Check(resources) || PyDict_Size(resources) == 0) invalid("destructible catalog resources are unavailable");
    Py_ssize_t cursor = 0;
    PyObject *name, *record;
    while (PyDict_Next(resources, &cursor, &name, &record)) {
        if (!isText(name)) invalid("destructible resource filename is invalid");
        const auto filename = utf8(name);
        const auto normalized = normalizedFilename(filename);
        if (normalized.empty() || normalized.size() < 6 || normalized.compare(normalized.size() - 6, 6, ".model") ||
            out.resources.count(normalized))
            invalid("destructible resource filename is invalid");
        if (!PyDict_Check(record)) invalid("destructible resource record is invalid");
        PyObject* raw_kind = get(record, "kind");
        const std::string kind = isText(raw_kind) ? utf8(raw_kind) : std::string();
        Catalog::Resource resource;
        resource.filename = filename;
        if (kind == "falling") resource.kind = Kind::Falling;
        else if (kind == "fragile") resource.kind = Kind::Fragile;
        else if (kind == "structure") resource.kind = Kind::Structure;
        else invalid("destructible resource kind is invalid");
        PyObject* boxes = get(record, "boxes");
        if (!PyList_Check(boxes) || PyList_GET_SIZE(boxes) == 0) invalid("destructible resource boxes are invalid");
        for (PyObject* box : listItems(boxes)) {
            if (!PyList_Check(box) || PyList_GET_SIZE(box) != 7) invalid("destructible resource box is invalid");
            std::array<double, 6> values{};
            for (Py_ssize_t i = 0; i < 6; ++i)
                if (!finiteNumber(PyList_GET_ITEM(box, i), values[std::size_t(i)])) invalid("destructible resource box is invalid");
            if (!(values[0] < values[3] && values[1] < values[4] && values[2] < values[5])) invalid("destructible resource box is invalid");
            PyObject* material = PyList_GET_ITEM(box, 6);
            int mat = -1;
            if (resource.kind != Kind::Structure) {
                if (material != Py_None) invalid("non-structure resource carries a module material");
            } else {
                const auto value = coerced(material, "structure module material is invalid");
                if (value < structure_material_min || value >= structure_material_max) invalid("structure module material is invalid");
                mat = int(value);
            }
            resource.boxes.push_back(values);
            resource.materials.push_back(mat);
        }
        PyObject* locators = get(record, "locators");
        const bool structure = resource.kind == Kind::Structure;
        if (structure && locators != Py_None) invalid("structure resource carries instance locators");
        if (!structure && resource.boxes.size() > 1) {
            if (!PyList_Check(locators) || PyList_GET_SIZE(locators) == 0) invalid("ambiguous destructible resource has no instance locators");
        } else if (locators != Py_None) {
            invalid("unambiguous resource carries instance locators");
        }
        std::set<Catalog::Signature> seen;
        if (PyList_Check(locators))
            for (PyObject* locator : listItems(locators)) {
                if (!PyList_Check(locator) || PyList_GET_SIZE(locator) != 13 || !integers(locator, 0, 13))
                    invalid("destructible instance locator is invalid");
                const auto box_index = integerValue(PyList_GET_ITEM(locator, 12));
                if (!seen.insert(signatureOf(locator)).second || box_index < 0 || box_index >= std::int64_t(resource.boxes.size()))
                    invalid("destructible instance locator is invalid");
            }
        PyObject* retained = PyDict_GetItemString(record, "retained_collision_boxes");
        if (retained) {
            if (!PyList_Check(retained) && !PyTuple_Check(retained)) invalid("destructible retained collision boxes are invalid");
            for (Py_ssize_t i = 0; i < PySequence_Size(retained); ++i) {
                p::Ref index(PySequence_GetItem(retained, i));
                if (!exactInteger(index.get())) invalid("destructible retained collision boxes are invalid");
                const auto value = integerValue(index.get());
                if (value < 0 || value >= std::int64_t(resource.boxes.size())) invalid("destructible retained collision boxes are invalid");
                resource.retained.insert(std::size_t(value));
            }
        }
        out.resources.emplace(normalized, std::move(resource));
    }
}

// The pinned _instance_candidate: (filename, box_index) naming a resource and one of its boxes.
std::pair<std::string, std::optional<std::size_t>> candidate(const Catalog& c, PyObject* filename, PyObject* box_index) {
    if (!isText(filename)) invalid("destructible instance candidate is invalid");
    const auto normalized = normalizedFilename(utf8(filename));
    const auto found = c.resources.find(normalized);
    if (found == c.resources.end()) invalid("destructible instance candidate is invalid");
    if (found->second.kind == Kind::Structure) {
        if (box_index != Py_None) invalid("structure instance candidate has a box index");
        return {normalized, std::nullopt};
    }
    if (!exactInteger(box_index)) invalid("destructible instance box index is invalid");
    const auto index = integerValue(box_index);
    if (index < 0 || index >= std::int64_t(found->second.boxes.size())) invalid("destructible instance box index is invalid");
    return {normalized, std::size_t(index)};
}

Point cross(const Point& a, const Point& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
double dot(const Point& a, const Point& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// The pinned _baked_world_boxes_1513: the instance OBBs rebuilt from the quantized world signature.
std::vector<offline_destructible::Box> worldBoxes(const Catalog::Resource& r, const Catalog::Signature& s,
                                                  const std::optional<std::size_t>& box_index) {
    const double scale = double(Catalog::quantization);
    const Point origin{double(s[0]) / scale, double(s[1]) / scale, double(s[2]) / scale};
    std::array<Point, 3> basis{};
    for (unsigned axis = 0; axis < 3; ++axis)
        for (unsigned component = 0; component < 3; ++component) basis[axis][component] = double(s[3 + axis * 3 + component]) / scale;
    std::vector<std::size_t> indexes;
    if (r.kind == Kind::Structure) for (std::size_t i = 0; i < r.boxes.size(); ++i) indexes.push_back(i);
    else indexes.push_back(*box_index);
    std::vector<offline_destructible::Box> out;
    for (const auto index : indexes) {
        const auto& box = r.boxes[index];
        const Point local{(box[0] + box[3]) * .5, (box[1] + box[4]) * .5, (box[2] + box[5]) * .5};
        const Point half{(box[3] - box[0]) * .5, (box[4] - box[1]) * .5, (box[5] - box[2]) * .5};
        offline_destructible::Box world;
        for (unsigned component = 0; component < 3; ++component) {
            double value = origin[component];
            for (unsigned axis = 0; axis < 3; ++axis) value += basis[axis][component] * local[axis];
            world.center[component] = value;
        }
        for (unsigned axis = 0; axis < 3; ++axis)
            world.axes.push_back({basis[axis][0] * half[axis], basis[axis][1] * half[axis], basis[axis][2] * half[axis]});
        // Older fixtures did not promise a usable transform; never expose degenerate geometry.
        if (std::abs(dot(world.axes[0], cross(world.axes[1], world.axes[2]))) <= 1.e-9) return {};
        world.material = r.materials[index];
        out.push_back(std::move(world));
    }
    return out;
}

// The pinned _baked_quantization_margin_1513: world error bound introduced by the locator grid.
double quantizationMargin(const Catalog::Resource& r, const std::optional<std::size_t>& box_index) {
    double maximum = 0.;
    for (std::size_t i = 0; i < r.boxes.size(); ++i) {
        if (r.kind != Kind::Structure && i != *box_index) continue;
        const auto& box = r.boxes[i];
        maximum = std::max(maximum, std::max(std::abs(box[0]), std::abs(box[3])) + std::max(std::abs(box[1]), std::abs(box[4])) +
                                        std::max(std::abs(box[2]), std::abs(box[5])));
    }
    return std::sqrt(3.) * .5 / double(Catalog::quantization) * (1. + maximum);
}

void readInstances(PyObject* data, Catalog& out) {
    PyObject* instances = get(data, "instances");
    PyObject* ambiguous = get(data, "ambiguous_instances");
    if (!PyList_Check(instances) || PyList_GET_SIZE(instances) == 0) invalid("destructible instance index is unavailable");
    if (!PyList_Check(ambiguous)) invalid("ambiguous destructible instance index is invalid");
    std::set<Catalog::Signature> signatures;
    std::set<Catalog::Key> wires;
    for (PyObject* row : listItems(instances)) {
        if (!PyList_Check(row) || PyList_GET_SIZE(row) != 17 || !integers(row, 0, 12)) invalid("destructible instance row is invalid");
        const auto signature = signatureOf(row);
        if (!signatures.insert(signature).second) invalid("destructible instance signature is invalid");
        const auto resource = candidate(out, PyList_GET_ITEM(row, 12), PyList_GET_ITEM(row, 13));
        PyObject *chunk = PyList_GET_ITEM(row, 14), *item = PyList_GET_ITEM(row, 15);
        if (!exactInteger(chunk) || !exactInteger(item)) invalid("destructible instance wire is invalid");
        const auto chunk_id = integerValue(chunk), item_index = integerValue(item);
        if (chunk_id < 0 || chunk_id > 0xFFFFFFFFLL || item_index < 0 || item_index > 0xFFFFFFFFLL)
            invalid("destructible instance wire is invalid");
        const Catalog::Key wire{std::uint32_t(chunk_id), std::uint32_t(item_index)};
        if (!wires.insert(wire).second) invalid("destructible instance wire is duplicated");
        double item_scale = 0.;
        if (!finiteNumber(PyList_GET_ITEM(row, 16), item_scale) || item_scale <= 0.) invalid("destructible instance scale is invalid");
        const auto& record = out.resources.at(resource.first);
        Catalog::Placement placement;
        placement.kind = record.kind;
        placement.resource = resource.first;
        placement.descriptor = record.filename;
        placement.box_index = resource.second;
        placement.signature = signature;
        placement.item_scale = item_scale;
        placement.boxes = worldBoxes(record, signature, resource.second);
        placement.margin = quantizationMargin(record, resource.second);
        out.placements.emplace(wire, std::move(placement));
    }
    for (PyObject* row : listItems(ambiguous)) {
        if (!PyList_Check(row) || PyList_GET_SIZE(row) != 13 || !integers(row, 0, 12) || !PyList_Check(PyList_GET_ITEM(row, 12)) ||
            PyList_GET_SIZE(PyList_GET_ITEM(row, 12)) < 2)
            invalid("ambiguous destructible instance row is invalid");
        const auto signature = signatureOf(row);
        if (signatures.count(signature) || out.ambiguous.count(signature)) invalid("ambiguous destructible instance signature is invalid");
        std::set<std::pair<std::string, std::optional<std::size_t>>> unique;
        for (PyObject* entry : listItems(PyList_GET_ITEM(row, 12))) {
            if (!PyList_Check(entry) || PyList_GET_SIZE(entry) != 2) invalid("destructible instance candidate is invalid");
            unique.insert(candidate(out, PyList_GET_ITEM(entry, 0), PyList_GET_ITEM(entry, 1)));
        }
        // Duplicate candidates naming one descriptor and box describe one gameplay object, not an ambiguity.
        if (unique.size() > 1) out.ambiguous.insert(signature);
        else signatures.insert(signature);
    }
    PyObject* trees = get(data, "tree_instances");
    if (!PyList_Check(trees)) invalid("tree instance index is unavailable");
    for (PyObject* row : listItems(trees)) {
        if (!PyList_Check(row) || PyList_GET_SIZE(row) != 15 || !integers(row, 0, 12) || !isText(PyList_GET_ITEM(row, 12)) ||
            !integers(row, 13, 15))
            invalid("tree instance row is invalid");
        const auto authored = utf8(PyList_GET_ITEM(row, 12));
        const auto filename = normalizedFilename(authored);
        const auto chunk_id = integerValue(PyList_GET_ITEM(row, 13)), item_index = integerValue(PyList_GET_ITEM(row, 14));
        if (filename.size() < 4 || filename.compare(filename.size() - 4, 4, ".spt") || chunk_id < 0 || item_index < 0 ||
            chunk_id > 0xFFFFFFFFLL || item_index > 0xFFFFFFFFLL)
            invalid("tree instance row is invalid");
        const Catalog::Key wire{std::uint32_t(chunk_id), std::uint32_t(item_index)};
        if (!wires.insert(wire).second) invalid("tree instance wire is duplicated");
        Catalog::Placement placement;
        placement.kind = Kind::Tree;
        placement.resource = filename;
        placement.descriptor = authored;
        placement.signature = signatureOf(row);
        double sum = 0.;
        for (unsigned i = 6; i < 9; ++i) sum += std::pow(double(placement.signature[i]) / double(Catalog::quantization), 2);
        placement.item_scale = std::sqrt(sum);
        out.placements.emplace(wire, std::move(placement));
    }
    PyObject* excluded = get(data, "excluded_instances");
    if (!PyList_Check(excluded)) invalid("excluded instance index is unavailable");
    for (PyObject* row : listItems(excluded)) {
        if (!PyList_Check(row) || PyList_GET_SIZE(row) != 3 || !integers(row, 0, 3)) invalid("excluded instance row is invalid");
        const auto chunk_id = integerValue(PyList_GET_ITEM(row, 0)), item_index = integerValue(PyList_GET_ITEM(row, 1)),
                   mode = integerValue(PyList_GET_ITEM(row, 2));
        if (chunk_id < 0 || item_index < 0 || mode < 0 || chunk_id > 0xFFFFFFFFLL || mode > 0xFFFFFFFFLL || (mode & 1) ||
            item_index > 0xFFFFFFFFLL)
            invalid("excluded instance row is invalid");
        const Catalog::Key wire{std::uint32_t(chunk_id), std::uint32_t(item_index)};
        if (!wires.insert(wire).second) invalid("excluded instance row is invalid");
        out.excluded.insert(wire);
    }
}
}

std::string shortMapName(std::string map) {
    for (auto& c : map) if (c == '\\') c = '/';
    while (!map.empty() && map.back() == '/') map.pop_back();
    const auto slash = map.rfind('/');
    return slash == std::string::npos ? map : map.substr(slash + 1);
}

bool exists(const std::string& path) { std::ifstream in(path); return in.good(); }

p::Ref readJson(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw p::Error("baked map data is unavailable: " + path);
    std::stringstream text;
    text << in.rdbuf();
    p::Ref json(PyImport_ImportModule("json"));
    p::Ref raw(PyString_FromStringAndSize(text.str().data(), Py_ssize_t(text.str().size())));
    return p::Ref(PyObject_CallMethod(json.get(), const_cast<char*>("loads"), const_cast<char*>("O"), raw.get()));
}

// The pinned _normalized_filename: forward slashes, stripped, lower case.
std::string normalizedFilename(const std::string& filename) {
    std::string out = filename;
    for (auto& c : out) c = c == '\\' ? '/' : char(std::tolower(static_cast<unsigned char>(c)));
    const auto first = out.find_first_not_of(" \t\r\n\v\f");
    if (first == std::string::npos) return {};
    const auto last = out.find_last_not_of(" \t\r\n\v\f");
    return out.substr(first, last - first + 1);
}

std::unique_ptr<DestructibleCatalog> loadDestructibleCatalog(const std::string& config_dir, const std::string& raw_map) {
    const auto map = shortMapName(raw_map);
    if (map.empty()) return nullptr;
    const auto directory = config_dir + "/destructibles";
    const auto manifest = directory + "/manifest.json";
    if (!exists(manifest)) {
        // A shipped catalog directory always has its manifest.
        std::ifstream probe(directory + "/" + map + ".json");
        if (probe.good()) throw p::Error("destructible catalog manifest is missing");
        return nullptr;
    }
    checkManifest(directory, map);
    const auto path = directory + "/" + map + ".json";
    auto data = readJson(path);
    if (!PyDict_Check(data.get())) invalid("destructible catalog root is not an object");
    PyObject* format = get(data.get(), "format");
    if (!isText(format) || utf8(format) != format_name) invalid("unsupported destructible catalog format");
    if (coerced(PyDict_GetItemString(data.get(), "version") ? get(data.get(), "version") : p::integer(-1).get(),
                "unsupported destructible catalog version") != format_version)
        invalid("unsupported destructible catalog version");
    PyObject* name = get(data.get(), "map");
    if (shortMapName(name == Py_None ? std::string() : p::string(p::Ref(PyObject_Str(name)).get())) != map)
        invalid("destructible catalog map does not match the battle");
    if (coerced(get(data.get(), "locator_quantization"), "destructible locator quantization is invalid") != DestructibleCatalog::quantization)
        invalid("destructible locator quantization is invalid");
    auto out = std::make_unique<DestructibleCatalog>();
    out->map = map;
    readResources(data.get(), *out);
    readInstances(data.get(), *out);
    return out;
}
}
}
