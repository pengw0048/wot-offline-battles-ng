#pragma once
#include "stock_native_client_state.hpp"
#include "stock_native_bot_combat.hpp"
#include <memory>
namespace stock_native_client_drive {
using CriticalStat=wot::offline_bot_step::CriticalStat;
struct StatusDescriptor {
    std::map<std::string,double> maxima;
    std::map<std::string,double> regeneration;
    std::map<std::string,std::set<std::string>> crew_roles;
};
wot::offline_bot_step::CriticalState decodeCritical(
    const stock_native_client_state::Value&,const StatusDescriptor&,
    std::set<std::string>* physical_casualties=nullptr);
class TrackOverlay {
public:
    virtual ~TrackOverlay()=default;
    virtual void reconcile(const stock_native_client_state::Actor&,
        wot::offline_bot_step::CriticalState&)=0;
};
class Status {
    std::weak_ptr<stock_native_client_state::Actor> actor_;
    StatusDescriptor descriptor_;
    bool installed_=false,has_stun_=false;
    std::uint64_t revision_=0;
    stock_native_client_state::Value stun_signature_;
    std::map<std::string,double> factors_;
    std::set<std::string> casualties_;
    wot::offline_bot_step::CriticalState critical_;
    std::weak_ptr<TrackOverlay> track_overlay_;
public:
    Status(std::shared_ptr<stock_native_client_state::Actor>,StatusDescriptor);
    void descriptor(StatusDescriptor value){descriptor_=std::move(value);installed_=false;}
    void update(std::uint64_t accepted_revision,std::optional<double> accepted_server_ms);
    double factor(CriticalStat)const;
    double factor(const std::string&)const;
    double stunFactor(const std::string&)const;
    const wot::offline_bot_step::CriticalState&critical()const{return critical_;}
    const std::set<std::string>&casualties()const{return casualties_;}
    const StatusDescriptor&metadata()const{return descriptor_;}
    void trackOverlay(std::shared_ptr<TrackOverlay> value){track_overlay_=std::move(value);}
    void localTracks(const wot::offline_bot_step::CriticalState&);
};
std::shared_ptr<Status> installStatus(std::shared_ptr<stock_native_client_state::Actor>,StatusDescriptor);
// Requires the current battle's installed status producer. No healthy default
// or Python stat/crew/module call is used when an actor is not installed.
double statusFactor(const stock_native_client_state::Actor&,CriticalStat);
std::shared_ptr<const Status> status(const stock_native_client_state::Actor&);
std::shared_ptr<Status> localStatus(const stock_native_client_state::Actor&);
class StatusOwner {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    StatusOwner();~StatusOwner();
    StatusOwner(const StatusOwner&)=delete;
    StatusOwner&operator=(const StatusOwner&)=delete;
    void synchronize(stock_native_client_state::State&,double now,std::optional<double> server_ms={});
};
}
