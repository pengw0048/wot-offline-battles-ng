#if !defined(WOT_CLIENT_INPUT_RUNTIME_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_input_runtime.hpp"
#include "stock_native_client_input.hpp"
#include "stock_native_client_landing.hpp"
#include "stock_native_client_drive_python.hpp"
#include "stock_native_client_drive_publication.hpp"
#include "stock_native_client_weapon_runtime.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_client_critical.hpp"
#include "stock_native_battle_local.hpp"
namespace stock_native_client_weapon_runtime {
// Provided by the projectiles/gun subsystem: the gun advanced to `now` (optionally along a frozen motion sample).
Gun* advanceWeapon(native_battle::Battle&,double now);
Gun* advanceWeaponMotion(native_battle::Battle&,double now,double speed,double turn_speed);
}
namespace native_battle {namespace vehicles {
// Provided by the vehicles subsystem: the accepted motion revision time (us) and the local clock it arrived at.
std::optional<std::pair<std::int64_t,double>> poseMotionAnchor(const Battle&);
}}
#include <algorithm>
namespace stock_native_client_input {
namespace p=stock_native_bot_python;
namespace drive=stock_native_client_drive;
namespace state=stock_native_client_state;
namespace weapon=stock_native_client_weapon_runtime;
namespace {
double roundThree(double value){
    p::Ref builtins(PyImport_ImportModule("__builtin__"));auto method=p::value(builtins.get(),"round");
    if(!PyCFunction_Check(method.get()))throw p::Error("Native input decimal round is not a builtin");
    p::Ref result(PyObject_CallFunction(method.get(),const_cast<char*>("di"),value,3));return p::scalar(result.get());
}
PyObject*get(PyObject*d,const char*key){auto*v=PyDict_Check(d)?PyDict_GetItemString(d,key):nullptr;return v?v:Py_None;}
std::optional<std::int64_t> repairRevision(PyObject*value,std::int64_t minimum){
    if(PyBool_Check(value)||(!PyInt_Check(value)&&!PyLong_Check(value)))return {};
    const auto number=PyLong_Check(value)?PyLong_AsLongLong(value):PyInt_AS_LONG(value);
    if(PyErr_Occurred()){PyErr_Clear();return {};}if(number<minimum||number>2147483647)return {};return number;
}
double repairNumber(PyObject*value){
    auto*raw=PyNumber_Float(value);if(!raw){PyErr_Clear();return -1.;}p::Ref parsed(raw);
    const auto number=PyFloat_AS_DOUBLE(raw);return std::isfinite(number)?number:-1.;
}
bool hasCapability(PyObject*client,const char*key){auto values=p::value(client,"server_capabilities"),name=p::text(key);const int result=PySequence_Contains(values.get(),name.get());if(result<0)throw p::Error("Native input capability unavailable");return result!=0;}
void trackRepair(PyObject*client,const native_battle::Battle&b){
    // The shared critical producer owns the repair timer and durable revision.
    // This copies only its terminal/versioned report, never advances repair.
    auto report=p::Ref(state::write(stock_native_client_critical::report(b)));auto*tracks=get(report.get(),"tracks");
    if(!PyList_Check(tracks)&&!PyTuple_Check(tracks))return;const auto n=PySequence_Size(tracks);if(n<1||n>2)return;
    const auto base=repairRevision(get(report.get(),"critical_base_revision"),0),seq=repairRevision(get(report.get(),"critical_seq"),1);if(!base||!seq)return;
    auto rows=p::list();std::set<std::string>seen;
    for(Py_ssize_t i=0;i<n;++i){auto raw=p::item(tracks,i);if(!PyDict_Check(raw.get())||PyDict_Size(raw.get())!=4)return;auto name=p::value(raw.get(),"name"),status=p::value(raw.get(),"state");
        if((!PyString_Check(name.get())&&!PyUnicode_Check(name.get()))||(!PyString_Check(status.get())&&!PyUnicode_Check(status.get())))return;auto key=p::string(name.get()),mode=p::string(status.get());
        if((key!="leftTrackHealth"&&key!="rightTrackHealth")||!seen.insert(key).second||(mode!="destroyed"&&mode!="critical"))return;const double hp=repairNumber(get(raw.get(),"hp")),maximum=repairNumber(get(raw.get(),"max_hp"));if(maximum<=0.||hp<0.||hp>maximum)return;
        // CPython 2.7 decimal rounding preserves the pinned wire checkpoint.
        auto row=p::dict();p::set(row.get(),"name",name);p::set(row.get(),"state",status);p::set(row.get(),"hp",p::number(roundThree(hp)));p::set(row.get(),"max_hp",p::number(roundThree(maximum)));p::append(rows.get(),row);
    }
    auto message=p::dict(),round=p::value(client,"round_id");p::set(message.get(),"type",p::text("track_repair"));p::set(message.get(),"round_id",round);p::set(message.get(),"critical_base_revision",p::integer(*base));p::set(message.get(),"repair_seq",p::integer(*seq));p::set(message.get(),"tracks",rows);enqueue(client,message.get());
}
bool publishInput(native_battle::Battle&battle,double now,std::optional<bool>siege,const drive::FrozenInput*frozen=nullptr){
    if(!std::isfinite(now))throw p::Error("Native input clock is invalid");auto access=drive::driverAccess(battle);auto&driver=*access.driver;
    auto*gun=frozen?weapon::advanceWeaponMotion(battle,now,frozen->state.speed,frozen->state.turn_speed):weapon::advanceWeapon(battle,now);auto current=drive::driverAccess(battle);if(current.driver!=access.driver||current.publication!=access.publication)return false;
    const auto vehicle=frozen?frozen->state:driver.state();const auto controls=frozen?frozen->controls:driver.controls();auto client=p::retain(battle.engine.client.get());
    auto args=p::Ref(PyTuple_Pack(1,client.get())),kwargs=p::dict();const bool braking=vehicle.siege_braking.has_value()||siege.has_value();
    p::set(kwargs.get(),"forward",p::number(braking?0.:controls.forward));p::set(kwargs.get(),"turn",p::number(braking?0.:controls.turn));p::set(kwargs.get(),"aim_yaw",p::number(controls.aim_yaw));p::set(kwargs.get(),"gun_pitch",p::number(controls.gun_pitch));p::set(kwargs.get(),"position",p::tupleNumbers(vehicle.position));p::set(kwargs.get(),"yaw",p::number(vehicle.yaw));p::set(kwargs.get(),"pitch",p::number(vehicle.pitch));p::set(kwargs.get(),"roll",p::number(vehicle.roll));p::set(kwargs.get(),"speed",p::number(vehicle.speed));p::set(kwargs.get(),"up_cosine",p::number(std::clamp(vehicle.surface_up_cosine.value_or(std::cos(vehicle.pitch)*std::cos(vehicle.roll)),-1.,1.)));
    auto&ledger=*access.publication;p::set(kwargs.get(),"ram_contacts",p::Ref(state::write(frozen?frozen->ram_contacts:ledger.ram_contacts)));p::set(kwargs.get(),"tank_pushes",p::Ref(state::write(frozen?frozen->tank_pushes:ledger.tank_pushes)));p::set(kwargs.get(),"turret_pushes",p::Ref(state::write(frozen?frozen->turret_pushes:ledger.turret_pushes)));p::set(kwargs.get(),"destructible_contacts",p::Ref(state::write(frozen?frozen->destructible_contacts:ledger.destructible_contacts)));
    if(gun){auto checkpoint=p::dict();p::set(checkpoint.get(),"reload_time",p::number(gun->reload_time));p::set(checkpoint.get(),"reload_duration",p::number(gun->reload_duration));p::set(checkpoint.get(),"clip",p::integer(gun->clip));p::set(checkpoint.get(),"clip_size",p::integer(gun->config.clip_size));p::set(checkpoint.get(),"dispersion",p::number(gun->dispersion));p::set(kwargs.get(),"gun_checkpoint",checkpoint);p::set(kwargs.get(),"shell_index",p::integer(gun->shot));p::set(kwargs.get(),"next_shell_index",p::integer(gun->pending.value_or(gun->shot)));p::set(kwargs.get(),"shell_change_pending",p::boolean(gun->pending.has_value()));}
    if(const auto anchor=native_battle::vehicles::poseMotionAnchor(battle)){const double elapsed=std::max(0.,now-anchor->second);const auto sample=static_cast<std::int64_t>(double(anchor->first)+elapsed*1000000.);p::set(kwargs.get(),"pose_time_us",p::integer(sample));}
    if(siege)p::set(kwargs.get(),"siege_enabled",p::boolean(*siege));p::Ref accepted(sendInput(nullptr,args.get(),kwargs.get()));if(!p::flag(accepted.get()))return false;
    // The transport owns the round's counter; the local vehicle keeps the sequence it just admitted.
    if(battle.local)battle.local->input_seq=p::integral(client.get(),"_input_seq");
    if(!hasCapability(client.get(),"ram_contact_ledger_v2"))throw p::Error("Server lost the RAM contact ledger contract");
    if(!ledger.destructible_contacts.array.empty()&&!hasCapability(client.get(),"human_ram_timeline_v1"))throw p::Error("Server lost the player pose timeline contract");
    trackRepair(client.get(),battle);return true;
}
}
bool flushBarrierInputsAt(native_battle::Battle&battle,double now){
    auto access=drive::driverAccess(battle);auto&ledger=*access.publication;
    while(!ledger.barriers().empty()){
        const auto frozen=ledger.barriers().front();
        if(!publishInput(battle,now,{},&frozen))return false;
        auto current=drive::driverAccess(battle);if(current.driver!=access.driver||current.publication!=access.publication)return false;
        if(!ledger.barrierAdmitted(frozen.sequence))throw p::Error("Native input barrier frontier changed during publication");
        access.driver->input_sent();
    }
    return true;
}
bool sendCurrentAt(native_battle::Battle&battle,double now,std::optional<bool>siege){
    return flushBarrierInputsAt(battle,now)&&publishInput(battle,now,siege);
}
bool flushLandingAt(native_battle::Battle&battle,double now){
    auto access=drive::driverAccess(battle);auto&ledger=*access.publication;
    if(ledger.landing_impacts.array.empty())return false;
    const auto observation=ledger.landing_impacts.array.front();
    const bool object=observation.resolved().kind==state::Value::Kind::Object;
    const auto speed=object?observation.get("impact_speed").numeric():observation.numeric();
    auto contacts=object?p::Ref(state::write(observation.get("module_contacts"))):p::list();
    if(!sendCurrentAt(battle,now))return false;
    auto current=drive::driverAccess(battle);if(current.driver!=access.driver||current.publication!=access.publication)return false;
    if(!stock_native_client_landing::sendObservation(battle.engine.client.get(),speed,contacts.get()))return false;
    current=drive::driverAccess(battle);if(current.driver!=access.driver||current.publication!=access.publication)return false;
    if(!ledger.landingAdmitted())throw p::Error("Native landing frontier changed during publication");
    access.driver->landing_admitted();return true;
}
bool sendCurrent(native_battle::Battle&battle,std::optional<bool>siege){return sendCurrentAt(battle,battle.clock(),siege);}
}
