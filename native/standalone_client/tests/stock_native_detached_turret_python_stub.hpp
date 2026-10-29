#pragma once
#include "stock_native_detached_turret_runtime.hpp"
#include "offline_simulation_types.h"
#include <stdexcept>
namespace stock_native_bot {
struct DetachedTestActor {
    struct Motion {stock_native_world::Point position{};double yaw=0.,pitch=0.,roll=0.,terrain_pitch=0.,speed=0.,push_x=0.,push_z=0.;bool alive=true;}motion;
    struct Weapon {double aim_yaw=0.;}weapon;
};
class Runtime {
    std::map<std::int64_t,DetachedTestActor> actors_;
public:
    const auto& actors()const{return actors_;}
    struct Store {struct Row {struct Aim{double turret_yaw=0.,gun_pitch=0.;}aim;};Row row;const Row&at(offline_simulation::ActorKey)const{return row;}};
    const Store& weapons()const{static Store store;return store;}
};
class NativeServices {};
}
namespace stock_native_bot_geometry {class Vehicles {};}
namespace stock_native_detached_turret {
inline std::optional<double> nativeTurretYaw(const stock_native_bot_geometry::Vehicles&,Owner){return {};}
class NativeWorld:public World {
public:
    inline static unsigned queries=0,transactions=0;
    inline static bool fail=false;
    NativeWorld(stock_native_bot::Runtime&,stock_native_world::Registry&,stock_native_world::Scene&,std::uint16_t){}
    void now(double){}
    std::optional<Surface> collide(const Point&a,const Point&b)override{++queries;if(fail)throw std::runtime_error("fixture native scenery failure");if(a[1]>=0.&&b[1]<=0.&&a[1]!=b[1]){const auto fraction=a[1]/(a[1]-b[1]);Point p{};for(unsigned i=0;i<3;++i)p[i]=a[i]+(b[i]-a[i])*fraction;return Surface{p,{{0.,1.,0.}}};}return {};}
    void applyVehicle(Vehicle&,const Contact&,double)override{}
    double clock()override{return 0.;}
    void begin(Owner)override{++transactions;}
};
inline Vehicle vehicle(Owner owner,const VehicleGeometry&geometry,const VehiclePose&pose,double mass,double,double,double,bool alive,bool attached){Vehicle result;result.owner=owner;result.mass=mass;result.alive=alive;result.boxes=vehicleSupportBoxes(geometry,pose,attached);return result;}
}
