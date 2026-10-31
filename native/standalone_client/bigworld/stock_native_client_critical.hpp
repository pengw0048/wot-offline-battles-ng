#pragma once
#include "stock_native_client_drive_status.hpp"
#include <map>
#include <memory>
#include <optional>
namespace native_battle {struct Battle;}
namespace stock_native_client_critical {
namespace shared=stock_native_client_state;
using Critical=wot::offline_bot_step::CriticalState;
struct Metadata {
    std::map<std::string,double> maxima,regeneration;
    std::map<std::string,int> extra_indexes;
    double repair_speed_factor=1.,network_seconds=1.;
};
struct State {
    std::int64_t base_revision=0,next_sequence=0,server_revision=0;
    bool owned=false;
    double next_report_time=0.;
    shared::Value report;
    std::map<std::string,int> progress;
};
struct Progress {std::string name;int extra_index=0,percentage=0;double seconds_left=0.;bool close=false;};
struct Effects {
    std::optional<shared::Value> payload,report;
    std::vector<Progress> progress;
};
class Owner final:public stock_native_client_drive::TrackOverlay {
    Metadata metadata_;State state_;Critical tracks_;
    bool have_revision_=false;
    std::set<std::string> casualties_;
public:
    Owner(Metadata,Critical,State={},std::set<std::string>casualties={});
    void metadata(Metadata value){metadata_=std::move(value);}
    const State&state()const{return state_;}
    const Critical&tracks()const{return tracks_;}
    // The supplied current state is canonical except for this owner's two
    // track fields. All other module, fire and physical crew updates survive.
    bool accept(std::int64_t revision,std::int64_t base,std::int64_t ack,Critical&);
    void reconcile(const shared::Actor&,Critical&)override;
    Effects advance(Critical,double dt,double now,bool eligible,double repair_factor,
        const std::set<std::string>&casualties);
};
// The local owner lives on the battle's LocalVehicle; its callbacks are only discrete stock presentation.
void tick(native_battle::Battle&,double dt);
// One authority revision for the local vehicle; false when it is stale. `canonical` keeps the owned tracks.
bool reconcile(native_battle::Battle&,shared::Actor& local,const shared::Value& source,shared::Value* canonical);
void close(native_battle::Battle&);
// The pending local damage report (Null when none).
const shared::Value& report(const native_battle::Battle&);
}
