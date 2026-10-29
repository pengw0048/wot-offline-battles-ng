#include "stock_native_client_input_runtime.hpp"
#include "stock_native_client_input.hpp"
#include "stock_native_client_input_controls_python.hpp"
#include "stock_native_client_landing.hpp"
#include "stock_native_client_drive_python.hpp"
#include "stock_native_client_drive_publication.hpp"
#include "stock_native_client_drive_install.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_client_weapon_runtime.hpp"
#include "stock_native_bot_python.hpp"
#include <memory>
namespace p=stock_native_bot_python;
namespace d=stock_native_client_drive;
namespace s=stock_native_client_state;
namespace w=stock_native_client_weapon;
namespace {
std::unique_ptr<d::PlayerDriver> driver;
std::unique_ptr<d::PublicationLedger> publication;
PyObject* battle_identity=nullptr;
w::Gun gun;
int advances=0;
bool cold_install=false;
std::vector<std::array<double,2>> gun_motion;
PyObject* fail(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
PyObject* install(PyObject*,PyObject*args){PyObject*battle,*raw;if(!PyArg_ParseTuple(args,"OO",&battle,&raw))return nullptr;try{
 d::PlayerState st;st.position=p::numbers<3>(p::value(raw,"position").get());st.yaw=p::numeric(raw,"yaw");st.pitch=p::numeric(raw,"pitch");st.roll=p::numeric(raw,"roll");st.speed=p::numeric(raw,"speed");
 auto up=p::value(raw,"up_cosine");if(up.get()!=Py_None)st.surface_up_cosine=p::scalar(up.get());auto braking=p::value(raw,"braking");if(braking.get()!=Py_None)st.siege_braking=p::flag(braking.get());
 driver=std::make_unique<d::PlayerDriver>(d::PlayerDescriptor{},st);auto controls=p::value(raw,"controls");auto&c=driver->controls();c.forward=p::numeric(controls.get(),"forward");c.turn=p::numeric(controls.get(),"turn");c.aim_yaw=p::numeric(controls.get(),"aim_yaw");c.gun_pitch=p::numeric(controls.get(),"gun_pitch");c.aim_pitch=p::numeric(controls.get(),"aim_pitch");c.handbrake=p::truth(controls.get(),"handbrake");auto point=p::value(controls.get(),"aim_point");if(point.get()!=Py_None)c.aim_point=p::numbers<3>(point.get());
 publication=std::make_unique<d::PublicationLedger>();for(const char*key:{"ram_contacts","destructible_contacts","tank_pushes","turret_pushes"}){auto value=s::read(p::value(raw,key).get());if(std::string(key)=="ram_contacts")publication->ram_contacts=value;else if(std::string(key)=="destructible_contacts")publication->destructible_contacts=value;else if(std::string(key)=="tank_pushes")publication->tank_pushes=value;else publication->turret_pushes=value;}
 auto g=p::value(raw,"gun");gun={};gun.shot=int(p::integral(g.get(),"shot_index"));auto pending=p::value(g.get(),"pending_index");if(pending.get()!=Py_None)gun.pending=int(p::wide(pending.get()));gun.clip=int(p::integral(g.get(),"clip"));gun.config.clip_size=int(p::integral(g.get(),"clip_size"));gun.reload_time=p::numeric(g.get(),"reload_time");gun.reload_duration=p::numeric(g.get(),"reload_duration");gun.dispersion=p::numeric(g.get(),"dispersion");
 Py_XDECREF(battle_identity);battle_identity=battle;Py_INCREF(battle);advances=0;gun_motion.clear();Py_RETURN_NONE;
}catch(const std::exception&e){return fail(e);}}
PyObject* sendAt(PyObject*,PyObject*args){PyObject*battle,*siege=Py_None;double now;if(!PyArg_ParseTuple(args,"Od|O",&battle,&now,&siege))return nullptr;try{return p::boolean(stock_native_client_input::sendCurrentAt(battle,now,siege==Py_None?std::nullopt:std::optional<bool>(p::flag(siege)))).release();}catch(const std::exception&e){return fail(e);}}
PyObject* count(PyObject*,PyObject*){return PyInt_FromLong(advances);}
PyObject*cold(PyObject*,PyObject*battle){driver.reset();publication.reset();cold_install=true;Py_XDECREF(battle_identity);battle_identity=battle;Py_INCREF(battle);advances=0;Py_RETURN_NONE;}
PyObject*pose(PyObject*,PyObject*){const auto&s=driver->state();return Py_BuildValue("(ddd)ddd",s.position[0],s.position[1],s.position[2],s.yaw,s.pitch,s.roll);}

PyObject* barrier(PyObject*,PyObject*args){PyObject*position;double yaw,speed=8.,turn=0.;if(!PyArg_ParseTuple(args,"Od|dd",&position,&yaw,&speed,&turn))return nullptr;try{auto frozen=driver->state();frozen.speed=speed;frozen.turn_speed=turn;publication->barrier(frozen,driver->controls(),p::numbers<3>(position),yaw);Py_RETURN_NONE;}catch(const std::exception&e){return fail(e);}}
PyObject* gunMotion(PyObject*,PyObject*){auto out=p::list();for(const auto&row:gun_motion)p::append(out.get(),p::tupleNumbers(row));return out.release();}
PyObject* landing(PyObject*,PyObject*observation){try{publication->queueLanding(s::read(observation),32);Py_RETURN_NONE;}catch(const std::exception&e){return fail(e);}}
PyObject* flushLanding(PyObject*,PyObject*args){PyObject*battle;double now;if(!PyArg_ParseTuple(args,"Od",&battle,&now))return nullptr;try{return p::boolean(stock_native_client_input::flushLandingAt(battle,now)).release();}catch(const std::exception&e){return fail(e);}}
PyObject* frontiers(PyObject*,PyObject*){return Py_BuildValue("(nn)",Py_ssize_t(publication->barriers().size()),Py_ssize_t(publication->landing_impacts.array.size()));}
PyObject* readControls(PyObject*,PyObject*){const auto&c=driver->controls();auto out=p::dict();p::set(out.get(),"forward",p::number(c.forward));p::set(out.get(),"turn",p::number(c.turn));p::set(out.get(),"aim_yaw",p::number(c.aim_yaw));p::set(out.get(),"gun_pitch",p::number(c.gun_pitch));p::set(out.get(),"aim_pitch",p::number(c.aim_pitch));p::set(out.get(),"aim_point",c.aim_point?p::tupleNumbers(*c.aim_point):p::retain(Py_None));p::set(out.get(),"handbrake",p::boolean(c.handbrake));return out.release();}
PyMethodDef extra[]={{"cold",cold,METH_O,""},{"pose",pose,METH_NOARGS,""},{"install",install,METH_VARARGS,""},{"send_at",sendAt,METH_VARARGS,""},{"advance_count",count,METH_NOARGS,""},{"gun_motion",gunMotion,METH_NOARGS,""},{"controls",readControls,METH_NOARGS,""},{"barrier",barrier,METH_VARARGS,""},{"landing",landing,METH_O,""},{"flush_landing",flushLanding,METH_VARARGS,""},{"frontiers",frontiers,METH_NOARGS,""},{"control",stock_native_client_input::control,METH_VARARGS,""},{"align_control",stock_native_client_input::alignControl,METH_VARARGS,""},{"assign_control",stock_native_client_input::assignControl,METH_VARARGS,""},{"send_current",stock_native_client_input::sendCurrentPython,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
// Physical integration and Gun advancement have independent differential tests.
// This seam supplies their already-computed current typed state, with an identity
// fence, while exercising the actual production input/control/queue code.
namespace stock_native_client_drive {
PlayerDriver::PlayerDriver(PlayerDescriptor a,PlayerState b):descriptor_(std::move(a)),state_(std::move(b)){}
void PlayerDriver::landing_admitted(){input_elapsed_=std::fmod(input_elapsed_,descriptor_.network_seconds);}
DriverAccess driverAccess(PyObject*battle){if(cold_install&&!driver&&battle==battle_identity){stock_native_client_state::Actor local;local.local=true;local.has_current=false;local.current={999.,999.,999.,2.,2.,2.,0.,0.};auto state=readPlayerInitialState(battle,local);driver=std::make_unique<PlayerDriver>(PlayerDescriptor{},std::move(state));driver->controls()=readPlayerInitialControls(battle);publication=std::make_unique<PublicationLedger>();}if(!driver||battle!=battle_identity||p::truth(battle,"_test_retired"))throw p::Error("Test current player identity retired");return {{},driver.get(),nullptr,publication.get()};}
}
namespace stock_native_client_weapon_runtime {Gun*advanceWeaponMotion(PyObject*battle,double,double speed,double turn){stock_native_client_drive::driverAccess(battle);++advances;gun_motion.push_back({speed,turn});return &gun;}Gun*advanceWeapon(PyObject*battle,double now){const auto d=stock_native_client_drive::driverAccess(battle);return advanceWeaponMotion(battle,now,d.driver->state().speed,d.driver->state().turn_speed);}}
extern "C" void initoffline_client_input_runtime_test(){static std::vector<PyMethodDef> table;for(auto*m=stock_native_client_input::methods();m->ml_name;++m)table.push_back(*m);for(auto*m=stock_native_client_landing::methods();m->ml_name;++m)table.push_back(*m);for(auto*m=extra;m->ml_name;++m)table.push_back(*m);table.push_back({nullptr,nullptr,0,nullptr});Py_InitModule("offline_client_input_runtime_test",table.data());}

namespace stock_native_bot_descriptor {Bounds bodyBounds(PyObject*){throw p::Error("Unexpected descriptor body read in cold pose test");}Bounds hullBounds(PyObject*){throw p::Error("Unexpected descriptor hull read in cold pose test");}std::array<double,4>chassisShape(PyObject*){throw p::Error("Unexpected descriptor shape read in cold pose test");}}
namespace stock_native_bot_motion {void validate(const Descriptor&){} }
