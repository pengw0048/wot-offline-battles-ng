"""Exercise the configured Tank owner graph under ASan/UBSan.

Host math services are declared stand-ins for D3DX9_43 and UCRT; they prove
ownership, ordering and transaction behavior, not Win32 last-bit results.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

SOURCE = r'''
#include "wot/vehicle_physics.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace wot {
class DynamicsSimulator {
public:
    static void publish(VehiclePhysics& physics) { physics.publish_entity_transform(); }
};
}

using namespace wot;

namespace {
float host_tangent(float value) { return std::tan(value); }
Vector3 host_normalize(Vector3 v) {
    const float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return {v.x / length, v.y / length, v.z / length};
}
Vector3 host_coordinate(Vector3 v, const VehicleMatrix& m) {
    const auto& r = m.rows;
    return {v.x * r[0][0] + v.y * r[1][0] + v.z * r[2][0] + r[3][0],
            v.x * r[0][1] + v.y * r[1][1] + v.z * r[2][1] + r[3][1],
            v.x * r[0][2] + v.y * r[1][2] + v.z * r[2][2] + r[3][2]};
}
Vector3 host_normal(Vector3 v, const VehicleMatrix& m) {
    const auto& r = m.rows;
    return {v.x * r[0][0] + v.y * r[1][0] + v.z * r[2][0],
            v.x * r[0][1] + v.y * r[1][1] + v.z * r[2][1],
            v.x * r[0][2] + v.y * r[1][2] + v.z * r[2][2]};
}
unsigned inverse_calls = 0;
void host_inverse(VehicleMatrix& out, const VehicleMatrix& in) {
    ++inverse_calls;
    // Gauss-Jordan; a singular input leaves the destination unchanged.
    double a[4][8];
    for (int i = 0; i != 4; ++i)
        for (int j = 0; j != 8; ++j) a[i][j] = j < 4 ? in.rows[i][j] : (j - 4 == i);
    for (int c = 0; c != 4; ++c) {
        int p = c;
        for (int r = c + 1; r != 4; ++r) if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
        if (a[p][c] == 0) return;
        for (int j = 0; j != 8; ++j) std::swap(a[c][j], a[p][j]);
        const double d = a[c][c];
        for (int j = 0; j != 8; ++j) a[c][j] /= d;
        for (int r = 0; r != 4; ++r) if (r != c) {
            const double f = a[r][c];
            for (int j = 0; j != 8; ++j) a[r][j] -= f * a[c][j];
        }
    }
    for (int i = 0; i != 4; ++i) for (int j = 0; j != 4; ++j) out.rows[i][j] = float(a[i][j + 4]);
}
const VehicleNativeMath math{&host_tangent, &host_normalize, &host_coordinate,
                             &host_normal, &host_inverse};

float value(unsigned seed, unsigned index, float lo, float hi) {
    unsigned x = seed * 2654435761u + index * 40503u + 12345u;
    x ^= x >> 13; x *= 0x5bd1e995u; x ^= x >> 15;
    return lo + (hi - lo) * float(x % 100000u) / 100000.0f;
}

VehiclePhysicsModeConfiguration make_mode(unsigned seed, std::int32_t index, bool complex) {
    VehiclePhysicsModeConfiguration mode{};
    unsigned k = 0;
    auto v = [&](float lo, float hi) { return value(seed, k++, lo, hi); };
    mode.name = index == 0 ? "normal" : "siegeMode";
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
    b.simple_rotation_speed = v(0.5f, 1);
    b.angular_velocity_factor = v(0.5f, 2);
    b.allowed_rpm_excess = v(1, 1.5f);
    b.overspeed_resistance_factor = v(0, 1);
    b.center_rotation_forward_speed = v(0, 2);
    b.movement_revert_speed = v(0, 2);
    b.com_friction_y_offset = v(0, 1);
    b.com_side_friction = v(0, 1);
    b.gimlet_pushed_rotation = v(0, 1);
    b.gimlet_pushed_handbrake = v(0, 1);
    auto& s = mode.shape;
    s.use_complex_form = complex;
    s.auxiliary_clearance = v(0.5f, 1.5f);
    s.tank_board_angle = v(5, 30);
    s.tank_aft_chamfer_fraction = v(0.05f, 0.3f);
    s.tank_front_chamfer_fraction = v(0.05f, 0.3f);
    s.terrain_board_angle = v(5, 30);
    s.terrain_front_chamfer_fraction = v(0.05f, 0.3f);
    s.terrain_aft_chamfer_fraction = v(0.05f, 0.3f);
    mode.siege_mode_available = index != 0;
    auto& c = mode.chassis;
    c.chassis_mass = v(2, 12);
    c.chassis_mass_fraction = v(0.1f, 0.4f);
    c.clearance = v(0.2f, 0.5f);
    c.track_gauge_factor = v(0.7f, 0.95f);
    c.wheel_radius = v(0.2f, 0.5f);
    c.wheel_inertia_factor = v(0.5f, 2);
    c.wheel_forward_inertia_factor = v(0.5f, 2);
    c.stiffness_zero = v(50, 200);
    c.stiffness_one = index == 0 ? v(0.5f, 5) : 0.0f;
    for (unsigned i = 0; i != 5; ++i) {
        c.road_wheel_positions[i] = -2.0f + float(i) + v(-0.2f, 0.2f);
        c.stiffness_factors[i] = v(0.5f, 1.5f);
    }
    c.grounds.assign(1, VehicleGroundSettings());
    auto& e = mode.engine;
    e.losses = std::array<float, 2>{{v(0, 1), v(0, 1)}};
    e.simple_engine_power = v(50, 900);
    e.engine_inertia = v(0.001f, 0.02f);
    e.simple_min_rpm = v(500, 900);
    e.idle_rpm = v(600, 1000);
    e.start_rpm = v(1000, 1500);
    e.maximum_forward_angular_velocity = v(200, 300);
    e.maximum_backward_angular_velocity = v(150, 250);
    e.rotation_angular_velocity = v(100, 200);
    return mode;
}

VehiclePhysicsConfiguration make(unsigned seed, bool complex,
                                 const std::vector<std::int32_t>& indices, unsigned gears = 3) {
    VehiclePhysicsConfiguration result{};
    result.vehicle_type = 0;
    for (std::size_t i = 0; i != indices.size(); ++i)
        result.modes.push_back(make_mode(seed + unsigned(i) * 7919u, indices[i], complex));
    result.anchor = VehicleAnchorConfiguration{0.5f, 0.25f, 0.125f};
    result.rail_factor_in_contact = 0.75f;
    result.gimlet = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    result.gimlet_fields = 16;
    result.gears = VehicleFakeGearboxConfiguration{};
    for (unsigned i = 0; i != gears; ++i) {
        result.gears->forward.push_back({2.0f + 3 * i, 1.0f + i, 0.2f + 0.1f * i, 0.9f - 0.1f * i});
        result.gears->backward.push_back({1.0f + i, 0.5f + i, 0.3f, 0.8f});
    }
    return result;
}

bool same(const VehicleMatrix& a, const VehicleMatrix& b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}
bool same(Vector3 a, Vector3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

VehicleMatrix pose(float yaw, Vector3 at) {
    VehicleMatrix m = vehicle_identity_matrix();
    m.rows[0][0] = std::cos(yaw); m.rows[0][2] = -std::sin(yaw);
    m.rows[2][0] = std::sin(yaw); m.rows[2][2] = std::cos(yaw);
    m.rows[3][0] = at.x; m.rows[3][1] = at.y; m.rows[3][2] = at.z;
    return m;
}

void check_fresh(const VehiclePhysics& physics, const VehiclePhysicsConfiguration& cfg) {
    const VehicleTank* tank = physics.tank();
    assert(tank && physics.configuration_epoch() == 1);
    const auto& mode = *std::find_if(cfg.modes.begin(), cfg.modes.end(),
        [](const VehiclePhysicsModeConfiguration& m) { return m.mode_index == 0 || m.mode_index == -1; });
    const auto& body = tank->current_settings();
    assert(body.body.full_mass == mode.body.full_mass && body.wheel_bounds_offset == 0.2f);
    // f84370 hull mass/inertia, identity pose.
    const auto hull = build_vehicle_hull_mass_properties(mode.body, mode.chassis);
    const auto& rigid = physics.rigid_body();
    assert(tank->hull.mass == hull.mass && rigid.inverse_mass == hull.inverse_mass);
    assert(tank->hull.inertia.rows[0][0] == hull.inertia_diagonal.x);
    assert(tank->hull.inertia.rows[2][2] == hull.inertia_diagonal.z);
    VehicleMatrix expected{};
    host_inverse(expected, tank->hull.inertia);
    assert(same(rigid.inverse_body_inertia, expected));
    assert(same(rigid.inverse_world_inertia, expected));
    assert(same(rigid.transform, vehicle_identity_matrix()));
    assert(rigid.solver_id == -1 && rigid.record_history && rigid.last_step == 0.05f);
    // Engine f7ea60.
    assert(tank->engine.inertia.mass == 1.0f && tank->engine.body.inverse_mass == 1.0f);
    assert(tank->engine.inertia.inertia.rows[1][1] == mode.engine.engine_inertia);
    // f84e80 vertical force, f87e90 ten wheels.
    const float force = ((1.0f - mode.chassis.chassis_mass_fraction) * mode.body.full_mass) *
                        mode.body.gravity;
    assert(tank->chassis.vertical_force == force);
    const auto wheels = build_vehicle_chassis_wheels(mode.body, mode.chassis, force);
    assert(tank->chassis.wheels.size() == 10);
    for (std::size_t i = 0; i != 10; ++i) {
        const auto& wheel = tank->chassis.wheels[i];
        assert(std::memcmp(&wheel.construction, &wheels[i], sizeof(wheels[i])) == 0);
        assert(wheel.construction.side == i % 2 && wheel.construction.index == i / 2);
        assert(same(wheel.body.transform, wheel.local_pose));
        assert(wheel.local_pose.rows[3][0] == wheels[i].local_position.x);
        const auto mass = build_vehicle_wheel_mass_properties(wheels[i]);
        assert(wheel.inertia.mass == mass.mass && wheel.body.inverse_mass == mass.inverse_mass);
        assert(wheel.inertia.inertia.rows[0][0] == mass.inertia_diagonal.x);
        assert(wheel.body.solver_id == -1 && same(wheel.body.linear_velocity, {}));
        assert(wheel.rows[0] == -1 && wheel.handles[10].row == -1 && wheel.enabled);
        assert(wheel.side_contact_scale == 1.0f && wheel.forward_contact_scale == 1.0f);
    }
    assert(tank->chassis.tracks.size() == 2);
    assert((tank->chassis.tracks[0] == std::vector<std::uint32_t>{0, 1, 2, 3, 4}));
    assert((tank->chassis.tracks[1] == std::vector<std::uint32_t>{1, 2, 3, 4, 5}));
    assert((tank->components == std::vector<VehicleTankComponent>{
        VehicleTankComponent::anchor, VehicleTankComponent::gimlet,
        VehicleTankComponent::rail, VehicleTankComponent::fake_gearbox}));
    // Globals and fake gearbox curves.
    assert(tank->anchor.velocity_factor == 0.125f && tank->rail.factor_in_contact == 0.75f);
    assert(tank->gimlet.settings.maximum_velocity_scale == 16);
    const auto& curves = tank->fake_gearbox.curves;
    assert(curves.size() == 3 && curves[0].size() == cfg.gears->forward.size() + 1);
    assert(curves[0][0].previous_speed == 0 && curves[0][0].speed == cfg.gears->forward[0].switch_speed);
    assert(curves[0][1].previous_low_rpm == cfg.gears->forward[0].low_rpm);
    assert(curves[0].back().speed == mode.body.simple_forward_max_speed);
    assert(curves[0].back().high_rpm == 1.0f && curves[0].back().hysteresis == 0.0f);
    assert(curves[2].size() == 1 && curves[2][0].speed == mode.engine.rotation_angular_velocity);
    assert(curves[2][0].previous_speed == *mode.engine.simple_min_rpm * 0.10471976f);
    // f8b8d0 shapes at the identity pose.
    const auto geometry = build_vehicle_tank_geometry(mode.body, mode.shape, mode.chassis, math);
    for (int i = 0; i != 2; ++i) {
        const auto topology = build_vehicle_convex_topology(geometry.polyhedra[i], &host_normalize);
        const auto& shape = tank->embedded[i];
        assert(shape.topology.vertices.size() == topology.vertices.size());
        assert(shape.topology.vertex_faces == topology.vertex_faces);
        assert(shape.world_vertices.size() == topology.vertices.size());
        assert(same(shape.world_vertices[0], host_coordinate(topology.vertices[0], vehicle_identity_matrix())));
        assert(same(shape.transform, vehicle_identity_matrix()));
        assert(!(shape.world_bounds.minimum.x > shape.world_bounds.maximum.x));
    }
    assert(tank->collections[0].polyhedra.size() == 1 && tank->collections[1].polyhedra.size() == 1);
    assert(tank->collections[0].boxes.size() == (mode.shape.use_complex_form ? 1u : 0u));
    assert(tank->collections[1].boxes.empty());
    if (mode.shape.use_complex_form) {
        const auto& box = tank->collections[0].boxes[0];
        assert(same(box.center, geometry.auxiliary_box_center));
        assert(same(box.dimensions, geometry.auxiliary_box_dimensions));
        assert(same(box.inverse, vehicle_identity_matrix()));
    }
    // f91170 snapshot and identities.
    assert(same(tank->snapshot.transform, rigid.transform) && tank->snapshot_inertia.mass == hull.mass);
    assert(same(tank->snapshot.linear_velocity, {}) && same(tank->snapshot.loads.force, {}));
    for (const auto& handle : tank->handles) assert(handle.context == 0 && handle.row == -1);
    assert(tank->engine.handles[1].row == -1 && tank->chassis.handle.row == -1);
    for (bool flag : tank->input.flags) assert(!flag);
    // configure does not publish world bounds.
    assert(physics.collision_bounds().minimum.x > physics.collision_bounds().maximum.x);
    assert(physics.hull_com_z() == [&] { float m = 0;
        for (float p : mode.chassis.road_wheel_positions) m += p; return m / 5.0f; }());
}
}

int main() {
    for (unsigned seed = 1; seed != 40; ++seed) {
        const bool complex = seed % 2 == 0;
        const auto cfg = make(seed, complex, seed % 3 ? std::vector<std::int32_t>{0, 1}
                                                     : std::vector<std::int32_t>{1, -1});
        VehiclePhysics physics;
        physics.configure(cfg, math);
        check_fresh(physics, cfg);
        const VehicleTank* first = physics.tank();
        assert(first->settings.size() == 2 && first->settings[1]->siege_mode_available);

        // Tank::setTransform then publication (f91450, f56240, f84be0).
        const VehicleMatrix world = pose(0.3f + 0.01f * seed, {10.0f, 2.0f, -5.0f});
        physics.set_tank_transform(world, math);
        wot::DynamicsSimulator::publish(physics);
        const auto& bounds = physics.collision_bounds();
        const VehicleTank& tank = *physics.tank();
        VehicleCollisionBounds expected{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
        auto add = [&](Vector3 p) {
            expected.minimum = {std::min(expected.minimum.x, p.x), std::min(expected.minimum.y, p.y),
                                std::min(expected.minimum.z, p.z)};
            expected.maximum = {std::max(expected.maximum.x, p.x), std::max(expected.maximum.y, p.y),
                                std::max(expected.maximum.z, p.z)};
        };
        for (const auto& v : tank.embedded[1].topology.vertices) add(host_coordinate(v, world));
        for (const auto& v : tank.collections[0].polyhedra[0].topology.vertices) add(host_coordinate(v, world));
        for (const auto& box : tank.collections[0].boxes) {
            add(box.world_bounds.minimum);
            add(box.world_bounds.maximum);
        }
        const float up[3] = {world.rows[1][0], world.rows[1][1], world.rows[1][2]};
        for (const auto& wheel : tank.chassis.wheels) {
            assert(same(wheel.body.transform, multiply_vehicle_matrices(wheel.local_pose, world)));
            const float lowered = 0.2f + wheel.construction.radius;
            const auto& p = wheel.body.transform.rows[3];
            add({p[0] - up[0] * lowered, p[1] - up[1] * lowered, p[2] - up[2] * lowered});
        }
        assert(same(bounds.minimum, expected.minimum) && same(bounds.maximum, expected.maximum));
        // The inactive embedded/collection shapes keep their identity pose.
        assert(same(tank.embedded[0].transform, vehicle_identity_matrix()));
        assert(same(tank.collections[1].polyhedra[0].transform, vehicle_identity_matrix()));

        // In-place reconfigure keeps pose and velocity, rebuilds the graph and
        // reproduces the original appends.
        physics.apply_impulse_to_com({1.0f, 0.0f, 0.0f});
        const Vector3 velocity = physics.linear_velocity();
        const auto adjacency = tank.embedded[0].topology.vertex_faces;
        const unsigned before = inverse_calls;
        physics.configure(cfg, math);
        assert(physics.configuration_epoch() == 2 && physics.tank() != first);
        const VehicleTank& again = *physics.tank();
        assert(same(physics.rigid_body().transform, world) && same(physics.linear_velocity(), velocity));
        assert(again.components.size() == 8 && again.chassis.wheels.size() == 10);
        assert(again.collections[0].polyhedra.size() == 2 && again.collections[1].polyhedra.size() == 2);
        assert(again.collections[0].boxes.size() == (complex ? 2u : 0u));
        assert(again.chassis.tracks[0].size() == 10 && again.chassis.tracks[1][5] == 1);
        for (std::size_t v = 0; v != adjacency.size(); ++v) {
            auto doubled = adjacency[v];
            doubled.insert(doubled.end(), adjacency[v].begin(), adjacency[v].end());
            assert(again.embedded[0].topology.vertex_faces[v] == doubled);
        }
        for (const auto& wheel : again.chassis.wheels)
            assert(same(wheel.body.transform, multiply_vehicle_matrices(wheel.local_pose, world)));
        assert(same(again.snapshot.transform, world) && same(again.snapshot.linear_velocity, {}));
        assert(inverse_calls - before == 1 + 1 + 10 + (complex ? 2 : 0));

        // Rejected transactions leave the live graph, hull and epoch unchanged.
        VehicleRigidBody body_before;
        std::memcpy(&body_before, &physics.rigid_body(), sizeof(body_before));
        const VehicleTank* live = physics.tank();
        for (int bad = 0; bad != 4; ++bad) {
            auto invalid = make(seed + 100, complex, {0});
            if (bad == 0) invalid.modes.clear();
            if (bad == 1) invalid.modes[0].mode_index = -2;
            if (bad == 2) invalid.vehicle_type = 1;
            if (bad == 3) { invalid.modes[0].mode_index = 3; }
            bool rejected = false;
            try { physics.configure(invalid, math); } catch (const std::invalid_argument&) { rejected = true; }
            if (bad == 0 || bad == 3) {
                // Slot 0 already exists: an empty or sparse mode set is legal
                // here and re-initializes from the retained settings.
                assert(!rejected && physics.tank() != live);
                assert(bad == 0 || (physics.tank()->settings.size() == 4 && !physics.tank()->settings[2]));
                live = physics.tank();
                std::memcpy(&body_before, &physics.rigid_body(), sizeof(body_before));
                continue;
            }
            assert(rejected && physics.tank() == live);
            assert(std::memcmp(&body_before, &physics.rigid_body(), sizeof(body_before)) == 0);
        }

        // A fresh owner rejects a configuration that leaves mode 0 empty.
        VehiclePhysics sparse;
        bool rejected = false;
        try { sparse.configure(make(seed, complex, {1}), math); }
        catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected && !sparse.tank() && sparse.configuration_epoch() == 0);
        rejected = false;
        try { sparse.configure(make(seed, complex, {}), math); }
        catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected && !sparse.tank());
        try { (void)sparse.hull_com_z(); assert(false); } catch (const std::logic_error&) {}
        try { sparse.set_frozen(true); assert(false); } catch (const std::logic_error&) {}
        assert(!sparse.frozen());

        // -1 replaces each owner's current slot only.
        auto current = make(seed + 5, complex, {-1});
        physics.configure(current, math);
        assert(physics.tank()->current_settings().body.full_mass == current.modes[0].body.full_mass);
        assert(physics.tank()->settings[1]->body.full_mass == cfg.modes[1].body.full_mass ||
               cfg.modes[1].mode_index == -1);

        // isFrozen (f55d10).
        physics.apply_impulse_to_com({0.0f, 2.0f, 0.0f});
        physics.set_frozen(true);
        assert(physics.frozen() && same(physics.linear_velocity(), {}));
        physics.set_frozen(false);
        assert(!physics.frozen() && !physics.frozen_during_frame());
    }

    // Caught global regions keep the Tank's previous values (f85c70/f84e80).
    {
        VehiclePhysics physics;
        auto first = make(5, false, {0});
        physics.configure(first, math);
        auto partial = make(6, false, {0});
        partial.anchor.reset();
        partial.rail_factor_in_contact.reset();
        partial.gimlet = {101, 102, 103, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        partial.gimlet_fields = 3;
        partial.gears.reset();
        const auto curves = physics.tank()->fake_gearbox.curves;
        physics.configure(partial, math);
        const VehicleTank& tank = *physics.tank();
        assert(tank.anchor.velocity_factor == 0.125f && tank.rail.factor_in_contact == 0.75f);
        assert(tank.gimlet.settings.pushed_handbrake_weight == 103 && tank.gimlet.settings.push_in_contact == 4);
        assert(tank.fake_gearbox.curves.size() == curves.size() &&
               tank.fake_gearbox.curves[0].size() == curves[0].size() &&
               tank.fake_gearbox.curves[0][0].speed == curves[0][0].speed);
        // A fresh Tank whose gearbox region failed has no curves (f97a60 never ran).
        VehiclePhysics fresh;
        fresh.configure(partial, math);
        assert(fresh.tank()->fake_gearbox.curves.empty());
        assert(fresh.tank()->gimlet.settings.on_spot_fixed_period == 0.2f);
        // Constructor-default engine values when f7ecd0's region failed early.
        auto engine = make(7, false, {0});
        engine.modes[0].engine = VehicleEngineConfiguration{};
        VehiclePhysics defaults;
        defaults.configure(engine, math);
        assert(defaults.tank()->fake_gearbox.curves[2][0].previous_speed == 52.35988f);
        assert(defaults.tank()->engine.inertia.inertia.rows[0][0] == 0.006f);
    }

    // GroundSettings (f968d0 install, f96860 lookup) with the registry.
    {
        VehicleGroundMaterialRegistry registry;
        std::vector<VehicleGroundSettings> table(3);
        VehicleGroundSurfaceConfiguration firm, medium, soft;
        firm.rolling_friction = 1; medium.rolling_friction = 2; soft.rolling_friction = 3;
        assert(install_vehicle_ground_settings(table, 2, firm, medium, soft));
        assert(!install_vehicle_ground_settings(table, 3, firm, medium, soft));
        assert(!install_vehicle_ground_settings(table, -1, firm, medium, soft));
        assert(!install_vehicle_ground_settings(table, -2, firm, medium, soft));
        assert(table[0].firm.rolling_friction == 0.07f && table[2].soft.rolling_friction == 3);
        // Before any setup every matKind maps to group 0.
        assert(&lookup_vehicle_ground_surface(table, registry, 77, 1) == &table[0].firm);
        registry.groups.fill(-1);
        registry.groups[9] = 2;
        registry.groups[10] = 5;
        assert(&lookup_vehicle_ground_surface(table, registry, 9, 1) == &table[2].firm);
        assert(&lookup_vehicle_ground_surface(table, registry, 9, 2) == &table[2].medium);
        assert(&lookup_vehicle_ground_surface(table, registry, 9, 3) == &table[2].soft);
        assert(&lookup_vehicle_ground_surface(table, registry, 9, 0) == &table[2].firm);
        assert(&lookup_vehicle_ground_surface(table, registry, 9, 7) == &table[2].firm);
        assert(&lookup_vehicle_ground_surface(table, registry, 10, 2) == &table[0].medium);
        assert(&lookup_vehicle_ground_surface(table, registry, 11, 3) == &table[0].soft);
        assert(&lookup_vehicle_ground_surface(table, registry, 256, 1) == &table[0].firm);
        assert(&lookup_vehicle_ground_surface(table, registry, -5, 1) == &table[0].firm);
        std::vector<VehicleGroundSettings> empty;
        bool threw = false;
        try { (void)lookup_vehicle_ground_surface(empty, registry, 0, 1); }
        catch (const std::out_of_range&) { threw = true; }
        assert(threw);
        assert(vehicle_ground_material_registry().count == 1);
    }

    // An empty fake gearbox duplicates its terminal point.
    VehiclePhysics empty;
    empty.configure(make(77, false, {0}, 0), math);
    assert(empty.tank()->fake_gearbox.curves[0].size() == 2);
    assert(empty.tank()->fake_gearbox.curves[1][0].speed == empty.tank()->fake_gearbox.curves[1][1].speed);

    // In-place convex rebuild truncates then appends adjacency.
    VehicleConvexPolyhedron owner;
    VehicleTankConvexSource cube{{{-1,-1,-1},{-1,-1,1},{1,-1,1},{1,-1,-1},
                                  {-1,1,-1},{-1,1,1},{1,1,1},{1,1,-1}},
                                 {4,0,1,2,3,4,4,7,6,5,4,0,4,5,1,4,3,2,6,7,4,0,3,7,4,4,1,5,6,2}};
    rebuild_vehicle_convex_polyhedron(owner, cube, math);
    assert((owner.topology.vertex_faces[0] == std::vector<std::uint32_t>{0, 2, 4}));
    VehicleTankConvexSource tetra{{{0,0,0},{1,0,0},{0,1,0},{0,0,1}}, {3,0,2,1, 3,0,1,3, 3,0,3,2, 3,1,2,3}};
    rebuild_vehicle_convex_polyhedron(owner, tetra, math);
    assert(owner.topology.vertex_faces.size() == 4 && owner.world_vertices.size() == 4);
    assert((owner.topology.vertex_faces[0] == std::vector<std::uint32_t>{0, 2, 4, 0, 1, 2}));

    // Repeated construction and teardown of complete graphs.
    for (unsigned i = 0; i != 50; ++i) {
        VehiclePhysics physics;
        physics.configure(make(i, i % 2, {0}), math);
        physics.configure(make(i + 1, i % 2, {0, 1}), math);
    }
}
'''


class VehiclePhysicsTankTests(unittest.TestCase):
    def test_owner_graph_reconfigure_failure_and_publication(self):
        with tempfile.TemporaryDirectory(prefix='vehicle-tank-graph-') as temporary:
            root = Path(temporary)
            (root / 'probe.cpp').write_text(SOURCE)
            command = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                       '-ffp-contract=off', '-fno-fast-math', '-fsanitize=address,undefined',
                       '-fno-sanitize-recover=all', '-I', str(ROOT / 'include'), str(root / 'probe.cpp')]
            command += [str(ROOT / 'src' / name) for name in
                        ['vehicle_physics.cpp', 'vehicle_dynamics.cpp', 'point_velocity.cpp']]
            subprocess.run(command + ['-o', str(root / 'probe')], check=True)
            subprocess.run([str(root / 'probe')], check=True, env=dict(os.environ,
                           ASAN_OPTIONS='detect_leaks=0'))


if __name__ == '__main__':
    unittest.main()
