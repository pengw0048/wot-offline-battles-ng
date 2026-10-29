#pragma once
#include "stock_native_detached_turret_runtime.hpp"
#include "stock_native_bot_runtime.hpp"
namespace stock_native_bot_geometry {class Vehicles;}
namespace stock_native_detached_turret {
std::optional<double> nativeTurretYaw(const stock_native_bot_geometry::Vehicles&,Owner);
class NativeWorld final:public World {
    stock_native_bot::Runtime& bots_;
    stock_native_world::Pipeline queries_;
    std::optional<stock_native_bot::ExternalContactsSnapshot> saved_;
    double now_=0.;
public:
    NativeWorld(stock_native_bot::Runtime&,stock_native_world::Registry&,
        stock_native_world::Scene&,std::uint16_t vehicle_skip_flags);
    void now(double value){now_=value;}
    std::optional<Surface> collide(const Point&,const Point&)override;
    void applyVehicle(Vehicle&,const Contact&,double)override;
    double clock()override;
    void begin(Owner)override;
    void commit(Owner)override;
    void rollback(Owner)override;
};
Vehicle vehicle(Owner,const VehicleGeometry&,const VehiclePose&,double mass,
    double speed,double push_x,double push_z,bool alive,bool attached);
}
