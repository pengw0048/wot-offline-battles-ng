#pragma once
#include "stock_native_client_drive_runtime.hpp"
#include <deque>
namespace stock_native_client_drive {
struct MotionSample {double time=0.;Point position{};};
struct CameraMotion {Point velocity{},acceleration{};};
class CameraHistory {
    double window_,clock_=0.;
    std::deque<MotionSample> samples_;
    std::optional<Point> at(double)const;
public:
    explicit CameraHistory(double sample_window);
    void reset(std::optional<Point> spawn={});
    CameraMotion sample(const Point&,double dt,const Point& previous_velocity);
    const std::deque<MotionSample>&samples()const{return samples_;}
    double clock()const{return clock_;}
};
struct EngineMode {int power=0,flags=0;bool operator==(const EngineMode&v)const{return power==v.power&&flags==v.flags;}bool operator!=(const EngineMode&v)const{return !(*this==v);}};
EngineMode engineMode(bool alive,double forward,double effective_turn,double speed);
Vec trackScroll(const PlayerDescriptor&,const PlayerState&,double minimum,double maximum);
struct SwingEdge {double period=0.,direction=0.;};
std::optional<SwingEdge> accelerationSwing(int flags,int previous_flags,double move_seconds,double rotate_seconds);
struct HydraulicPitch {
    bool available=false,enabled=false;
    double minimum=0.,maximum=0.,speed=0.,center_z=0.,gun_minimum=0.,gun_maximum=0.;
};
struct HydraulicCorrection {double correction=0.;Point translation{};};
// flat_target_pitch is the native gun/aim owner's angle in the flat body's
// coordinate frame; otherwise world_pitch is resolved against terrain pitch.
HydraulicCorrection hydraulicCorrection(const HydraulicPitch&,double current,double dt,bool active,
    std::optional<double> flat_target_pitch,double world_pitch,double terrain_pitch);
}
