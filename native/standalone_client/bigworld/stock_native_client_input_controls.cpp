#include "stock_native_client_input_controls.hpp"
#include <algorithm>
#include <cmath>
namespace stock_native_client_input {
double movementThrottle(int flags){const double direction=(flags&1)?1.:(flags&2)?-1.:0.;return direction*((flags&32)?.25:(flags&16)?.5:1.);}
double cruiseThrottle(int mode){switch(mode){case -2:return -1.;case -1:return -.5;case 1:return .25;case 2:return .5;case 3:return 1.;default:return 0.;}}
Aim trackAim(Point target,bool relative,const Point&position,const Point&stabilised){
    Aim out;Point delta=target;if(relative){for(unsigned i=0;i<3;++i)target[i]+=stabilised[i];}else for(unsigned i=0;i<3;++i)delta[i]-=position[i];
    out.point=target;out.yaw=std::atan2(delta[0],delta[2]);out.pitch=-std::atan2(delta[1],std::max(std::sqrt(delta[0]*delta[0]+delta[2]*delta[2]),.001));out.world_pitch=out.pitch;return out;
}
Aim alignedAim(double hull,double terrain,double hydraulic,double turret,double gun){Aim out;out.yaw=hull+turret;out.pitch=gun;out.world_pitch=terrain+hydraulic+gun;return out;}
unsigned packedAim(double yaw,double pitch,const std::array<double,2>&limits){
    constexpr double pi=3.14159265358979323846;double wrapped=std::fmod(yaw+pi,2.*pi);if(wrapped<0.)wrapped+=2.*pi;wrapped-=pi;
    // Exact #1513 encodeAngleToUint/encodeRestrictedValueToUint, including
    // CPython 2.7's away-from-zero round at the bounded quantization boundary.
    const auto yaw_code=static_cast<unsigned>(std::round(1024.*(wrapped+pi)/(pi*2.)))&1023;
    const double t=limits[1]==limits[0]?0.:(pitch-limits[0])/(limits[1]-limits[0]);const auto pitch_code=static_cast<unsigned>(std::round(63.*std::clamp(t,0.,1.)))&63;return (yaw_code<<6)|pitch_code;
}
}
