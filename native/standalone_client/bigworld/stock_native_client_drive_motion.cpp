#include "stock_native_client_drive_motion.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace stock_native_client_drive {
namespace {namespace world=stock_native_world;constexpr double pi=3.14159265358979323846;
bool pass(world::ContactStatus status){return status==world::ContactStatus::Clear||status==world::ContactStatus::Crushed||status==world::ContactStatus::Approach;}
std::string kinds(const std::set<world::Kind>&values){std::set<std::string>names;for(auto value:values)switch(value){case world::Kind::Tree:names.insert("tree");break;case world::Kind::Falling:names.insert("falling");break;case world::Kind::Fragile:names.insert("fragile");break;case world::Kind::Structure:names.insert("structure");break;case world::Kind::Other:names.insert("other");break;}std::string result;for(const auto&name:names){if(!result.empty())result+=',';result+=name;}return result.empty()?"-":result;}
offline_destructible::Pose sweep(const PlayerDescriptor&descriptor,const PlayerState&state,const Point&position,double yaw,double speed,double dt,std::optional<double>motion={}){offline_destructible::Pose out;for(unsigned i=0;i<3;++i)out.position[i]=float(position[i]);out.minimum=descriptor.motion.body_minimum;out.maximum=descriptor.motion.body_maximum;out.yaw=yaw;out.pitch=state.pitch;out.roll=state.roll;out.travel=speed*std::max(0.,dt);out.explicit_motion_yaw=bool(motion);out.motion_yaw=motion.value_or(0.);return out;}
}
world::ContactResult mergePlayerProposals(const world::ContactResult&catalog,const world::ContactResult&tree,bool consumed){
    auto out=catalog;const auto rank=[](world::ContactStatus status){switch(status){case world::ContactStatus::Hard:return 0;case world::ContactStatus::Pending:return 1;case world::ContactStatus::Kinetic:return 2;case world::ContactStatus::Crushed:return 4;case world::ContactStatus::Approach:return 5;case world::ContactStatus::Clear:return 6;case world::ContactStatus::Failed:throw std::runtime_error("Native player motion proposal failed");}throw std::runtime_error("Native player motion proposal is invalid");};
    if(rank(tree.status)<rank(catalog.status))out.status=tree.status;
    // Consumed trees retain their verdict and accepted bit; their token must
    // never become a second catalog commit or a duplicate reliable receipt.
    if(!consumed&&tree.requires_commit)out.contacts.insert(tree.contacts.begin(),tree.contacts.end());
    out.requires_commit=catalog.requires_commit||(!consumed&&tree.requires_commit);out.accepted_now=catalog.accepted_now||tree.accepted_now||consumed;out.pending=catalog.pending||tree.pending;out.kinds.insert(tree.kinds.begin(),tree.kinds.end());if(tree.status!=world::ContactStatus::Clear||!tree.contacts.empty())out.kinds.insert(world::Kind::Tree);if(out.contacts.size()>64)throw std::runtime_error("Native player motion token exceeds its bound");return out;
}
SceneMotion::SceneMotion(world::Pipeline&pipeline,PublicationLedger&publication,const PlayerDescriptor&descriptor,const Controls&controls,MotionConfig config):pipeline_(pipeline),publication_(publication),descriptor_(descriptor),controls_(controls),config_(config){if(config.maximum_angle_step<=0.||!config.maximum_steps||config.drivable_gradient<=0.)throw std::invalid_argument("Native player motion sweep metadata is invalid");}
double SceneMotion::driveCap(double speed)const{const bool reverse=speed<0.;double cap=reverse?descriptor_.motion.drive.reverse:descriptor_.motion.drive.forward;const auto travel=reverse?config_.travel_reverse:config_.travel_forward;if(travel&&cap>0.)cap=std::max(cap,*travel);if(!std::isfinite(cap)||cap<0.)throw std::runtime_error("Native player crush speed cap is invalid");return reverse?-cap:cap;}
void SceneMotion::barrier(const PlayerState&state,const Point&position,double yaw){publication_.barrier(state,controls_,position,yaw);}
void SceneMotion::trace(PlayerState&state,const stock_motion_rotation::Trace&value){if(!value.present)return;state.trace.point=Point{value.response.point[0],value.response.point[1],value.response.point[2]};state.trace.normal=Point{value.response.normal[0],value.response.normal[1],value.response.normal[2]};state.trace.reason=std::to_string(value.request.reason);}
void SceneMotion::result(PlayerState&state,const world::ContactResult&value){state.trace.kinds=kinds(value.kinds);switch(value.status){case world::ContactStatus::Clear:state.trace.status=ContactStatus::Clear;break;case world::ContactStatus::Crushed:state.trace.status=ContactStatus::Crushed;break;case world::ContactStatus::Approach:state.trace.status=ContactStatus::Approach;break;case world::ContactStatus::Kinetic:state.trace.status=ContactStatus::Soft;break;case world::ContactStatus::Pending:state.trace.status=ContactStatus::Pending;break;case world::ContactStatus::Hard:state.trace.status=ContactStatus::Hard;break;case world::ContactStatus::Failed:throw std::runtime_error("Native player scene query failed");}}
bool SceneMotion::commitTree(PlayerState&state,const world::ContactResult&proposal,const Point&start,double yaw,const Point&end,double end_yaw,double speed,double dt,double pivot){
    if(proposal.status!=world::ContactStatus::Crushed||!proposal.requires_commit)return true;
    if(proposal.contacts.empty())throw std::runtime_error("Native player tree proposal lost its exact token");
    const auto committed=pipeline_.trees(start,yaw,end,end_yaw,descriptor_.motion.hull_minimum,descriptor_.motion.hull_maximum,speed,dt,pivot,true,&proposal.contacts);
    if(committed.status==world::ContactStatus::Pending){state.trace.status=ContactStatus::Pending;return false;}
    if(committed.status!=world::ContactStatus::Crushed||committed.contacts!=proposal.contacts){state.trace.status=ContactStatus::Hard;return false;}
    if(!publication_.queueDestructible(proposal,start,yaw,end,end_yaw,speed,dt)){state.trace.status=ContactStatus::Hard;return false;}return true;
}
bool SceneMotion::commitCatalog(PlayerState&state,const world::ContactResult&proposal,const Point&start,double yaw,const Point&end,double end_yaw,double speed,double dt,std::optional<double>catalog_speed){
    for(const auto&key:proposal.contacts)if(!pipeline_.registry().destroyed(key)&&!pipeline_.commit(key,start,catalog_speed.value_or(speed),yaw)){state.trace.status=ContactStatus::Hard;return false;}
    if(!publication_.queueDestructible(proposal,start,yaw,end,end_yaw,speed,dt))return false;return true;
}
bool SceneMotion::translation(PlayerState&state,const Point&start,double yaw,double speed,double dt,bool powered,std::optional<double>hull){
    state.trace={};const double orientation=hull.value_or(yaw),travel=hull?yaw+(speed<0.?pi:0.):orientation+(speed<0.?pi:0.);
    const Point end{start[0]+std::sin(travel)*std::abs(speed)*dt,start[1],start[2]+std::cos(travel)*std::abs(speed)*dt};
    world::ActorInputs actor;actor.mass=descriptor_.motion.drive.mass;actor.now=now_;actor.yaw=orientation;actor.speed=speed;actor.allow_kinetic=powered;actor.has_kinetic_speed=powered;actor.kinetic_speed=powered?driveCap(speed):0.;actor.commit_enabled=true;pipeline_.actor(actor);
    const auto pose=sweep(descriptor_,state,start,orientation,speed,dt,hull?std::optional<double>{travel}:std::nullopt);
    const auto catalog=pipeline_.catalogMotion(pose,speed,dt,true,powered?&pose:nullptr,false,true);
    const auto trees=pipeline_.trees(start,orientation,end,orientation,descriptor_.motion.hull_minimum,descriptor_.motion.hull_maximum,speed,dt,0.,false);
    const bool tree_contact=trees.status==world::ContactStatus::Crushed&&trees.requires_commit;
    if(!commitTree(state,trees,start,orientation,end,orientation,speed,dt,0.))return false;
    const auto proposal=mergePlayerProposals(catalog,trees,tree_contact);
    result(state,proposal);
    if(proposal.status==world::ContactStatus::Pending){if(tree_contact)barrier(state,start,orientation);return false;}
    if(proposal.requires_commit){if(!commitCatalog(state,proposal,start,orientation,end,orientation,speed,dt)){if(tree_contact)barrier(state,start,orientation);return false;}barrier(state,start,orientation);}
    else if(tree_contact)barrier(state,start,orientation);
    // All subsequent native rays are read-only. The speed cap nominated an
    // exact destructible; it never becomes realised chassis travel.
    actor.commit_enabled=false;pipeline_.actor(actor);WorldSnapshot input{};for(unsigned i=0;i<3;++i)input.position[i]=float(start[i]);input.yaw=orientation;input.speed=speed;input.dt=dt;input.pitch=state.pitch;input.roll=state.roll;input.passive=hull?1u:0u;input.motion_yaw=travel;input.airborne=state.airborne;
    input.bounds[0]=descriptor_.motion.body_minimum[0];input.bounds[1]=descriptor_.motion.body_maximum[0];input.bounds[2]=-descriptor_.motion.body_minimum[2];input.bounds[3]=descriptor_.motion.body_maximum[2];input.heights[0]=descriptor_.motion.body_minimum[1];input.heights[1]=descriptor_.motion.body_maximum[1];
    const auto native=pipeline_.world(input);trace(state,native.trace);if(native.status<0)throw std::runtime_error("Native player horizontal query failed");if(native.status==1){state.trace.status=ContactStatus::Hard;return false;}
    if(proposal.requires_commit)return pass(proposal.status);
    // The fixed player's final catalog query is read-only and cannot commit a
    // token which was not proved by the producing proposal above.
    actor.commit_enabled=false;pipeline_.actor(actor);const auto detail=pipeline_.catalogMotion(pose,speed,dt,false,powered?&pose:nullptr,false,false);result(state,detail);
    if(detail.used_kinetic_speed&&(!detail.accepted_now||detail.status!=world::ContactStatus::Crushed||detail.contacts.empty()))throw std::runtime_error("Native player crush receipt is inconsistent");
    return pass(detail.status);
}
bool SceneMotion::rotation(PlayerState&state,const Point&start,double from,const Point&end,double to,double speed,double dt,double traverse_factor){
    state.trace={};world::ActorInputs actor;actor.mass=descriptor_.motion.drive.mass;actor.now=now_;actor.yaw=from;actor.speed=speed;actor.commit_enabled=true;pipeline_.actor(actor);
    world::RotationRequest request;auto&input=request.world;input={};input.position=start;input.body={descriptor_.motion.body_minimum,descriptor_.motion.body_maximum};input.start_yaw=from;input.end_yaw=to;input.pitch=state.pitch;input.roll=state.roll;
    const double x=std::cos(from)-std::cos(to),z=std::sin(to)-std::sin(from),norm=x*x+z*z;
    if(!descriptor_.rotation_around_center&&norm>1.e-18){const double offset=((end[0]-start[0])*x+(end[2]-start[2])*z)/norm;if(std::abs(offset)<=descriptor_.motion.track_center+1.e-7&&std::abs(end[0]-start[0]-offset*x)<=1.e-7&&std::abs(end[2]-start[2]-offset*z)<=1.e-7&&std::abs(end[1]-start[1])<=1.e-7)input.pivot_offset=offset;}
    input.translation={end[0]-start[0]-input.pivot_offset*x,end[2]-start[2]-input.pivot_offset*z};input.max_angle_step=config_.maximum_angle_step;input.max_steps=config_.maximum_steps;input.drivable_gradient=config_.drivable_gradient;input.include_static=std::hypot(input.translation[0],input.translation[1])>1.e-9;input.replacement_reader_available=true;
    if(replacement_generation_!=pipeline_.registry().generation){replacement_active_=false;for(const auto&key:pipeline_.registry().skin.destroyed){const auto row=pipeline_.registry().instances.find({std::get<0>(key),std::get<1>(key)});if(row!=pipeline_.registry().instances.end()&&row->second.retained_collision){replacement_active_=true;break;}}replacement_generation_=pipeline_.registry().generation;}input.replacement_active=replacement_active_;
    request.geometry_end=end;request.hull_minimum=descriptor_.motion.hull_minimum;request.hull_maximum=descriptor_.motion.hull_maximum;request.dt=dt;request.has_rotation_speed_cap=true;request.rotation_speed_cap=descriptor_.motion.drive.rotation*traverse_factor;request.has_drive_speed_cap=request.rotation_speed_cap!=0.;request.drive_speed_cap=request.has_drive_speed_cap?std::abs(driveCap(speed)):0.;
    const auto catalog=pipeline_.poseSweep(request,false);const auto trees=pipeline_.trees(start,from,end,to,request.hull_minimum,request.hull_maximum,speed,dt,input.pivot_offset,false);const bool tree_contact=trees.status==world::ContactStatus::Crushed&&trees.requires_commit;
    if(!commitTree(state,trees,start,from,end,to,speed,dt,input.pivot_offset))return false;const auto proposal=mergePlayerProposals(catalog,trees,tree_contact);result(state,proposal);
    if(proposal.status==world::ContactStatus::Pending){if(tree_contact)barrier(state,start,from);return false;}
    if(proposal.requires_commit){if(!commitCatalog(state,proposal,start,from,end,to,speed,dt,proposal.impact_speed)){if(tree_contact)barrier(state,start,from);return false;}barrier(state,start,from);}
    else if(tree_contact)barrier(state,start,from);
    if(!pass(proposal.status))return false;actor.commit_enabled=false;pipeline_.actor(actor);const auto native=pipeline_.rotation(input);trace(state,native.blocking_trace);if(!native.clear){state.trace.status=ContactStatus::Hard;return false;}return true;
}
}
