#include "wot/vehicle_physics.hpp"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <utility>

namespace wot {

VehicleBodyMassProperties build_vehicle_hull_mass_properties(
    const VehicleBodyConfiguration& body,
    const VehicleChassisConfiguration& chassis) {
    const float mass = (1.0f - chassis.chassis_mass_fraction) * body.full_mass;
    const float squared_x = body.hull_size.x * body.hull_size.x;
    const float squared_y = body.hull_size.y * body.hull_size.y;
    const float squared_z = body.hull_size.z * body.hull_size.z;
    // Preserve the original per-axis multiply order, including its float 1/12.
    const Vector3 inertia{
        ((squared_y + squared_z) * body.hull_inertia_factors.x) * mass * 0.083333336f,
        ((squared_x + squared_z) * body.hull_inertia_factors.y) * mass * 0.083333336f,
        ((squared_x + squared_y) * body.hull_inertia_factors.z) * mass * 0.083333336f};
    return {mass, 1.0f / mass, inertia};
}

namespace {
// COMISS/JBE followed by MINSS preserves the input on unordered comparisons.
float shape_clamp(float value, float lower, float upper) {
    if (lower > value) return lower;
    return upper < value ? upper : value;
}
float shape_height(float half_height, float height) {
    return !(half_height > height) ? height : half_height;
}
}

VehicleTankShapeSource build_vehicle_tank_shape_source(
    const VehicleBodyConfiguration& body,
    const VehicleShapeConfiguration& shape,
    const VehicleChassisConfiguration& chassis,
    float terrain_tangent, float tank_tangent) {
    VehicleTankShapeSource result{};
    float mean_wheel_z = 0.0f;
    for (float position : chassis.road_wheel_positions) mean_wheel_z += position;
    mean_wheel_z /= 5.0f; // Chassis::Settings +0x19c is constructed as five.
    result.local_translation = {0.0f, -body.hull_com_shift_y,
                               body.hull_box_offset_z - mean_wheel_z};
    const float hx = body.hull_size.x * 0.5f;
    const float hy = body.hull_size.y * 0.5f;
    const float hz = body.hull_size.z * 0.5f;
    const float top_half_width = body.turret_top_width * 0.5f;
    const float top_y = shape_height(hy, body.turret_top_position.y);
    const float terrain_half_z = hz * (shape.use_complex_form ? 0.8f : 1.0f);
    const float selected_tangent = !(terrain_tangent > tank_tangent) ?
        tank_tangent : terrain_tangent;
    const float top_half_x = hx - selected_tangent *
        (body.turret_top_position.y - result.local_translation.y);
    std::array<Vector3, 4> top{{
        {shape_clamp(body.turret_top_position.x - top_half_width,
                     -top_half_x, top_half_x), top_y,
         shape_clamp(body.turret_top_position.z - top_half_width,
                     -terrain_half_z, terrain_half_z)},
        {shape_clamp(body.turret_top_position.x - top_half_width,
                     -top_half_x, top_half_x), top_y,
         shape_clamp(body.turret_top_position.z + top_half_width,
                     -terrain_half_z, terrain_half_z)},
        {shape_clamp(body.turret_top_position.x + top_half_width,
                     -top_half_x, top_half_x), top_y,
         shape_clamp(body.turret_top_position.z + top_half_width,
                     -terrain_half_z, terrain_half_z)},
        {shape_clamp(body.turret_top_position.x + top_half_width,
                     -top_half_x, top_half_x), top_y,
         shape_clamp(body.turret_top_position.z - top_half_width,
                     -terrain_half_z, terrain_half_z)}
    }};
    const float body_side_offset = hy * terrain_tangent;
    const float body_front_height =
        (shape.terrain_front_chamfer_fraction + shape.terrain_front_chamfer_fraction) * hy;
    const float body_aft_height =
        (shape.terrain_aft_chamfer_fraction + shape.terrain_aft_chamfer_fraction) * hy;
    // f8bd66 adds Chassis::Settings +0x74 (wheelPenetration) and +0x6c.
    const float clearance = (chassis.wheel_penetration + chassis.clearance) *
        shape.auxiliary_clearance;
    const float terrain_side_offset = hy * tank_tangent;
    const float terrain_lower_side = (clearance + hy) * tank_tangent;
    const float total_height = (hy + hy) + clearance;
    float terrain_front_height = shape.tank_front_chamfer_fraction * total_height;
    float terrain_aft_height = shape.tank_aft_chamfer_fraction * total_height;
    float terrain_front_extent = terrain_front_height;
    float terrain_aft_extent = terrain_aft_height;
    if (shape.use_complex_form) {
        terrain_front_extent = terrain_front_height * 4.0f;
        terrain_front_height = terrain_front_height + terrain_front_height;
        terrain_aft_extent = terrain_aft_height * 4.0f;
        terrain_aft_height = terrain_aft_height + terrain_aft_height;
    }
    auto& body_vertices = result.polyhedra[0].vertices;
    body_vertices = {
        {-hx - body_side_offset, -hy, body_aft_height - hz},
        {-hx - body_side_offset, -hy, hz - body_front_height},
        {hx + body_side_offset, -hy, hz - body_front_height},
        {hx + body_side_offset, -hy, body_aft_height - hz},
        {body_side_offset - hx, hy, -hz},
        {body_side_offset - hx, hy, hz},
        {hx - body_side_offset, hy, hz},
        {hx - body_side_offset, hy, -hz},
        {(body_front_height - hy) * terrain_tangent - hx, body_front_height - hy, hz},
        {hx - (body_front_height - hy) * terrain_tangent, body_front_height - hy, hz},
        {(body_aft_height - hy) * terrain_tangent - hx, body_aft_height - hy, -hz},
        {hx - (body_aft_height - hy) * terrain_tangent, body_aft_height - hy, -hz}
    };
    const std::vector<std::uint32_t> base_faces{
        4,0,1,2,3, 4,10,11,7,4, 4,0,3,11,10, 4,8,5,6,9,
        4,1,8,9,2, 6,9,6,7,11,3,2, 6,0,10,4,5,8,1, 4,4,7,6,5};
    result.polyhedra[0].faces = base_faces;
    auto& extended_body = result.polyhedra[1];
    extended_body.vertices = body_vertices;
    extended_body.vertices.insert(extended_body.vertices.end(), top.begin(), top.end());
    extended_body.faces.assign(base_faces.begin(), base_faces.end() - 5);
    const std::array<std::uint32_t, 25> top_faces{{
        4,12,15,14,13, 4,4,7,15,12, 4,7,6,14,15,
        4,5,13,14,6, 4,4,12,13,5}};
    extended_body.faces.insert(extended_body.faces.end(), top_faces.begin(), top_faces.end());
    const float lower_y = -hy - clearance;
    const float lower_left = -hx - terrain_lower_side;
    const float lower_right = hx + terrain_lower_side;
    const float lower_aft = terrain_aft_extent - terrain_half_z;
    const float lower_front = hz - terrain_front_extent;
    const float front_y = (terrain_front_height - hy) - clearance;
    const float aft_y = (terrain_aft_height - hy) - clearance;
    // The original front chamfer's x coordinate uses the extended height,
    // while its y coordinate uses the doubled height in complex mode.
    const float front_side = ((terrain_front_extent - hy) - clearance) * tank_tangent;
    const float aft_side = aft_y * tank_tangent;
    auto& terrain_vertices = result.polyhedra[2].vertices;
    terrain_vertices = {
        {lower_left, lower_y, lower_aft}, {lower_left, lower_y, lower_front},
        {lower_right, lower_y, lower_front}, {lower_right, lower_y, lower_aft},
        {terrain_side_offset - hx, hy, -terrain_half_z},
        {terrain_side_offset - hx, hy, hz},
        {hx - terrain_side_offset, hy, hz},
        {hx - terrain_side_offset, hy, -terrain_half_z},
        {front_side - hx, front_y, hz}, {hx - front_side, front_y, hz},
        {aft_side - hx, aft_y, -terrain_half_z}, {hx - aft_side, aft_y, -terrain_half_z}
    };
    result.polyhedra[2].faces = base_faces;
    auto& extended_terrain = result.polyhedra[3];
    extended_terrain.vertices = terrain_vertices;
    extended_terrain.vertices.insert(extended_terrain.vertices.end(), top.begin(), top.end());
    extended_terrain.faces = extended_body.faces;
    result.has_auxiliary_box = shape.use_complex_form;
    if (result.has_auxiliary_box) {
        result.auxiliary_box_endpoints = {{{hx, 0.0f,
            terrain_half_z - terrain_front_extent * 0.5f},
            {-hx, lower_y, terrain_aft_extent * 0.5f - terrain_half_z}}};
    }
    return result;
}

namespace {
Vector3 convex_subtract(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
float convex_squared_length(Vector3 value) {
    return (value.x * value.x + value.y * value.y) + value.z * value.z;
}
Vector3 convex_cross(Vector3 a, Vector3 b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}
}

VehicleConvexTopology build_vehicle_convex_topology(
    const VehicleTankConvexSource& source, Vector3 (*normalize)(Vector3)) {
    if (!normalize || source.vertices.empty())
        throw std::invalid_argument("Convex construction requires vertices and D3DX normalization");
    VehicleConvexTopology result{};
    std::vector<std::uint32_t> mapping;
    mapping.reserve(source.vertices.size());
    for (const Vector3 vertex : source.vertices) {
        std::uint32_t index = 0;
        for (; index < result.vertices.size(); ++index) {
            if (convex_squared_length(convex_subtract(vertex, result.vertices[index])) < 1e-6f)
                break;
        }
        if (index == result.vertices.size()) result.vertices.push_back(vertex);
        mapping.push_back(index);
    }
    std::size_t offset = 0;
    while (offset != source.faces.size()) {
        const std::uint32_t count = source.faces[offset++];
        if (count == 0 || count > source.faces.size() - offset)
            throw std::invalid_argument("Invalid convex face width");
        for (std::uint32_t i = 0; i != count; ++i) {
            if (source.faces[offset + i] >= source.vertices.size())
                throw std::invalid_argument("Invalid convex face vertex");
        }
        std::array<Vector3, 3> selected{};
        selected[0] = source.vertices[source.faces[offset]];
        std::uint32_t selected_count = 1;
        Vector3 normal{};
        for (std::uint32_t i = 1; i != count; ++i) {
            const Vector3 candidate = source.vertices[source.faces[offset + i]];
            bool coincident = false;
            for (std::uint32_t j = 0; j != selected_count; ++j) {
                if (convex_squared_length(convex_subtract(selected[j], candidate)) < 1e-6f) {
                    coincident = true;
                    break;
                }
            }
            if (coincident) continue;
            selected[selected_count++] = candidate;
            if (selected_count == 3) {
                normal = convex_cross(convex_subtract(selected[2], selected[1]),
                                      convex_subtract(selected[1], selected[0]));
                // Native JBE accepts unordered results at this exact branch.
                if (!(1e-6f > convex_squared_length(normal))) break;
                selected_count = 2;
            }
        }
        if (selected_count == 3) {
            normal = normalize(normal);
            VehicleConvexFace face{};
            face.vertices.push_back(mapping[source.faces[offset]]);
            for (std::uint32_t i = 1; i != count; ++i) {
                const std::uint32_t index = mapping[source.faces[offset + i]];
                if (face.vertices.back() != index) face.vertices.push_back(index);
            }
            face.normal_index = 0;
            for (; face.normal_index < result.normal_directions.size(); ++face.normal_index) {
                const Vector3 existing = result.normal_directions[face.normal_index];
                if (convex_squared_length(convex_cross(existing, normal)) < 1e-6f) break;
            }
            if (face.normal_index == result.normal_directions.size()) {
                result.normal_directions.push_back(normal);
                face.normal_scale = 1.0f;
            } else {
                const Vector3 existing = result.normal_directions[face.normal_index];
                face.normal_scale = (existing.y * normal.y + existing.x * normal.x) +
                    existing.z * normal.z;
            }
            result.faces.push_back(std::move(face));
            for (std::uint32_t i = 0; i != count; ++i) {
                const Vector3 edge = convex_subtract(
                    source.vertices[source.faces[offset + (i + 1) % count]],
                    source.vertices[source.faces[offset + i]]);
                if (1e-6f > convex_squared_length(edge)) continue;
                bool parallel = false;
                for (const Vector3 existing : result.edge_directions) {
                    if (convex_squared_length(convex_cross(existing, edge)) < 1e-6f) {
                        parallel = true;
                        break;
                    }
                }
                if (!parallel) result.edge_directions.push_back(normalize(edge));
            }
        }
        offset += count;
    }
    result.vertex_faces.resize(result.vertices.size());
    for (std::uint32_t i = 0; i < result.faces.size(); ++i) {
        for (const std::uint32_t vertex : result.faces[i].vertices)
            result.vertex_faces[vertex].push_back(i);
    }
    result.local_bounds.minimum = result.local_bounds.maximum = result.vertices[0];
    for (std::size_t i = 1; i != result.vertices.size(); ++i) {
        const Vector3 vertex = result.vertices[i];
        auto& lo = result.local_bounds.minimum;
        auto& hi = result.local_bounds.maximum;
        if (vertex.x < lo.x) lo.x = vertex.x;
        if (vertex.y < lo.y) lo.y = vertex.y;
        if (vertex.z < lo.z) lo.z = vertex.z;
        if (hi.x < vertex.x) hi.x = vertex.x;
        if (hi.y < vertex.y) hi.y = vertex.y;
        if (hi.z < vertex.z) hi.z = vertex.z;
    }
    return result;
}

float vehicle_wheel_equilibrium_deflection(
    const VehicleChassisConfiguration& chassis, std::uint32_t index,
    float chassis_vertical_force, float normalized_load) {
    const float load = chassis_vertical_force * normalized_load;
    const float linear = chassis.stiffness_zero * chassis.stiffness_factors.at(index);
    const float quadratic = chassis.stiffness_one * chassis.stiffness_factors.at(index);
    if (chassis.stiffness_one > 0.01f) {
        const float root = std::sqrt((quadratic * 4.0f) * load + linear * linear);
        return (root - linear) * (0.5f / quadratic);
    }
    return load / linear;
}

std::array<VehicleWheelConstruction, 10> build_vehicle_chassis_wheels(
    const VehicleBodyConfiguration& body,
    const VehicleChassisConfiguration& chassis, float chassis_vertical_force) {
    std::array<VehicleWheelConstruction, 10> wheels{};
    const float half_gauge = (body.hull_size.x * chassis.track_gauge_factor) * 0.5f;
    const float local_y = body.hull_size.y * -0.5f - body.hull_com_shift_y;
    float mean_z = 0.0f;
    for (float position : chassis.road_wheel_positions) mean_z += position;
    mean_z /= 5.0f;
    // The constructor's five +0x40 weights are each 1; f837f0 returns twice
    // their sum. configure does not replace these fields with stiffnessFactors.
    float load_sum = 0.0f;
    for (std::uint32_t i = 0; i != 5; ++i) load_sum += 1.0f;
    const float normalized_load = 1.0f / (load_sum + load_sum);
    const float wheel_mass = chassis.chassis_mass / 10.0f;
    for (std::uint32_t i = 0; i != 5; ++i) {
        const float z = chassis.road_wheel_positions[i] - mean_z;
        const float deflection = vehicle_wheel_equilibrium_deflection(
            chassis, i, chassis_vertical_force, normalized_load);
        for (std::uint32_t side = 0; side != 2; ++side) {
            wheels[i * 2 + side] = {
                {side == 0 ? -half_gauge : half_gauge, local_y, z},
                side, i, 10.0f, chassis.stiffness_factors[i], deflection,
                normalized_load, chassis.wheel_radius, wheel_mass,
                chassis.wheel_forward_inertia_factor, chassis.wheel_inertia_factor};
        }
    }
    return wheels;
}

VehicleBodyMassProperties build_vehicle_wheel_mass_properties(
    const VehicleWheelConstruction& wheel) {
    const float sideways =
        ((wheel.mass * wheel.forward_inertia_factor) * wheel.radius) * wheel.radius * 0.25f;
    const float forward =
        ((wheel.mass * wheel.inertia_factor) * wheel.radius) * wheel.radius * 0.5f;
    return {wheel.mass, 1.0f / wheel.mass, {forward, sideways, sideways}};
}

VehicleMatrix multiply_vehicle_matrices(const VehicleMatrix& left,
                                        const VehicleMatrix& right) {
    VehicleMatrix result;
    for (int row = 0; row != 4; ++row) {
        const float* a = left.rows[row];
        for (int column = 0; column != 4; ++column) {
            const float high = a[3] * right.rows[3][column] + a[2] * right.rows[2][column];
            const float low = a[1] * right.rows[1][column] + a[0] * right.rows[0][column];
            result.rows[row][column] = high + low;
        }
    }
    return result;
}

float vehicle_engine_minimum_angular_velocity(const VehicleEngineConfiguration& engine) {
    // f90a3b constructor value 0x42517084 unless f7ecd0 read smplMinRPM.
    return engine.simple_min_rpm ? *engine.simple_min_rpm * 0.10471976f : 52.35988f;
}

float vehicle_engine_start_angular_velocity(const VehicleEngineConfiguration& engine) {
    // Constructor value 0x431d1463 unless f7ecd0 read startRPM.
    return engine.start_rpm ? *engine.start_rpm * 0.10471976f : 157.07964f;
}

const std::array<float VehicleGimletConfiguration::*, 16> vehicle_gimlet_fields{{
    &VehicleGimletConfiguration::pushed_diagonal_weight, &VehicleGimletConfiguration::pushed_rotation_weight,
    &VehicleGimletConfiguration::pushed_handbrake_weight, &VehicleGimletConfiguration::push_in_contact,
    &VehicleGimletConfiguration::push_handbrake, &VehicleGimletConfiguration::push_stop,
    &VehicleGimletConfiguration::on_spot_fixed_period, &VehicleGimletConfiguration::on_spot_grow_period,
    &VehicleGimletConfiguration::on_spot_initial_push, &VehicleGimletConfiguration::on_spot_final_push,
    &VehicleGimletConfiguration::on_move_fixed_period, &VehicleGimletConfiguration::on_move_grow_period,
    &VehicleGimletConfiguration::on_move_initial_push, &VehicleGimletConfiguration::on_move_final_push,
    &VehicleGimletConfiguration::minimum_velocity_scale, &VehicleGimletConfiguration::maximum_velocity_scale}};

VehicleGroundMaterialRegistry& vehicle_ground_material_registry() {
    static VehicleGroundMaterialRegistry registry;
    return registry;
}

bool install_vehicle_ground_settings(std::vector<VehicleGroundSettings>& table,
    std::int32_t group, const VehicleGroundSurfaceConfiguration& firm,
    const VehicleGroundSurfaceConfiguration& medium,
    const VehicleGroundSurfaceConfiguration& soft) {
    // f968f5 compares unsigned, so every negative group is out of range.
    const auto index = static_cast<std::uint32_t>(group);
    if (index >= table.size()) return false;
    table[index].firm = firm;
    table[index].medium = medium;
    table[index].soft = soft;
    return true;
}

const VehicleGroundSurfaceConfiguration& lookup_vehicle_ground_surface(
    const std::vector<VehicleGroundSettings>& table,
    const VehicleGroundMaterialRegistry& registry,
    std::int32_t material_kind, std::int32_t strength) {
    std::int32_t group = static_cast<std::uint32_t>(material_kind) > 0xff ? -1 :
        registry.groups[static_cast<std::size_t>(material_kind)];
    if (group < 0 || static_cast<std::size_t>(group) >= table.size()) group = 0;
    // The original reads entry 0 of an empty table out of bounds.
    if (table.empty()) throw std::out_of_range("GroundSettings table is empty");
    const auto& entry = table[static_cast<std::size_t>(group)];
    return strength == 2 ? entry.medium : strength == 3 ? entry.soft : entry.firm;
}

VehicleRigidBody make_vehicle_rigid_body() {
    // #1513 0x00f6f800. D3DX inverts the identity inertia; mass is one.
    VehicleRigidBody body;
    body.inverse_mass = 1.0f;
    body.inverse_body_inertia = vehicle_identity_matrix();
    body.inverse_world_inertia = vehicle_identity_matrix();
    body.transform = vehicle_identity_matrix();
    body.inverse_transform = vehicle_identity_matrix();
    body.last_step = 0.05f;
    body.solver_id = -1;
    body.orthonormalization_countdown = static_cast<std::uint32_t>(std::rand() % 100);
    body.record_history = true;
    body.interpolation.transforms.fill(body.transform);
    return body;
}

namespace {
VehicleMatrix translation_matrix(Vector3 value) {
    VehicleMatrix result = vehicle_identity_matrix();
    result.rows[3][0] = value.x;
    result.rows[3][1] = value.y;
    result.rows[3][2] = value.z;
    return result;
}

VehicleMatrix scaling_matrix(Vector3 value) {
    VehicleMatrix result = vehicle_identity_matrix();
    result.rows[0][0] = value.x;
    result.rows[1][1] = value.y;
    result.rows[2][2] = value.z;
    return result;
}

float mean_road_wheel_position(const VehicleChassisConfiguration& chassis) {
    // f82b30 over the five positions counted at Chassis::Settings +0x19c.
    float mean = 0.0f;
    for (float position : chassis.road_wheel_positions) mean += position;
    return mean / 5.0f;
}

bool invalid_bounds(const VehicleCollisionBounds& bounds) {
    // COMISS/JA: unordered comparisons do not invalidate a box.
    return bounds.minimum.x > bounds.maximum.x || bounds.minimum.y > bounds.maximum.y ||
           bounds.minimum.z > bounds.maximum.z;
}

void union_bounds(VehicleCollisionBounds& output, const VehicleCollisionBounds& value) {
    // f6c180/f84be0 skip an invalid input and replace an invalid output.
    if (invalid_bounds(value)) return;
    if (invalid_bounds(output)) {
        output = value;
        return;
    }
    if (output.minimum.x > value.minimum.x) output.minimum.x = value.minimum.x;
    if (output.minimum.y > value.minimum.y) output.minimum.y = value.minimum.y;
    if (output.minimum.z > value.minimum.z) output.minimum.z = value.minimum.z;
    if (value.maximum.x > output.maximum.x) output.maximum.x = value.maximum.x;
    if (value.maximum.y > output.maximum.y) output.maximum.y = value.maximum.y;
    if (value.maximum.z > output.maximum.z) output.maximum.z = value.maximum.z;
}

void union_point(VehicleCollisionBounds& output, Vector3 value) {
    union_bounds(output, {value, value});
}

void invert_inertia(VehicleRigidBody& body, VehicleBodyInertia& owner,
                    Vector3 diagonal, const VehicleNativeMath& math) {
    // D3DXMatrixScaling, D3DXMatrixInverse, then two f705c0 products using
    // the body's current pose. The inverse's failure result is not checked.
    owner.inertia = scaling_matrix(diagonal);
    math.inverse(body.inverse_body_inertia, owner.inertia);
    body.inverse_world_inertia = body.inverse_body_inertia;
    multiply_vehicle_rotation_rows(body.inverse_world_inertia, body.inverse_transform,
                                   body.inverse_world_inertia);
    multiply_vehicle_rotation_rows(body.inverse_world_inertia, body.inverse_world_inertia,
                                   body.transform);
}

void reset_engine_speed(VehicleEngine& engine, float speed) {
    // #1513 0x00f560d0 stores the engine speed in the body's linear x.
    engine.value_304 = 0.0f;
    engine.body.linear_velocity = {speed, 0.0f, 0.0f};
    engine.body.interpolation.linear_velocities.fill(engine.body.linear_velocity);
}

std::int32_t native_direction(std::uint32_t signals) {
    return (signals & 4) ? -1 : static_cast<std::int32_t>((signals >> 3) & 1);
}

void set_tank_input(VehicleTank& tank, std::uint32_t signals) {
    // #1513 Tank::setInput 0x00f85a30.
    auto& input = tank.input;
    if (input.signals != signals) {
        const float start = vehicle_engine_start_angular_velocity(tank.current_engine());
        if (start > tank.engine.body.linear_velocity.x)
            reset_engine_speed(tank.engine, start);
    }
    if (native_direction(signals) != native_direction(input.signals))
        input.rotation_time = 0.0f;
    input.signals = signals;
    auto& flag = input.flags;
    flag[7] = (signals & 1) != 0;      // +0x1b
    flag[8] = (signals & 2) != 0;      // +0x1c
    flag[9] = (signals & 3) != 0;      // +0x1d
    const std::uint32_t masked = input.mask_steering ? (signals & 0xc) : signals;
    const bool turning = (masked & 0xc) != 0;
    const bool moving = (masked & 3) != 0;
    flag[0] = (masked & 4) != 0;       // +0x14
    flag[1] = (masked & 8) != 0;       // +0x15
    flag[2] = turning;                 // +0x16
    flag[3] = (masked & 1) != 0;       // +0x17
    flag[4] = (masked & 2) != 0;       // +0x18
    flag[5] = moving;                  // +0x19
    flag[6] = !moving && turning;      // +0x1a
    flag[10] = !flag[9] && turning;    // +0x1e
    flag[11] = flag[9] && !turning;    // +0x1f
    flag[12] = moving && !turning;     // +0x20
}

void install_fake_gearbox(VehicleTankFakeGearBox& gearbox, const VehicleTank& tank,
                          const VehicleFakeGearboxConfiguration& gears) {
    const auto& forward = gears.forward;
    const auto& backward = gears.backward;
    // #1513 FakeGearBox::install 0x00f97a60. Each curve carries the previous
    // gear's speed and low RPM; a single terminal point is duplicated.
    const auto& body = tank.current_settings().body;
    const auto& engine = tank.current_engine();
    gearbox.curves.resize(3);
    const auto build = [](std::vector<VehicleFakeGearPoint>& curve,
                          const std::vector<VehicleFakeGearConfiguration>& gears,
                          float maximum_speed) {
        curve.clear();
        curve.reserve(gears.size() + 1);
        float previous_speed = 0.0f;
        float previous_low = 0.0f;
        for (const auto& gear : gears) {
            curve.push_back({previous_speed, gear.switch_speed, previous_low,
                             gear.high_rpm, gear.switch_hysteresis});
            previous_speed = gear.switch_speed;
            previous_low = gear.low_rpm;
        }
        const VehicleFakeGearPoint last{previous_speed, maximum_speed, previous_low, 1.0f, 0.0f};
        curve.push_back(last);
        if (curve.size() < 2) curve.push_back(last);
    };
    build(gearbox.curves[0], forward, body.simple_forward_max_speed);
    build(gearbox.curves[1], backward, body.simple_backward_max_speed);
    gearbox.curves[2].clear();
    gearbox.curves[2].push_back({vehicle_engine_minimum_angular_velocity(engine),
                                 engine.rotation_angular_velocity, 0.0f, 1.0f, 0.0f});
}

template<class T>
void install_mode(std::vector<std::optional<T>>& settings, std::int32_t current,
                  std::int32_t index, const T& value) {
    // f900a0/f90a3b/f90d6c: -1 selects the owner's current index; the slot
    // vector grows to index + 1 and replaces only that shared_ptr slot.
    const std::size_t slot = static_cast<std::size_t>(index == -1 ? current : index);
    if (settings.size() <= slot) settings.resize(slot + 1);
    settings[slot] = value;
}

template<class T>
bool installed_after(const std::vector<std::optional<T>>& settings, std::int32_t current,
                     const VehiclePhysicsConfiguration& configuration) {
    if (current < 0) return false;
    for (const auto& mode : configuration.modes) {
        const std::int32_t index = mode.mode_index == -1 ? current : mode.mode_index;
        if (index == current) return true;
    }
    return static_cast<std::size_t>(current) < settings.size() &&
           settings[static_cast<std::size_t>(current)].has_value();
}

template<class T>
const T& current_slot(const std::vector<std::optional<T>>& settings, std::int32_t current) {
    return *settings.at(static_cast<std::size_t>(current));
}

void initialize_polyhedron(VehicleConvexPolyhedron& owner, const VehicleNativeMath& math) {
    // f6d270 ends with f55e40 at DAT_01b5b390, the identity matrix.
    transform_vehicle_convex_polyhedron(owner, vehicle_identity_matrix(), math);
}
} // namespace

const VehicleTankModeSettings& VehicleTank::current_settings() const {
    return current_slot(settings, mode);
}

const VehicleChassisConfiguration& VehicleTank::current_chassis() const {
    return current_slot(chassis.settings, chassis.mode);
}

const VehicleEngineConfiguration& VehicleTank::current_engine() const {
    return current_slot(engine.settings, engine.mode);
}

VehicleTankGeometry build_vehicle_tank_geometry(
    const VehicleBodyConfiguration& body, const VehicleShapeConfiguration& shape,
    const VehicleChassisConfiguration& chassis, const VehicleNativeMath& math) {
    // f8b8d0 calls tankBoardAngle first, then terrBoardAngle. The embedded
    // shapes use the terrain fields; do not infer them from owner names.
    const float tank_tangent = math.tangent(shape.tank_board_angle * 0.017453292f);
    const float terrain_tangent = math.tangent(shape.terrain_board_angle * 0.017453292f);
    const VehicleTankShapeSource source = build_vehicle_tank_shape_source(
        body, shape, chassis, terrain_tangent, tank_tangent);
    const VehicleMatrix local = translation_matrix(source.local_translation);
    VehicleTankGeometry geometry{};
    std::vector<Vector3> translated;
    for (std::size_t index = 0; index != source.polyhedra.size(); ++index) {
        const auto& input = source.polyhedra[index];
        if ((index & 1) == 0) translated.clear();
        // The extended variant appends four top vertices to the same twelve
        // translated base vertices, matching the original SDK call order.
        for (std::size_t vertex = translated.size(); vertex != input.vertices.size(); ++vertex)
            translated.push_back(math.transform_coordinate(input.vertices[vertex], local));
        geometry.polyhedra[index] = {translated, input.faces};
    }
    geometry.has_auxiliary_box = source.has_auxiliary_box;
    if (geometry.has_auxiliary_box) {
        const Vector3 a = math.transform_coordinate(source.auxiliary_box_endpoints[0], local);
        const Vector3 b = math.transform_coordinate(source.auxiliary_box_endpoints[1], local);
        geometry.auxiliary_box_center = {
            (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f};
        geometry.auxiliary_box_dimensions = {a.x - b.x, a.y - b.y, a.z - b.z};
    }
    return geometry;
}

void rebuild_vehicle_convex_polyhedron(VehicleConvexPolyhedron& owner,
    const VehicleTankConvexSource& source, const VehicleNativeMath& math) {
    VehicleConvexTopology topology = build_vehicle_convex_topology(source, math.normalize);
    // f65260 resizes the adjacency vector without clearing retained entries.
    auto adjacency = std::move(owner.topology.vertex_faces);
    adjacency.resize(topology.vertices.size());
    for (std::size_t vertex = 0; vertex != adjacency.size(); ++vertex)
        adjacency[vertex].insert(adjacency[vertex].end(),
            topology.vertex_faces[vertex].begin(), topology.vertex_faces[vertex].end());
    topology.vertex_faces = std::move(adjacency);
    owner.topology = std::move(topology);
    const auto& bounds = owner.topology.local_bounds;
    owner.local_box = scaling_matrix({bounds.maximum.x - bounds.minimum.x,
                                      bounds.maximum.y - bounds.minimum.y,
                                      bounds.maximum.z - bounds.minimum.z});
    owner.local_box.rows[3][0] = bounds.minimum.x;
    owner.local_box.rows[3][1] = bounds.minimum.y;
    owner.local_box.rows[3][2] = bounds.minimum.z;
    owner.world_vertices.resize(owner.topology.vertices.size());
    owner.world_edges.resize(owner.topology.edge_directions.size());
    owner.world_normals.resize(owner.topology.normal_directions.size());
    initialize_polyhedron(owner, math);
}

void transform_vehicle_convex_polyhedron(VehicleConvexPolyhedron& owner,
    const VehicleMatrix& transform, const VehicleNativeMath& math) {
    owner.transform = transform;
    const auto& topology = owner.topology;
    for (std::size_t i = 0; i != topology.vertices.size(); ++i)
        owner.world_vertices[i] = math.transform_coordinate(topology.vertices[i], transform);
    for (std::size_t i = 0; i != topology.normal_directions.size(); ++i)
        owner.world_normals[i] = math.transform_normal(topology.normal_directions[i], transform);
    for (std::size_t i = 0; i != topology.edge_directions.size(); ++i)
        owner.world_edges[i] = math.transform_normal(topology.edge_directions[i], transform);
    // COMISS/JBE: a new minimum skips the maximum test; NaNs change neither.
    Vector3 lo = owner.world_vertices.at(0);
    Vector3 hi = lo;
    for (std::size_t i = 1; i < owner.world_vertices.size(); ++i) {
        const Vector3 v = owner.world_vertices[i];
        if (lo.x > v.x) lo.x = v.x; else if (v.x > hi.x) hi.x = v.x;
        if (lo.y > v.y) lo.y = v.y; else if (v.y > hi.y) hi.y = v.y;
        if (lo.z > v.z) lo.z = v.z; else if (v.z > hi.z) hi.z = v.z;
    }
    owner.world_bounds = {lo, hi};
    owner.world_box = multiply_vehicle_matrices(owner.local_box, transform);
}

void refresh_vehicle_convex_box(VehicleConvexBox& box, const VehicleNativeMath& math) {
    // #1513 0x00933220, preserving its absolute-value summation order.
    const float scales[3] = {box.dimensions.x * 0.5f, box.dimensions.y * 0.5f,
                             box.dimensions.z * 0.5f};
    for (int axis = 0; axis != 3; ++axis) {
        const float* row = box.transform.rows[axis];
        box.axes[axis] = {row[0], row[1], row[2]};
        box.half_axes[axis] = {row[0] * scales[axis], row[1] * scales[axis],
                               row[2] * scales[axis]};
    }
    box.world_center = math.transform_coordinate(box.center, box.transform);
    const auto& h = box.half_axes;
    const Vector3 extent{
        (std::fabs(h[0].x) + std::fabs(h[1].x)) + std::fabs(h[2].x),
        (std::fabs(h[1].y) + std::fabs(h[0].y)) + std::fabs(h[2].y),
        (std::fabs(h[1].z) + std::fabs(h[0].z)) + std::fabs(h[2].z)};
    const Vector3 c = box.world_center;
    box.world_bounds = {{c.x - extent.x, c.y - extent.y, c.z - extent.z},
                        {c.x + extent.x, c.y + extent.y, c.z + extent.z}};
    for (int axis = 0; axis != 3; ++axis) {
        const Vector3 a = box.axes[axis];
        const Vector3 half = h[axis];
        const float distance = (c.y * a.y + a.x * c.x) + c.z * a.z;
        const float radius = (half.y * a.y + a.x * half.x) + half.z * a.z;
        box.projections[axis] = {{distance - radius, distance + radius}};
    }
}

namespace {
void rebuild_tank_shape(VehicleTank& tank, const VehicleNativeMath& math) {
    // Tank vtable +0x38, 0x00f8b8d0.
    const auto& settings = tank.current_settings();
    const VehicleTankGeometry geometry = build_vehicle_tank_geometry(
        settings.body, settings.shape, tank.current_chassis(), math);
    rebuild_vehicle_convex_polyhedron(tank.embedded[0], geometry.polyhedra[0], math);
    rebuild_vehicle_convex_polyhedron(tank.embedded[1], geometry.polyhedra[1], math);
    tank.collections[1].polyhedra.emplace_back();
    rebuild_vehicle_convex_polyhedron(tank.collections[1].polyhedra.back(),
                                      geometry.polyhedra[2], math);
    tank.collections[0].polyhedra.emplace_back();
    rebuild_vehicle_convex_polyhedron(tank.collections[0].polyhedra.back(),
                                      geometry.polyhedra[3], math);
    if (!settings.shape.use_complex_form) return;
    // f65930 constructs and refreshes an identity box before f8b8d0 sets it.
    tank.collections[0].boxes.emplace_back();
    auto& box = tank.collections[0].boxes.back();
    math.inverse(box.inverse, box.transform);
    refresh_vehicle_convex_box(box, math);
    box.center = geometry.auxiliary_box_center;
    box.dimensions = geometry.auxiliary_box_dimensions;
    box.transform = vehicle_identity_matrix();
    math.inverse(box.inverse, box.transform);
    refresh_vehicle_convex_box(box, math);
}

void construct_chassis_wheels(VehicleTank& tank, const VehicleRigidBody& hull,
                              const VehicleNativeMath& math) {
    // TrackChassis vtable +4, 0x00f87e90 -> f866f0 -> f7f280.
    auto& chassis = tank.chassis;
    chassis.wheels.clear();
    const auto constructions = build_vehicle_chassis_wheels(
        tank.current_settings().body, tank.current_chassis(), chassis.vertical_force);
    chassis.tracks.resize(2);
    for (auto& track : chassis.tracks)
        if (track.capacity() < 5) track.reserve(5);
    chassis.wheels.reserve(constructions.size());
    for (std::uint32_t index = 0; index != 5; ++index) {
        for (std::uint32_t side = 0; side != 2; ++side) {
            const auto& construction = constructions[index * 2 + side];
            chassis.wheels.emplace_back();
            auto& wheel = chassis.wheels.back();
            wheel.construction = construction;
            wheel.body = make_vehicle_rigid_body();
            wheel.local_pose = translation_matrix(construction.local_position);
            install_vehicle_rigid_body_transform(
                wheel.body, multiply_vehicle_matrices(wheel.local_pose, hull.transform));
            const auto mass = build_vehicle_wheel_mass_properties(construction);
            wheel.inertia.mass = mass.mass;
            wheel.body.inverse_mass = mass.inverse_mass;
            invert_inertia(wheel.body, wheel.inertia, mass.inertia_diagonal, math);
            set_vehicle_rigid_body_velocity(wheel.body, {}, {});
        }
        chassis.tracks[0].push_back(index);
        chassis.tracks[1].push_back(index + 1);
    }
}

void initialize_tank(VehicleTank& tank, VehicleRigidBody& hull, const VehicleNativeMath& math) {
    // Tank vtable +8, 0x00f8b8a0 -> f84370.
    const auto hull_mass = build_vehicle_hull_mass_properties(
        tank.current_settings().body, tank.current_chassis());
    tank.hull.mass = hull_mass.mass;
    hull.inverse_mass = hull_mass.inverse_mass;
    invert_inertia(hull, tank.hull, hull_mass.inertia_diagonal, math);
    // Engine::initialize 0x00f7ea60.
    auto& engine = tank.engine;
    if (engine.flag_308) {
        engine.flag_308 = false;
        reset_engine_speed(engine, 0.0f);
    }
    engine.inertia.mass = 1.0f;
    engine.body.inverse_mass = 1.0f;
    const float engine_inertia = tank.current_engine().engine_inertia;
    invert_inertia(engine.body, engine.inertia,
                   {engine_inertia, engine_inertia, engine_inertia}, math);
    construct_chassis_wheels(tank, hull, math);
    set_tank_input(tank, 0);
    tank.input.value_24 = 0;
    for (const auto component : {VehicleTankComponent::anchor, VehicleTankComponent::gimlet,
                                 VehicleTankComponent::rail, VehicleTankComponent::fake_gearbox})
        tank.components.push_back(component);
    rebuild_tank_shape(tank, math);
    // f91170 copies the hull into the +0x740 snapshot without motion.
    tank.snapshot_inertia = tank.hull;
    tank.snapshot = hull;
    clear_vehicle_loads(tank.snapshot);
    set_vehicle_rigid_body_velocity(tank.snapshot, {}, {});
}
} // namespace

void VehiclePhysics::configure(const VehiclePhysicsConfiguration& configuration,
                               const VehicleNativeMath& math) {
    if (configuration.vehicle_type != 0)
        throw std::invalid_argument("WGVehiclePhysics.configure requires Tank vehicleType 0");
    if (configuration.gimlet_fields > 16)
        throw std::invalid_argument("Gimlet has sixteen fields");
    for (const auto& mode : configuration.modes) {
        if (mode.mode_index < -1)
            throw std::invalid_argument("mode_index below -1 has no native slot");
    }
    // Validate the post-install current slots that f84e80/f8b8a0 dereference.
    static const VehicleTank initial{};
    const VehicleTank& live = tank_ ? *tank_ : initial;
    if (!installed_after(live.settings, live.mode, configuration) ||
        !installed_after(live.chassis.settings, live.chassis.mode, configuration) ||
        !installed_after(live.engine.settings, live.engine.mode, configuration))
        throw std::invalid_argument("configure leaves the current mode without settings");

    // Stage every allocation and calculation before touching live state.
    VehicleRigidBody hull;
    std::unique_ptr<VehicleTank> next;
    if (tank_) {
        hull = body_;
        next = std::make_unique<VehicleTank>(*tank_);
    } else {
        // Original construction order: hull (Vehicle), snapshot (Tank), engine.
        hull = make_vehicle_rigid_body();
        next = std::make_unique<VehicleTank>();
        next->snapshot = make_vehicle_rigid_body();
        next->engine.body = make_vehicle_rigid_body();
    }
    VehicleTank& tank = *next;
    for (const auto& mode : configuration.modes) {
        install_mode(tank.settings, tank.mode, mode.mode_index,
                     VehicleTankModeSettings{mode.body, mode.shape, mode.siege_mode_available});
        install_mode(tank.chassis.settings, tank.chassis.mode, mode.mode_index, mode.chassis);
        install_mode(tank.engine.settings, tank.engine.mode, mode.mode_index, mode.engine);
    }
    // f85c70: modes.normal anchor, rail and gimlet. A caught failure keeps
    // the Tank's previous values for everything not yet installed.
    if (configuration.anchor) {
        tank.anchor.velocity_factor = configuration.anchor->velocity_factor;
        tank.anchor.constant_fraction = configuration.anchor->constant_fraction;
        tank.anchor.maximum_reaction_factor = configuration.anchor->maximum_reaction_factor;
    }
    if (configuration.rail_factor_in_contact)
        tank.rail.factor_in_contact = *configuration.rail_factor_in_contact;
    for (std::uint32_t field = 0; field != configuration.gimlet_fields; ++field)
        tank.gimlet.settings.*vehicle_gimlet_fields[field] =
            configuration.gimlet.*vehicle_gimlet_fields[field];
    // f84e80: fake gearbox (all or nothing), then the load used by f82200.
    if (configuration.gears)
        install_fake_gearbox(tank.fake_gearbox, tank, *configuration.gears);
    const auto& settings = tank.current_settings().body;
    tank.chassis.vertical_force = ((1.0f - tank.current_chassis().chassis_mass_fraction) *
                                   settings.full_mass) * settings.gravity;
    initialize_tank(tank, hull, math);

    // Commit: moves only.
    body_ = std::move(hull);
    tank_ = std::move(next);
    ++configuration_epoch_;
}

float VehiclePhysics::hull_com_z() const {
    if (!tank_) throw std::logic_error("WGVehiclePhysics is not configured");
    return mean_road_wheel_position(tank_->current_chassis());
}

void VehiclePhysics::set_tank_transform(const VehicleMatrix& transform,
                                        const VehicleNativeMath& math) {
    if (!tank_) throw std::logic_error("WGVehiclePhysics is not configured");
    VehicleTank& tank = *tank_;
    install_vehicle_rigid_body_transform(body_, transform);
    transform_vehicle_convex_polyhedron(tank.embedded[tank.turret_collision ? 1 : 0],
                                        transform, math);
    auto& collection = tank.collections[tank.turret_collision ? 0 : 1];
    for (auto& box : collection.boxes) {
        // 0x00f6d1f0: replace the pose, D3DX-invert it, refresh the box.
        box.transform = transform;
        math.inverse(box.inverse, transform);
        refresh_vehicle_convex_box(box, math);
    }
    for (auto& polyhedron : collection.polyhedra)
        transform_vehicle_convex_polyhedron(polyhedron, transform, math);
    for (auto& wheel : tank.chassis.wheels)
        install_vehicle_rigid_body_transform(
            wheel.body, multiply_vehicle_matrices(wheel.local_pose, transform));
    tank.snapshot_inertia = tank.hull;
    tank.snapshot = body_;
    clear_vehicle_loads(tank.snapshot);
    set_vehicle_rigid_body_velocity(tank.snapshot, {}, {});
}

void VehiclePhysics::set_frozen(bool value) {
    if (frozen_ == value) return;
    if (value) {
        if (!allow_freeze_) return;
        // The original zeroes Tank +0x2d8..+0x2ec, the hull velocities.
        if (!tank_) throw std::logic_error("WGVehiclePhysics is not configured");
        body_.linear_velocity = {};
        body_.angular_velocity = {};
    } else {
        frozen_during_frame_ = false;
    }
    frozen_ = value;
}

VehiclePhysics::VehiclePhysics() : entity_transform_(vehicle_identity_matrix()) {
    body_.transform = vehicle_identity_matrix();
    body_.inverse_transform = vehicle_identity_matrix();
}

void VehiclePhysics::clear_damper_springs() {
    // #1513 0x00f4a82f..0x00f4a838 clears both vectors without rebuilding
    // the vehicle or evaluating a suspension law.
    damper_springs_[0].clear();
    damper_springs_[1].clear();
}

void VehiclePhysics::add_damper_spring(VehicleDamperSpring spring) {
    // #1513 0x00f49cdd..0x00f49d47: preserve the supplied record and append
    // it to the isLeft-selected vector. There is no native length clamp here.
    damper_springs_[spring.left ? 0 : 1].push_back(spring);
}

const std::vector<VehicleDamperSpring>& VehiclePhysics::damper_springs(bool left) const {
    return damper_springs_[left ? 0 : 1];
}

bool VehiclePhysics::set_damper_springs_lengths(const std::vector<float>& left,
                                               const std::vector<float>& right) {
    // #1513 0x00f4acad..0x00f4ad9a validates both counts before any writes.
    if (left.size() != damper_springs_[0].size() ||
        right.size() != damper_springs_[1].size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index)
        damper_springs_[0][index].carrying_length = left[index];
    for (std::size_t index = 0; index < right.size(); ++index)
        damper_springs_[1][index].carrying_length = right[index];
    return true;
}

void VehiclePhysics::apply_impulse_to_com(Vector3 impulse) {
    apply_vehicle_impulse_to_com(body_, impulse);
}

void VehiclePhysics::apply_impulse(Vector3 point, Vector3 impulse) {
    apply_vehicle_impulse(body_, point, impulse);
}

Vector3 VehiclePhysics::point_velocity(Vector3 point) const {
    const Vector3 origin{body_.transform.rows[3][0], body_.transform.rows[3][1],
                         body_.transform.rows[3][2]};
    return wot::point_velocity(point, origin, body_.angular_velocity,
                              body_.linear_velocity);
}

void VehiclePhysics::publish_entity_transform() {
    // D3DXVec3TransformNormal(-centerOfMass, bodyPose), followed by adding
    // its three components to the copied body's translation row. Keep the
    // CoM pose separate: using it directly as the entity pose shifts every
    // model, filter provider and collision shape by the suspension offset.
    entity_transform_ = body_.transform;
    const Vector3 negative{-center_of_mass_.x, -center_of_mass_.y,
                           -center_of_mass_.z};
    const auto& matrix = body_.transform.rows;
    const Vector3 offset{
        (negative.x * matrix[0][0] + negative.y * matrix[1][0]) +
            negative.z * matrix[2][0],
        (negative.x * matrix[0][1] + negative.y * matrix[1][1]) +
            negative.z * matrix[2][1],
        (negative.x * matrix[0][2] + negative.y * matrix[1][2]) +
            negative.z * matrix[2][2]};
    entity_transform_.rows[3][0] += offset.x;
    entity_transform_.rows[3][1] += offset.y;
    entity_transform_.rows[3][2] += offset.z;
    if (tank_) publish_collision_bounds();
}

void VehiclePhysics::publish_collision_bounds() {
    // #1513 0x00f84be0: active collection (f6c180), active embedded shape,
    // then each wheel point lowered along the hull's up row (f803a0).
    const VehicleTank& tank = *tank_;
    VehicleCollisionBounds& bounds = collision_bounds_;
    bounds = {{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
               std::numeric_limits<float>::max()},
              {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
               -std::numeric_limits<float>::max()}};
    const auto& collection = tank.collections[tank.turret_collision ? 0 : 1];
    for (const auto& box : collection.boxes) union_bounds(bounds, box.world_bounds);
    for (const auto& polyhedron : collection.polyhedra)
        union_bounds(bounds, polyhedron.world_bounds);
    union_bounds(bounds, tank.embedded[tank.turret_collision ? 1 : 0].world_bounds);
    const float* up = body_.transform.rows[1];
    const float offset = tank.current_settings().wheel_bounds_offset;
    for (const auto& wheel : tank.chassis.wheels) {
        const float lowered = offset + wheel.construction.radius;
        const float* position = wheel.body.transform.rows[3];
        union_point(bounds, {position[0] - up[0] * lowered, position[1] - up[1] * lowered,
                             position[2] - up[2] * lowered});
    }
}

} // namespace wot
