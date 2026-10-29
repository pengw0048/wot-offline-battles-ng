#pragma once
// Typed declaration substitutes exercise the production adapter's routing and
// resource fences. They are not BigWorld matrix/BSP acceptance evidence.
#include "stock_native_client_state.hpp"
#include "stock_native_bot_armor_law.hpp"
#include <array>
#include <vector>
namespace BW {class Matrix {float pitch_=0.,roll_=0.;public:void setRotate(float,float pitch,float roll){pitch_=pitch;roll_=roll;}float pitch()const{return pitch_;}float roll()const{return roll_;}};}
namespace stock_native_bot_geometry {
using Point=stock_native_client_state::Point;
struct Component {Point minimum{},maximum{};bool bounds_available=false;};
struct Vehicle {bool ready=false;std::array<Component,4>components;Point hull_offset{};};
struct Pose {Point position{};double yaw=0.,pitch=0.,roll=0.;bool native_hydraulics=true,native_components=false;};
struct Hit {double distance=0.;const stock_native_bot_armor::Material*material=nullptr;};
extern Pose ram_adapter_pose;
std::vector<Hit>intersections(const Vehicle&,const Pose&,const Point&,const Point&);
}
