#include "wot/vehicle_physics.hpp"

#include <cmath>
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
    const float clearance = (chassis.wheel_radius + chassis.clearance) *
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
}

} // namespace wot
