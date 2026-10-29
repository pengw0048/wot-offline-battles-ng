#pragma once
#include "stock_native_detached_turret_runtime.hpp"
namespace stock_native_detached_turret {
struct Impulse {Point velocity{},spin{};};
struct LaunchInput {
    Point position{},attitude{},mount{};
    double turret_yaw=0.,clearance=0.;
    bool ready=false,detached=false,geometry_ready=false,exploded_models=false;
};
std::uint32_t launchSeed(const std::string& round_id,Owner);
Impulse launchImpulse(std::uint32_t seed);
Flight resolveFlight(const Point& origin,const Point& velocity,World&,double clearance=0.,
    double step=flight_step_seconds,double limit=8.,int deflections=3);
Row freezeVisualPlan(Owner,const LaunchInput&,std::uint32_t seed,World&);
class LaunchSource {
public:
    virtual ~LaunchSource()=default;
    virtual std::optional<LaunchInput> lookup(Owner)=0;
};
class LaunchQueue {
    std::string round_id_;
    std::map<Owner,Row> accepted_,proposals_;
    std::map<Owner,double> retries_;
    std::vector<Owner> retry_order_;
public:
    explicit LaunchQueue(std::string round_id):round_id_(std::move(round_id)){}
    std::vector<Failure> failures;
    bool queue(Owner,const LaunchInput&,double now,World&);
    void retry(double now,LaunchSource&,World&);
    bool accept(const Row&);
    void retryAt(Owner owner,double due){if(!retries_.count(owner))retry_order_.push_back(owner);retries_[owner]=due;}
    void proposal(Row);
    std::vector<Row> rows()const;
    const std::map<Owner,Row>& proposals()const{return proposals_;}
    bool pending()const{return !proposals_.empty();}
};
}
