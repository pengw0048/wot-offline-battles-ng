#if !defined(WOT_CLIENT_INPUT_CONTROLS_HOST_BUILD)
#include "pch.hpp"
#include "connection_control.hpp"
#include "entity.hpp"
#include "py_entity.hpp"
#endif
#include "stock_native_client_input_controls_python.hpp"
#include "stock_native_client_input_controls.hpp"
#include "stock_native_client_input_runtime.hpp"
#include "stock_native_client_drive_python.hpp"
#include "stock_native_client_drive_runtime.hpp"
#include "stock_native_client_state.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_battle_local.hpp"
namespace native_battle {namespace projectiles {
// Provided by the projectiles subsystem: one local trigger edge at the current LAN aim.
bool shoot(Battle&,double aim_yaw,double gun_pitch);
// Provided by the projectiles subsystem: cancels #1513's post-mailbox wait for a rejected local trigger.
bool cancelNativeShotWait(Battle&);
}}
namespace stock_native_client_input {
namespace p=stock_native_bot_python;
namespace drive=stock_native_client_drive;
namespace state=stock_native_client_state;
namespace {
p::Ref field(PyObject*o,const char*n){if(!o||o==Py_None)return p::retain(Py_None);if(PyDict_Check(o))return p::value(o,n);auto key=p::text(n);auto*v=p::directAttribute(o,key.get());if(!v&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();return p::retain(Py_None);}return p::Ref(v);}
void put(PyObject*o,const char*n,const p::Ref&v){auto key=p::text(n);if(PyObject_GenericSetAttr(o,key.get(),v.get())<0)throw p::Error("Native input state write failed");}
double number(PyObject*v){auto*raw=PyNumber_Float(v);if(!raw){PyErr_Clear();return 0.;}p::Ref held(raw);const auto n=PyFloat_AS_DOUBLE(raw);return std::isfinite(n)?n:0.;}
Point xyz(PyObject*v){Point out{};const char*keys[]={"x","y","z"};if(PyDict_Check(v)){for(unsigned i=0;i<3;++i){auto item=p::value(v,keys[i]);out[i]=number(item.get());}return out;}bool sequence=true;for(unsigned i=0;i<3;++i){auto*raw=PySequence_GetItem(v,i);if(!raw){PyErr_Clear();sequence=false;break;}p::Ref held(raw);out[i]=number(raw);}if(!sequence)for(unsigned i=0;i<3;++i){auto item=field(v,keys[i]);out[i]=number(item.get());}return out;}
using native_battle::Battle;
bool live(const Battle&b){return !b.worker&&b.running();}
void install(drive::Controls&controls,const Aim&aim){controls.aim_yaw=aim.yaw;controls.gun_pitch=aim.pitch;controls.aim_pitch=aim.world_pitch;controls.aim_point=aim.point;}
p::Ref entity(const Battle&b){
    const auto&owner=*b.vehicles;auto*actor=owner.find("player:"+std::to_string(owner.local_player_id));if(!actor||owner.retired||!actor->local)throw p::Error("Native input player identity retired");
#if defined(WOT_CLIENT_INPUT_CONTROLS_HOST_BUILD)
    return b.entity(actor->engine_id);
#else
    auto*connection=BW::ConnectionControl::pInstance();auto*value=connection?connection->findEntity(static_cast<BW::EntityID>(actor->engine_id)):nullptr;if(!value||value->isDestroyed()||!value->isInWorld())throw p::Error("Native input player entity retired");return p::Ref(value->pPyEntity().newRef());
#endif
}
// The stabilised body position: the snapshot provider, else the Siege pose, else the root matrix.
Point stabilised(const Battle&b){
    const auto&local=*b.local;const auto&provider=local.stabilised_snapshot?local.stabilised_snapshot:local.pose_matrix?local.pose_matrix:local.matrix;
    auto type=field(b.engine.math.get(),"Matrix");p::Ref args(PyTuple_Pack(1,provider.get())),matrix(PyObject_CallObject(type.get(),args.get()));auto translation=field(matrix.get(),"translation");return xyz(translation.get());
}
// The LAN controls other subsystems read; the driver's copy is authoritative once it exists.
void mirrorControls(Battle&b,const drive::Controls&c){auto&m=b.local->controls;m.forward=c.forward;m.turn=c.turn;m.aim_yaw=c.aim_yaw;m.gun_pitch=c.gun_pitch;m.aim_pitch=c.aim_pitch;m.handbrake=c.handbrake;m.aim_point=c.aim_point;}
}
bool autorotationEnabled(Battle&b){auto handler=field(b.engine.avatar.get(),"inputHandler");if(handler.get()==Py_None)return false;auto value=field(handler.get(),"_AvatarInputHandler__isAutorotation");if(value.get()!=Py_None)return p::flag(value.get());auto getter=field(handler.get(),"getAutorotation");if(getter.get()==Py_None||!PyCallable_Check(getter.get()))return false;throw p::Error("#1513 autorotation handler state is unavailable");}
void echoGun(Battle&b,double turret,double pitch){auto value=entity(b),descriptor=field(value.get(),"typeDescriptor"),gun=field(descriptor.get(),"gun"),limits=field(gun.get(),"pitchLimits"),absolute=field(limits.get(),"absolute");const auto bounds=p::numbers<2>(absolute.get());auto&vehicle=drive::driverAccess(b).driver->state();const double aim=vehicle.yaw+turret;const unsigned packed=packedAim(aim-vehicle.yaw,pitch,bounds);put(value.get(),"gunAnglesPacked",p::integer(packed));
    // The exact stock property consumer only decodes and invokes syncGunAngles.
    // The accepted offline Vehicle filter proxy deliberately makes that sync a
    // no-op: this client-created entity has no retail interpolation owner.
}
}

namespace native_battle {namespace local {
namespace input=stock_native_client_input;
namespace dr=stock_native_client_drive;
// One original #1513 Avatar control edge (the pinned _LANInputSender.send_avatar_input).
bool control(Battle&b,const std::string&operation,PyObject*payload){
    if(operation=="shoot"){const auto&c=b.local->controls;return projectiles::shoot(b,c.aim_yaw,c.gun_pitch);}
    if(operation=="development")return true;
    if(operation!="move"&&operation!="cruise"&&operation!="track_world"&&operation!="track_relative"&&operation!="stop_tracking")return false;
    if(!input::live(b))return false;if(!PyDict_Check(payload))payload=Py_None;
    auto access=dr::driverAccess(b);auto&controls=access.driver->controls();const auto&vehicle=access.driver->state();
    if(operation=="move"){auto*raw=PyDict_Check(payload)?PyDict_GetItemString(payload,"flags"):nullptr;p::Ref parsed(PyNumber_Long(raw?raw:Py_False));const auto bits=p::wide(parsed.get());controls.forward=input::movementThrottle(int(bits));controls.turn=(bits&8)?1.:(bits&4)?-1.:0.;controls.handbrake=(bits&64)!=0;}
    else if(operation=="cruise"){auto*raw=PyDict_Check(payload)?PyDict_GetItemString(payload,"mode"):nullptr;p::Ref parsed(PyNumber_Long(raw?raw:Py_False));controls.forward=input::cruiseThrottle(int(p::wide(parsed.get())));}
    else if(operation=="stop_tracking"){auto turret=input::field(payload,"turret_yaw"),pitch=input::field(payload,"gun_pitch");const double yaw=input::number(turret.get()),gun=input::number(pitch.get());input::install(controls,input::alignedAim(vehicle.yaw,vehicle.pitch,b.local->siege_aim_pitch,yaw,gun));input::echoGun(b,yaw,gun);}
    else{auto point=input::field(payload,"point");const bool relative=operation=="track_relative";input::install(controls,input::trackAim(input::xyz(point.get()),relative,vehicle.position,relative?input::stabilised(b):Point{}));auto rotator=input::field(b.engine.avatar.get(),"gunRotator"),turret=input::field(rotator.get(),"_VehicleGunRotator__turretYaw"),pitch=input::field(rotator.get(),"_VehicleGunRotator__gunPitch");input::echoGun(b,p::scalar(turret.get()),p::scalar(pitch.get()));}
    input::mirrorControls(b,controls);return input::sendCurrent(b);
}
// Seeds the world-space LAN aim from the attached native gun.
bool alignAim(Battle&b,double turret,double pitch){
    if(!input::live(b))return false;auto access=dr::driverAccess(b);auto&vehicle=access.driver->state();
    input::install(access.driver->controls(),input::alignedAim(vehicle.yaw,vehicle.pitch,b.local->siege_aim_pitch,turret,pitch));input::mirrorControls(b,access.driver->controls());return true;
}
bool sendCurrent(Battle&b,std::optional<bool>siege){return input::live(b)&&input::sendCurrent(b,siege);}
// The pinned _LANInputSender team command: a stock target id resolves to one ready LAN vehicle.
bool sendTeamCommand(Battle&b,PyObject*request){
    if(b.worker||!b.engine.avatar||!b.running()||!PyDict_Check(request))return false;
    const Actor*match=nullptr;auto stock=input::field(request,"stock_target_id");
    if(stock.get()!=Py_None){int matches=0;const auto engine=p::wide(stock.get());for(const auto&row:b.vehicles->actors){const auto&a=*row.second;if(a.engine_id==engine&&a.ready&&!a.tombstone){match=&a;++matches;}}
        if(matches!=1||(match->kind!="bot"&&match->kind!="player"))return false;}
    auto kind=match?p::text(match->kind=="player"?"human":"bot"):p::retain(Py_None),id=match?p::integer(match->presented.network_id):p::retain(Py_None);
    auto command=input::field(request,"command"),cell=input::field(request,"cell_index"),details=input::field(request,"details");
    p::Ref args(PyTuple_Pack(1,command.get())),kwargs(PyDict_New());p::set(kwargs.get(),"target_kind",kind);p::set(kwargs.get(),"target_id",id);p::set(kwargs.get(),"cell_index",cell);p::set(kwargs.get(),"details",details);
    auto send=input::field(b.engine.client.get(),"send_team_command");return p::flag(p::Ref(PyObject_Call(send.get(),args.get(),kwargs.get())).get());
}
bool sendTeamChat(Battle&b,PyObject*request){
    if(b.worker||!b.engine.avatar||!b.running()||!PyDict_Check(request))return false;auto text=input::field(request,"text");
    return p::flag(py::call(b.engine.client.get(),"send_team_chat",text.get()).get());
}
bool rejectNativeShotWait(Battle&b){return projectiles::cancelNativeShotWait(b);}
}}
