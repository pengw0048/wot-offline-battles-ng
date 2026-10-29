#pragma once
#include "stock_native_bot_combat.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace stock_native_player_environment {
using Point=std::array<double,3>;
using Critical=wot::offline_bot_step::CriticalState;
using Event=wot::offline_bot_step::CriticalEvent;
struct Pose {Point position{};double yaw=0.,pitch=0.,roll=0.;};
struct Metadata {Point minimum{},maximum{},mount{};std::map<std::string,double> maxima;std::set<std::string> crew;};
struct Player {std::int64_t id=0,input_sequence=0;bool native_remote=false;int siege_state=0;Pose pose;Metadata descriptor;Critical critical;};
struct Proposal {Critical state;std::vector<Event> events;};
struct Observation {std::int64_t id=0,input_sequence=0;int level=0;std::optional<Proposal> drowning;};
struct Batch {std::int64_t sequence=0;std::vector<Observation> observations;};
class World {public:virtual ~World()=default;
 // The native remote hydraulic body pose, if available; absent uses the copied pose.
 virtual std::optional<Pose> bodyPose(const Player&)=0;
 // Returns -1 for an unavailable/missing water hit, as the fixed source does.
 virtual double waterDepth(const Point&)=0;
};
class Owner {double elapsed_=0.;std::int64_t sequence_=0;
public:
 Owner(double elapsed=0.,std::int64_t sequence=0):elapsed_(elapsed),sequence_(sequence){}
 bool willSample(double dt,bool authority)const;
 std::optional<Batch> advance(double dt,bool authority,const std::vector<Player>&,World&);
 void acknowledge(std::int64_t sequence,bool sent){if(sent&&sequence==sequence_+1)sequence_=sequence;}
 double elapsed()const{return elapsed_;}std::int64_t sequence()const{return sequence_;}
};
std::optional<std::pair<Point,double>> hullSample(const Metadata&,const Pose&);
std::optional<Proposal> drowningProposal(const Critical&,const Metadata&);
}
