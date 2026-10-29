#if !defined(WOT_NATIVE_PRESENTATION_HOST_BUILD)
#include "pch.hpp"
#include "pyscript/script_math.hpp"
#endif
#include "stock_native_bot_presentation.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_resource_lifecycle.hpp"
#include "stock_native_bot_services.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace stock_native_bot {
namespace p=stock_native_bot_python;
namespace {
using Pose=std::array<double,6>;
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
void remove(PyObject*object,const char*name){
    if(PyDict_GetItemString(object,name)&&PyDict_DelItemString(object,name)<0)throw p::Error("Native pose cache removal failed");
}
bool equal(PyObject*a,PyObject*b){const int value=PyObject_RichCompareBool(a,b,Py_EQ);if(value<0)throw p::Error("Native pose cache comparison failed");return value!=0;}
void bump(PyObject*battle,const char*name){assign(battle,name,p::integer(p::integral(battle,name)+1));}
void clear(PyObject*record){for(const char*name:{"_authority_presentation_lifecycle","_authority_pose_signature","_authority_aim_signature","_authority_projectile_pose_cache"})remove(record,name);}
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
bool presentNativeBotPoses(PyObject*battle,PyObject*states,double now,const NativeServices&services,std::vector<std::string>&errors){
    auto records=p::value(battle,"_records"),bots=p::value(battle,"_bots"),times=p::value(battle,"_bot_pose_times"),rates=p::value(battle,"_bot_yaw_rates");
    const auto epoch=p::value(bots.get(),"_sample_time_us");bool applied=false;
    for(Py_ssize_t index=0;index<p::size(states);++index){auto state=p::item(states,index);if(!PyDict_Check(state.get()))continue;
        auto id=p::value(state.get(),"id");if(id.get()==Py_None)continue;const auto bot=p::wide(id.get());
        auto record=p::value(records.get(),("bot:"+std::to_string(bot)).c_str());if(!PyDict_Check(record.get()))continue;
        try{
            if(!p::truth(record.get(),"ready")){clear(record.get());continue;}
            const auto*installed=services.find({offline_simulation::ActorKind::Bot,bot});if(!installed)continue;
            const auto&vehicle=installed->geometry[installed->mode];const auto engine=p::integral(record.get(),"engine_id");
            p::Ref entity(readNativeEntity(battle,engine));if(entity.get()==Py_None)throw p::Error("Native pose entity is unavailable");
            auto presentation=field(entity.get(),"_offlineLANPresentation");
            if(presentation.get()!=Py_True)throw p::Error("Native worker pose requires its RemoteVehicle owner");
            const auto generation=p::integer(p::integral(battle,"_generation")),engine_value=p::integer(engine);p::Ref address(PyLong_FromVoidPtr(record.get()));
            p::Ref lifecycle(PyTuple_Pack(4,generation.get(),id.get(),engine_value.get(),address.get()));auto old_lifecycle=p::value(record.get(),"_authority_presentation_lifecycle");
            if(!equal(lifecycle.get(),old_lifecycle.get())){clear(record.get());p::set(record.get(),"_authority_presentation_lifecycle",lifecycle);
                for(auto*map:{times.get(),rates.get()})if(PyDict_GetItem(map,id.get())&&PyDict_DelItem(map,id.get())<0)throw p::Error("Native pose lifecycle reset failed");}
            Pose pose={p::numeric(state.get(),"x"),p::numeric(state.get(),"y"),p::numeric(state.get(),"z"),principal(p::numeric(state.get(),"yaw")),principal(p::numeric(state.get(),"pitch")),principal(p::numeric(state.get(),"roll"))};
            const double aim=principal(p::numeric(state.get(),"aim_yaw",pose[3])),pitch=principal(p::numeric(state.get(),"gun_pitch")),turret=principal(p::numeric(state.get(),"turret_yaw",wrap(aim-pose[3])));
            p::Ref position(presentationVector(xyz(pose)));const auto actual=presentationXYZ(position.get());const auto location=p::tupleNumbers(actual),rotation=p::tupleNumbers(Point{pose[5],pose[4],pose[3]});p::Ref sample(PyTuple_Pack(2,location.get(),rotation.get()));
            std::optional<double> relax;auto*old_time=PyDict_GetItem(times.get(),id.get());
            if(!old_time||!equal(PyTuple_GetItem(old_time,2),sample.get())){const auto stamp=p::number(now),yaw=p::number(p::numeric(state.get(),"yaw"));p::Ref entry(PyTuple_Pack(3,stamp.get(),yaw.get(),sample.get()));
                double rate=0.;if(old_time){const double elapsed=std::min(.5,now-p::scalar(PyTuple_GetItem(old_time,0)));if(elapsed>0.){rate=wrap(p::numeric(state.get(),"yaw")-p::scalar(PyTuple_GetItem(old_time,1)))/elapsed;relax=elapsed*1.35;}}
                if(PyDict_SetItem(times.get(),id.get(),entry.get())<0)throw p::Error("Native pose clock mirror failed");const auto value=p::number(rate);if(PyDict_SetItem(rates.get(),id.get(),value.get())<0)throw p::Error("Native pose turn mirror failed");}
            const double speed=p::numeric(state.get(),"speed");const bool alive=p::truth(state.get(),"alive",true)&&p::numeric(state.get(),"health",1.)>0.;bool moving=alive&&(speed!=0.||p::truth(state.get(),"airborne"));
            if(alive)for(const char*name:{"movement_dir","rotation_dir","vertical_speed","slide_speed","push_x","push_z","air_lateral_x","air_lateral_z"})moving=moving||p::numeric(state.get(),name)!=0.;
            const auto raw_position=p::tupleNumbers(xyz(pose)),velocity=p::number(speed),live=p::boolean(alive),mode=p::integer(p::integral(state.get(),"siege_state"));
            p::Ref signature(PyTuple_Pack(7,lifecycle.get(),raw_position.get(),rotation.get(),velocity.get(),moving?epoch.get():Py_None,live.get(),mode.get()));auto old_pose=p::value(record.get(),"_authority_pose_signature");
            if(equal(signature.get(),old_pose.get()))bump(battle,"_authority_pose_skips");else try{setPose(entity.get(),pose,relax,now,moving);p::set(record.get(),"_authority_pose_signature",signature);bump(battle,"_authority_pose_writes");}catch(const std::exception&e){remove(record.get(),"_authority_pose_signature");PyErr_Clear();errors.push_back(e.what());}
            const auto aim_values=p::tupleNumbers(std::array<double,4>{pose[3],aim,pitch,pose[4]});p::Ref aim_signature(PyTuple_Pack(3,lifecycle.get(),aim_values.get(),mode.get()));auto old_aim=p::value(record.get(),"_authority_aim_signature");
            if(equal(aim_signature.get(),old_aim.get()))bump(battle,"_authority_aim_skips");else try{setAim(entity.get(),vehicle,pose[3],aim,pitch);p::set(record.get(),"_authority_aim_signature",aim_signature);bump(battle,"_authority_aim_writes");}catch(const std::exception&e){remove(record.get(),"_authority_aim_signature");PyErr_Clear();errors.push_back(e.what());}
            const auto projectile_values=p::tupleNumbers(std::array<double,8>{pose[0],pose[1],pose[2],pose[3],pose[4],pose[5],turret,pitch});p::Ref projectile_signature(PyTuple_Pack(3,lifecycle.get(),projectile_values.get(),mode.get()));auto old_projectile=p::value(record.get(),"_authority_projectile_pose_cache"),collision=p::value(record.get(),"projectile_collision_pose");
            if(!PyTuple_Check(old_projectile.get())||PyTuple_GET_SIZE(old_projectile.get())!=2||!equal(PyTuple_GET_ITEM(old_projectile.get(),0),projectile_signature.get())||PyTuple_GET_ITEM(old_projectile.get(),1)!=collision.get()){
                auto value=p::dict();const char*names[]={"x","y","z","yaw","pitch","roll"};for(unsigned i=0;i<6;++i)p::set(value.get(),names[i],p::number(pose[i]));p::set(value.get(),"turret_yaw",p::number(turret));p::set(value.get(),"gun_pitch",p::number(pitch));p::set(value.get(),"siege_state",mode);
                p::Ref cache(PyTuple_Pack(2,projectile_signature.get(),value.get()));p::set(record.get(),"projectile_collision_pose",value);p::set(record.get(),"_authority_projectile_pose_cache",cache);}
            applied=true;
        }catch(const std::exception&e){clear(record.get());PyErr_Clear();errors.push_back(e.what());}
    }
    return applied;
}
}
