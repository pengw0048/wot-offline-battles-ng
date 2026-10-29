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
namespace stock_native_client_input {
namespace p=stock_native_bot_python;
namespace drive=stock_native_client_drive;
namespace state=stock_native_client_state;
namespace {
p::Ref field(PyObject*o,const char*n){if(!o||o==Py_None)return p::retain(Py_None);if(PyDict_Check(o))return p::value(o,n);auto key=p::text(n);auto*v=p::directAttribute(o,key.get());if(!v&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();return p::retain(Py_None);}return p::Ref(v);}
void put(PyObject*o,const char*n,const p::Ref&v){auto key=p::text(n);if(PyObject_GenericSetAttr(o,key.get(),v.get())<0)throw p::Error("Native input state write failed");}
double number(PyObject*v){auto*raw=PyNumber_Float(v);if(!raw){PyErr_Clear();return 0.;}p::Ref held(raw);const auto n=PyFloat_AS_DOUBLE(raw);return std::isfinite(n)?n:0.;}
Point xyz(PyObject*v){Point out{};const char*keys[]={"x","y","z"};if(PyDict_Check(v)){for(unsigned i=0;i<3;++i){auto item=p::value(v,keys[i]);out[i]=number(item.get());}return out;}bool sequence=true;for(unsigned i=0;i<3;++i){auto*raw=PySequence_GetItem(v,i);if(!raw){PyErr_Clear();sequence=false;break;}p::Ref held(raw);out[i]=number(raw);}if(!sequence)for(unsigned i=0;i<3;++i){auto item=field(v,keys[i]);out[i]=number(item.get());}return out;}
bool live(PyObject*battle){auto status=field(battle,"state");return !p::truth(battle,"_worker_mode")&&p::string(status.get())=="running";}
void install(drive::Controls&controls,const Aim&aim){controls.aim_yaw=aim.yaw;controls.gun_pitch=aim.pitch;controls.aim_pitch=aim.world_pitch;controls.aim_point=aim.point;}
p::Ref entity(PyObject*battle){
#if defined(WOT_CLIENT_INPUT_CONTROLS_HOST_BUILD)
    return field(battle,"_test_entity");
#else
    auto owner=state::fromBattle(battle);auto*actor=owner->find("player:"+std::to_string(owner->local_player_id));if(!actor||owner->retired||!actor->local)throw p::Error("Native input player identity retired");auto*connection=BW::ConnectionControl::pInstance();auto*value=connection?connection->findEntity(static_cast<BW::EntityID>(actor->engine_id)):nullptr;if(!value||value->isDestroyed()||!value->isInWorld())throw p::Error("Native input player entity retired");return p::Ref(value->pPyEntity().newRef());
#endif
}
Point stabilised(PyObject*battle){
    auto provider=[&](){auto snapshot=field(battle,"_local_stabilised_snapshot");if(snapshot.get()!=Py_None)return snapshot;auto pose=field(battle,"_local_pose_matrix");return pose.get()!=Py_None?std::move(pose):field(battle,"_local_matrix");}();
    auto runtime=field(battle,"_runtime"),math=field(runtime.get(),"math"),type=field(math.get(),"Matrix");p::Ref args(PyTuple_Pack(1,provider.get())),matrix(PyObject_CallObject(type.get(),args.get()));auto translation=field(matrix.get(),"translation");return xyz(translation.get());
}
PyObject*failure(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
}
bool autorotationEnabled(PyObject*battle){auto avatar=field(battle,"_avatar"),handler=field(avatar.get(),"inputHandler");if(handler.get()==Py_None)return false;auto value=field(handler.get(),"_AvatarInputHandler__isAutorotation");if(value.get()!=Py_None)return p::flag(value.get());auto getter=field(handler.get(),"getAutorotation");if(getter.get()==Py_None||!PyCallable_Check(getter.get()))return false;throw p::Error("#1513 autorotation handler state is unavailable");}
void mirrorControls(PyObject*battle){auto access=drive::driverAccess(battle);const auto&c=access.driver->controls();auto sender=field(battle,"_sender");put(sender.get(),"forward",p::number(c.forward));put(sender.get(),"turn",p::number(c.turn));put(sender.get(),"aim_yaw",p::number(c.aim_yaw));put(sender.get(),"gun_pitch",p::number(c.gun_pitch));put(sender.get(),"aim_pitch",p::number(c.aim_pitch));put(sender.get(),"handbrake",p::boolean(c.handbrake));put(sender.get(),"aim_point",c.aim_point?p::tupleNumbers(*c.aim_point):p::retain(Py_None));}
void echoGun(PyObject*battle,double turret,double pitch){auto value=entity(battle),descriptor=field(value.get(),"typeDescriptor"),gun=field(descriptor.get(),"gun"),limits=field(gun.get(),"pitchLimits"),absolute=field(limits.get(),"absolute");const auto bounds=p::numbers<2>(absolute.get());auto&vehicle=drive::driverAccess(battle).driver->state();const double aim=vehicle.yaw+turret;const unsigned packed=packedAim(aim-vehicle.yaw,pitch,bounds);put(value.get(),"gunAnglesPacked",p::integer(packed));
    // The exact stock property consumer only decodes and invokes syncGunAngles.
    // The accepted offline Vehicle filter proxy deliberately makes that sync a
    // no-op: this client-created entity has no retail interpolation owner.
}
PyObject*control(PyObject*,PyObject*args){PyObject*battle,*payload;const char*kind;if(!PyArg_ParseTuple(args,"OsO",&battle,&kind,&payload))return nullptr;try{if(!live(battle))Py_RETURN_NONE;if(!PyDict_Check(payload))payload=Py_None;const std::string operation(kind);if(operation!="move"&&operation!="cruise"&&operation!="track_world"&&operation!="track_relative"&&operation!="stop_tracking")Py_RETURN_NONE;auto access=drive::driverAccess(battle);auto&controls=access.driver->controls();const auto&vehicle=access.driver->state();if(operation=="move"){auto*raw=PyDict_Check(payload)?PyDict_GetItemString(payload,"flags"):nullptr;p::Ref parsed(PyNumber_Long(raw?raw:Py_False));const auto bits=p::wide(parsed.get());controls.forward=movementThrottle(int(bits));controls.turn=(bits&8)?1.:(bits&4)?-1.:0.;controls.handbrake=(bits&64)!=0;}else if(operation=="cruise"){auto*raw=PyDict_Check(payload)?PyDict_GetItemString(payload,"mode"):nullptr;p::Ref parsed(PyNumber_Long(raw?raw:Py_False));controls.forward=cruiseThrottle(int(p::wide(parsed.get())));}else if(operation=="stop_tracking"){auto turret=field(payload,"turret_yaw"),pitch=field(payload,"gun_pitch");const double yaw=number(turret.get()),gun=number(pitch.get());install(controls,alignedAim(vehicle.yaw,vehicle.pitch,p::numeric(battle,"_local_siege_aim_pitch"),yaw,gun));echoGun(battle,yaw,gun);}else{auto point=field(payload,"point");const bool relative=operation=="track_relative";install(controls,trackAim(xyz(point.get()),relative,vehicle.position,relative?stabilised(battle):Point{}));auto avatar=field(battle,"_avatar"),rotator=field(avatar.get(),"gunRotator"),turret=field(rotator.get(),"_VehicleGunRotator__turretYaw"),pitch=field(rotator.get(),"_VehicleGunRotator__gunPitch");echoGun(battle,p::scalar(turret.get()),p::scalar(pitch.get()));}mirrorControls(battle);return p::boolean(sendCurrent(battle)).release();}catch(const std::exception&e){return failure(e);}}
PyObject*alignControl(PyObject*,PyObject*args){PyObject*battle;double turret=0.,pitch=0.;if(!PyArg_ParseTuple(args,"O|dd",&battle,&turret,&pitch))return nullptr;try{if(!live(battle))Py_RETURN_NONE;auto access=drive::driverAccess(battle);auto&vehicle=access.driver->state();install(access.driver->controls(),alignedAim(vehicle.yaw,vehicle.pitch,p::numeric(battle,"_local_siege_aim_pitch"),turret,pitch));mirrorControls(battle);Py_RETURN_TRUE;}catch(const std::exception&e){return failure(e);}}
PyObject*assignControl(PyObject*,PyObject*args){PyObject*battle,*value;const char*name;if(!PyArg_ParseTuple(args,"OsO",&battle,&name,&value))return nullptr;try{if(!live(battle))Py_RETURN_FALSE;const std::string key(name);if(key!="forward"&&key!="turn"&&key!="aim_yaw"&&key!="gun_pitch"&&key!="aim_pitch"&&key!="aim_point"&&key!="handbrake")Py_RETURN_FALSE;auto access=drive::driverAccess(battle);auto&c=access.driver->controls();if(key=="forward")c.forward=p::scalar(value);else if(key=="turn")c.turn=p::scalar(value);else if(key=="aim_yaw")c.aim_yaw=p::scalar(value);else if(key=="gun_pitch")c.gun_pitch=p::scalar(value);else if(key=="aim_pitch")c.aim_pitch=p::scalar(value);else if(key=="handbrake")c.handbrake=p::flag(value);else c.aim_point=value==Py_None?std::optional<Point>{}:std::optional<Point>{xyz(value)};Py_RETURN_TRUE;}catch(const std::exception&e){return failure(e);}}
}
