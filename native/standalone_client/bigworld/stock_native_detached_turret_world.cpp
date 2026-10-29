#include "stock_native_detached_turret_world.hpp"
#include <chrono>
#include <cmath>
#include <stdexcept>
namespace stock_native_detached_turret {
NativeWorld::NativeWorld(stock_native_bot::Runtime&bots,stock_native_world::Registry&registry,stock_native_world::Scene&scene,std::uint16_t flags):bots_(bots),queries_(registry,scene,flags){}
std::optional<Surface> NativeWorld::collide(const Point&start,const Point&end){Point a{},b{};for(unsigned i=0;i<3;++i){a[i]=float(start[i]);b[i]=float(end[i]);}stock_native_world::Hit hit;const auto status=queries_.ground(a,b,hit);if(status==stock_native_world::QueryStatus::Failed)throw std::runtime_error("Detached turret native scenery query failed");if(status==stock_native_world::QueryStatus::Miss)return {};return Surface{hit.point,hit.normal};}
void NativeWorld::applyVehicle(Vehicle&vehicle,const Contact&hit,double step){if(vehicle.human)return;const auto found=bots_.actors().find(vehicle.owner.id);if(found==bots_.actors().end())throw std::runtime_error("Detached turret contact actor disappeared");const auto before=found->second.motion.position;stock_native_bot_motion::ContactResponse response;response.delta_velocity={{hit.delta[0],hit.delta[2]}};response.correction={{hit.vehicle_correction[0],hit.vehicle_correction[2]}};if(!bots_.externalContact(vehicle.owner.id,response,step,now_))throw std::runtime_error("Detached turret native vehicle response failed");const auto&state=bots_.actors().at(vehicle.owner.id).motion;const Point moved{{state.position[0]-before[0],0.,state.position[2]-before[2]}};for(auto&box:vehicle.boxes)for(unsigned i=0;i<3;++i)box.center[i]+=moved[i];vehicle.velocity={{std::sin(state.yaw)*state.speed+state.push_x,0.,std::cos(state.yaw)*state.speed+state.push_z}};}
double NativeWorld::clock(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
void NativeWorld::begin(Owner){if(saved_)throw std::logic_error("Detached turret response transaction is already open");saved_=bots_.snapshotExternalContacts();}
void NativeWorld::commit(Owner){saved_.reset();}
void NativeWorld::rollback(Owner){if(!saved_)return;const auto snapshot=std::move(*saved_);saved_.reset();bots_.restoreExternalContacts(snapshot,now_);}
Vehicle vehicle(Owner owner,const VehicleGeometry&geometry,const VehiclePose&pose,double mass,double speed,double push_x,double push_z,bool alive,bool attached){if(!std::isfinite(mass)||mass<=0.)throw std::invalid_argument("Vehicle collision mass is unavailable");Vehicle out;out.owner=owner;out.boxes=vehicleSupportBoxes(geometry,pose,attached);out.mass=mass;out.human=owner.human;out.alive=alive;out.velocity={{std::sin(pose.attitude[0])*speed+push_x,0.,std::cos(pose.attitude[0])*speed+push_z}};return out;}
}
