#pragma once
#include "stock_native_client_drive_contacts.hpp"
#include "stock_native_client_state.hpp"
namespace stock_native_client_drive {
struct HistoricalMotion {
    Point velocity{};
    std::optional<std::array<double,21>> witness;
    std::optional<std::array<ID,4>> bracket;
};
std::optional<HistoricalMotion> historicalMotion(const stock_native_client_state::Actor&,double cursor,ID player);
std::array<double,3> predictContactVelocity(const PlayerDescriptor&,const stock_native_client_state::Value&checkpoint,
    const std::array<double,4>&shape,double sample_time,double now,const std::vector<std::array<double,4>>&impulses,double fixed_step);
std::array<double,3>wreckContactStep(const PlayerDescriptor&,const std::array<double,4>&,
    double vx,double vz,double omega,double yaw,double dt,double normal_y);
class NativePeers {
    struct Entry {std::shared_ptr<stock_native_client_state::Actor>actor;PlayerDescriptor descriptor;bool dynamics=true;};
    std::map<std::string,Entry> entries_;
    std::vector<std::string> order_;
    std::map<ID,std::pair<ID,ID>> receipt_clocks_;
    PlayerContacts&contacts_;
    double broadphase_padding_,fixed_step_;
public:
    NativePeers(PlayerContacts&,double broadphase_padding,double fixed_step);
    void install(std::shared_ptr<stock_native_client_state::Actor>,PlayerDescriptor);
    void installGeometry(std::shared_ptr<stock_native_client_state::Actor>,PlayerDescriptor);
    void forget(const std::set<std::string>&current);
    std::vector<ContactPeer> gather(const PlayerState&,const PlayerDescriptor&,const Point&,double dt,
        double extra_reach,const std::set<ID>&active_episodes,ID player,std::optional<ID>motion_time);
    const PlayerDescriptor*descriptor(const std::string&)const;
};
}
