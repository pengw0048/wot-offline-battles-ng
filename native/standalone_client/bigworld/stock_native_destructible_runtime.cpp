#include "stock_native_destructible_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stock_native_destructible {
namespace {
constexpr double pi=3.14159265358979323846;
MaterialKey identity(const Destruction & event) {
    return {event.key.first,event.key.second,event.kind==Kind::Structure?event.material:-1};
}
double encoded(double value,double steps) {
    const double integer=std::trunc(steps*(value+pi)/(2.*pi));
    if(!std::isfinite(integer))throw std::invalid_argument("Destructible angle cannot be encoded");
    double remainder=std::fmod(integer,steps);if(remainder<0.)remainder+=steps;
    // CPython stores the multiplication before subtraction; forbid an FMA
    // across that boundary even on a host compiler that contracts by default.
    volatile double product=(remainder/steps)*(2.*pi);
    return product-pi;
}
std::uint64_t angleCode(double value,double steps) {
    const double integer=std::trunc(steps*(value+pi)/(2.*pi));if(!std::isfinite(integer))throw std::invalid_argument("Destructible angle cannot be encoded");
    double remainder=std::fmod(integer,steps);if(remainder<0.)remainder+=steps;return std::uint64_t(remainder);
}
int speed(double value) {
    const double magnitude=std::abs(value);if(!std::isfinite(magnitude))throw std::invalid_argument("Destructible speed is invalid");
    return magnitude>=3.?3:std::max(1,int(magnitude));
}
}
double encodedYaw(double yaw){return encoded(yaw,64.);}
double encodedTreePitch(double pitch){return encoded(pitch,65536.);}
Body makeBody(const Destruction & order,const Parameters & raw,double yaw,double pitch,int discrete_speed,bool touchdown_effects) {
    if(!(raw.scale>0.)||!std::isfinite(raw.scale))throw std::invalid_argument("Destructible scale is unavailable");
    Body body;body.order=order;body.initial=raw.initial;body.fall_yaw=yaw;body.pitch_constraint=pitch;
    body.height=raw.height*raw.scale;body.mass=raw.mass*raw.scale*raw.scale*raw.scale;
    body.stiffness=raw.stiffness*raw.scale*raw.scale;body.spring_angle=raw.spring_angle;
    body.spring_resist=raw.spring_resist*raw.scale*raw.scale;body.air_resist=raw.air_resist*raw.scale*raw.scale;
    body.bury_depth=raw.bury_depth*raw.scale;body.inertia=body.mass*body.height*body.height/3.;
    body.pitch_speed=(discrete_speed+1)*.45;body.touchdown_pending=order.kind==Kind::Falling||touchdown_effects;
    body.touchdown_effects=touchdown_effects;return body;
}
MoveResult moveBody(Body & body,double dt) {
    MoveResult result;double pitch=body.pitch;
    if(pitch<0.){body.pitch_speed=0.;body.pitch=0.;pitch=0.;}
    if(pitch>body.pitch_constraint){body.pitch_speed=0.;body.pitch=body.pitch_constraint;pitch=body.pitch_constraint;}
    const double mp=body.height*body.height*.25;
    double torque=.5*body.height*9.81*body.mass*std::sin(pitch);
    torque-=body.pitch_speed*body.air_resist*mp;
    if(pitch+body.spring_angle>body.pitch_constraint) {
        if(body.touchdown_pending){body.touchdown_pending=false;result.touchdown=true;}
        const double penetration=pitch+body.spring_angle-body.pitch_constraint;
        torque-=penetration*body.stiffness*mp;
        torque-=body.pitch_speed*body.spring_resist*mp;
    }
    if(body.inertia!=0.)body.pitch_speed+=dt*torque/body.inertia;
    body.simulation_time+=dt;
    if(std::abs(body.pitch_speed)+std::abs(torque)<pi/500.||body.simulation_time>8.){result.active=false;return result;}
    body.pitch+=dt*body.pitch_speed;return result;
}
bool Runtime::present(const Destruction & order,PresentationPhase phase) {
    if(!backend_.current())return false;
    const auto epoch=epoch_;
    const int module=order.kind==Kind::Structure?int(order.material-73):order.kind==Kind::Fragile?-1:0;
    ++counters_.present_calls;return backend_.present(order,module,phase)&&epoch==epoch_&&backend_.current();
}
bool Runtime::moduleChildEffect(const Destruction & order) {
    if(!present(order,PresentationPhase::StopLifetime))return false;
    return present(order,order.shot?PresentationPhase::Hit:PresentationPhase::Ram);
}
bool Runtime::geometry(const Destruction & order) {
    if(!backend_.current())return false;
    const auto epoch=epoch_;
    ++counters_.geometry;
    return backend_.geometry(order,[&](const Destruction & child){return epoch==epoch_&&moduleChildEffect(child);})&&epoch==epoch_&&backend_.current();
}
bool Runtime::apply(Record & record,double now) {
    const auto epoch=epoch_;
    const auto event=record.order;
    const std::uint64_t item=event.key.second;
    if(event.kind==Kind::Fragile)record.data=(item<<8)|(event.shot?1u:0u);
    else if(event.kind==Kind::Structure){if(event.material<73||event.material>=86)return false;record.data=(item<<8)|(std::uint64_t(event.material)<<1)|(event.shot?1u:0u);}
    else if(event.kind==Kind::Falling)record.data=(item<<8)|(angleCode(event.yaw,64.)<<2)|std::uint64_t(speed(event.speed));
    Parameters parameters;const auto snapshot=backend_.snapshot(record.order,parameters);
    if(epoch!=epoch_||!backend_.current())return false;
    if(snapshot==SnapshotStatus::Missing)return true;
    if(snapshot!=SnapshotStatus::Ready)return false;
    if(event.kind==Kind::Fragile||event.kind==Kind::Structure) {
        if(!present(event,PresentationPhase::StopLifetime))return false;
        if(event.kind==Kind::Fragile) {
            if(!present(event,PresentationPhase::Decay)||!present(event,event.shot?PresentationPhase::Hit:PresentationPhase::Ram))return false;
        } else if(!present(event,event.shot?PresentationPhase::Hit:PresentationPhase::Ram))return false;
        if(event.shot){record.delayed=true;record.due=now+.2;}
        else if(!geometry(event))return false;
        record.applied=true;
        if(event.kind==Kind::Fragile&&!present(event,PresentationPhase::RemoveLightSound))return false;
        return true;
    }
    if(event.kind!=Kind::Tree&&event.kind!=Kind::Falling)return false;
    double pitch=pi/2.;std::uint32_t flags=0;
    if(event.kind==Kind::Tree) {
        if(!backend_.fallPitch(event,event.yaw,pitch,flags)||epoch!=epoch_||!backend_.current())return false;
        record.data=(((item<<8)|(angleCode(event.yaw,64.)<<2)|std::uint64_t(speed(event.speed)))<<16)|angleCode(pitch,65536.);
        pitch=encodedTreePitch(pitch);
    }
    const double yaw=encodedYaw(event.yaw);
    double decoded_pitch=pi/2.;
    if(!backend_.fallPitch(event,yaw,decoded_pitch,flags)||epoch!=epoch_||!backend_.current())return false;
    if(event.kind==Kind::Falling)pitch=decoded_pitch;
    // Module constant is math.radians(60), not the animator's spring angle.
    const bool touchdown_effects=(flags&8)||pitch>pi/3.;
    if(!present(event,PresentationPhase::StopLifetime)||!present(event,PresentationPhase::Fracture))return false;
    auto body=makeBody(event,parameters,yaw,pitch,speed(event.speed),touchdown_effects);
    record.last_pose=body;
    bodies_.push_back(std::move(body));record.applied=true;
    // Stock __startUpdate invokes its first callback immediately with one
    // UPDATE_INTERVAL already banked. Existing bodies share that same update.
    if(bodies_.size()==1&&!updating_)update(.016666,now);
    if(epoch!=epoch_||!backend_.current())return false;
    if(event.kind==Kind::Tree&&!present(event,PresentationPhase::RemoveLightSound))return false;
    return true;
}
bool Runtime::destroy(const Destruction & order) {
    if(!backend_.current()||accepted_.count(identity(order)))return false;
    const auto epoch=epoch_;
    const auto at=accepted_.emplace(identity(order),Record{order,false,false,0.,{},{},0}).first;
    const bool was_applying=applying_;applying_=true;
    try {
        const bool result=apply(at->second,order.now);applying_=was_applying;
        if(epoch!=epoch_)return false;
        if(!result){accepted_.erase(at);return false;}
        if(at->second.applied)at->second.generation=backend_.generation(order);
        ++counters_.accepted;if(!at->second.applied)++counters_.queued;return true;
    } catch(...) {applying_=was_applying;if(epoch==epoch_)accepted_.erase(identity(order));throw;}
}
std::optional<std::uint64_t> Runtime::acceptedData(const Destruction& order)const {
    const auto found=accepted_.find(identity(order));return found==accepted_.end()?std::nullopt:found->second.data;
}
bool Runtime::applied(const Destruction& order)const {const auto found=accepted_.find(identity(order));return found!=accepted_.end()&&found->second.applied;}
void Runtime::mappingChanged(double now) {
    if(!std::isfinite(now))throw std::invalid_argument("Destructible mapping time is invalid");
    if(applying_||!backend_.current())return;
    applying_=true;
    try {
        for(auto & row:accepted_) {
            auto&record=row.second;
            if(!record.applied) {
                if(!apply(record,now))throw std::runtime_error("Queued native destructible could not be applied");
                if(record.applied)record.generation=backend_.generation(record.order);
            } else {
                const auto generation=backend_.generation(record.order);
                if(generation&&generation!=record.generation) {
                    if(!record.delayed){++counters_.replays;if(!backend_.replay(record.order,record.last_pose?&*record.last_pose:nullptr))throw std::runtime_error("Native destructible incarnation could not be restored");}
                    record.generation=generation;
                }
            }
        }
        applying_=false;
    }
    catch(...){applying_=false;throw;}
}
void Runtime::update(double elapsed,double now) {
    if(updating_||!backend_.current())return;
    if(!std::isfinite(elapsed)||elapsed<0.||!std::isfinite(now))throw std::invalid_argument("Destructible update time is invalid");
    updating_=true;
    try {
        for(auto & row:accepted_)if(row.second.delayed&&now>=row.second.due) {
            row.second.delayed=false;if(!geometry(row.second.order))throw std::runtime_error("Native destructible geometry completion failed");
        }
        if(elapsed>0.&&!bodies_.empty()) {
            const double count=std::ceil(elapsed/.02);const double step=elapsed/count;
            std::vector<std::size_t> removed;
            for(std::size_t index=0;index<bodies_.size();++index) {
                for(double i=0.;i<count;++i) {
                    const auto moved=moveBody(bodies_[index],step);++counters_.body_steps;
                    if(moved.touchdown&&bodies_[index].touchdown_effects) {
                        const auto order=bodies_[index].order;
                        if(!present(order,PresentationPhase::Touchdown))throw std::runtime_error("Native destructible touchdown presentation failed");
                        if(order.kind==Kind::Falling&&!present(order,PresentationPhase::TouchdownBreak))throw std::runtime_error("Native destructible break presentation failed");
                    }
                    if(!moved.active){removed.push_back(index);break;}
                }
            }
            // Like stock __update, retired bodies do not receive one last
            // transform. Their last actually published matrix remains owned.
            for(auto at=removed.rbegin();at!=removed.rend();++at)bodies_.erase(bodies_.begin()+*at);
            for(const auto & body:bodies_) {
                ++counters_.transforms;
                if(!backend_.position(body,std::max(1.-body.simulation_time/2.,0.)))throw std::runtime_error("Native destructible transform failed");
                const auto record=accepted_.find(identity(body.order));if(record!=accepted_.end())record->second.last_pose=body;
            }
        }
        updating_=false;
    } catch(...){updating_=false;throw;}
}
void Runtime::clear() {++epoch_;accepted_.clear();bodies_.clear();counters_={};applying_=updating_=false;}
}
