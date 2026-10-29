#include "stock_native_bot_perception.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stock_native_bot_perception {
namespace c=offline_simulation::control;
namespace v=offline_visibility;
namespace w=stock_native_world;
namespace {
v::ActorKey key(const Key& value)
{return {{static_cast<std::int64_t>(value.kind),value.id}};}
double distance(const native_visibility::Vec3& a,const native_visibility::Vec3& b)
{
    const float x=float(a[0])-float(b[0]),y=float(a[1])-float(b[1]),z=float(a[2])-float(b[2]);
    const float sum=float(float(x*x+y*y)+z*z);
    return std::sqrt(sum);
}
}
Perception::Perception(c::Store& control,w::Registry& registry,w::Scene& scene,
    native_visibility::FoliageSnapshot foliage):control_(control),sight_(registry,scene,0x80)
{
    visibility_=v::open(std::move(foliage));
    if(visibility_<=0)throw std::runtime_error("Native visibility lifetime could not be created");
}
Perception::~Perception(){finish();if(visibility_>0)v::close(visibility_);}
v::FrontierActor Perception::frontier(const Actor& a)const
{
    v::FrontierActor out;out.key=key(a.config.key);out.identity=a.identity;
    out.target_available=a.target_available;out.checkpoints=a.checkpoints;
    out.observer_pose=a.observer_pose;out.target_pose=a.target_pose;return out;
}
void Perception::foliage(v::Update changes)
{if(!v::update(visibility_,std::move(changes)))throw std::runtime_error("Native foliage update was not accepted");}
void Perception::begin(const offline_simulation::TickToken& token,const std::vector<Actor>& rows,
    double now,int budget,bool include_humans,std::uint64_t phase)
{
    std::vector<c::ActorConfig> configs;std::vector<c::ActorSample> samples;
    std::vector<Key> order;std::vector<v::FrontierActor> frontiers;
    std::map<Key,Actor> actors;
    for(const auto& row:rows) {
        if(row.config.key!=row.sample.key||!actors.emplace(row.config.key,row).second)
            throw std::invalid_argument("Duplicate or mismatched perception actor");
        configs.push_back(row.config);samples.push_back(row.sample);order.push_back(row.config.key);
        frontiers.push_back(frontier(row));
    }
    for(const auto& previous:actors_)if(!actors.count(previous.first))control_.forget(previous.first);
    control_.configure(configs);control_.update(samples);
    if(!v::frontier_frame(visibility_,std::move(frontiers),phase,now))
        throw std::runtime_error("Native sight frame was not accepted");
    actors_=std::move(actors);phase_=phase;observations_.clear();
    control_.begin(token,order,budget,true,include_humans,now);open_=true;
}
void Perception::update(Actor actor)
{
    if(actor.config.key!=actor.sample.key)throw std::invalid_argument("Mismatched perception delta");
    control_.configure({actor.config});control_.update({actor.sample});
    if(!v::frontier_actor(visibility_,frontier(actor)))
        throw std::runtime_error("Native sight actor delta was not accepted");
    actors_[actor.config.key]=std::move(actor);
}
bool Perception::ray(const native_visibility::Ray& request)
{
    std::set<w::Key> members;
    const auto& registry=sight_.registry();
    for(const auto& id:registry.skin.destroyed)members.insert({std::get<0>(id),std::get<1>(id)});
    for(const auto& id:registry.skin.speculative)members.insert({std::get<0>(id),std::get<1>(id)});
    w::Hit hit;
    const auto result=sight_.horizontal(request.start,request.end,hit,&members);
    if(result==w::QueryStatus::Failed)throw std::runtime_error("Native sight ray failed");
    return result==w::QueryStatus::Miss||distance(hit.point,request.start)+1.5>=distance(request.end,request.start);
}
c::SightReply Perception::sight(const c::SightRequest& request)
{
    c::SightReply reply;reply.sampled_at=request.now;
    native_visibility::DetectionInputs inputs;
    const auto& d=request.detection;
    inputs.distance=d.distance;inputs.view_range=d.view;
    inputs.base_camouflage={{d.camo_moving,d.camo_still}};
    inputs.moving=d.moving;inputs.fired_recently=d.fired;inputs.additive=d.additive;
    inputs.multiplier=d.multiplier;inputs.shot_factor=d.shot_factor;
    try {
        const auto result=v::frontier_detect(visibility_,key(request.observer),key(request.target),
            request.now,request.fire_sequence,inputs,[&](const native_visibility::Ray& r){return ray(r);},
            [&]{return phase_;},request.world_query);
        reply.status=result.status==0?offline_simulation::QueryStatus::DetectionComplete:
            result.status==2?offline_simulation::QueryStatus::Pending:offline_simulation::QueryStatus::Failed;
        reply.has_detection=result.status==0;reply.detected=result.detected;reply.sampled_at=result.sampled_at;
    } catch(const std::exception&) {reply.status=offline_simulation::QueryStatus::Failed;}
    return reply;
}
void Perception::events(bool aggregate)
{
    for(const auto& event:control_.take_observations()) {
        const auto source=actors_.find(event.observer),target=actors_.find(event.target);
        if(source==actors_.end()||target==actors_.end())continue;
        const TeamKey k{source->second.config.team,event.target};
        auto& memory=remembered_[k];memory.pose=event.pose;
        memory.until=std::max(memory.until,event.sampled_at+std::min(12.,std::max(0.,event.duration)));
        if(aggregate) {
            auto& observation=observations_[k];observation.visible=true;observation.pose=event.pose;
            (event.observer.kind==offline_simulation::ActorKind::Human?observation.human_observers:
                observation.bot_observers).insert(event.observer.id);
        }
    }
}
std::vector<Contact> Perception::contacts(const Key& source)
{
    auto result=control_.contacts(source,[&](const c::SightRequest& request){return sight(request);});
    events(false);return result;
}
void Perception::observeHumans()
{
    std::set<int> teams;
    for(const auto& a:actors_)if(a.first.kind==offline_simulation::ActorKind::Human)teams.insert(a.second.config.team);
    for(const int team:teams)for(const auto& a:actors_)
        if(a.second.target_available&&a.second.config.team!=team) {
            auto& row=observations_[{team,a.first}];row.pose=a.second.sample.pose;
        }
    control_.observe_humans([&](const c::SightRequest& request){return sight(request);});events(true);
}
void Perception::finish(){if(open_){control_.finish();open_=false;}}
std::vector<c::TeamContact> Perception::teamContacts()const{return control_.team_contacts();}
v::FrontierSnapshot Perception::diagnostics()const{return v::frontier_snapshot(visibility_);}
}
