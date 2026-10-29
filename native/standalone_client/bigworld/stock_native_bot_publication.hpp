#pragma once
#include "stock_native_bot_combat.hpp"
#include "stock_native_bot_contacts.hpp"
#include "offline_simulation_weapons.h"

namespace stock_native_bot_publication {
using ID=std::int64_t;
enum Flag:std::uint32_t {
    Alive=1u<<0,WorldPose=1u<<1,AmmoReloadPending=1u<<2,BurstActive=1u<<3,
    Fire=1u<<4,AmmoRackDeath=1u<<5,ShotAngles=1u<<6,Critical=1u<<7,
    Equipment=1u<<8,Ammo=1u<<9,Forward=1u<<10,Backward=1u<<11,
    Left=1u<<12,Right=1u<<13,Burst=1u<<14,Clip=1u<<15,Siege=1u<<16,
    ContactMomentum=1u<<17,Airborne=1u<<18,ServiceBrake=1u<<19,
    RouteWreckBlocked=1u<<20,RamMotion=1u<<21
};
struct EquipmentRow {
    ID contract_revision=0;
    int uses=0;
    bool active=false;
    double ready_at=0.;
    std::optional<double> auto_pending_since,ai_pending_since;
};
struct Input {
    // Exact order is bot_state_codec.SCALARS from the fixed gameplay release.
    // Values in integral columns have already crossed the event-ingress check.
    std::array<double,37> scalars{};
    std::uint32_t flags=0;
    std::vector<int> ammunition;
    wot::offline_bot_step::CriticalState critical;
    bool crew_roster_present=true;
    std::vector<EquipmentRow> equipment;
    std::array<double,3> push{};
    std::map<ID,stock_native_bot_contacts::Checkpoint> acknowledgements;
    std::vector<stock_native_bot_contacts::RamMotion> motions;
};
std::int64_t fixed(double value,double scale);
std::vector<std::int64_t> encode(const Input&,double equipment_now);
struct Signature {
    std::vector<std::vector<std::optional<double>>> actors;
    std::vector<std::array<ID,2>> launches;
    std::vector<std::array<ID,4>> rams;
    bool operator==(const Signature&)const;
};
struct Batch {
    std::vector<std::vector<ID>> rows;
    std::vector<ID> failed;
    std::vector<offline_simulation::weapons::LaunchRecord> launches;
    std::vector<stock_native_bot_contacts::Report> reports;
    ID sample_us=0,edge_sample_us=0,edge_revision=0;
};
class Publisher {
    std::optional<Signature> previous_;
    ID edge_sample_us_=0,edge_revision_=0;
public:
    Batch publish(const std::vector<Input>&,double equipment_now,ID sample_us,
        const std::deque<offline_simulation::weapons::LaunchRecord>&,
        std::vector<stock_native_bot_contacts::Report>& pending_reports);
};
}
