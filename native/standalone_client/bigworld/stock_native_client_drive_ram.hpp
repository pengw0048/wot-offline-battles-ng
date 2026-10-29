#pragma once
#include "stock_native_client_state.hpp"
#include "offline_contact_roster.h"
#include <memory>
#include <optional>
#include <set>
#include <vector>
namespace stock_native_client_drive {
struct ContactPeer;
class PublicationLedger;
using RamPose=std::array<double,6>;
struct RamProfile {double spall_coefficient=0.,ramming_bonus=0.;};
// Physical instances, including secondary-role carriers, remain distinct.
RamProfile actualRamProfile(const stock_native_client_state::Value& effective,
    const std::set<std::string>& casualties);
struct RamLayer {double distance=0.;std::optional<double> armor,vehicle_damage_factor;std::string device;};
class RamSurface {
public:
    virtual ~RamSurface()=default;
    virtual std::optional<std::array<double,2>> hullSpan()const=0;
    virtual std::vector<RamLayer> intersections(const stock_native_client_state::Point&,
        const stock_native_client_state::Point&)const=0;
};
class RamGeometry {
public:
    virtual ~RamGeometry()=default;
    virtual std::shared_ptr<const RamSurface> capture(
        const std::shared_ptr<stock_native_client_state::Actor>&,
        const RamPose&,const std::array<double,4>& shape)const=0;
};
// Retains the query owner's exact Model/BSP resources; no Python read occurs.
std::shared_ptr<const RamGeometry> nativeRamGeometry();
class PlayerRam {
    class Impl;
    std::unique_ptr<Impl> impl_;
public:
    PlayerRam(PublicationLedger&,std::shared_ptr<stock_native_client_state::State>,
        std::shared_ptr<stock_native_client_state::Actor> local,
        std::shared_ptr<const RamGeometry>);
    ~PlayerRam();
    PlayerRam(const PlayerRam&)=delete;
    PlayerRam&operator=(const PlayerRam&)=delete;
    bool poll(const offline_contact::Body& own,const std::vector<ContactPeer>&,
        double local_pitch,double local_roll,std::optional<std::int64_t> motion_us,
        const RamProfile&,std::int64_t player_id);
    void retry();
    const std::set<std::int64_t>& activeEpisodes()const;
    std::size_t pendingProofs()const;
    // Native collision callbacks use the same frozen proof and retry owner.
    bool observe(const ContactPeer&,const stock_native_client_state::Point&hit,
        const stock_native_client_state::Point&own_velocity,
        const stock_native_client_state::Point&peer_velocity,
        std::int64_t motion_us,const offline_contact::Body&own,const RamProfile&);
};
}
