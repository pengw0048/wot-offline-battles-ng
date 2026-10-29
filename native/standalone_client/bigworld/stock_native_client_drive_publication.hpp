#pragma once
#include "stock_native_client_state.hpp"
#include "stock_native_client_drive_contacts.hpp"
namespace stock_native_client_drive {
struct FrozenInput {
    std::uint64_t sequence=0;
    PlayerState state;Controls controls;
    stock_native_client_state::Value ram_contacts,destructible_contacts,tank_pushes,turret_pushes;
};
// A single native queue owns every admitted one-shot until the corresponding
// durable server frontier resolves it. Enqueue is deliberately not an ACK.
class PublicationLedger {
    using Value=stock_native_client_state::Value;
    std::map<ID,Value> ram_,destructible_;
    ID ram_sequence_=0,destructible_sequence_=0,ram_admitted_=0,destructible_admitted_=0;
    std::size_t token_limit_=64,destructible_window_=16,inflight_limit_=64;
    std::deque<FrozenInput> barriers_;
    std::uint64_t barrier_sequence_=0;
public:
    Value ram_contacts,destructible_contacts,tank_pushes,turret_pushes,landing_impacts;
    PublicationLedger();
    bool queueDestructible(const stock_native_world::ContactResult&,const Point&,double,
        const Point&,double,double speed,double dt);
    ID queueRam(Value frozen_receipt);
    std::size_t ramPendingSize()const{return ram_.size();}
    void replaceTankPushes(const std::map<ID,PushCheckpoint>&);
    void replaceTurretPushes(Value rows);
    bool acknowledgeRam(ID admitted,ID resolved);
    // Rejected exact rows are returned for the physical owner to retire its
    // speculative skin. An old worker disagreement never rewinds the pose.
    std::vector<Value> acknowledgeDestructible(ID admitted,ID resolved,
        const std::vector<ID>&exact,const std::vector<ID>&rejected);
    void queueLanding(Value observation,std::size_t maximum);
    bool landingAdmitted();
    void refresh();
    void barrier(const PlayerState&,const Controls&,const Point&,double yaw);
    const std::deque<FrozenInput>&barriers()const{return barriers_;}
    bool barrierAdmitted(std::uint64_t sequence);
};
}
