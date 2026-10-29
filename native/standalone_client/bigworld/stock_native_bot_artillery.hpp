#pragma once
#include "stock_native_bot_step_state.hpp"
#include "offline_simulation_weapons.h"
#include <deque>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace wot::offline_bot_step {
struct ArcKey {
    std::int64_t actor=0, target=0, fire_sequence=0, descriptor_revision=0;
    std::string target_kind;
    int shell=0, arc_mask=0;
    Point source_position{}, source_angles{}, origin{};
    double yaw=0, pitch=0, speed=0, gravity=0, maximum=0, flight_time=0;
    bool operator<(const ArcKey&) const;
};
struct ArcCandidate {
    std::string arc;
    Point aim{}, origin{}, velocity{};
    double yaw=0, pitch=0, flight_time=0, speed=0, gravity=0, maximum=0;
    double proof_maximum_step=0;
    std::vector<Point> path;
    std::optional<Point> terminal_impact;
};
enum class ArcQuery { clear, hit, failed };
class ArcWorld {
public:
    virtual ~ArcWorld()=default;
    virtual ArcQuery segment(const Point&,const Point&,Point& hit)=0;
};
struct ArcResult {
    bool ready=false, local_blockage=false;
    std::string reason;
    std::optional<double> completed;
    std::optional<ArcCandidate> candidate;
};
class ArcQueue {
    struct Job {
        double created=0;
        Point target{};
        std::vector<ArcCandidate> candidates;
        std::size_t candidate=0,chord=0,blocks=0;
        bool all_hits=true,all_local=true;
    };
    struct Cached {double deadline=0;ArcResult result;};
    std::size_t max_jobs_,max_waiting_;
    double success_ttl_,failure_ttl_,max_age_,target_slop_;
    bool remote_impact_;
    std::map<ArcKey,Job> jobs_,waiting_;
    std::deque<ArcKey> order_,waiting_order_;
    std::map<ArcKey,Cached> results_;
    void purge(double now);
    void complete(const ArcKey&,double now,std::optional<ArcCandidate>,const std::string& reason="");
public:
    ArcQueue(std::size_t max_jobs,double success_ttl,double failure_ttl,
             double maximum_age,bool remote_impact,std::size_t max_waiting=64,double target_slop=7.);
    void reset();
    void discard(const ArcKey&);
    ArcResult result(const ArcKey&,double now);
    bool is_pending(const ArcKey&,double now);
    ArcResult request(const ArcKey&,std::vector<ArcCandidate>,Point target,double now);
    std::size_t advance(double now,std::size_t ray_budget,ArcWorld&);
    std::size_t pending() const { return jobs_.size()+waiting_.size(); }
};
struct ArtilleryInput {
    ArcKey identity;
    AimPose pose;
    Point target{},velocity{},origin{},barrel_point{};
    double speed=0,gravity=0,maximum=0;
    offline_simulation::weapons::AimConfig pitch_config;
    bool valid_pitch=false,source_available=false;
};
std::vector<ArcCandidate> artillery_candidates(const ArtilleryInput&);
std::optional<ArcCandidate> artillery_launch_candidate(const ArcKey&,double maximum_step=.12);
class ArtilleryController {
    struct Rejection {ArcKey key;Point target{};std::int64_t fire_sequence=0;int mask=0;double until=0;};
    ArcQueue planning_{8,2.5,.25,60.,false};
    ArcQueue launch_{8,.35,.25,40.,true};
    std::map<std::tuple<std::int64_t,std::string,std::int64_t,int>,ArcKey> planning_keys_;
    std::map<std::int64_t,ArcKey> launch_keys_;
    std::map<ArcKey,ArcCandidate> receipts_;
    std::map<std::tuple<std::int64_t,std::string,std::int64_t,int>,Rejection> rejected_;
public:
    void reset();
    ArcResult plan(const ArtilleryInput&,double now);
    ArcResult request_launch(ArcKey,ArcCandidate,double now);
    void cancel_launch(std::int64_t actor);
    void reject_launch_arc(const ArtilleryInput&,const std::string& arc,
                           std::optional<Point> planned_target,double now);
    std::size_t advance(double now,std::size_t ray_budget,ArcWorld&);
};
struct ArtilleryReproof {
    AimPose source;
    std::string target_kind, arc;
    std::int64_t target_id=0, fire_sequence=0;
    int shell=0, attempts=0;
    double speed=0, gravity=0, maximum=0, created=0, deadline=0, absolute_deadline=0;
    double proof_latency=0, proof_maximum_step=0;
    int proof_chords=0;
    std::optional<ArcCandidate> hold_solution;
};
struct ArtilleryIntent {
    ArcCandidate solution;
    Point target_position{};
    double yaw=0,pitch=0,created=0;
};
struct ArtilleryIntentState {
    std::optional<ArtilleryReproof> reproof;
    std::optional<ArtilleryIntent> intent;
};
struct WeaponFrame;
struct HydraulicConfig;
class DirectWorld;
struct LaneBody;
std::optional<ArcCandidate> artillery_reproof_solution(const ArtilleryInput&,
    const ArtilleryReproof&,const WeaponFrame&,const HydraulicConfig&);
bool artillery_reproof_current(ArtilleryIntentState&,ArtilleryController&,
    const ArtilleryInput&,std::int64_t next_fire,bool target_alive,double now);
bool begin_artillery_intent(ArtilleryIntentState&,ArtilleryController&,const ArtilleryInput&,
    const ArcCandidate&,std::int64_t next_fire,std::int64_t round_id,double speed,
    double dispersion,bool aligned,bool target_alive,double now);
bool artillery_receipt_stale(ArtilleryIntentState&,ArtilleryController&,
    const ArtilleryInput&,const ArcCandidate& receipt,bool target_alive,double now);
enum class ArtilleryProofStatus { pending, ready, failed, stale, friendly_blocked };
struct ArtilleryProof {
    ArtilleryProofStatus status=ArtilleryProofStatus::pending;
    std::optional<offline_simulation::weapons::LaunchRecord> launch;
    std::string reason;
    std::optional<double> completed;
    std::optional<double> shot_yaw;
    bool has_blocker=false;
    std::string blocker_kind;
    std::int64_t blocker_id=0;
    int blocker_team=0;
    Point blocker_position{};
    double blocker_radius=0;
};
ArtilleryProof prove_artillery_launch(ArtilleryIntentState&,ArtilleryController&,
    const ArtilleryInput&,const ArcCandidate& intended,std::int64_t next_fire,
    std::int64_t round_id,double vehicle_speed,double dispersion,bool aligned,
    bool target_alive,double now,double splash_radius,int team,
    const std::vector<LaneBody>&,DirectWorld&);
} // namespace wot::offline_bot_step
