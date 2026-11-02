"""Applied Bot/Human hydraulics keep one body/aim owner through presentation.

Compile the production presentation decision and geometry-row publisher with
explicit engine traps: the canonical branch must never query or feed a second
native filter owner. Human receipts preserve the separate terrain baseline.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_stock_vehicle_physics_owner import block
from native_test_compile_flags import native_test_compile_flags

ROOT = Path(__file__).resolve().parents[1]
HOST = r'''
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <map>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
struct Object {int id=0;void*get()const{return reinterpret_cast<void*>(std::uintptr_t(id));}explicit operator bool()const{return id!=0;}};
void*Py_None=nullptr;
namespace p {struct Error:std::runtime_error{using std::runtime_error::runtime_error;};Object number(double){return {};}}
namespace py {
Object optional(void*,const char*){throw std::logic_error("native filter sampled");}
Object attr(void*,const char*){throw std::logic_error("native descriptor sampled");}
bool callable(const Object&){return false;}
template<class...T>Object invoke(T...){throw std::logic_error("native physics input fed");}
}
struct Actor {std::int64_t engine=0;std::string kind;};
struct Battle {
 bool worker=false;Actor*actor=nullptr;
 Actor*vehicleByEngine(std::int64_t id)const{return actor&&actor->engine==id?actor:nullptr;}
};
struct RemoteVehicle {
 bool authority_geometry=true,siege=true;std::int64_t engine_id=42;
 double pitch=.31,local_pitch=-99.;Object entity,aim,hydraulic_matrix,provider;
 std::optional<double>aim_desired_gun_pitch,aim_relative_yaw;
 std::optional<std::array<double,8>>hydraulic_body;
};
bool hasSiege(const RemoteVehicle&rv){return rv.siege;}
bool prepareSiegePose(Battle&,RemoteVehicle&){throw std::logic_error("legacy body/ground ratio sampled");}
bool siegeActive(const RemoteVehicle&){return true;}
std::pair<double,double>absolutePitchLimits(void*){return {-.1,.1};}
double wrap(double n){return n;}
bool writeAimPitch(RemoteVehicle&rv,double pitch){rv.local_pitch=pitch;return true;}
RemoteVehicle* current_remote=nullptr;
RemoteVehicle* vehicle(const Battle&,std::int64_t id){return current_remote&&current_remote->engine_id==id?current_remote:nullptr;}
int body_writes=0,body_rebinds=0;
Object matrix(Battle&){return {1};}
void writePose(void*,const std::array<double,6>&){++body_writes;}
void rebindProvider(Battle&,RemoteVehicle&rv){assert(rv.hydraulic_body);++body_rebinds;}
void nativeMatrix(void*,const std::array<double,3>&,const std::array<double,3>&,bool){}
@CANONICAL@
@SIEGE@
namespace receipt {
using Pose=std::array<double,8>;
@BODY@
}
using receipt::setHydraulicBody;
@PENDING@
namespace offline_simulation {enum class ActorKind {Human,Bot};struct ActorKey{ActorKind kind;std::int64_t id;};}
using Key=offline_simulation::ActorKey;
using Point=std::array<double,3>;
namespace step {struct AimPose{Point position{};double pitch=0,turret_yaw=0,gun_pitch=0;std::optional<std::array<double,8>>hydraulic_body;bool require_hydraulic_body=false;};}
struct GeometryPose {Point position{};double yaw=0,pitch=0,roll=0,turret_yaw=0,gun_pitch=0;int siege=0;bool native_components=false,native_hydraulics=false;std::optional<std::array<double,8>>hydraulic_body;bool require_hydraulic_body=false;};
struct ServiceEntity {
 Key key;int mode=0,team=0;bool alive=true,available=true;
 struct Target {step::AimPose pose;struct Position{Point position{};}target;bool alive=true;}target;
};
struct GeometryRow {Key key;GeometryPose pose;int team=0;bool alive=false,available=false;};
std::int64_t plannerId(Key k){return k.kind==offline_simulation::ActorKind::Human?1000000+k.id:k.id;}
GeometryPose geomPose(const step::AimPose&p){GeometryPose q;q.position=p.position;q.pitch=p.pitch;q.turret_yaw=p.turret_yaw;q.gun_pitch=p.gun_pitch;q.hydraulic_body=p.hydraulic_body;q.require_hydraulic_body=p.require_hydraulic_body;return q;}
struct NativeServices {std::map<std::int64_t,GeometryRow>rows_;void updatePose(ServiceEntity&,const step::AimPose&,bool);};
@PUBLISH@
// Matrix arithmetic is an explicit engine boundary. The production frames
// function must select its canonical inputs, never sample the trapped native
// providers; this fixture does not claim D3DX numeric equivalence.
namespace BW {
struct Vector3 {
 float x=0,y=0,z=0;
 Vector3 operator-()const{return {-x,-y,-z};}
 Vector3 operator-(const Vector3&r)const{return {x-r.x,y-r.y,z-r.z};}
};
struct Matrix {
 float y=0,p=0,r=0;Vector3 position;
 static const Matrix identity;
 void setRotate(float yaw,float pitch,float roll){y=yaw;p=pitch;r=roll;}
 void setRotateY(float yaw){y=yaw;}
 void setRotateX(float pitch){p=pitch;}
 void setTranslate(Vector3 v){position=v;}
 void translation(Vector3 v){position=v;}
 bool invert(const Matrix&){return true;}
 void postMultiply(const Matrix&m){y+=m.y;p+=m.p;r+=m.r;}
 void preMultiply(const Matrix&m){postMultiply(m);}
 float yaw()const{return y;}float pitch()const{return p;}
};
const Matrix Matrix::identity{};
struct MatrixProvider {void matrix(Matrix&)const{throw std::logic_error("old native pose provider sampled");}};
}
using Pose=GeometryPose;
struct Vehicle {
 bool authority_geometry=true,has_siege=true;
 BW::MatrixProvider*native_body=nullptr,*native_ground=nullptr,*native_turret=nullptr,*native_gun=nullptr;
 std::optional<double>static_turret_yaw,static_gun_pitch;
 Point hull_offset{},turret_offset{},gun_offset{};
};
struct Frames {BW::Matrix body,chassis,body_inverse,chassis_inverse;std::array<BW::Matrix,4>component_inverse;};
BW::Vector3 native(Point p){return {float(p[0]),float(p[1]),float(p[2])};}
BW::Matrix product(BW::Matrix a,const BW::Matrix&b){a.postMultiply(b);return a;}
@FRAMES@
int main(){
 Actor bot{42,"bot"},human{42,"player"};Battle battle{true,&bot};RemoteVehicle remote;
 for(double body:{-.5,0.,.4})for(double gun:{-.08,0.,.07}){
  remote.pitch=body;remote.aim_desired_gun_pitch=gun;
  assert(siegePose(battle,remote));assert(remote.local_pitch==gun);
 }
 remote.aim_desired_gun_pitch.reset();assert(siegePose(battle,remote));
 // A client-only Human physics object has no simulation Tank. Missing
 // body data remains unknown; a real receipt supplies the applied gun angle.
 battle.actor=&human;remote.aim_desired_gun_pitch=.9;
 current_remote=&remote;assert(hydraulicBodyPending(battle,42));
 assert(!siegePose(battle,remote));
 const auto received=std::array<double,8>{11.,3.4,-5.2,.2,.3,-.1,.07,-.04};
 assert(setHydraulicBody(battle,42,received));assert(!hydraulicBodyPending(battle,42));
 assert(body_writes==1&&body_rebinds==1&&remote.provider.id==1);
 assert(siegePose(battle,remote)&&remote.local_pitch==-.04);
 // Losing a receipt retains the complete old visual provider without writing
 // new pose/aim. The next real sample restores the same provider immediately.
 assert(!setHydraulicBody(battle,42,{}));assert(hydraulicBodyPending(battle,42));
 assert(body_writes==1&&remote.provider.id==1);
 assert(setHydraulicBody(battle,42,received));assert(!hydraulicBodyPending(battle,42)&&body_writes==2&&body_rebinds==1);
 auto invalid=received;invalid[4]=std::numeric_limits<double>::quiet_NaN();bool rejected=false;
 try{setHydraulicBody(battle,42,invalid);}catch(const p::Error&){rejected=true;}
 assert(rejected&&remote.hydraulic_body==received&&body_writes==2);
 remote.hydraulic_body.reset();battle.actor=nullptr;assert(!siegePose(battle,remote));
 battle.actor=&bot;battle.worker=false;assert(!canonicalBotPose(battle,remote));
 assert(!hydraulicBodyPending(battle,42));
 battle.worker=true;remote.siege=false;assert(!siegePose(battle,remote));
 battle.actor=&human;assert(!hydraulicBodyPending(battle,42));
 NativeServices services;step::AimPose pose;pose.position={10.,3.,-5.};pose.pitch=.23;pose.turret_yaw=.4;pose.gun_pitch=-.06;
 BW::MatrixProvider oldProvider;Vehicle geometry;
 geometry.native_body=geometry.native_ground=geometry.native_turret=geometry.native_gun=&oldProvider;
 for(const auto kind:{offline_simulation::ActorKind::Bot,offline_simulation::ActorKind::Human}){
  ServiceEntity entity;entity.key={kind,7};entity.mode=1;
  services.updatePose(entity,pose,true);const auto&row=services.rows_.at(plannerId(entity.key));
  assert(row.pose.position==pose.position&&row.pose.pitch==pose.pitch&&row.pose.siege==2);
  assert(row.pose.native_components==(kind==offline_simulation::ActorKind::Human));
  assert(row.pose.native_hydraulics==(kind==offline_simulation::ActorKind::Human));
  bool trapped=false;
  try{
   const auto queried=frames(geometry,row.pose);
   assert(queried.body.pitch()==float(pose.pitch));
   assert(queried.component_inverse[2].yaw()==-float(pose.turret_yaw));
   assert(queried.component_inverse[3].pitch()==-float(pose.gun_pitch));
  }catch(const std::exception&){trapped=true;}
  assert(trapped==(kind==offline_simulation::ActorKind::Human));
  if(kind==offline_simulation::ActorKind::Human){
   pose.hydraulic_body=std::array<double,8>{11.,3.4,-5.2,.2,.3,-.1,.07,-.04};
   services.updatePose(entity,pose,true);
   const auto applied=frames(geometry,services.rows_.at(plannerId(entity.key)).pose);
   assert(applied.chassis.pitch()==float(.23));
   assert(applied.body.pitch()==float(.3));
   assert(applied.body.position.x==11.f&&applied.body.position.y==float(3.4)&&applied.body.position.z==float(-5.2));
   assert(applied.component_inverse[2].yaw()==-float(.07));
   assert(applied.component_inverse[3].pitch()==float(.04));
   pose.hydraulic_body.reset();services.updatePose(entity,pose,true);
   geometry.has_siege=false;
   geometry.native_turret=geometry.native_gun=nullptr;
   assert(frames(geometry,services.rows_.at(plannerId(entity.key)).pose).body.pitch()==float(.23));
   geometry.native_turret=geometry.native_gun=&oldProvider;
   geometry.has_siege=true;
  }
  entity.mode=0;services.updatePose(entity,pose,false);
  assert(services.rows_.at(plannerId(entity.key)).pose.siege==0);
 }
}
'''


class NativeBotHydraulicOwner(unittest.TestCase):
    def test_production_canonical_pose_and_local_aim_do_not_read_native_filter(self):
        remote = (ROOT / 'bigworld/stock_native_battle_remote_vehicle.cpp').read_text()
        services = (ROOT / 'bigworld/stock_native_bot_services.cpp').read_text()
        geometry = (ROOT / 'bigworld/stock_native_bot_vehicle_geometry.cpp').read_text()
        source = HOST.replace('@CANONICAL@', block(remote, 'bool canonicalBotPose('))
        source = source.replace('@SIEGE@', block(remote, 'bool siegePose('))
        source = source.replace('@BODY@', block(remote, 'bool setHydraulicBody('))
        source = source.replace('@PENDING@', block(remote, 'bool hydraulicBodyPending('))
        source = source.replace('@PUBLISH@', block(services, 'void NativeServices::updatePose('))
        source = source.replace('@FRAMES@', block(geometry, 'Frames frames('))
        with tempfile.TemporaryDirectory(prefix='bot-hydraulic-owner-') as temporary:
            folder = Path(temporary)
            (folder / 'probe.cpp').write_text(source)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            *native_test_compile_flags(), str(folder / 'probe.cpp'),
                            '-o', str(folder / 'probe')], check=True)
            subprocess.run([str(folder / 'probe')], check=True)


if __name__ == '__main__':
    unittest.main()
