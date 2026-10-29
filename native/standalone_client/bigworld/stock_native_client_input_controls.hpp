#pragma once
#include <array>
#include <optional>
namespace stock_native_client_input {
using Point=std::array<double,3>;
struct Aim {double yaw=0.,pitch=0.,world_pitch=0.;std::optional<Point> point;};
double movementThrottle(int flags);
double cruiseThrottle(int mode);
Aim trackAim(Point target,bool relative,const Point&position,const Point&stabilised);
Aim alignedAim(double hull,double terrain_pitch,double hydraulic_pitch,double turret,double gun);
unsigned packedAim(double relative_yaw,double pitch,const std::array<double,2>&limits);
}
