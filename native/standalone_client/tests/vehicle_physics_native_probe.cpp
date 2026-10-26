// Win32 probe for the production Tank graph with the actual adapter services.
// Modes: "tangent" compares the UCRT __libm_sse2_tanf entry with std::tan;
// "inverse" records D3DXMatrixInverse failure semantics; "configure" runs the
// production configure/setTransform/publication path and records every SDK
// call so an original-instruction replay can consume identical SDK results.
#include "wot/vehicle_physics.hpp"
#include "stock_vehicle_native_math.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace wot {
class DynamicsSimulator {
public:
    static void publish(VehiclePhysics& physics) { physics.publish_entity_transform(); }
};
}

namespace {
namespace math = stock_vehicle_native_math;

std::uint32_t bits(float value) {
    std::uint32_t word;
    std::memcpy(&word, &value, 4);
    return word;
}

std::ostream* calls = nullptr;
void words(std::ostream& out, const float* values, std::size_t count) {
    for (std::size_t i = 0; i != count; ++i) out << ' ' << bits(values[i]);
}
void vector(std::ostream& out, wot::Vector3 value) {
    const float values[3] = {value.x, value.y, value.z};
    words(out, values, 3);
}
void matrix(std::ostream& out, const wot::VehicleMatrix& value) {
    words(out, &value.rows[0][0], 16);
}

float logged_tangent(float value) {
    const float result = math::tangent(value);
    *calls << "tan " << bits(value) << ' ' << bits(result) << '\n';
    return result;
}
wot::Vector3 logged_normalize(wot::Vector3 value) {
    const wot::Vector3 result = math::normalize(value);
    *calls << "normalize";
    vector(*calls, value);
    vector(*calls, result);
    *calls << '\n';
    return result;
}
wot::Vector3 logged_coordinate(wot::Vector3 value, const wot::VehicleMatrix& transform) {
    const wot::Vector3 result = math::transformCoordinate(value, transform);
    *calls << "coordinate";
    vector(*calls, value);
    matrix(*calls, transform);
    vector(*calls, result);
    *calls << '\n';
    return result;
}
wot::Vector3 logged_normal(wot::Vector3 value, const wot::VehicleMatrix& transform) {
    const wot::Vector3 result = math::transformNormal(value, transform);
    *calls << "normal";
    vector(*calls, value);
    matrix(*calls, transform);
    vector(*calls, result);
    *calls << '\n';
    return result;
}
void logged_inverse(wot::VehicleMatrix& destination, const wot::VehicleMatrix& source) {
    *calls << "inverse";
    matrix(*calls, destination);
    matrix(*calls, source);
    math::inverse(destination, source);
    matrix(*calls, destination);
    *calls << '\n';
}

float value(unsigned seed, unsigned index, float lo, float hi) {
    unsigned x = seed * 2654435761u + index * 40503u + 12345u;
    x ^= x >> 13; x *= 0x5bd1e995u; x ^= x >> 15;
    return lo + (hi - lo) * float(x % 100000u) / 100000.0f;
}

wot::VehiclePhysicsModeConfiguration make_mode(unsigned seed, std::int32_t index, bool complex) {
    wot::VehiclePhysicsModeConfiguration mode{};
    unsigned k = 0;
    auto v = [&](float lo, float hi) { return value(seed, k++, lo, hi); };
    mode.mode_index = index;
    auto& b = mode.body;
    b.gravity = v(9.0f, 10.5f);
    b.full_mass = v(5.0f, 80.0f);
    b.hull_inertia_factors = {v(0.5f, 2), v(0.5f, 2), v(0.5f, 2)};
    b.hull_size = {v(2, 3.5f), v(0.8f, 1.6f), v(4, 7)};
    b.hull_com_shift_y = v(-0.3f, 0.3f);
    b.hull_box_offset_z = v(-0.5f, 0.5f);
    b.turret_top_position = {v(-0.2f, 0.2f), v(0.7f, 1.5f), v(-1, 1)};
    b.turret_top_width = v(1, 2);
    b.simple_forward_max_speed = v(10, 20);
    b.simple_backward_max_speed = v(3, 8);
    auto& s = mode.shape;
    s.use_complex_form = complex;
    s.auxiliary_clearance = v(0.5f, 1.5f);
    s.tank_board_angle = v(5, 30);
    s.tank_aft_chamfer_fraction = v(0.05f, 0.3f);
    s.tank_front_chamfer_fraction = v(0.05f, 0.3f);
    s.terrain_board_angle = v(5, 30);
    s.terrain_front_chamfer_fraction = v(0.05f, 0.3f);
    s.terrain_aft_chamfer_fraction = v(0.05f, 0.3f);
    auto& c = mode.chassis;
    c.chassis_mass = v(2, 12);
    c.chassis_mass_fraction = v(0.1f, 0.4f);
    c.clearance = v(0.2f, 0.5f);
    c.track_gauge_factor = v(0.7f, 0.95f);
    c.wheel_radius = v(0.2f, 0.5f);
    c.wheel_inertia_factor = v(0.5f, 2);
    c.wheel_forward_inertia_factor = v(0.5f, 2);
    c.wheel_penetration = v(0.01f, 0.1f);
    c.stiffness_zero = v(50, 200);
    c.stiffness_one = seed % 3 ? v(0.5f, 5) : 0.0f;
    for (unsigned i = 0; i != 5; ++i) {
        c.road_wheel_positions[i] = -2.0f + float(i) + v(-0.2f, 0.2f);
        c.stiffness_factors[i] = v(0.5f, 1.5f);
    }
    auto& e = mode.engine;
    e.engine_inertia = v(0.001f, 0.02f);
    e.simple_min_rpm = v(500, 900);
    e.start_rpm = v(1000, 1500);
    e.rotation_angular_velocity = v(100, 200);
    return mode;
}

void dump_body(std::ostream& out, const char* name, const wot::VehicleBodyInertia& inertia,
               const wot::VehicleRigidBody& body) {
    out << name << ".mass " << bits(inertia.mass) << ' ' << bits(body.inverse_mass) << '\n';
    out << name << ".inertia";
    matrix(out, inertia.inertia);
    matrix(out, body.inverse_body_inertia);
    matrix(out, body.inverse_world_inertia);
    out << '\n' << name << ".pose";
    matrix(out, body.transform);
    matrix(out, body.inverse_transform);
    out << ' ' << body.orthonormalization_countdown << '\n';
}

void dump_polyhedron(std::ostream& out, const std::string& name, const wot::VehicleConvexPolyhedron& shape) {
    const auto& t = shape.topology;
    out << name << ".local " << t.vertices.size();
    for (auto v : t.vertices) vector(out, v);
    out << ' ' << t.edge_directions.size();
    for (auto v : t.edge_directions) vector(out, v);
    out << ' ' << t.normal_directions.size();
    for (auto v : t.normal_directions) vector(out, v);
    out << ' ' << t.faces.size();
    for (const auto& face : t.faces) {
        out << ' ' << face.vertices.size();
        for (auto index : face.vertices) out << ' ' << index;
        out << ' ' << face.normal_index << ' ' << bits(face.normal_scale);
    }
    out << ' ' << t.vertex_faces.size();
    for (const auto& adjacency : t.vertex_faces) {
        out << ' ' << adjacency.size();
        for (auto index : adjacency) out << ' ' << index;
    }
    vector(out, t.local_bounds.minimum);
    vector(out, t.local_bounds.maximum);
    out << '\n' << name << ".world";
    for (auto v : shape.world_vertices) vector(out, v);
    for (auto v : shape.world_normals) vector(out, v);
    for (auto v : shape.world_edges) vector(out, v);
    matrix(out, shape.transform);
    vector(out, shape.world_bounds.minimum);
    vector(out, shape.world_bounds.maximum);
    matrix(out, shape.local_box);
    matrix(out, shape.world_box);
    out << '\n';
}

void dump_box(std::ostream& out, const std::string& name, const wot::VehicleConvexBox& box) {
    out << name;
    matrix(out, box.transform);
    matrix(out, box.inverse);
    vector(out, box.center);
    vector(out, box.dimensions);
    vector(out, box.world_center);
    for (auto v : box.axes) vector(out, v);
    for (auto v : box.half_axes) vector(out, v);
    vector(out, box.world_bounds.minimum);
    vector(out, box.world_bounds.maximum);
    for (const auto& p : box.projections) words(out, p.data(), 2);
    out << '\n';
}

void handles(std::ostream& out, const wot::VehicleConstraintHandle* begin, std::size_t count) {
    for (std::size_t i = 0; i != count; ++i)
        out << ' ' << begin[i].context << ' ' << std::uint32_t(begin[i].row) << ' ' << begin[i].generation;
}

void dump_identities(std::ostream& out, const wot::VehicleTank& tank) {
    // Native offsets of the registered components, in registration order.
    static const unsigned offsets[] = {0x688, 0x6b4, 0x70c, 0x660};
    out << "identity.components";
    for (auto component : tank.components) out << ' ' << offsets[unsigned(component)];
    out << "\nidentity.tank";
    handles(out, tank.handles.data(), tank.handles.size());
    for (auto row : tank.chassis.rows) out << ' ' << std::uint32_t(row);
    handles(out, &tank.chassis.handle, 1);
    handles(out, &tank.anchor.handle, 1);
    handles(out, &tank.gimlet.handle, 1);
    handles(out, &tank.rail.handle, 1);
    handles(out, tank.engine.handles.data(), tank.engine.handles.size());
    out << ' ' << unsigned(tank.turret_collision) << "\nidentity.input " << bits(tank.input.rotation_time)
        << ' ' << tank.input.signals;
    for (bool flag : tank.input.flags) out << ' ' << unsigned(flag);
    out << ' ' << tank.input.value_24 << '\n';
    for (std::size_t i = 0; i != tank.chassis.wheels.size(); ++i) {
        const auto& wheel = tank.chassis.wheels[i];
        out << "wheel" << i << ".identity " << unsigned(wheel.enabled);
        for (auto row : wheel.rows) out << ' ' << std::uint32_t(row);
        handles(out, wheel.handles.data(), wheel.handles.size());
        out << '\n';
    }
}

void dump_tank(std::ostream& out, const wot::VehiclePhysics& physics) {
    const wot::VehicleTank& tank = *physics.tank();
    dump_identities(out, tank);
    dump_body(out, "hull", tank.hull, physics.rigid_body());
    dump_body(out, "engine", tank.engine.inertia, tank.engine.body);
    out << "chassis.force " << bits(tank.chassis.vertical_force) << '\n';
    for (std::size_t i = 0; i != tank.chassis.wheels.size(); ++i) {
        const auto& wheel = tank.chassis.wheels[i];
        const auto& c = wheel.construction;
        const std::string name = "wheel" + std::to_string(i);
        out << name << ".record";
        const float scalars[] = {c.carrying_length, c.stiffness_factor, c.equilibrium_deflection,
                                 c.normalized_load, c.radius};
        words(out, scalars, 5);
        out << ' ' << c.side << ' ' << c.index;
        matrix(out, wheel.local_pose);
        out << '\n';
        dump_body(out, name.c_str(), wheel.inertia, wheel.body);
    }
    for (std::size_t i = 0; i != tank.chassis.tracks.size(); ++i) {
        out << "track" << i;
        for (auto index : tank.chassis.tracks[i]) out << ' ' << index;
        out << '\n';
    }
    dump_polyhedron(out, "embedded0", tank.embedded[0]);
    dump_polyhedron(out, "embedded1", tank.embedded[1]);
    for (int collection = 0; collection != 2; ++collection) {
        const auto& shapes = tank.collections[collection];
        for (std::size_t i = 0; i != shapes.polyhedra.size(); ++i)
            dump_polyhedron(out, "collection" + std::to_string(collection) + ".poly" + std::to_string(i),
                            shapes.polyhedra[i]);
        for (std::size_t i = 0; i != shapes.boxes.size(); ++i)
            dump_box(out, "collection" + std::to_string(collection) + ".box" + std::to_string(i),
                     shapes.boxes[i]);
    }
    dump_body(out, "snapshot", tank.snapshot_inertia, tank.snapshot);
}

int run_configure(unsigned first, unsigned count) {
    calls = &std::cerr;
    const wot::VehicleNativeMath services{&logged_tangent, &logged_normalize, &logged_coordinate,
                                          &logged_normal, &logged_inverse};
    for (unsigned seed = first; seed != first + count; ++seed) {
        const bool complex = seed % 2 == 0;
        wot::VehiclePhysicsConfiguration configuration{};
        configuration.vehicle_type = 0;
        configuration.modes.push_back(make_mode(seed, 0, complex));
        configuration.modes.push_back(make_mode(seed + 7919u, 1, complex));
        std::srand(seed);
        std::cout << "case " << seed << '\n' << "config";
        const auto& mode = configuration.modes[0];
        const float body[] = {mode.body.gravity, mode.body.full_mass,
            mode.body.hull_inertia_factors.x, mode.body.hull_inertia_factors.y, mode.body.hull_inertia_factors.z,
            mode.body.hull_size.x, mode.body.hull_size.y, mode.body.hull_size.z,
            mode.body.hull_com_shift_y, mode.body.hull_box_offset_z,
            mode.body.turret_top_position.x, mode.body.turret_top_position.y, mode.body.turret_top_position.z,
            mode.body.turret_top_width,
            mode.shape.auxiliary_clearance, mode.shape.tank_board_angle, mode.shape.tank_aft_chamfer_fraction,
            mode.shape.tank_front_chamfer_fraction, mode.shape.terrain_board_angle,
            mode.shape.terrain_front_chamfer_fraction, mode.shape.terrain_aft_chamfer_fraction,
            mode.chassis.chassis_mass, mode.chassis.chassis_mass_fraction, mode.chassis.clearance,
            mode.chassis.track_gauge_factor, mode.chassis.wheel_radius, mode.chassis.wheel_inertia_factor,
            mode.chassis.wheel_forward_inertia_factor, mode.chassis.stiffness_zero, mode.chassis.stiffness_one,
            mode.engine.engine_inertia};
        words(std::cout, body, sizeof(body) / sizeof(body[0]));
        words(std::cout, mode.chassis.road_wheel_positions.data(), 5);
        words(std::cout, mode.chassis.stiffness_factors.data(), 5);
        std::cout << ' ' << int(complex) << ' ' << bits(mode.chassis.wheel_penetration) << '\n';
        std::cerr << "case " << seed << '\n';
        wot::VehiclePhysics physics;
        physics.configure(configuration, services);
        dump_tank(std::cout, physics);
        wot::VehicleMatrix pose = wot::vehicle_identity_matrix();
        const float yaw = 0.3f + 0.01f * float(seed);
        pose.rows[0][0] = std::cos(yaw); pose.rows[0][2] = -std::sin(yaw);
        pose.rows[2][0] = std::sin(yaw); pose.rows[2][2] = std::cos(yaw);
        pose.rows[3][0] = 10.0f; pose.rows[3][1] = 2.0f; pose.rows[3][2] = -5.0f;
        std::cout << "pose";
        matrix(std::cout, pose);
        std::cout << '\n';
        std::cerr << "transform\n";
        physics.set_tank_transform(pose, services);
        wot::DynamicsSimulator::publish(physics);
        dump_tank(std::cout, physics);
        std::cout << "bounds";
        vector(std::cout, physics.collision_bounds().minimum);
        vector(std::cout, physics.collision_bounds().maximum);
        // In-place reconfigure of the same owner at the published pose.
        std::cout << "\nreconfigure\n";
        std::cerr << "reconfigure\n";
        physics.configure(configuration, services);
        dump_tank(std::cout, physics);
        std::cout << "end\n";
    }
    return 0;
}

int run_tangent() {
    // The configure argument is degrees * 0.017453292f; sweep that domain and
    // a dense bit-pattern band across +/-pi/2.
    std::uint64_t compared = 0, different = 0;
    auto compare = [&](float x) {
        const float a = math::tangent(x);
        const float b = std::tan(x);
        ++compared;
        if (bits(a) != bits(b)) {
            if (different < 20) std::cout << "differ " << bits(x) << ' ' << bits(a) << ' ' << bits(b) << '\n';
            ++different;
        }
    };
    for (int degrees = -90000; degrees <= 90000; ++degrees)
        compare(float(degrees) / 1000.0f * 0.017453292f);
    for (std::uint32_t word = 0; word < 0x3fc90fdbu; word += 97) {
        float x;
        std::memcpy(&x, &word, 4);
        compare(x);
        compare(-x);
    }
    std::cout << "tangent compared " << compared << " different " << different << '\n';
    return 0;
}

int run_inverse() {
    wot::VehicleMatrix destination{};
    for (int i = 0; i != 16; ++i) (&destination.rows[0][0])[i] = float(i) + 0.5f;
    const wot::VehicleMatrix before = destination;
    wot::VehicleMatrix singular{};
    singular.rows[0][0] = 1.0f;
    math::inverse(destination, singular);
    std::cout << "singular unchanged " << (std::memcmp(&before, &destination, sizeof(before)) == 0) << '\n';
    D3DXMATRIX out, in;
    std::memset(&in, 0, sizeof(in));
    const D3DXMATRIX* result = D3DXMatrixInverse(&out, NULL, &in);
    std::cout << "singular returns null " << (result == NULL) << '\n';
    // Scaling/Translation only assign elements; check that on special values.
    const float specials[] = {0.0f, -0.0f, 1.5f, -2.25f, 1e-30f, 3e38f,
                              std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()};
    unsigned exact = 0, checked = 0;
    for (float x : specials) for (float y : specials) for (float z : specials) {
        D3DXMATRIX scaling, translation;
        D3DXMatrixScaling(&scaling, x, y, z);
        D3DXMatrixTranslation(&translation, x, y, z);
        wot::VehicleMatrix expected = wot::vehicle_identity_matrix();
        expected.rows[0][0] = x; expected.rows[1][1] = y; expected.rows[2][2] = z;
        exact += std::memcmp(&scaling, &expected, 64) == 0;
        expected = wot::vehicle_identity_matrix();
        expected.rows[3][0] = x; expected.rows[3][1] = y; expected.rows[3][2] = z;
        exact += std::memcmp(&translation, &expected, 64) == 0;
        checked += 2;
    }
    std::cout << "scaling/translation exact " << exact << " of " << checked << '\n';
    std::cout << "entry " << math::resolveTangentEntry() << ' '
              << reinterpret_cast<void*>(GetProcAddress(LoadLibraryW(L"api-ms-win-crt-math-l1-1-0.dll"),
                                                          "__libm_sse2_tanf")) << '\n';
    return 0;
}
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    math::resolveTangentEntry();
    const std::string mode = argv[1];
    if (mode == "tangent") return run_tangent();
    if (mode == "inverse") return run_inverse();
    if (mode == "configure" && argc == 4)
        return run_configure(unsigned(std::strtoul(argv[2], nullptr, 10)),
                             unsigned(std::strtoul(argv[3], nullptr, 10)));
    return 3;
}
