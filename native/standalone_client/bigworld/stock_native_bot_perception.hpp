#pragma once
#include "offline_simulation_control.h"
#include "offline_visibility.h"
#include "stock_native_world_pipeline.hpp"
#include <map>
#include <set>

namespace stock_native_bot_perception {
using Key=offline_simulation::ActorKey;
using Pose=offline_simulation::control::PoseSample;
using Contact=offline_simulation::control::Contact;
struct Actor {
    offline_simulation::control::ActorConfig config;
    offline_simulation::control::ActorSample sample;
    native_visibility::Checkpoints checkpoints;
    native_visibility::Pose observer_pose,target_pose;
    std::uint64_t identity=0;
    bool target_available=false;
};
struct TeamKey {
    int team=0;Key target;
    bool operator<(const TeamKey& other)const
    {return team!=other.team?team<other.team:target<other.target;}
};
struct Remembered {
    Pose pose;
    double until=0.;
};
struct Observation {
    bool visible=false;
    Pose pose;
    std::set<std::int64_t> human_observers,bot_observers;
};
// Owns both the current roster and asynchronous geometry preparation. The
// callbacks passed to the release cores are C++ closures over typed owners.
class Perception {
    offline_simulation::control::Store& control_;
    stock_native_world::Pipeline sight_;
    std::int64_t visibility_=0;
    std::map<Key,Actor> actors_;
    std::map<TeamKey,Remembered> remembered_;
    std::map<TeamKey,Observation> observations_;
    std::uint64_t phase_=0;
    bool open_=false;
    offline_visibility::FrontierActor frontier(const Actor&)const;
    offline_simulation::control::SightReply sight(const offline_simulation::control::SightRequest&);
    bool ray(const native_visibility::Ray&);
    void events(bool aggregate);
public:
    Perception(offline_simulation::control::Store&,stock_native_world::Registry&,
        stock_native_world::Scene&,native_visibility::FoliageSnapshot);
    ~Perception();
    Perception(const Perception&)=delete;
    Perception& operator=(const Perception&)=delete;
    void foliage(offline_visibility::Update);
    void begin(const offline_simulation::TickToken&,const std::vector<Actor>&,
        double now,int budget,bool include_humans,std::uint64_t observer_phase);
    void update(Actor);
    std::vector<Contact> contacts(const Key&);
    const offline_simulation::control::Store& control()const{return control_;}
    void rememberPose(const TeamKey&key,const Pose&pose){remembered_[key].pose=pose;}
    void observeHumans();
    void finish();
    const std::map<TeamKey,Remembered>& remembered()const{return remembered_;}
    const std::map<TeamKey,Observation>& observations()const{return observations_;}
    std::vector<offline_simulation::control::TeamContact> teamContacts()const;
    offline_visibility::FrontierSnapshot diagnostics()const;
};
}
