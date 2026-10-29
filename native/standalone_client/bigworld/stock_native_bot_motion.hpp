#pragma once

#include "offline_release_drive_law.hpp"
#include "stock_native_bot_step_state.hpp"
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace stock_native_bot_motion {
using Point=wot::offline_bot_step::Point;
using Vec=std::array<double,2>;
struct Descriptor {
    release_drive_law::Law drive{};
    std::array<double,4> collision_shape{};
    Point body_minimum{},body_maximum{},hull_minimum{},hull_maximum{};
    double half_length=0.,half_width=0.,track_center=0.;
    std::optional<double> siege_speed_limit;
    std::array<double,4> glancing_yaws{};
    double hard_brake_decay=0.,hard_stop_speed=0.,hard_entry_factor=0.,hard_slide_decay=0.;
    int hard_grind_ticks=0;
    // Contact friction comes from the same installed vehicle/tuning snapshot.
    double side_min_y=0.,side_full_y=0.,side_min=0.,side_full=0.;
    int constraint_iterations=0;
};
struct RejectedRotation {Point origin{};double yaw=0.,delta=0.,until=0.;};
struct DriveSweep {Point start{};Vec movement{};};
struct State : wot::offline_bot_step::VerticalState {
    std::int64_t id=0;
    double contact_forward=0.,direction_command=0.;
    double rotation_refused_until=0.,push_yaw=0.;
    bool service_brake=false,rotation_contact_blocked=false,contact_dynamics=false;
    std::optional<int> contact_motor_turn;
    std::optional<double> destructible_contact_speed;
    std::optional<DriveSweep> drive_sweep;
    std::map<int,RejectedRotation> rejected_rotations;
    int hard_grinds=0;
};
enum class MotionStatus {Clear,Crushed,Soft,CapCrushed,Hard};
struct Input {
    double dt=0.,now=0.,throttle=0.,turn=0.,slope_pitch=0.,travel_sign=1.;
    bool path_clear=true,pose_frozen=false,probe_collision=false,exact_collision=true;
    bool brake=false,siege_locked=false,wet_escape=false,shallow_admitted=false;
    std::optional<double> maximum_probe_distance;
    std::optional<Point> move_target;
};
struct Trace {
    double rotation_fraction=1.,attempted_yaw=0.,passive_forward=0.;
    bool rotation_blocked=false,rotation_drive_held=false,baked_veto=false;
    bool hard_contact=false,resolved_motion=false,path_clear=true;
    MotionStatus motion_status=MotionStatus::Clear;
};
// The implementation supplies only synchronous typed native operations.
// Impact may change the actor's terminal/critical state before glancing tests.
class World {
public:
    virtual ~World()=default;
    virtual double rotation_fraction(const State&,const Descriptor&,const Point&,
        double from,double to,const Vec& translation={})=0;
    virtual bool pose_progress(const State&,const Point&,double,const Point&,double)=0;
    virtual bool turret_clear(const State&,const Point&,double,const Point&,double)=0;
    virtual bool rotation(const State&,const Descriptor&,const Point&,double,double,
        double dt,double now,double speed_cap,const Vec& translation={})=0;
    virtual bool corridor(const State&,double yaw,bool wet_escape,bool shallow,
        const std::optional<double>& distance)=0;
    virtual MotionStatus motion(const State&,const Descriptor&,const Point&,double yaw,
        double speed,double dt,double now,bool passive,bool commit)=0;
    virtual void impact(State&,double speed,double now)=0;
    virtual void invalidate(std::int64_t,double yaw,bool motion)=0;
    virtual void report_contact(const State&,const Point&,const Point&,double yaw,double now)=0;
    virtual void motion_finished(const State&,MotionStatus,double initial,double final)=0;
    virtual Vec slide(const State&,const Descriptor&,const Vec&,double& fraction)=0;
    virtual bool vertical(State&,const Descriptor&,const Point& previous,
        double previous_yaw,double dt)=0;
};
void validate(const Descriptor&);
double retained_contact_speed(double speed,double forced);
double stopping_distance(const Descriptor&,double speed,double pitch,double steering,
    double stop_speed,double time_step);
Trace advance(State&,const Descriptor&,const Input&,World&);
Vec contact_push_deceleration(const State&,const Descriptor&,bool rolling);
Vec contact_push_step(const State&,const Descriptor&,Vec,double dt);
Vec contact_traverse(const State&,const Descriptor&,double turn,double dt,double direction);
struct ContactResponse {Vec correction{},delta_velocity{};double delta_yaw=0.;};
struct ContactOptions {
    bool advance_push=true,apply_correction=true,apply_friction=true,advance_forward=false;
};
void contact(State&,const Descriptor&,const ContactResponse&,double dt,double now,
    World&,ContactOptions={});
void wreck_contact(State&,const Descriptor&,const ContactResponse&,double dt,double now,World&);
}
