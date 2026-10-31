#include "stock_native_projectile_wire.hpp"
#include "stock_native_bot_python.hpp"
using namespace stock_native_projectile;
namespace p=stock_native_bot_python;
namespace cs=stock_native_client_state;
PyObject* pack(const std::vector<WireDispatch>&rows){try{auto out=p::list();for(const auto&row:rows){p::Ref tuple(PyTuple_New(3));PyTuple_SET_ITEM(tuple.get(),0,p::Ref(PyLong_FromUnsignedLongLong(row.serial)).release());PyTuple_SET_ITEM(tuple.get(),1,p::text(row.kind).release());PyTuple_SET_ITEM(tuple.get(),2,cs::write(row.wire));p::append(out.get(),tuple);}return out.release();}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
PyObject* parse(PyObject*,PyObject* raw){Launch l;try{std::string error;if(!readLaunch(cs::read(raw),l,&error)){if(!error.empty()){PyErr_SetString(PyExc_ValueError,error.c_str());return nullptr;}Py_RETURN_NONE;}}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}try{auto out=p::dict();p::set(out.get(),"id",p::text(l.id));p::set(out.get(),"shooter",p::integer(l.shooter));p::set(out.get(),"signature",p::text(l.source_signature));p::set(out.get(),"damage",p::number(l.shot.shell.damage));p::set(out.get(),"device_damage",p::number(l.shot.device_damage_mean.value_or(-1)));p::set(out.get(),"radius",p::number(l.shot.explosion_radius));p::set(out.get(),"segment",p::integer(l.segment));return out.release();}catch(...){return nullptr;}}
PyObject* packed(PyObject*,PyObject*){Dispatch d;d.serial=13;d.epoch=7;d.kind="resolve";Proposal v;v.frontier.projectile="x";v.outcome="impact";v.impact={1,2,3};v.hit_vehicle=true;damage::Effect e;e.target={false,4};e.damage=10;e.result=2;e.impact=v.impact;e.hull_damage=11;e.critical_target_base_revision=15;e.critical_target_ack_sequence=9;damage::CriticalOutcome c;c.device_loss["engineHealth"]=12.34567;c.device_loss["gunHealth"]=.0004;c.crew_operations.insert("loader1");c.ignite=true;c.state.crew_ko.insert("loader1");e.critical=c;e.crew_roster={"commander","driver","loader1","gunner1"};v.direct=e;d.proposal=v;return pack(packDispatches({d}));}
PyObject* terminalKinds(PyObject*,PyObject*){
    std::vector<Dispatch> rows;
    for(const std::string name:{"impact","miss","expired","wreck_player","wreck_bot","vehicle_player","vehicle_bot","splash","ricochet","progress"}){
        Dispatch d;d.serial=rows.size()+1;d.epoch=7;d.kind="resolve";
        Proposal v;v.frontier.projectile=name;v.frontier.checked_ms=98;v.frontier.distance=10.;v.frontier.penetration_factor=1.;v.outcome=name=="miss"||name=="expired"?name:"impact";v.impact={1,2,3};
        if(name.find("wreck_")==0){v.wreck=damage::TargetIdentity{name=="wreck_player",4};v.hit_vehicle=true;}
        if(name.find("vehicle_")==0||name=="ricochet"){
            damage::Effect e;e.target={name!="vehicle_bot",4};e.damage=name=="ricochet"?0:10;e.result=name=="ricochet"?0:2;e.impact=v.impact;
            e.potential_damage=0;e.damage_sticker=0;e.structural_armor_hit=false;e.high_explosive=false;
            v.direct=e;v.hit_vehicle=true;
        }
        if(name=="splash"){damage::Effect e;e.target={false,4};e.damage=3;e.result=2;e.impact=v.impact;e.target_position=Point{4,2,3};v.splash.push_back(e);}
        if(name=="ricochet"){v.ricochet=true;v.segment_origin=v.impact;v.segment_velocity={0,0,-100};v.multiplier=.75;d.kind="ricochet";}
        if(name=="progress"){d.kind="progress";d.progress.push_back(v.frontier);}else d.proposal=v;
        rows.push_back(std::move(d));
    }
    return pack(packDispatches(rows));
}
PyMethodDef methods[]={{"parse",parse,METH_O,nullptr},{"packed",packed,METH_NOARGS,nullptr},{"terminal_kinds",terminalKinds,METH_NOARGS,nullptr},{nullptr,nullptr,0,nullptr}};
PyMODINIT_FUNC initprojectile_wire_test(){Py_InitModule("projectile_wire_test",methods);}
