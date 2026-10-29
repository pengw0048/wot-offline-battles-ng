#include "stock_native_client_drive_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace stock_native_client_drive {
namespace {double clamp(double v,double low,double high){return std::max(low,std::min(high,v));}constexpr double pi=3.1415926535897932384626433832795;}
CameraHistory::CameraHistory(double window):window_(window){if(!std::isfinite(window)||window<=0.)throw std::invalid_argument("Native player camera sample interval is invalid");}
void CameraHistory::reset(std::optional<Point>spawn){clock_=0.;samples_.clear();if(spawn)samples_.push_back({0.,*spawn});}
std::optional<Point>CameraHistory::at(double moment)const{
    if(samples_.empty()||moment<samples_.front().time)return {};
    std::size_t index=samples_.size()-1;while(index>0&&samples_[index].time>moment)--index;
    if(index+1>=samples_.size())return samples_[index].position;
    const auto&a=samples_[index];const auto&b=samples_[index+1];const double span=b.time-a.time;if(span<=0.)return b.position;
    const double ratio=(moment-a.time)/span;Point out;for(unsigned i=0;i<3;++i)out[i]=a.position[i]+(b.position[i]-a.position[i])*ratio;return out;
}
CameraMotion CameraHistory::sample(const Point&position,double dt,const Point&previous){
    if(!std::isfinite(dt))throw std::invalid_argument("Native player camera elapsed time is invalid");
    CameraMotion out;out.velocity=previous;if(dt<=0.)return out;
    clock_+=dt;samples_.push_back({clock_,position});const double limit=clock_-2.*window_;
    while(samples_.size()>1&&samples_[1].time<=limit)samples_.pop_front();
    const auto recent=at(clock_-window_);if(!recent)return out;
    for(unsigned i=0;i<3;++i)out.velocity[i]=(position[i]-(*recent)[i])/window_;
    const auto earlier=at(clock_-2.*window_);if(!earlier)return out;
    for(unsigned i=0;i<3;++i)out.acceleration[i]=(out.velocity[i]-((*recent)[i]-(*earlier)[i])/window_)/window_;return out;
}
EngineMode engineMode(bool alive,double forward,double turn,double speed){
    if(!alive)return {};int flags=0;if(forward>0.)flags|=1;else if(forward<0.)flags|=2;
    if(turn<0.)flags|=4;else if(turn>0.)flags|=8;return {flags||std::abs(speed)>.05?2:1,flags};
}
Vec trackScroll(const PlayerDescriptor&d,const PlayerState&s,double minimum,double maximum){
    double speed=s.speed+pivot_offset(d,s.speed,s.turn_speed,s.airborne,s.drive_throttle)*s.turn_speed;
    const double cap=d.motion.drive.forward*.995;return {clamp(clamp(speed+s.turn_speed*d.motion.track_center,-cap,cap),minimum,maximum),clamp(clamp(speed-s.turn_speed*d.motion.track_center,-cap,cap),minimum,maximum)};
}
std::optional<SwingEdge>accelerationSwing(int flags,int previous,double move,double rotate){
    if((flags&3)^(previous&3))return SwingEdge{move,flags&1?-1.:flags&2?1.:0.};
    if(!(flags&3)&&((flags&12)^(previous&12)))return SwingEdge{rotate,0.};return {};
}
HydraulicCorrection hydraulicCorrection(const HydraulicPitch&p,double current,double dt,bool active,std::optional<double>flat,double world,double terrain){
    for(double v:{current,dt,p.minimum,p.maximum,p.speed,p.center_z,p.gun_minimum,p.gun_maximum,world,terrain})if(!std::isfinite(v))throw std::invalid_argument("Native hydraulic pitch parameter is invalid");
    if(p.minimum>p.maximum||p.speed<0.||p.gun_minimum>p.gun_maximum)throw std::invalid_argument("Native hydraulic pitch limits are inverted");
    HydraulicCorrection out;if(!p.available||(active&&!p.enabled))return out;
    double desired=0.;if(active){double target=flat.value_or(world-terrain-std::floor((world-terrain+pi)/(2.*pi))*(2.*pi));if(!std::isfinite(target))throw std::invalid_argument("Native hydraulic aim is invalid");
        desired=clamp(target<p.gun_minimum?target-p.gun_minimum:target>p.gun_maximum?target-p.gun_maximum:0.,p.minimum,p.maximum);}
    const double maximum=p.speed*std::max(0.,dt),difference=desired-current;
    out.correction=difference>maximum?current+maximum:difference<-maximum?current-maximum:desired;
    out.translation={0.,p.center_z*std::sin(out.correction),p.center_z*(1.-std::cos(out.correction))};return out;
}
}
