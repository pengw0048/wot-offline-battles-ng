#if !defined(WOT_NATIVE_PRESENTATION_HOST_BUILD)
#include "pch.hpp"
#include "pyscript/script_math.hpp"
#endif
#include "stock_native_bot_presentation.hpp"
#include "stock_native_battle_remote.hpp"
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
#if defined(WOT_NATIVE_PRESENTATION_HOST_BUILD)
// The portable fixture supplies explicitly native matrix/vector test doubles.
extern PyObject* presentationVector(const Point&);
extern Point presentationXYZ(PyObject*);
#else
PyObject* presentationVector(const Point&v){return BW::Script::getData(BW::Vector3(float(v[0]),float(v[1]),float(v[2])));}
Point presentationXYZ(PyObject*object){BW::Vector3 value;if(BW::Script::setData(object,value)!=0)throw p::Error("Native pose Vector3 is invalid");return {value.x,value.y,value.z};}
#endif
Point xyz(const Pose&pose){return {pose[0],pose[1],pose[2]};}
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
            if(shown.pose!=signature)try{shown.pose.reset();
                if(!native_battle::remote::setPose(b,engine,{{pose[0],pose[1],pose[2]}},pose[3],pose[4],pose[5],relax,now))throw p::Error("Native worker pose has no native remote owner");
                shown.pose=signature;}catch(const std::exception&e){PyErr_Clear();errors.push_back(e.what());}
            const std::array<double,5> aim_signature{{pose[3],aim,pitch,pose[4],mode}};
            if(shown.aim!=aim_signature)try{shown.aim.reset();
                const double static_aim=geometry.static_turret_yaw?pose[3]+*geometry.static_turret_yaw:aim,static_pitch=geometry.static_gun_pitch.value_or(pitch);
                native_battle::remote::setAim(b,engine,pose[3],static_aim,static_pitch);shown.aim=aim_signature;}catch(const std::exception&e){PyErr_Clear();errors.push_back(e.what());}
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
