#pragma once

#include "wot/vehicle_dynamics.hpp"

#include <cstdint>
#include <limits>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace wot {

class DynamicsSimulator;

struct VehicleCollisionBounds {
    Vector3 minimum;
    Vector3 maximum;
};

struct VehicleCollisionRay {
    Vector3 origin;
    Vector3 direction;
    float length;
};

struct VehicleCollisionRayHit {
    float distance;
    Vector3 normal;
    // Native ray output #3 is a terrain/static byte, not a material kind.
    bool is_terrain;
};

// The filter and world-step share one scene-query owner. The #1513 factory
// 0x00f64730 creates SceneObstaclesCollider only for CompiledSpace; providing
// public-engine scene queries is an explicit host responsibility. A missing
// provider must not be reported as a successfully queried empty world.
// The original SceneObstaclesCollider::collidePolyhedra at 0x00f5f0f0 logs
// "SceneObstaclesCollider::collidePolyhedra: UNIMPLEMENTED\n" and returns false
// without populating contacts. Vehicle 0x00f4e9c1 ignores that return value and
// consumes the empty output lists. This interface does not invent a replacement
// hull-scene algorithm; pair collisions are a separate world-solver path.
class VehicleCollisionProvider {
public:
    virtual ~VehicleCollisionProvider() = default;
    virtual bool prepare(const VehicleCollisionBounds& bounds,
                          std::uint32_t space_id,
                          std::uint32_t visibility_mask) = 0;
    virtual bool query_ray(const VehicleCollisionRay& ray,
                            VehicleCollisionRayHit& hit) = 0;
};

// #1513 Vehicle::Settings (0x00f846c0) fields of Tank::Settings. Members
// start at the f900a0 constructor values; a caught Reader failure keeps the
// values of the fields after it, exactly as the native settings object does.
struct VehicleBodyConfiguration {
    float gravity = 9.81f;
    float full_mass = 36.0f;
    Vector3 hull_inertia_factors{1.0f, 0.0f, 0.0f};
    Vector3 hull_size{2.5f, 1.1f, 6.0f};
    float hull_com_shift_y = -0.3f;
    float hull_box_offset_z = 0.0f;
    Vector3 turret_top_position{0.0f, 1.1f, -0.4f};
    float turret_top_width = 1.5f;
    float simple_forward_max_speed = 10.0f;
    float simple_backward_max_speed = 5.0f;
    float simple_rotation_speed = 1.0f;
    float angular_velocity_factor = 0.3f;
    float allowed_rpm_excess = 1.3f;
    float overspeed_resistance_factor = 1.0f;
    bool rotation_around_center = false;
    float center_rotation_forward_speed = 4.0f;
    float movement_revert_speed = 0.5f;
    float com_friction_y_offset = 0.7f;
    float com_side_friction = 0.5f;
    bool stabilization_enabled = true;
    float gimlet_pushed_rotation = 1.0f;
    float gimlet_pushed_handbrake = 1.0f;
};

// #1513 Tank::Shape::Settings (0x00f8fe40), constructor values zero.
struct VehicleShapeConfiguration {
    bool use_complex_form = false;
    float auxiliary_clearance = 0.0f;
    float tank_board_angle = 0.0f;
    float tank_aft_chamfer_fraction = 0.0f;
    float tank_front_chamfer_fraction = 0.0f;
    float terrain_board_angle = 0.0f;
    float terrain_front_chamfer_fraction = 0.0f;
    float terrain_aft_chamfer_fraction = 0.0f;
};

// #1513 ground record (0x00f830d0) in its native field order, with the
// f81150 table-entry and f83340 temporary constructor values.
struct VehicleGroundSurfaceConfiguration {
    float dirt_cumulation_rate = 1.0f;
    float dirt_release_rate = 0.5f;
    float dirt_side_velocity = 2.5f;
    float max_dirt = 2.0f;
    float handbrake_com_side_friction = 0.0f;
    float side_friction = 1.0f;
    float forward_friction = 1.0f;
    float rolling_friction = 0.07f;
    float handbrake_side_friction_addition = 0.0f;
    float rotation_factor = 1.0f;
};

// GroundSettings table entry (0x78): soft +0x00, medium +0x28, firm +0x50.
struct VehicleGroundSettings {
    VehicleGroundSurfaceConfiguration soft;
    VehicleGroundSurfaceConfiguration medium;
    VehicleGroundSurfaceConfiguration firm;
};

// BigWorld.wg_setupGroundMaterialsMap (0x00f5fde0) state: the group count
// at 0x01a5b968 (static 1) and the matKind -> group map at 0x021316f8
// (zero until the first call, which resets it to -1).
struct VehicleGroundMaterialRegistry {
    std::uint32_t count = 1;
    std::array<std::int32_t, 256> groups{};
};
VehicleGroundMaterialRegistry& vehicle_ground_material_registry();
// f968d0: unsigned bound check against the table, then firm/medium/soft.
bool install_vehicle_ground_settings(std::vector<VehicleGroundSettings>& table,
    std::int32_t group, const VehicleGroundSurfaceConfiguration& firm,
    const VehicleGroundSurfaceConfiguration& medium,
    const VehicleGroundSurfaceConfiguration& soft);
// f96860 -> f600d0: matKind to group (out of range -> group 0), then strength
// 2 medium, 3 soft, anything else firm. An empty table has no entry to return.
const VehicleGroundSurfaceConfiguration& lookup_vehicle_ground_surface(
    const std::vector<VehicleGroundSettings>& table,
    const VehicleGroundMaterialRegistry& registry,
    std::int32_t material_kind, std::int32_t strength);

// #1513 Chassis::Settings (0x00f822a0), followed by TrackChassis::Settings
// (0x00f88700), with the f814d0/f900a0 constructor values. A slope tuple
// absent here keeps its constructor curve; each tuple installs atomically.
struct VehicleChassisConfiguration {
    float chassis_mass = 0.0f;
    float chassis_mass_fraction = 0.15f;
    float clearance = 0.8f;
    float track_gauge_factor = 1.0f;
    bool wheel_detach_on_roll = false;
    float track_to_be_locked_delay = 0.3f;
    float wheel_radius = 0.4f;
    float wheel_inertia_factor = 1.0f;
    float wheel_forward_inertia_factor = 1.0f;
    float stiffness_zero = 265.65f;
    float stiffness_one = 1.0f;
    float damping = 2.3908498f;
    float wheel_restitution = 0.2f;
    float wheel_penetration = 0.02f;
    bool wheel_use_pseudo_contacts = false;
    float brake = 10.0f;
    float rotation_brake = 10.0f;
    float wheel_sinkage_resistance_factor = 0.0f;
    float wheel_dry_friction_factor = 1.0f;
    float side_friction_constant_ratio = 0.0f;
    std::array<float, 5> road_wheel_positions{};
    std::array<float, 5> stiffness_factors{{1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};
    std::optional<std::array<float, 3>> slope_resistance_terrain;
    std::optional<std::array<float, 3>> slope_resistance_static_object;
    std::optional<std::array<float, 3>> slope_resistance_dynamic_object;
    std::optional<std::array<float, 4>> slope_grip_forward_terrain;
    std::optional<std::array<float, 4>> slope_grip_side_terrain;
    std::optional<std::array<float, 4>> slope_grip_forward_static_object;
    std::optional<std::array<float, 4>> slope_grip_side_static_object;
    std::optional<std::array<float, 4>> slope_grip_forward_dynamic_object;
    std::optional<std::array<float, 4>> slope_grip_side_dynamic_object;
    bool flat_side_friction = true;
    // +0x54, sized from the registry count when the settings are constructed.
    std::vector<VehicleGroundSettings> grounds;
    // f83340 result: every item installed and one per table entry. f900a0
    // ignores it; it is kept for diagnostics.
    bool grounds_complete = false;
    float pitch_correction_center_z = 0.0f;
    float pitch_correction_speed = 0.5f;
    float pitch_min = -0.17453294f;
    float pitch_max = 0.17453294f;
    float pitch_correction_stiffness = 50.0f;
    float pitch_correction_damping = 0.2f;
    float pitch_correction_scale = 0.5f;
    float yaw_gimlet_force = 4.0f;
    float yaw_stiffness = 10000.0f;
    float yaw_damping = 900.0f;
    float yaw_precise_restitution = 0.3f;
    float yaw_damping_distance = 0.03f;
    float yaw_precise_distance = 0.006f;
};

// #1513 Engine::Settings (0x00f7ecd0) with the f90a3b constructor values.
// RPM inputs keep the source units; absent ones mean the constructor angular
// speeds. Absent losses keep the constructor PointCurve.
struct VehicleEngineConfiguration {
    std::optional<std::array<float, 2>> losses;
    float simple_engine_power = 1000.0f;
    float engine_inertia = 0.006f;
    float simple_engine_join_ratio = 0.0f;
    float idle_choker = 0.1f;
    float rotation_choker = 1.0f;
    float rotation_by_lock_choker = 1.0f;
    std::optional<float> simple_min_rpm;
    std::optional<float> idle_rpm;
    std::optional<float> start_rpm;
    float maximum_forward_angular_velocity = 209.43951f;
    float maximum_backward_angular_velocity = 183.25958f;
    float rotation_angular_velocity = 183.25958f;
};
// Engine::Settings +0x00/+0x04/+0x08 angular speeds (rpm * 0.10471976f).
float vehicle_engine_minimum_angular_velocity(const VehicleEngineConfiguration& engine);
float vehicle_engine_start_angular_velocity(const VehicleEngineConfiguration& engine);

struct VehiclePhysicsModeConfiguration {
    // Optional byte-string key metadata; native dispatch uses mode_index.
    // Global helpers separately read the original modes.normal dictionary.
    std::string name;
    // Native mode_index=-1 means the current mode when replacing a setting.
    std::int32_t mode_index = 0;
    VehicleBodyConfiguration body;
    VehicleShapeConfiguration shape;
    bool siege_mode_available = false;
    VehicleChassisConfiguration chassis;
    VehicleEngineConfiguration engine;
};

// #1513 0x00f84e80 reads four equal-length sequences per gear direction.
// Each row keeps those source units; gearbox construction performs conversion.
struct VehicleFakeGearConfiguration {
    float switch_speed;
    float switch_hysteresis;
    float low_rpm;
    float high_rpm;
};

// #1513 0x00f97d30. These settings belong to modes.normal.gimlet rather than
// to the wrapper or a second presentation-side vehicle controller.
struct VehicleGimletConfiguration {
    float pushed_diagonal_weight;
    float pushed_rotation_weight;
    float pushed_handbrake_weight;
    float push_in_contact;
    float push_handbrake;
    float push_stop;
    float on_spot_fixed_period;
    float on_spot_grow_period;
    float on_spot_initial_push;
    float on_spot_final_push;
    float on_move_fixed_period;
    float on_move_grow_period;
    float on_move_initial_push;
    float on_move_final_push;
    float minimum_velocity_scale;
    float maximum_velocity_scale;
};
// f97d30 read order of the gimlet fields.
extern const std::array<float VehicleGimletConfiguration::*, 16> vehicle_gimlet_fields;

// f85c70 calls f97a30 only after all three anchor reads succeed.
struct VehicleAnchorConfiguration {
    float maximum_reaction_factor;
    float constant_fraction;
    float velocity_factor;
};

struct VehicleFakeGearboxConfiguration {
    std::vector<VehicleFakeGearConfiguration> forward;
    std::vector<VehicleFakeGearConfiguration> backward;
};

// Full source-side configure input. Per-mode settings are fresh native
// objects, so unread fields hold constructor values. The globals live on the
// Tank: an absent anchor, rail or gearbox and the unread gimlet suffix keep
// the Tank's previous values (f85c70 and f84e80 catch regions).
struct VehiclePhysicsConfiguration {
    std::int32_t vehicle_type = 0;
    std::vector<VehiclePhysicsModeConfiguration> modes;
    std::optional<VehicleAnchorConfiguration> anchor;
    std::optional<float> rail_factor_in_contact;
    VehicleGimletConfiguration gimlet{};
    std::uint32_t gimlet_fields = 0;  // f97d30 fields read, in member order
    std::optional<VehicleFakeGearboxConfiguration> gears;
};

// Hull portion of f84370, before the real D3DX inertia-matrix boundary.
// The adapter constructs/inverts that matrix; these are the original scalar
// inputs and do not substitute a complete configured Tank or solver graph.
struct VehicleBodyMassProperties {
    float mass;
    float inverse_mass;
    Vector3 inertia_diagonal;
};
VehicleBodyMassProperties build_vehicle_hull_mass_properties(
    const VehicleBodyConfiguration& body,
    const VehicleChassisConfiguration& chassis);

// Inputs emitted by Tank::rebuildShape (0x00f8b8d0) before its real D3DX
// calls. Each face is encoded as its vertex count followed by vertex indices.
// Slots 0/1 are embedded shapes; slots 2/3 belong to the shape collection.
// D3DX transforms, convex topology and body installation remain separate
// construction stages; this value alone is not a configured physical vehicle.
struct VehicleTankConvexSource {
    std::vector<Vector3> vertices;
    std::vector<std::uint32_t> faces;
};
struct VehicleTankShapeSource {
    Vector3 local_translation;
    std::array<VehicleTankConvexSource, 4> polyhedra;
    bool has_auxiliary_box;
    std::array<Vector3, 2> auxiliary_box_endpoints;
};
// Tangents are the results of the original two libm calls, supplied by their
// source field names. Keeping that boundary explicit permits an instruction oracle to
// validate geometry without substituting host libm for the original CRT.
VehicleTankShapeSource build_vehicle_tank_shape_source(
    const VehicleBodyConfiguration& body,
    const VehicleShapeConfiguration& shape,
    const VehicleChassisConfiguration& chassis,
    float terrain_board_tangent, float tank_board_tangent);

// ConvexPolyhedron's actual f6d270 topology. Parallel opposite face normals
// share one direction and retain a signed per-face dot product. Adjacency is
// indexed by deduplicated vertices, including repeated closing face indices.
struct VehicleConvexFace {
    std::vector<std::uint32_t> vertices;
    std::uint32_t normal_index;
    float normal_scale;
};
struct VehicleConvexTopology {
    std::vector<Vector3> vertices;
    std::vector<Vector3> edge_directions;
    std::vector<Vector3> normal_directions;
    std::vector<VehicleConvexFace> faces;
    std::vector<std::vector<std::uint32_t>> vertex_faces;
    VehicleCollisionBounds local_bounds;
};
// The native adapter supplies D3DXVec3Normalize; no portable approximation is
// substituted for that API in a physical owner. Source faces are produced by
// build_vehicle_tank_shape_source, then their vertices are D3DX-transformed.
VehicleConvexTopology build_vehicle_convex_topology(
    const VehicleTankConvexSource& source, Vector3 (*normalize)(Vector3));

// f87e90 emits ten wheel constructors in interleaved left/right order.
// These values precede the real rigid-body/D3DX construction at f7f280.
struct VehicleWheelConstruction {
    Vector3 local_position;
    std::uint32_t side;
    std::uint32_t index;
    float carrying_length;
    float stiffness_factor;
    float equilibrium_deflection;
    float normalized_load;
    float radius;
    float mass;
    float forward_inertia_factor;
    float inertia_factor;
};
float vehicle_wheel_equilibrium_deflection(
    const VehicleChassisConfiguration& chassis, std::uint32_t index,
    float chassis_vertical_force, float normalized_load);
std::array<VehicleWheelConstruction, 10> build_vehicle_chassis_wheels(
    const VehicleBodyConfiguration& body,
    const VehicleChassisConfiguration& chassis, float chassis_vertical_force);
VehicleBodyMassProperties build_vehicle_wheel_mass_properties(
    const VehicleWheelConstruction& wheel);

// Win32 entry points the original construction graph calls. The native
// adapter binds UCRT __libm_sse2_tanf and D3DX9_43; host tests bind declared
// stand-ins. inverse leaves the destination unchanged when D3DX returns NULL.
struct VehicleNativeMath {
    float (*tangent)(float);
    Vector3 (*normalize)(Vector3);
    Vector3 (*transform_coordinate)(Vector3, const VehicleMatrix&);
    Vector3 (*transform_normal)(Vector3, const VehicleMatrix&);
    void (*inverse)(VehicleMatrix&, const VehicleMatrix&);
};

// #1513 0x00f527b0, including its SSE pairwise summation order.
VehicleMatrix multiply_vehicle_matrices(const VehicleMatrix& left,
                                        const VehicleMatrix& right);

// Unassigned (context, row, generation) identity written by the original
// constructors. Solver registration happens per step, not during configure.
struct VehicleConstraintHandle {
    std::uint32_t context = 0;
    std::int32_t row = -1;
    std::uint32_t generation = 0;
};

// ConvexPolyhedron (0x150): f6d270 local topology plus f55e40 world state.
struct VehicleConvexPolyhedron {
    VehicleConvexTopology topology;
    std::vector<Vector3> world_vertices;  // +0x48
    std::vector<Vector3> world_normals;   // +0x3c
    std::vector<Vector3> world_edges;     // +0x30
    VehicleMatrix transform = vehicle_identity_matrix();  // +0x60
    VehicleCollisionBounds world_bounds{
        {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
         std::numeric_limits<float>::max()},
        {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
         -std::numeric_limits<float>::max()}};
    VehicleMatrix local_box{};  // +0xb8: scaled local extent at local minimum
    VehicleMatrix world_box{};  // +0xf8
};

// Oriented box allocated by f65930 (0x11c) and refreshed by 0x00933220.
struct VehicleConvexBox {
    VehicleMatrix transform = vehicle_identity_matrix();  // +0x00
    VehicleMatrix inverse{};                              // +0x40
    Vector3 center{};                                     // +0x80
    Vector3 dimensions{1.0f, 1.0f, 1.0f};                 // +0x8c
    Vector3 world_center{};                               // +0x98
    std::array<Vector3, 3> axes{};                        // +0xa4
    std::array<Vector3, 3> half_axes{};                   // +0xc8
    VehicleCollisionBounds world_bounds{
        {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
         std::numeric_limits<float>::max()},
        {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
         -std::numeric_limits<float>::max()}};            // +0xec
    std::array<std::array<float, 2>, 3> projections{};    // +0x104
};

// Shape collection (boxes, then polyhedra). Tank::rebuildShape appends;
// it never clears previously appended shapes.
struct VehicleShapeCollection {
    std::vector<VehicleConvexBox> boxes;
    std::vector<VehicleConvexPolyhedron> polyhedra;
};

// #1513 0x00f6f800 RigidBody constructor state, plus the separately stored
// mass (+0x00) and inertia (+0x08) that VehicleRigidBody does not carry.
struct VehicleBodyInertia {
    float mass = 1.0f;
    VehicleMatrix inertia = vehicle_identity_matrix();
};
VehicleRigidBody make_vehicle_rigid_body();

// Tank::Settings (0xe0) for one mode. The trailing fields are constructor
// values (f900a0) that no Reader assigns; f803a0 consumes +0xb8.
struct VehicleTankModeSettings {
    VehicleBodyConfiguration body;
    VehicleShapeConfiguration shape;
    bool siege_mode_available;
    bool flag_7c = false;            // +0x7c
    float value_80 = 2.0f;           // +0x80
    float value_84 = 2.0f;           // +0x84
    float wheel_bounds_offset = 0.2f;  // +0xb8
    std::array<float, 6> values_bc{{1.0f, 1.0f, 1.0f, 0.1f, 0.0f, 0.8f}};  // +0xbc
    Vector3 vector_d4{};             // +0xd4
};

// Wheel record (0x4b4) built by f866f0 -> f7f280.
struct VehicleChassisWheel {
    VehicleWheelConstruction construction;
    bool enabled = true;                 // +0x10, constructor argument
    VehicleBodyInertia inertia;          // body +0x00/+0x08 at wheel +0x1c
    VehicleRigidBody body;               // +0x1c
    VehicleMatrix local_pose{};          // +0x308
    std::array<bool, 2> flags_35c{{true, true}};  // +0x35c
    Vector3 down{0.0f, -1.0f, 0.0f};     // +0x360
    float scale_36c = 1.0f;              // +0x36c
    float side_contact_scale = 1.0f;     // +0x3ac
    float forward_contact_scale = 1.0f;  // +0x3b0
    std::array<std::int32_t, 2> rows{{-1, -1}};       // +0x428
    std::array<VehicleConstraintHandle, 11> handles;  // +0x430
};

// TrackChassis (0x148): Chassis f81380 + f86a50, wheels from f87e90.
struct VehicleTrackChassis {
    std::vector<std::optional<VehicleChassisConfiguration>> settings;  // +0xc4
    std::int32_t mode = 0;                       // +0xc0
    float vertical_force = 0.0f;                 // +0x64, written by f84e80
    std::vector<VehicleChassisWheel> wheels;     // +0xa4
    // +0xb4: f85f60 resizes to two tracks; f87e90 then appends 0..4 to the
    // first and 1..5 to the second without clearing either.
    std::vector<std::vector<std::uint32_t>> tracks;
    std::array<std::int32_t, 4> rows{};          // +0x50, f82b80 per step
    VehicleMatrix matrix_d0 = vehicle_identity_matrix();  // +0xd0
    VehicleConstraintHandle handle;              // +0x13c
};

// Engine (0x33c): RigidBody at +4; settings install by mode_index.
struct VehicleEngine {
    std::vector<std::optional<VehicleEngineConfiguration>> settings;  // +0x330
    std::int32_t mode = 0;                     // +0x32c
    VehicleBodyInertia inertia;
    VehicleRigidBody body;                     // +0x04, f6f800 when created
    float value_2f8 = 0.05f;                   // +0x2f8
    float value_304 = 0.0f;                    // +0x304, cleared with a speed reset
    bool flag_308 = false;                     // +0x308, cleared by f7ea60
    float value_310 = 1.0f;                    // +0x310
    std::array<VehicleConstraintHandle, 2> handles;  // +0x314
};

// One curve entry (0x14) produced by FakeGearBox::install (0x00f97a60).
struct VehicleFakeGearPoint {
    float previous_speed;
    float speed;
    float previous_low_rpm;
    float high_rpm;
    float hysteresis;
};

struct VehicleTankAnchor {     // Tank +0x688, f97a30
    float velocity_factor = 0.0f;          // +0x0c
    float constant_fraction = 0.0f;        // +0x10
    float maximum_reaction_factor = 0.5f;  // +0x14
    VehicleConstraintHandle handle;        // +0x20
};
struct VehicleTankRail {       // Tank +0x70c, f98040
    float factor_in_contact = 0.0f;        // +0x10
    VehicleConstraintHandle handle;        // +0x14
};
struct VehicleTankGimlet {     // Tank +0x6b4, f97d30 (constructor values first)
    VehicleGimletConfiguration settings{1.0f, 1.0f, 1.0f, 0.5f, 2.0f, 2.0f,
        0.2f, 0.2f, 2.0f, 2.0f, 1.0f, 1.0f, 2.0f, 2.0f, 1.0f, 1.5f};
    VehicleConstraintHandle handle;        // +0x4c
};
struct VehicleTankFakeGearBox {  // Tank +0x660
    std::uint32_t value_10 = 0x7f;         // +0x10
    std::vector<std::vector<VehicleFakeGearPoint>> curves;  // +0x1c
};

enum class VehicleTankComponent : std::uint8_t { anchor, gimlet, rail, fake_gearbox };

// Vehicle +0x04..+0x24 input state written by Tank::setInput (0x00f85a30).
struct VehicleTankInputState {
    std::uint32_t mask_steering = 0;     // +0x04: nonzero keeps only bits 2..3
    float rotation_time = 0.0f;          // +0x08: cleared on direction change
    std::uint32_t signals = 0;           // +0x0c
    std::array<bool, 13> flags{};        // +0x14..+0x20
    std::uint32_t value_24 = 0;          // +0x24, cleared by f84370
};

// Tank (0xaf4): the native owner created by WGVehiclePhysics.configure. The
// hull RigidBody (+0x1c8) is VehiclePhysics::rigid_body(); this owner keeps
// only its mass and inertia, never a second copy of the live hull.
struct VehicleTank {
    std::vector<std::optional<VehicleTankModeSettings>> settings;  // +0x63c
    std::int32_t mode = 0;                          // +0x638
    VehicleBodyInertia hull;                        // +0x1c8/+0x1d0
    std::array<VehicleConvexPolyhedron, 2> embedded;    // +0x28, +0x4b4
    std::array<VehicleShapeCollection, 2> collections;  // +0x178, +0x190
    bool turret_collision = true;                   // +0x1a8
    VehicleTankInputState input;
    VehicleEngine engine;                           // +0x1c0
    VehicleTrackChassis chassis;                    // +0x1b8
    VehicleTankFakeGearBox fake_gearbox;
    VehicleTankAnchor anchor;
    VehicleTankGimlet gimlet;
    VehicleTankRail rail;
    std::vector<VehicleTankComponent> components;   // +0x654, appended by f84370
    VehicleBodyInertia snapshot_inertia;            // +0x740
    VehicleRigidBody snapshot;
    std::array<VehicleConstraintHandle, 16> handles;  // +0xa30

    const VehicleTankModeSettings& current_settings() const;
    const VehicleChassisConfiguration& current_chassis() const;
    const VehicleEngineConfiguration& current_engine() const;
};

// Tank::rebuildShape (0x00f8b8d0) geometry after actual libm/D3DX calls.
// polyhedra[0..1] rebuild the embedded shapes; [2] and [3] are appended to
// the second and first collections; the box joins the first collection.
struct VehicleTankGeometry {
    std::array<VehicleTankConvexSource, 4> polyhedra;
    bool has_auxiliary_box;
    Vector3 auxiliary_box_center;
    Vector3 auxiliary_box_dimensions;
};
VehicleTankGeometry build_vehicle_tank_geometry(
    const VehicleBodyConfiguration& body, const VehicleShapeConfiguration& shape,
    const VehicleChassisConfiguration& chassis, const VehicleNativeMath& math);

// f6d270 on an existing owner: local and world vectors are rebuilt, but each
// vertex's adjacency vector keeps its old face IDs before the new ones.
void rebuild_vehicle_convex_polyhedron(VehicleConvexPolyhedron& owner,
    const VehicleTankConvexSource& source, const VehicleNativeMath& math);
// #1513 0x00f55e40 and 0x00933220.
void transform_vehicle_convex_polyhedron(VehicleConvexPolyhedron& owner,
    const VehicleMatrix& transform, const VehicleNativeMath& math);
void refresh_vehicle_convex_box(VehicleConvexBox& box, const VehicleNativeMath& math);

// Original leftTrackScroll/rightTrackScroll outputs (TrackChassis +0x128/12c).
// These are not independently integrated by the presentation/filter owner.
struct VehicleTrackState {
    float left_scroll = 0;
    float right_scroll = 0;
};

struct VehiclePhysicsInputs {
    std::uint32_t movement_signals = 0;
    std::uint32_t cruise_signals = 0;
};

// The original addDamperSpring five-tuple, before native spring construction.
struct VehicleDamperSpring {
    Vector3 mount_point;
    float carrying_length;
    float indent;
    bool left;
    float hard_ratio;
};

// The WGVehiclePhysics state owner. DynamicsSimulator owns the group update;
// a filter consumes this object's published state and never runs a second
// controller. configure builds the native Tank graph; stepping it belongs to
// the separate world-simulation owner.
class VehiclePhysics {
public:
    VehiclePhysics();

    // #1513 f4fd30 -> f900a0 -> f8b8a0. Validation and every allocation
    // happen before the commit, so a thrown exception leaves the previous
    // graph, hull body and epoch unchanged. std::invalid_argument reports an
    // input the original would turn into a null or out-of-range native write.
    void configure(const VehiclePhysicsConfiguration& configuration,
                   const VehicleNativeMath& math);
    std::uint64_t configuration_epoch() const { return configuration_epoch_; }
    const VehicleTank* tank() const { return tank_.get(); }

    // hullCOMZ getter 0x00f50650: f82b30 over the current chassis settings.
    float hull_com_z() const;

    // Tank::setTransform (0x00f91450): hull, active shapes, wheels, snapshot.
    void set_tank_transform(const VehicleMatrix& transform,
                            const VehicleNativeMath& math);

    // isFrozen setter 0x00f55d10 without the unexposed onBecameFrozenCb.
    bool frozen() const { return frozen_; }
    bool frozen_during_frame() const { return frozen_during_frame_; }
    bool allow_freeze() const { return allow_freeze_; }
    void set_frozen(bool value);
    // Wrapper +0x7f4..+0x800 and +0x94 (constructor 0 and 1000).
    std::array<float, 4> freeze_epsilons{};
    float simulation_y_bound = 1000.0f;

    const VehicleMatrix& entity_transform() const { return entity_transform_; }
    Vector3 linear_velocity() const { return body_.linear_velocity; }
    Vector3 angular_velocity() const { return body_.angular_velocity; }
    const VehicleTrackState& track_state() const { return tracks_; }
    const VehicleRigidBody& rigid_body() const { return body_; }
    // Original f48905..f48928 initializes the physics-owned AABB inside-out.
    // configure does not publish it; f56240 does after a configured pose.
    const VehicleCollisionBounds& collision_bounds() const { return collision_bounds_; }

    // The original centerOfMass setter (0x00f542e0) only stores this offset;
    // the matrix setter and simulation publication consume it separately.
    Vector3 center_of_mass() const { return center_of_mass_; }
    void set_center_of_mass(Vector3 value) { center_of_mass_ = value; }

    const VehiclePhysicsInputs& inputs() const { return inputs_; }
    void set_inputs(VehiclePhysicsInputs value) { inputs_ = value; }

    // Original wrapper fields consumed by the world-step/collision owner.
    // These setters only store values; they do not fabricate a physical step.
    bool static_mode() const { return static_mode_; }
    void set_static_mode(bool value) { static_mode_ = value; }
    std::uint32_t visibility_mask() const { return visibility_mask_; }
    void set_visibility_mask(std::uint32_t value) { visibility_mask_ = value; }
    const std::array<float, 2>& arena_minimum() const { return arena_minimum_; }
    const std::array<float, 2>& arena_maximum() const { return arena_maximum_; }
    void set_arena_bounds(std::array<float, 2> minimum,
                          std::array<float, 2> maximum) {
        arena_minimum_ = minimum;
        arena_maximum_ = maximum;
    }

    VehicleCollisionProvider* collision_provider() const {
        return collision_provider_;
    }
    // Non-owning: the host's space/session outlives its registered vehicles.
    void set_collision_provider(VehicleCollisionProvider* provider) {
        collision_provider_ = provider;
    }

    void clear_damper_springs();
    void add_damper_spring(VehicleDamperSpring spring);
    const std::vector<VehicleDamperSpring>& damper_springs(bool left) const;
    bool set_damper_springs_lengths(const std::vector<float>& left,
                                   const std::vector<float>& right);

    void apply_impulse_to_com(Vector3 impulse);
    void apply_impulse(Vector3 point, Vector3 impulse);
    Vector3 point_velocity(Vector3 point) const;

private:
    friend class DynamicsSimulator;

    // #1513 0x00f56240: entity transform, then f84be0 world bounds once a
    // Tank exists. World stepping calls this after body integration.
    void publish_entity_transform();
    void publish_collision_bounds();

    std::unique_ptr<VehicleTank> tank_;
    std::uint64_t configuration_epoch_ = 0;
    bool frozen_during_frame_ = false;  // +0xb7
    bool frozen_ = false;               // +0xb8, mirrored at +0x8b2
    bool allow_freeze_ = true;          // +0xb9

    VehicleCollisionBounds collision_bounds_{
        {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
         std::numeric_limits<float>::max()},
        {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
         -std::numeric_limits<float>::max()}};
    VehicleRigidBody body_;
    VehicleMatrix entity_transform_;
    Vector3 center_of_mass_{};
    VehicleTrackState tracks_;
    VehiclePhysicsInputs inputs_;
    bool static_mode_ = false;
    std::uint32_t visibility_mask_ = 0;
    // #1513 constructor 0x00f48c5a..0x00f48c78, before setArenaBounds.
    std::array<float, 2> arena_minimum_{{-1000.0f, -1000.0f}};
    std::array<float, 2> arena_maximum_{{1000.0f, 1000.0f}};
    VehicleCollisionProvider* collision_provider_ = nullptr;
    std::array<std::vector<VehicleDamperSpring>, 2> damper_springs_;
};

} // namespace wot
