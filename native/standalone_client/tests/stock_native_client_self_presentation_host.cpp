#include "stock_native_client_self_presentation.cpp"
#include <map>
#include <cstring>
#include "stock_native_battle_test_lifetime.hpp"
namespace {
namespace p=stock_native_bot_python;
namespace d=stock_native_client_drive;
std::unique_ptr<p::Ref> matrixType,vectorType;
double gameTime=0.;
std::map<PyObject*,std::shared_ptr<d::PlayerDriver>> drivers;
std::map<PyObject*,std::shared_ptr<native_battle::Battle>> battles;
std::map<PyObject*,native_battle::Overlay> overlays;
PyObject* failure(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
PyObject* factory(PyObject*self,PyObject*args){auto*type=self==Py_True?matrixType->get():vectorType->get();return PyObject_CallObject(type,args);}
PyObject* dispatch(PyObject*self,PyObject*args){auto*object=PyTuple_GET_ITEM(self,0);const auto name=p::string(PyTuple_GET_ITEM(self,1));p::Ref method(PyObject_GetAttrString(object,("_"+name).c_str()));return PyObject_CallObject(method.get(),args);}
PyMethodDef dispatchDef={"native_primitive",dispatch,METH_VARARGS,"Fixture-only native Math delegate."};
PyMethodDef factoryDef={"native_factory",factory,METH_VARARGS,"Fixture-only native Math factory."};
PyObject* configure(PyObject*,PyObject*args){PyObject*m,*v;if(!PyArg_ParseTuple(args,"OO",&m,&v))return nullptr;matrixType.reset(new p::Ref(p::retain(m)));vectorType.reset(new p::Ref(p::retain(v)));Py_RETURN_NONE;}
PyObject* wrap(PyObject*,PyObject*args){return PyCFunction_New(&dispatchDef,args);}
PyObject* capture(PyObject*,PyObject*args){return p::retain(args).release();}
PyObject* timeValue(PyObject*,PyObject*){return PyFloat_FromDouble(gameTime);}
PyObject* setTime(PyObject*,PyObject*args){if(!PyArg_ParseTuple(args,"d",&gameTime))return nullptr;Py_RETURN_NONE;}
PyObject* packed(PyObject*,PyObject*args){PyObject*values;if(!PyArg_ParseTuple(args,"O",&values))return nullptr;try{return PyLong_FromUnsignedLongLong(d::packAux(p::numbers<6>(values)));}catch(const std::exception&e){return failure(e);}}
PyObject* pitch(PyObject*,PyObject*args){double yaw;PyObject*lo,*hi;if(!PyArg_ParseTuple(args,"dOO",&yaw,&lo,&hi))return nullptr;return Py_BuildValue("(dd)",p::scalar(lo)-std::abs(yaw)*.02,p::scalar(hi)+std::abs(yaw)*.03);}
PyObject* shot(PyObject*,PyObject*args){return Py_BuildValue("(dd)",0.,p::scalar(PyTuple_GET_ITEM(args,6))+.2);}
struct Link{BW::StockFloatLinkReader read;};
void deleteLink(PyObject*value){auto*link=static_cast<Link*>(PyCapsule_GetPointer(value,"test.floatlink"));if(link)delete link;else PyErr_Clear();}
PyObject* sample(PyObject*,PyObject*args){PyObject*value;if(!PyArg_ParseTuple(args,"O",&value))return nullptr;auto*link=static_cast<Link*>(PyCapsule_GetPointer(value,"test.floatlink"));if(!link)return nullptr;float output=0.;if(!link->read(output))Py_RETURN_NONE;return PyFloat_FromDouble(output);}
PyObject* install(PyObject*,PyObject*args){PyObject*b,*e;if(!PyArg_ParseTuple(args,"OO",&b,&e))return nullptr;try{auto shared=std::make_shared<stock_native_client_state::State>();shared->local_player_id=3;auto actor=std::make_shared<stock_native_client_state::Actor>();actor->local=true;actor->id=3;actor->alive=true;actor->has_current=true;actor->key="player:3";auto pos=p::numbers<3>(p::value(b,"_local_position").get());for(unsigned i=0;i<3;++i)actor->current[i]=pos[i];shared->actors[actor->key]=actor;p::Ref capsule(stock_native_client_state::capsule(shared));d::put(b,"_native_client_state",capsule);
    d::PlayerDescriptor descriptor;descriptor.motion.drive.mass=25000.;descriptor.motion.half_length=3.;descriptor.motion.half_width=1.5;descriptor.motion.drive.t[11]=1.;descriptor.motion.drive.t[14]=1.;descriptor.motion.drive.t[18]=1.;descriptor.motion.drive.t[24]=1.;for(auto&v:descriptor.motion.drive.terrain)v=1.;descriptor.network_seconds=.1;descriptor.motion.drive.forward=20.;descriptor.motion.track_center=1.25;descriptor.rotation_around_center=false;descriptor.hydraulic=p::truth(e,"hydraulic");d::PlayerState state;state.position=pos;drivers[b].reset(new d::PlayerDriver(descriptor,state));
    auto battle=std::make_shared<native_battle::Battle>();battle->vehicles=shared;battle->local=std::make_unique<native_battle::LocalVehicle>();auto&local=*battle->local;local.position=pos;
    auto runtime=p::value(b,"_runtime");battle->engine.modules=native_battle::borrow(runtime.get());battle->engine.shell=native_battle::borrow(b);battle->engine.avatar=native_battle::borrow(p::value(b,"_avatar").get());battle->engine.server=native_battle::borrow(b);battle->engine.math=native_battle::borrow(p::value(runtime.get(),"math").get());battle->engine.bigworld=native_battle::borrow(p::value(runtime.get(),"bigworld").get());battle->engine.constants=native_battle::borrow(p::value(runtime.get(),"constants").get());battle->engine.compatibility=native_battle::borrow(p::value(runtime.get(),"compatibility").get());
    struct Entry{const char*name;native_battle::Object native_battle::LocalVehicle::*field;};
    for(const auto&entry:std::initializer_list<Entry>{{"model",&native_battle::LocalVehicle::model},{"matrix",&native_battle::LocalVehicle::matrix},{"pose_matrix",&native_battle::LocalVehicle::pose_matrix},{"stabilised_matrix",&native_battle::LocalVehicle::stabilised_matrix},{"stabilised_snapshot",&native_battle::LocalVehicle::stabilised_snapshot},{"steady_rotation_matrix",&native_battle::LocalVehicle::steady_rotation_matrix},{"descriptor",&native_battle::LocalVehicle::descriptor},{"siege_aim_matrix",&native_battle::LocalVehicle::siege_aim_matrix},{"siege_flat_body_matrix",&native_battle::LocalVehicle::siege_flat_body_matrix},{"siege_body_matrix",&native_battle::LocalVehicle::siege_body_matrix}}){auto value=p::value(b,(std::string("_local_")+entry.name).c_str());local.*(entry.field)=native_battle::borrow(value.get());}
    local.engine_audio_report_at=1.e10;battles[b]=battle;overlays[e]={};Py_RETURN_NONE;}catch(const std::exception&e){return failure(e);}}
PyObject* frame(PyObject*,PyObject*args){PyObject*b,*e,*values;double dt;if(!PyArg_ParseTuple(args,"OOOd",&b,&e,&values,&dt))return nullptr;try{auto lifetime=drivers.at(b);auto&driver=*lifetime;auto&s=driver.state();s.position=p::numbers<3>(p::value(values,"position").get());s.yaw=p::numeric(values,"yaw");s.pitch=p::numeric(values,"pitch");s.roll=p::numeric(values,"roll");s.speed=p::numeric(values,"speed");s.turn_speed=p::numeric(values,"turn");s.drive_turn=p::numeric(values,"effective");s.drive_throttle=p::numeric(values,"throttle");s.airborne=p::truth(values,"airborne");auto&c=driver.controls();c.forward=p::numeric(values,"forward");c.aim_pitch=p::numeric(values,"aim_pitch");auto point=p::value(values,"aim_point");c.aim_point=point.get()==Py_None?std::optional<d::Point>{}:std::optional<d::Point>{p::numbers<3>(point.get())};auto shared=stock_native_client_state::fromBattle(b);shared->find("player:3")->alive=p::truth(values,"alive",true);auto battle=battles.at(b);d::selfPresentation(*battle,e,driver,dt,{},false);d::put(b,"_local_siege_aim_pitch",p::number(battle->local->siege_aim_pitch));d::put(b,"_local_swinging_event_seq",p::integer(battle->local->swinging_event_seq));return p::number(s.hydraulic_pitch).release();}catch(const std::exception&e){return failure(e);}}
PyObject* close(PyObject*,PyObject*args){PyObject*b;if(!PyArg_ParseTuple(args,"O",&b))return nullptr;try{d::retireSelfPresentation(*battles.at(b));d::put(b,"_native_client_self_presentation",p::retain(Py_None));drivers.erase(b);Py_RETURN_NONE;}catch(const std::exception&e){return failure(e);}}
PyObject* reset(PyObject*,PyObject*){drivers.clear();battles.clear();overlays.clear();matrixType.reset();vectorType.reset();Py_RETURN_NONE;}
PyMethodDef methods[]={{"configure",configure,METH_VARARGS,""},{"wrap",wrap,METH_VARARGS,""},{"capture",capture,METH_VARARGS,""},{"time",timeValue,METH_VARARGS,""},{"set_time",setTime,METH_VARARGS,""},{"packed",packed,METH_VARARGS,""},{"pitch",pitch,METH_VARARGS,""},{"shot",shot,METH_VARARGS,""},{"sample",sample,METH_VARARGS,""},{"install",install,METH_VARARGS,""},{"frame",frame,METH_VARARGS,""},{"close",close,METH_VARARGS,""},{"reset",reset,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
namespace BW {
StockAuxVehiclePhysicsData unpackStockAuxVehiclePhysicsData(std::uint64_t packed) {
    auto fp=[](std::uint32_t bits){float value;std::memcpy(&value,&bits,4);return value;};
    auto field=[&](std::uint32_t bits,std::uint32_t scale,std::uint32_t offset){volatile float product=float(bits)*fp(scale);return product-fp(offset);};
    return {field(std::uint32_t(packed&0x7fff),0x39490fdb,0x40490fdb),field(std::uint32_t((packed>>15)&0xfff),0x3a491c6d,0x3fc90fdb),field(std::uint32_t((packed>>27)&0x1fff),0x3a490fdb,0x40490fdb),field(std::uint32_t((packed>>40)&0xff),0x3e34b4b5,0x41700000),field(std::uint32_t((packed>>48)&0xff),0x3e34b4b5,0x41700000),float(packed>>56)*fp(0x3b9a33ce)};
}
PyObject*makeStockFloatLink(StockFloatLinkReader read){auto*link=new Link{std::move(read)};auto*value=PyCapsule_New(link,"test.floatlink",deleteLink);if(!value)delete link;return value;}}
namespace stock_native_client_query {
class Turrets:public ProjectileTurrets {std::vector<PhysicalTurret> physical_;public:const std::vector<PhysicalTurret>&physical()const override{return physical_;}bool active()const override{return true;}std::size_t count()const override{return 0;}bool clear(const stock_native_world::TurretSweep&,const stock_native_bot_turret::MovingDescriptor&,double)const override{return true;}std::optional<double>block(const stock_native_client_state::Point&,const stock_native_client_state::Point&,double,std::optional<double>)override{return {};}};
std::shared_ptr<ProjectileTurrets>projectileTurrets(native_battle::Battle&){return std::make_shared<Turrets>();}
}
namespace native_battle {
std::string describe(const std::exception&e){PyErr_Clear();return e.what();}
void Battle::warn(const std::string&name,const std::string&,bool disable){if(disable)disabled_features.insert(name);}
namespace compat {bool active(){return true;}}
namespace projectiles {std::optional<std::pair<double,double>> serverClockAnchor(const Battle&){return {};}}
namespace remote {
Overlay*overlay(PyObject*e,bool create){auto it=overlays.find(e);if(it==overlays.end()&&!create)return nullptr;return &overlays[e];}
void publishOverlay(PyObject*e,const OverlayPose&pose){
 auto&out=overlays.at(e);auto assign=[&](const char*n,PyObject*v){out.fields[n]=borrow(v);};assign("position",pose.position);assign("matrix",pose.matrix);assign("velocity",pose.velocity);assign("acceleration",pose.acceleration);assign("steady_rotation_matrix",pose.steady_rotation_matrix);assign("stabilised_matrix",pose.stabilised_matrix);out.fields["yaw"]=own(p::number(pose.yaw));out.fields["speed"]=own(p::number(pose.speed));out.fields["turn_speed"]=own(p::number(pose.turn_speed));
 for(const auto&entry:battles){auto compat=entry.second->engine.compatibility;auto mapping=p::value(compat.get(),"_vehicle_property_overlays");p::Ref key(PyLong_FromVoidPtr(e));auto*row=PyDict_GetItem(mapping.get(),key.get());if(row){for(const auto&field:out.fields)if(PyDict_SetItemString(row,field.first.c_str(),field.second.get())<0)throw p::Error("Fixture overlay write failed");}}
}
}}
extern "C" void initclient_self_test(){auto*m=Py_InitModule("client_self_test",methods);PyModule_AddObject(m,"Matrix",PyCFunction_New(&factoryDef,Py_True));PyModule_AddObject(m,"Vector3",PyCFunction_New(&factoryDef,Py_False));}
