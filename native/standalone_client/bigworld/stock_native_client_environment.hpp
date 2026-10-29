#pragma once
#include "stock_native_client_state.hpp"
#include "native_visibility_core.h"
#include <functional>
#include <map>
#include <optional>
#include <set>
struct _object;
namespace stock_native_client_environment {
using Point=stock_native_client_state::Point;
struct Profile {
    native_visibility::Checkpoints checkpoints;
    double base_view=0.,misc_vision=1.,crew_vision=1.,binocular_factor=1.,binocular_delay=3.;
    bool binoculars=false,net=false;
    double net_delay=3.,shot_factor=1.;
    std::array<double,2> camouflage{},moving_aspect{0.,1.},still_aspect{0.,1.};
    std::vector<std::pair<std::int64_t,double>> optional_devices;
};
struct ActorView {
    std::string key;std::int64_t engine_id=0,id=0;int team=0;
    bool local=false,ready=false,alive=true,missing=false,tombstone=false,dead_marker_known=false;
    Point position{};double speed=0.,shot_until=0.;
    bool model=false,marker=false;double direct_until=0.,radio_until=0.,next_probe=0.;
};
struct Transition {std::string key;std::int64_t engine_id=0;bool model=false,marker=false;};
struct DeviceTransition {std::int64_t id=0;bool active=false;};
struct Frame {
    double now=0.;int local_team=0;std::string local_key;
    Point presentation_origin{};bool spg_aiming=false,hud_only=false,radio_only=false;
    double local_vision_factor=1.;
};
struct Output {
    std::vector<Transition> transitions;
    std::optional<double> vision_radius;
    std::vector<DeviceTransition> devices;
};
class Spotting {
    struct Memory {
        bool direct=false,desired_model=false,desired_marker=false,applied_model=false,applied_marker=false,applied=false;
        double until=0.,radio_until=0.,next_probe=0.;std::optional<double> still_since;
    };
    std::map<std::string,Memory> memory_;
    std::set<std::string> radio_allies_;
    std::map<std::string,double> radio_deadlines_;
    double next_update_=0.;std::optional<double> local_still_since_,published_vision_;
    std::map<std::int64_t,bool> devices_;
public:
    // Native numeric callbacks only. No Python business callable enters a tick.
    using Sight=std::function<bool(const ActorView&,const ActorView&,const Profile&,const Profile&,double,double,bool)>;
    Output frame(const Frame&,const std::vector<ActorView>&,const std::map<std::string,Profile>&,const Sight&);
    void seed(const ActorView&);
    void radio(const std::map<std::string,double>& deadlines,std::set<std::string> allies);
    void acknowledge(const Transition&);
    void forget(const std::set<std::string>&);
};
_object* create(_object*,_object*);
_object* ingest(_object*,_object*);
_object* frame(_object*,_object*);
_object* foliage(_object*,_object*);
_object* lifecycle(_object*,_object*);
_object* activate(_object*,_object*);
_object* radio(_object*,_object*);
_object* acknowledge(_object*,_object*);
_object* close(_object*,_object*);
}
