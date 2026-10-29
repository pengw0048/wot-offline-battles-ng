#include "stock_native_client_projectile_syntax_stub.hpp"
#include "stock_native_client_projectile_python.hpp"
#include "stock_native_client_projectile.hpp"
#include "stock_native_projectile_random.hpp"
#include "stock_native_client_projectile_world.hpp"
#include "stock_native_client_weapon_runtime.hpp"
#include "stock_native_client_drive_status.hpp"
#include "stock_native_bot_python.hpp"
namespace p=stock_native_bot_python;namespace cs=stock_native_client_state;namespace cp=stock_native_client_projectile;namespace q=stock_native_projectile;
static std::shared_ptr<cs::State> shared;
static stock_native_client_weapon::Gun weapon;
static std::shared_ptr<stock_native_client_drive::Status> critical;
static stock_native_bot_geometry::Vehicle vehicle;
static PyObject* descriptor=nullptr;
static PyObject* queue=nullptr;
static bool accept_send=true,water=false;
static unsigned ticks=0;
static stock_native_client_drive::PlayerDriver driver;
static stock_native_client_drive::Controls controls;
static stock_native_world::Registry registry;
struct Scene final:stock_native_world::NativeScene {
 stock_native_world::QueryStatus segment(const stock_native_world::Point&,const stock_native_world::Point&,std::uint16_t,const stock_native_world::Filter&,stock_native_world::Hit&)override{return stock_native_world::QueryStatus::Miss;}
 stock_native_world::QueryStatus material(const stock_native_world::Point&,const stock_native_world::Point&,const stock_native_world::Point&,stock_native_world::MaterialHit&)override{return stock_native_world::QueryStatus::Miss;}
 stock_native_world::QueryStatus water(const stock_native_world::Point&,const stock_native_world::Point&,double&out)override{out=::water?.05:-1.;return stock_native_world::QueryStatus::Found;}
 stock_native_world::Admission refresh(const stock_native_world::Key&,stock_native_world::Registry&)override{return stock_native_world::Admission::Pending;}
 bool unresolved(const stock_native_world::Key&,std::vector<stock_native_world::Box>&)override{return false;}
 bool destroy(const stock_native_world::Destruction&)override{return false;}
 bool treeChunks(const std::vector<stock_native_world::Box>&,std::vector<std::uint32_t>&)override{return false;}
 bool turretSweep(const stock_native_world::TurretSweep&)override{return true;}
};
static Scene scene;
static stock_native_world::Pipeline pipeline(registry,scene,128);
namespace stock_native_client_drive {
Controls&PlayerDriver::controls(){return ::controls;}
SceneAccess sceneAccess(PyObject*){SceneAccess out;out.lifetime=shared;out.registry=&registry;out.scene=&scene;out.pipeline=&pipeline;return out;}
DriverAccess driverAccess(PyObject*){DriverAccess out;out.driver=&driver;out.lifetime=shared;return out;}
void synchronizeStatus(PyObject*,double){critical->update(shared->revision,0.);}
void mirrorDestruction(PyObject*,const stock_native_world::Destruction&){}
}
namespace stock_native_client_query {
const stock_native_bot_geometry::Vehicle*geometry(const cs::Actor&){return &vehicle;}
PyObject*descriptor(const cs::Actor&){return ::descriptor;}
std::shared_ptr<ProjectileTurrets>projectileTurrets(PyObject*){return {};}
}
namespace stock_native_client_weapon {Gun*gun(PyObject*){return &weapon;}}
namespace stock_native_client_weapon_runtime {
Gun*advanceWeapon(PyObject*,double){++ticks;weapon.tick(0.,true,0.,0.,0.,{0.,0.,0.});return &weapon;}
Checkpoint currentCheckpoint(PyObject*){Checkpoint c;c.shot_index=weapon.shot;c.dispersion=weapon.dispersion;return c;}
double reloadFactor(PyObject*){return .75;}
int livingSkillCount(PyObject*,const std::string&skill){return skill=="gunner_sniper"?1:0;}
}
namespace stock_native_client_input {
void mirrorControls(PyObject*){}
bool sendCurrentAt(PyObject*battle,double now,std::optional<bool>){stock_native_client_weapon_runtime::advanceWeapon(battle,now);auto client=p::value(battle,"client");auto seq=p::integer(p::integral(client.get(),"_input_seq")+1);if(PyObject_SetAttrString(client.get(),"_input_seq",seq.get())<0)throw p::Error("fixture input failed");return true;}
bool enqueue(PyObject*client,PyObject*message){p::Ref copy(PyDict_Copy(message));p::append(queue,copy);auto callback=p::value(client,"_during_enqueue");if(callback.get()!=Py_None){p::Ref result(PyObject_CallFunctionObjArgs(callback.get(),message,nullptr));}return accept_send;}
}
namespace stock_native_client_projectile {
struct World::State {std::shared_ptr<cs::State>shared;q::Random random;};
World::World(std::shared_ptr<cs::State>s,stock_native_client_drive::SceneAccess,std::shared_ptr<stock_native_client_query::ProjectileTurrets>):state_(new State){state_->shared=std::move(s);}
World::~World()=default;bool World::current()const{return !state_->shared->retired;}
bool World::install(PyObject*){p::Ref random(PyImport_ImportModule("random"));auto instance=p::value(random.get(),"_inst");return state_->random.install(instance.get());}
bool World::ingress(PyObject*,double){return true;}void World::clock(q::Owner*,double){}
std::vector<q::Record>World::records(){return {};}
bool World::liveGeometry(const q::Record&,const q::Pose&)const{return false;}
q::CollisionQuery World::collide(const q::Record&,const Point&,const Point&,const std::optional<q::Pose>&){return {};}
std::optional<double>World::turretBlock(const q::Chord&){return {};}
q::SceneResult World::scenery(const q::Cursor&,q::FlightMeta&,const Point&,const Point&,const Point&){return {};}
damage::Resolution World::resolve(q::Flight&,const q::Cursor&,const q::TerminalData&){return {};}
std::vector<stock_native_world::Destruction>World::takeDestructions(){return {};}
std::string World::shooterKey(const q::Launch&l)const{return "player:"+std::to_string(l.shooter);}
std::optional<double>World::localLaunchTime(const q::Launch&,double)const{return {};}
double World::wireValue(double value)const{return wireRound(value);}
void World::frozen(const Frozen&){}damage::Random&World::random(){return state_->random;}
}
PyObject*setup(PyObject*,PyObject*battle){try{weapon={};weapon.config.base_dispersion=.1;weapon.config.after_shot=2.;weapon.config.after_shot_in_burst=.5;weapon.config.reload=5.;weapon.config.clip_reload=.2;weapon.config.clip_size=3;weapon.config.burst_count=3;weapon.config.burst_interval=.1;weapon.ammunition={7,8};weapon.clip=3;weapon.dispersion=.01;accept_send=true;water=false;ticks=0;vehicle={};vehicle.ready=true;vehicle.hull_offset={0,1,0};vehicle.turret_offset={0,2,0};vehicle.gun_offset={0,.5,1};vehicle.components[3].bounds_available=true;vehicle.components[3].maximum={1,1,2};shared=std::make_shared<cs::State>();shared->round.kind=cs::Value::Kind::Integer;shared->round.integer=11;shared->local_player_id=4;shared->revision=1;auto actor=std::make_shared<cs::Actor>();actor->key="player:4";actor->kind="player";actor->id=4;actor->engine_id=104;actor->local=actor->ready=actor->has_current=true;auto state=p::value(battle,"_fixture_actor");actor->state=cs::read(state.get());actor->manifest=actor->state;shared->actors[actor->key]=actor;critical=stock_native_client_drive::installStatus(actor,{});critical->update(1,0.);auto capsule=p::Ref(cs::capsule(shared));if(PyObject_SetAttrString(battle,"_native_client_state",capsule.get())<0)return nullptr;auto exact=p::value(battle,"_fixture_descriptor");descriptor=exact.get();auto messages=p::value(battle,"_messages");queue=messages.get();Py_RETURN_NONE;}catch(const std::exception&e){PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
PyObject*state(PyObject*,PyObject*){auto out=p::dict();p::set(out.get(),"ammo",p::integers(std::array<std::int64_t,2>{weapon.ammunition[0],weapon.ammunition[1]}));p::set(out.get(),"shot",p::integer(weapon.shot));p::set(out.get(),"clip",p::integer(weapon.clip));p::set(out.get(),"burst",p::integer(weapon.burst_remaining));p::set(out.get(),"reload",p::number(weapon.reload_time));p::set(out.get(),"dispersion",p::number(weapon.dispersion));p::set(out.get(),"ticks",p::integer(ticks));return out.release();}
PyObject*sendFlag(PyObject*,PyObject*value){accept_send=p::flag(value);Py_RETURN_NONE;}
PyObject*waterFlag(PyObject*,PyObject*value){water=p::flag(value);Py_RETURN_NONE;}
PyObject*clock(PyObject*,PyObject*){return PyFloat_FromDouble(1.);}
PyMethodDef helpers[]={{"setup",setup,METH_O,nullptr},{"gun",state,METH_NOARGS,nullptr},{"send_flag",sendFlag,METH_O,nullptr},{"water_flag",waterFlag,METH_O,nullptr},{"clock",clock,METH_NOARGS,nullptr},{nullptr,nullptr,0,nullptr}};
PyMODINIT_FUNC initclient_projectile_test(){auto*module=Py_InitModule("client_projectile_test",cp::methods());if(module)Py_InitModule("client_projectile_test",helpers);}
