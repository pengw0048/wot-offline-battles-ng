#if !defined(WOT_NATIVE_PRESENTATION_HOST_BUILD)
#include "pch.hpp"
#include "pyscript/script_math.hpp"
#endif
#include "stock_native_bot_presentation.hpp"
#include "stock_native_battle_bots.hpp"
#include "stock_native_bot_runtime.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_resource_lifecycle.hpp"
#include "stock_native_bot_services.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>

namespace stock_native_bot {
namespace p=stock_native_bot_python;
namespace {
using Pose=std::array<double,6>;
using native_battle::Value;
constexpr double pi=3.1415926535897932384626433832795;
double wrap(double angle){return angle-std::floor((angle+pi)/(2*pi))*(2*pi);}
double principal(double angle){return angle<-pi||angle>pi?wrap(angle):angle;}
void assign(PyObject*object,const char*name,const p::Ref&value){
    p::Ref key(PyString_FromString(name));
    if(PyObject_GenericSetAttr(object,key.get(),value.get())<0)throw p::Error("Native pose mirror assignment failed");
}
p::Ref field(PyObject*object,const char*name){
    if(PyDict_Check(object))return p::value(object,name);
    p::Ref key(PyString_FromString(name));auto*value=PyObject_GenericGetAttr(object,key.get());
    if(!value&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();return p::retain(Py_None);}
    return p::Ref(value);
}
bool equal(PyObject*a,PyObject*b){const int value=PyObject_RichCompareBool(a,b,Py_EQ);if(value<0)throw p::Error("Native pose cache comparison failed");return value!=0;}
#if defined(WOT_NATIVE_PRESENTATION_HOST_BUILD)
// The portable fixture supplies explicitly native matrix/vector test doubles.
extern PyObject* presentationVector(const Point&);
extern Point presentationXYZ(PyObject*);
extern double presentationLength(const Point&);
extern void presentationMatrix(PyObject*,const Point&,const Point&,bool);
#else
PyObject* presentationVector(const Point&v){return BW::Script::getData(BW::Vector3(float(v[0]),float(v[1]),float(v[2])));}
Point presentationXYZ(PyObject*object){BW::Vector3 value;if(BW::Script::setData(object,value)!=0)throw p::Error("Native pose Vector3 is invalid");return {value.x,value.y,value.z};}
double presentationLength(const Point& value){return BW::Vector3(float(value[0]),float(value[1]),float(value[2])).length();}
void presentationMatrix(PyObject*object,const Point&xyz,const Point&rotation,bool rotate){
    if(!BW::PyMatrix::Check(object))throw p::Error("Native pose provider is not a writable Matrix");
    auto*matrix=static_cast<BW::PyMatrix*>(object);
    if(rotate)matrix->setRotate(float(rotation[0]),float(rotation[1]),float(rotation[2]));
    matrix->translation(BW::Vector3(float(xyz[0]),float(xyz[1]),float(xyz[2])));
}
#endif
Point xyz(const Pose&pose){return {pose[0],pose[1],pose[2]};}
Point angles(const Pose&pose){return {pose[3],pose[4],pose[5]};}
void writeMatrix(PyObject*matrix,const Pose&pose){auto rotation=angles(pose);for(auto&v:rotation)v=principal(v);presentationMatrix(matrix,xyz(pose),rotation,true);}
void writeAim(PyObject*matrix,double yaw,double pitch){presentationMatrix(matrix,{0.,0.,0.},{yaw,pitch,0.},true);}
void retarget(PyObject*motion,const Pose&target,const std::optional<double>&relax,double now){
    auto animation=field(motion,"_animation");const auto target_value=p::tupleNumbers(target);
    if(animation.get()==Py_None){assign(motion,"_render_pose",target_value);return;}
    auto to=field(motion,"_render_to"),from=field(motion,"_render_from"),render=field(motion,"_render_pose");
    Pose current=render.get()==Py_None?target:p::numbers<6>(render.get());
    if(to.get()!=Py_None){current=p::numbers<6>(to.get());const double duration=p::scalar(field(motion,"_render_duration").get());
        if(from.get()!=Py_None&&duration>0.){const auto source=p::numbers<6>(from.get());const double ratio=(now-p::scalar(field(motion,"_render_started").get()))/duration;
            if(ratio<1.)for(unsigned i=0;i<6;++i)current[i]=source[i]+(i<3?current[i]-source[i]:wrap(current[i]-source[i]))*std::max(0.,ratio);}
    }
    auto first=field(motion,"_key_from"),second=field(motion,"_key_to");const double duration=relax.value_or(0.);
    if(duration<=0.||(render.get()==Py_None&&to.get()==Py_None)){assign(motion,"_render_from",p::retain(Py_None));assign(motion,"_render_to",p::retain(Py_None));assign(motion,"_render_pose",target_value);writeMatrix(first.get(),target);writeMatrix(second.get(),target);}
    else{if(current==target)return;writeMatrix(first.get(),current);writeMatrix(second.get(),target);
        const auto current_value=p::tupleNumbers(current);assign(motion,"_render_from",current_value);assign(motion,"_render_to",target_value);assign(motion,"_render_pose",current_value);
        assign(motion,"_render_started",p::number(now));assign(motion,"_render_duration",p::number(duration));}
    const auto zero=p::number(0.),end=p::number(std::max(duration,.001));
    p::Ref first_key(PyTuple_Pack(2,zero.get(),first.get())),second_key(PyTuple_Pack(2,end.get(),second.get())),keys(PyTuple_Pack(2,first_key.get(),second_key.get()));
    try{assign(animation.get(),"keyframes",keys);assign(animation.get(),"time",zero);}
    catch(const std::exception&){
        PyErr_Clear();assign(motion,"_animation",p::retain(Py_None));
        auto model=field(motion,"model");if(model.get()!=Py_None){auto matrix=field(motion,"matrix");assign(model.get(),"matrix",matrix);}
    }
}
void setPose(PyObject*motion,const Pose&pose,const std::optional<double>&relax,double now,bool moving){
    auto previous_position=field(motion,"position"),last=field(motion,"_last_pose_time");
    const Point previous=presentationXYZ(previous_position.get());
    p::Ref position(presentationVector(xyz(pose)));const Point actual=presentationXYZ(position.get());
    assign(motion,"position",position);assign(motion,"yaw",p::number(pose[3]));assign(motion,"pitch",p::number(pose[4]));assign(motion,"roll",p::number(pose[5]));
    auto matrix=field(motion,"matrix"),cache=field(motion,"_matrix_pose");
    const auto rotation=p::tupleNumbers(angles(pose)),location=p::tupleNumbers(actual);
    bool rotated=true,translated=true;
    if(PyTuple_Check(cache.get())&&PyTuple_GET_SIZE(cache.get())==3&&PyTuple_GET_ITEM(cache.get(),0)==matrix.get()){
        rotated=!equal(PyTuple_GET_ITEM(cache.get(),2),rotation.get());translated=rotated||!equal(PyTuple_GET_ITEM(cache.get(),1),location.get());}
    assign(motion,"_matrix_pose",p::retain(Py_None));
    if(translated)presentationMatrix(matrix.get(),actual,angles(pose),rotated);
    p::Ref matrix_cache(PyTuple_Pack(3,matrix.get(),location.get(),rotation.get()));assign(motion,"_matrix_pose",matrix_cache);
    Pose target=pose;std::copy(actual.begin(),actual.end(),target.begin());retarget(motion,target,relax,now);
    Point velocity{};
    if(moving&&last.get()!=Py_None&&now>p::scalar(last.get())){const double dt=now-p::scalar(last.get());
        for(unsigned i=0;i<3;++i)velocity[i]=double(float(float(actual[i]-previous[i])*float(1./dt)));}
    assign(motion,"_last_pose_time",p::number(now));
    auto filter=field(motion,"filter"),appearance=field(motion,"appearance");
    p::Ref native_velocity(presentationVector(velocity));
    // Use the engine's Vector3 length rather than an independently rounded
    // equivalent, including the same float32 behaviour at the gear boundary.
    const double speed=presentationLength(velocity);
    assign(filter.get(),"position",p::Ref(presentationVector(actual)));
    assign(filter.get(),"velocity",native_velocity);assign(filter.get(),"speed",p::number(speed));
    assign(appearance.get(),"gear",p::integer(moving&&speed>.01?1:0));
}
void setAim(PyObject*entity,const geometry::Vehicle&vehicle,double hull,double aim,double pitch){
    const double relative=vehicle.static_turret_yaw.value_or(wrap(aim-hull)),component_pitch=vehicle.static_gun_pitch.value_or(pitch);
    assign(entity,"_aim_yaw",p::number(aim));assign(entity,"_gun_pitch",p::number(pitch));
    auto appearance=field(entity,"appearance"),turret=field(appearance.get(),"turretMatrix"),gun=field(appearance.get(),"gunMatrix");
    writeAim(turret.get(),relative,0.);writeAim(gun.get(),0.,component_pitch);
}
}
std::size_t presentNativeBotPoses(native_battle::Battle&b,const Runtime&runtime,double now,const NativeServices&services,std::vector<std::string>&errors){
    using Bot=native_battle::BotAuthority::PresentedBot;
    auto&authority=*b.bots;const double epoch=double(runtime.clock().sample_us);
    // The pinned presentation order: slot, then team.
    std::vector<const Actor*> order;for(const auto&entry:runtime.actors())order.push_back(&entry.second);
    std::stable_sort(order.begin(),order.end(),[](const Actor*l,const Actor*r){return std::tie(l->slot,l->team)<std::tie(r->slot,r->team);});
    for(const auto*actor:order){const auto bot=actor->motion.id;
        auto*vehicle=b.vehicle("bot:"+std::to_string(bot));if(!vehicle)continue;
        try{
            if(!vehicle->ready){authority.presented.erase(bot);continue;}
            const auto*installed=services.find({offline_simulation::ActorKind::Bot,bot});if(!installed)continue;
            const auto&geometry=installed->geometry[installed->mode];const auto engine=vehicle->engine_id;
            p::Ref entity=b.entity(engine);if(entity.get()==Py_None)throw p::Error("Native pose entity is unavailable");
            auto presentation=field(entity.get(),"_offlineLANPresentation");
            if(presentation.get()!=Py_True)throw p::Error("Native worker pose requires its RemoteVehicle owner");
            auto&shown=authority.presented[bot];
            if(shown.vehicle!=vehicle||shown.engine_id!=engine||shown.generation!=b.generation){
                shown=Bot{};shown.vehicle=vehicle;shown.engine_id=engine;shown.generation=b.generation;authority.yaw_rates.erase(bot);}
            const auto&m=actor->motion;const auto&weapon=runtime.weapons().at({offline_simulation::ActorKind::Bot,bot});
            Pose pose={m.position[0],m.position[1],m.position[2],principal(m.yaw),principal(m.pitch),principal(m.roll)};
            const double aim=principal(actor->weapon.aim_yaw),pitch=principal(weapon.aim.gun_pitch),turret=principal(weapon.aim.turret_yaw);
            p::Ref position(presentationVector(xyz(pose)));const auto actual=presentationXYZ(position.get());
            const std::array<double,6> sample{{actual[0],actual[1],actual[2],pose[5],pose[4],pose[3]}};
            std::optional<double> relax;
            if(!shown.sample||*shown.sample!=sample){double rate=0.;
                if(shown.clock){const double elapsed=std::min(.5,now-(*shown.clock)[0]);if(elapsed>0.){rate=wrap(m.yaw-(*shown.clock)[1])/elapsed;relax=elapsed*1.35;}}
                shown.clock=std::array<double,3>{{now,m.yaw,0.}};shown.sample=sample;authority.yaw_rates[bot]=rate;}
            const bool alive=actor->combat.alive&&actor->combat.health>0;bool moving=alive&&(m.speed!=0.||m.airborne);
            if(alive)for(double v:{double(m.movement_direction),double(m.rotation_direction),m.vertical_speed,m.slide_speed,m.push_x,m.push_z,m.air_lateral_x,m.air_lateral_z})moving=moving||v!=0.;
            const double mode=double(actor->siege.mode);
            const std::array<double,11> signature{{pose[0],pose[1],pose[2],pose[5],pose[4],pose[3],m.speed,moving?epoch:-1.,alive?1.:0.,mode,0.}};
            if(shown.pose!=signature)try{shown.pose.reset();setPose(entity.get(),pose,relax,now,moving);shown.pose=signature;}catch(const std::exception&e){PyErr_Clear();errors.push_back(e.what());}
            const std::array<double,5> aim_signature{{pose[3],aim,pitch,pose[4],mode}};
            if(shown.aim!=aim_signature)try{shown.aim.reset();setAim(entity.get(),geometry,pose[3],aim,pitch);shown.aim=aim_signature;}catch(const std::exception&e){PyErr_Clear();errors.push_back(e.what());}
            const std::array<double,9> projectile{{pose[0],pose[1],pose[2],pose[3],pose[4],pose[5],turret,pitch,mode}};
            if(shown.projectile!=projectile){Value value;value.kind=Value::Kind::Object;const char*names[]={"x","y","z","yaw","pitch","roll","turret_yaw","gun_pitch"};
                for(unsigned i=0;i<8;++i){auto&v=value.object[names[i]];v.kind=Value::Kind::Number;v.number=projectile[i];}
                auto&siege=value.object["siege_state"];siege.kind=Value::Kind::Integer;siege.integer=actor->siege.mode;
                vehicle->presented.collision_pose=std::move(value);shown.projectile=projectile;}
        }catch(const std::exception&e){authority.presented.erase(bot);PyErr_Clear();errors.push_back(e.what());}
    }
    return order.size();
}
}
