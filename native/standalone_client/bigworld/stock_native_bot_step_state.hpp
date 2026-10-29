#pragma once

#include <array>
#include <optional>
#include <vector>

namespace wot::offline_bot_step {

using Point = std::array<double, 3>;

struct AimPose {
    Point position{};
    double yaw = 0, pitch = 0, roll = 0;
    double turret_yaw = 0, gun_pitch = 0, aim_yaw = 0;
};

// All offsets come from the installed vehicle descriptor, not a proxy muzzle.
struct GunMount {
    Point hull_position{}, turret_position{}, gun_position{};
};

struct AimTarget {
    bool target_present = false;
    bool solution_present = false;
    bool solution_origin_present = false;
    Point aim_position{}, solution_origin{};
    double solution_yaw = 0, solution_pitch = 0, flight_time = 0;
};

struct AimIntent {
    double world_yaw = 0, world_pitch = 0, horizontal = 0;
    double raw_yaw = 0, raw_pitch = 0;
};

// The caller advances hydraulic suspension before calling local_aim_intent,
// because it may change the hull pose between the two calculations.
std::optional<AimIntent> world_aim_intent(const AimPose&, const AimTarget&,
                                         const std::optional<GunMount>&);
AimIntent local_aim_intent(const AimPose&, AimIntent);
Point shot_origin(const AimPose&, const GunMount&);
Point shot_direction(const AimPose&);
double world_barrel_yaw(const AimPose&);
double limited_traverse_target(double relative, double minimum,
                               double maximum, double rear_turn);

struct ContactVelocity {
    double speed=0, forward=0, push_x=0, push_z=0;
    bool contact_dynamics=false;
};
// Partitions the resolver impulse; the caller then applies track friction and
// proves the actual correction against the roster and world before moving.
ContactVelocity partition_contact_velocity(ContactVelocity, double yaw,
    bool alive, double delta_x, double delta_z);

struct SuspensionContact {
    double x=0, y=0, z=0, stiffness=0, damping=0, compression=0;
    double maximum=0, force=0, penetration=0;
    std::optional<double> ground;
    int side=0;
    bool spring=false, rigid=false, track=false;
    double rest_length=0, footprint_front=0, footprint_rear=0;
};
struct SuspensionSolveResult {
    std::array<double, 6> state{};
    int contacts=0, rigid=0, touched=0;
    bool left=false, right=false;
    std::optional<double> impact_speed;
    double compression=0, excess=0;
};
struct PosedContacts {
    bool expanded=false;
    double sweep_pitch=0, sweep_roll=0;
    std::vector<SuspensionContact> contacts;
};
PosedContacts suspension_pose_contacts(const std::vector<SuspensionContact>& pseudo,
    const std::vector<SuspensionContact>& rigid,
    const std::optional<Point>& turret_origin, const std::vector<Point>& turret_contacts,
    double pitch, double roll, double pitch_velocity, double roll_velocity,
    double elapsed, double turret_yaw);
// Parameters and tuning are taken from the fixed gameplay source and installed
// descriptor. The layout is the same as release_suspension_step's numeric ABI.
SuspensionSolveResult suspension_solve(const std::array<double, 5>& parameters,
    const std::array<double, 6>& state, const std::vector<SuspensionContact>&,
    double elapsed, double support_velocity, const std::array<double, 9>& tuning,
    int maximum_steps);

struct SupportPlane {
    double gradient_x=0, gradient_z=0;
    double center_x=0, center_y=0, center_z=0;
    Point normal{};
};
struct SupportCheckpoint { double x=0, z=0, expected_height=0; };
struct GroundMemory {
    double origin_x=0, origin_z=0, height=0, last_x=0, last_z=0, travelled=0;
    bool last_query_missed=false;
};
struct RetainedGround {
    std::optional<double> height;
    std::optional<GroundMemory> memory;
};
RetainedGround retain_ground(double x, double z, std::optional<double> ground,
    std::optional<GroundMemory> memory, double maximum_distance,
    std::optional<std::array<double,2>> gradient);
// Returns no proof when planes disagree or the candidate rise exceeds the
// height justified by the two planes. Each returned checkpoint must then be
// queried against actual upward-facing scene geometry.
std::optional<std::vector<SupportCheckpoint>> support_checkpoints(
    const SupportPlane& previous, const SupportPlane& current,
    const Point& motion, const Point& position, double proposed_height);
bool support_checkpoints_proved(const std::vector<SupportCheckpoint>&,
                                const std::vector<std::optional<double>>& heights);
struct VerticalState {
    Point position{};
    double yaw=0, pitch=0, terrain_pitch=0, hydraulic_pitch=0, roll=0;
    double vertical_speed=0, pitch_speed=0, roll_speed=0;
    double speed=0, turn_speed=0, push_x=0, push_z=0;
    double slide_speed=0, air_lateral_x=0, air_lateral_z=0;
    int movement_direction=0, rotation_direction=0;
    bool alive=true, airborne=false, grounded_once=false, left_flying=false, right_flying=false;
    bool suspension_disabled=false, basic_continuous_support=false;
    double last_drive_pitch=0, support_repair_elapsed=0;
    std::optional<double> basic_fall_origin, straddle_reference, slope_basis_yaw;
    std::optional<std::array<double,2>> slope_target;
    std::optional<double> ground_height;
    std::optional<SupportPlane> ground_plane;
    std::optional<std::array<double,2>> support_gradient;
    double support_vertical_speed=0;
};
struct VerticalTrial {
    VerticalState original;
    Point tick_position{};
    std::optional<Point> motion_position;
    std::optional<SupportPlane> current_plane;
    std::vector<SuspensionContact> contacts;
    SuspensionSolveResult solved;
    double elapsed=0, support_speed=0;
    std::optional<std::array<double,2>> support_gradient;
    bool pending_spawn=false, invalid=false, raised=false;
};
struct VerticalOutcome {
    bool blocked=false, pending_spawn=false, clear_contact_memory=false, suspension_retired=false;
    bool invalidate_motion=false, clear_pose_sample=false;
    std::optional<double> invalidated_yaw;
    std::optional<double> pose_sample_yaw;
    std::optional<double> landing_impact;
    bool normal_impact=false, ground_observed=false;
    std::vector<Point> landing_witnesses;
    Point landing_normal{0.,1.,0.};
};
struct SuspensionDescriptor {
    std::array<double,5> parameters{};
    std::array<double,9> tuning{};
    int maximum_steps=0;
    double width=0, clearance=0, rest_length=0, contact_memory_distance=0;
    double footprint_half_width=0;
    double slide_hold_tan=0, cohesion=0, slide_drag=0, slide_max=0;
    double half_length=0, half_width=0;
    std::vector<SuspensionContact> springs, pseudo, rigid;
    std::optional<Point> turret_origin;
    std::vector<Point> turret_contacts;
};
struct VerticalMemory {
    std::vector<std::optional<GroundMemory>> springs, pseudo;
};
// Native scene service. Implementations must return only actual admissible
// upward-facing geometry within the specified column; no Python callbacks.
class VerticalWorld {
public:
    virtual ~VerticalWorld() = default;
    virtual std::optional<double> initial_ground(double x,double z,double hint)=0;
    virtual std::optional<double> ground(double x,double z,double low,double high,
                                         std::optional<double> flat_high)=0;
    virtual std::optional<double> basic_ground(double x,double z,double hint,
                                                std::optional<double> maximum_y)=0;
    virtual bool turret_clear(const VerticalState& before,const VerticalState& after)=0;
    // Apply the contact resolver's passive correction, including vehicle and
    // world sweeps, on this same actor. Return the accepted X/Z displacement.
    virtual std::array<double,2> passive_move(VerticalState&, double x, double z,
                                              double elapsed)=0;
};
VerticalOutcome advance_vertical(VerticalState&, VerticalMemory&,
    const SuspensionDescriptor&, VerticalWorld&, double yaw, double turret_yaw,
    Point tick_position, std::optional<Point> motion_position, double elapsed);
VerticalOutcome advance_basic_vertical(VerticalState&, const SuspensionDescriptor&,
    VerticalWorld&, Point tick_position, double elapsed, int& support_repair_budget);
// Before sampling the first contact columns, initialise only if a real centre
// hit exists. A true return tells the caller to discard old contact memories.
bool prepare_vertical_spawn(VerticalState&, std::optional<double> initial_ground);
VerticalTrial vertical_trial(const VerticalState&, Point tick_position,
    std::optional<Point> motion_position, std::optional<SupportPlane> current_plane,
    const std::vector<SuspensionContact>&, const std::array<double,5>& parameters,
    const std::array<double,9>& tuning, int maximum_steps, double elapsed);
VerticalOutcome commit_vertical(VerticalState&, const VerticalTrial&,
                                bool continuous_support_proved);

} // namespace wot::offline_bot_step
