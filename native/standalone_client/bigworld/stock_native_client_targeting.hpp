#pragma once
#include "stock_native_client_state.hpp"
#include "stock_native_client_query.hpp"
#include <map>
#include <set>
namespace stock_native_client_targeting {
using ID=std::int64_t;
struct LockStatus {bool exists=false,alive=false,visible=true,lan=false,visual=false,model=false,in_world=false;};
bool lockHolds(const LockStatus&);
using DeviceSignature=std::pair<std::vector<int>,std::vector<int>>;
DeviceSignature expertDevices(const stock_native_client_state::Value&critical,const std::map<std::string,int>&extras);
struct ExpertActor {ID engine_id=0;bool local=false,tombstone=false,visible=true,exists=false,alive=false;};
struct ExpertHide {ID engine_id=0;std::string reason;};
struct ExpertShow {ID engine_id=0;DeviceSignature devices;};
struct ExpertChanges {bool accepted=false;std::optional<ExpertHide>hide;std::optional<ExpertShow>show;};
class Expert {
    ID target_=0;double due_=0.,delay_;
    std::optional<DeviceSignature>signature_;
public:
    explicit Expert(double delay):delay_(delay){}
    ID target()const{return target_;}double due()const{return due_;}
    ExpertChanges monitor(ID,double now,bool capable,bool state_allowed,
        const ExpertActor*,ID own_team,ID target_team,const std::string&reason="target_clear");
    ExpertChanges tick(double now,bool capable,const ExpertActor*,
        const stock_native_client_state::Value&,const std::map<std::string,int>&);
    void shown(ID,const DeviceSignature&);
};
struct OutlineChanges {
    bool unchanged=false,refresh=false;
    std::optional<ID>add;
    std::optional<stock_native_client_query::OutlineDecline>dropped;
};
OutlineChanges outlineChanges(std::optional<ID>held,const stock_native_client_query::OutlinePick&);
class OutlineClock {
    double next_=0.,period_;
public:
    explicit OutlineClock(double seconds):period_(seconds){}
    bool begin(double now,bool blocked);
};
}
