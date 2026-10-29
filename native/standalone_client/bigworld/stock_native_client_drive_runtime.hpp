#pragma once
#include "stock_native_client_drive.hpp"
#include "stock_native_bot_motion.hpp"
#include <deque>
#include <string>

namespace stock_native_client_drive {
using Vec=std::array<double,2>;
struct PlayerDescriptor {
    stock_native_bot_motion::Descriptor motion;
    bool rotation_around_center=true,hydraulic=false;
    int team=0;
    std::optional<std::array<double,2>> gun_yaw_limits;
    double gun_traverse_epsilon=0.,network_seconds=0.;
    double slide_drag=0.,slide_max=0.,ground_follow_base=0.,ground_follow_min=0.,ground_follow_max=0.;
};
struct Controls {
    double forward=0.,turn=0.,aim_yaw=0.,gun_pitch=0.,aim_pitch=0.;
    bool handbrake=false,autorotation=false;
    std::optional<Point> aim_point;
};
struct Conditions {
    bool ready=true,alive=true,crew_active=true,battle_over=false,has_alive_method=true;
    bool tracked=false,engine_dead=false,overturned=false,siege_locked=false;
    double mobility=1.,traverse=1.,power=1.,speed=1.;
};
enum class ContactStatus {Clear,Crushed,Approach,Soft,CapCrushed,Hard,Pending};
struct ContactTrace {
    std::optional<Point> point,normal;
    std::string reason,kinds="-";
    ContactStatus status=ContactStatus::Clear;
};
struct PlayerState:step::VerticalState {
    double drive_turn=0.,drive_throttle=0.,direction_command=0.;
    bool service_brake=false;
    int grind=0;
    Vec downhill{};
    double slope_tangent=0.;std::optional<double> surface_up_cosine;
    std::deque<double> drive_pitch_history;
    double smooth_drive_pitch=0.;
    double reaction_rate=0.;bool contact_reaction_paid=false;
    ContactTrace trace;
    std::optional<bool> siege_braking,siege_pending;
    std::optional<std::int64_t> siege_request_sequence;
    bool support_blocked=false,suspension_failed=false,suspension_slide_applied=false;
    bool has_presentation=false;
};
struct SliceTrace {
    Point initial_position{};
    double initial_yaw=0.,previous_speed=0.,drive_speed=0.,horizontal_speed=0.,slope_pitch=0.;
    std::string path;
    std::optional<ContactTrace> primary;
};
struct Recovery {Point position{};double yaw=0.;};
// Every operation below is implemented by typed native scene, roster, exact
// descriptor and presentation owners. No interpreter callable is admitted.
class PlayerWorld {
public:
    virtual ~PlayerWorld()=default;
    virtual Conditions conditions()const=0;
    virtual double clock()const=0;
    virtual double drive_pitch(const PlayerState&)=0;
    virtual std::optional<double> local_aim_yaw(const PlayerState&,const Controls&)=0;
    virtual bool motion(PlayerState&,const Point&,double travel_yaw,double speed,double dt,
        bool powered,std::optional<double> hull_yaw={})=0;
    virtual double rotation_fraction(const PlayerState&,const Point&,double from,double to,
        double pivot,double dt)=0;
    virtual std::optional<Recovery> held_peer_turn(PlayerState&,const Point&,double yaw,double turn,double dt)=0;
    virtual std::optional<Recovery> world_turn(PlayerState&,const Point&,double yaw,double turn,double dt,const ContactTrace&)=0;
    virtual bool arena_rotation(const PlayerState&,const Point&,double from,const Point&,double to)=0;
    virtual bool pose_sweep(PlayerState&,const Point&,double from,const Point&,double to,double speed,double dt)=0;
    virtual void impact(PlayerState&,const ContactTrace&,double speed,double yaw)=0;
    virtual bool detailed_suspension()const=0;
    virtual bool detached_turrets()const=0;
    virtual void checkpoint()=0;
    virtual void rollback_suspension()=0;
    virtual void vertical(PlayerState&,Point&,double yaw,double dt,const Point& tick,
        const std::optional<Point>& motion,double height_delta=0.,double speed_delta=0.)=0;
    virtual void ground_pose(PlayerState&,const Point&,double yaw)=0;
    virtual std::optional<step::SupportPlane> sample_plane(const Point&,double yaw)=0;
    virtual std::array<std::optional<double>,2> terrain_support(const Point&,double yaw)=0;
    virtual Point contacts(PlayerState&,const Point&,double yaw,double dt,const Point& tick)=0;
    virtual bool complete_pose_clear(const PlayerState& before,const Point&,double,
        const PlayerState& after,const Point&,double)=0;
    virtual void present(PlayerState&,double dt,const SliceTrace&,bool stopped)=0;
    virtual bool pending_landing()const=0;
    virtual bool pending_damage()const=0;
    virtual bool terminal_environment()const=0;
};
struct Publication {
    bool stopped=false,input=false,landing=false;
    std::optional<bool> siege_request;
};
class PlayerDriver {
    PlayerDescriptor descriptor_;
    PlayerState state_;
    Controls controls_;
    double input_elapsed_=0.;
    bool input_sent_=false;
    void canonicalize(double& yaw);
    Point slide(PlayerWorld&,Point,double,double,bool suspension);
    Point resettle(PlayerWorld&,const Point&,Point,double);
    bool slice(PlayerWorld&,double);
public:
    PlayerDriver(PlayerDescriptor,PlayerState);
    PlayerState& state(){return state_;}const PlayerState&state()const{return state_;}
    Controls& controls(){return controls_;}const Controls&controls()const{return controls_;}
    const PlayerDescriptor&descriptor()const{return descriptor_;}
    void descriptor(PlayerDescriptor value){descriptor_=std::move(value);}
    Publication advance(PlayerWorld&,double elapsed);
    void input_sent(){input_sent_=true;input_elapsed_=0.;}
    void landing_admitted();
    void siege_admitted(bool enabled,std::optional<std::int64_t>sequence={}){state_.siege_braking.reset();state_.siege_pending=enabled;state_.siege_request_sequence=sequence;input_sent();}
    void acknowledge_siege(std::int64_t mode,std::optional<std::int64_t>sequence){if(state_.siege_pending&&mode!=1&&mode!=3&&(!state_.siege_request_sequence||(sequence&&*sequence>=*state_.siege_request_sequence))){state_.siege_pending.reset();state_.siege_request_sequence.reset();}}
    double input_elapsed()const{return input_elapsed_;}
};
// Same fixed law used by the player held-world and held-peer turn paths.
struct FixedReaction {Vec translation{},normal{};double angle=0.,rate=0.,torque=0.,net_torque=0.,inertia=0.,arm=0.,gap=0.,normal_impulse=0.;};
std::optional<FixedReaction> fixed_reaction(const PlayerDescriptor&,const PlayerState&,
    const Point&,double yaw,double turn,double dt,const ContactTrace&,double previous_rate,double traverse_factor);
Vec push_step(const PlayerDescriptor&,const PlayerState&,Vec,double dt,bool rolling);
double pivot_offset(const PlayerDescriptor&,double speed,double turn_speed,bool airborne,double throttle);
}
