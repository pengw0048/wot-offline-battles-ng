#pragma once
#include "room_state.hpp"
#include <deque>
#include <array>
#include <functional>
#include <set>

namespace wot::room {
class Rules {
public:
    using ActorKey = std::pair<std::string, std::int64_t>;
    explicit Rules(State& state): state_(state) {}
    Outcome dispatch(Sender, const Value&);
    void reset();
    void authorityChanged();
    bool initializePlayerEquipment(std::int64_t);
    void initializeActor(const std::string& kind,std::int64_t id);
    void playerDisconnected(std::int64_t id);
    void playerLeftBattle(std::int64_t id,bool voluntary=true);
    void consumeEnvironmentPush(const Value&);
    Outcome checkpoint(const Value& decodedPublication);
    void observeVisibility(const Value& acceptedObservation);
    void beginTick();
    void advance(double dt, bool expire_stuns=true);
    void expireStuns();
    bool finish(int winner, const std::string& reason, int base_team=0, bool record_receipts=true);
    Value projectiles() const;
    Value humanRamProbes() const;
    Outcome installHumanProfiles(const Value&);
    Value validateCritical(const Value& value) const { return canonicalCritical(value); }
    Value statistics(const ActorKey&) const;
    void actorAdvanced(const std::string& kind,std::int64_t id,const Value& previous,const Value& publication={});
    Value pendingReceipts(const std::string& account) const;
    // The host supplies its atomic durable store. Failure leaves the round live.
    void receiptStore(std::function<bool(const Value&)> store) { persist_=std::move(store); }
    void restoreReceipts(const Value&);
private:
    struct Projectile { Value launch, wire, ricochet, progress; bool blocked=false;
        bool final_round=false,piercing=false,ammo_hit=false,ammo_penetrated=false;std::int64_t shot_index=0;int kills=0,ammo_damage=0;
        std::set<ActorKey> stunned; };
    struct Tombstone { Value launch, request; std::string outcome; };
    struct Capture {
        std::map<std::string,int> contributors;
        std::set<ActorKey> participants;
        std::size_t cursor=0;
    };
    State& state_;
    std::map<std::string,Projectile> projectiles_;
    std::map<std::string,Tombstone> tombstones_;
    std::map<ActorKey,Value> statistics_, interactions_;
    std::map<ActorKey,ActorKey> fire_attackers_, track_attackers_, stun_attackers_, last_shell_fire_;
    std::map<ActorKey,std::vector<std::pair<std::int64_t,std::int64_t>>> piercing_runs_;
    std::map<ActorKey,Value> participants_;
    std::map<ActorKey,Value> environment_pushes_;
    std::map<ActorKey,std::int64_t> environment_push_sequences_;
    std::map<std::int64_t,std::int64_t> environment_falls_;
    std::map<std::tuple<std::int64_t,std::string,std::uint32_t>,std::int64_t> environment_critical_totals_;
    std::map<std::string,std::array<std::int64_t,4>> stack_totals_;
    std::map<std::pair<std::int64_t,std::int64_t>,Value> pending_launches_, frozen_edges_;
    std::set<std::pair<std::int64_t,std::int64_t>> failed_launches_;
    std::map<std::pair<std::int64_t,std::int64_t>,Value> pending_contacts_;
    std::map<std::int64_t,std::int64_t> equipment_frontiers_, ram_frontiers_, environment_frontiers_;
    std::map<std::int64_t,std::map<std::int64_t,double>> equipment_ready_;
    std::map<std::int64_t,std::map<std::int64_t,Value>> equipment_requests_, repair_requests_;
    std::map<std::int64_t,std::map<std::int64_t,Value>> ram_requests_, landing_results_;
    std::map<std::string,Value> ram_fingerprints_;
    Capture capture_[3];
    Value receipts_=array();
    std::function<bool(const Value&)> persist_;
    using HumanPair=std::pair<std::int64_t,std::int64_t>;
    std::map<std::int64_t,Value> human_profiles_;
    std::map<std::int64_t,std::deque<Value>> human_poses_;
    std::map<HumanPair,Value> human_probes_;
    std::map<std::int64_t,Value> human_probe_receipts_;
    std::map<std::int64_t,HumanPair> retired_human_probes_;
    std::map<HumanPair,std::int64_t> human_frontiers_,human_episodes_;
    std::set<HumanPair> human_contacts_;
    std::int64_t human_probe_seq_=0;
    std::int64_t last_capture_tick_=-1;
    std::map<int,std::set<ActorKey>> team_visible_;
    std::map<int,std::map<ActorKey,double>> team_lit_deadlines_;
    std::set<ActorKey> ever_spotted_;
    std::vector<std::tuple<ActorKey,ActorKey,std::size_t>> pending_kill_events_;
    bool finishing_=false;
    bool accepting() const;
    Value* roundActor(const ActorKey&);
    const Value* roundActor(const ActorKey&) const;
    void abandonIfEmpty();
    void clearPlayerOperations(std::int64_t);
    bool owns(Sender,const std::string&) const;
    Outcome launch(Sender,const Value&);
    Outcome launchOutcome(Sender,const Value&,Outcome) const;
    Outcome progress(Sender,const Value&);
    Outcome terminal(Sender,const Value&,bool ricochet);
    void retire(const std::string&,const std::string&,const Value& request={});
    void recordPiercing(Projectile&,bool);
    void ammoLaunch(const Projectile&);
    void ammoTerminal(const Projectile&,bool known);
    Value validateEffect(const Value&,const Value&,bool splash,Sender) const;
    void applyEffect(Projectile&,const Value&,bool splash);
    void commitDestructibles(const Value&);
    void retryOperations();
    void failLaunch(std::int64_t,std::int64_t,const std::string&);
    Value mergeCritical(const Value& actor,const Value& proposal,const Value& delta) const;
    Value canonicalCritical(const Value&) const;
    void commitCritical(Value&,const Value&,const ActorKey&,bool external,bool restart=false);
    Outcome repair(Sender,const Value&);
    Outcome equipment(Sender,const Value&);
    Outcome environment(Sender,const Value&);
    Outcome landing(Sender,const Value&);
    Outcome ram(Sender,const Value&);
    Outcome contactResult(Sender,const Value&,bool finalizing=false);
    void recordHumanPose(std::int64_t,const Value&);
    Outcome humanArmors(const Value&);
    void resolveHumanRams();
    void workerProgress(const Value&,const std::map<std::int64_t,Value>&);
    void rememberPush(const ActorKey&,const ActorKey&,std::optional<std::int64_t> sequence={});
    ActorKey environmentPusher(const ActorKey&,bool hazard=false);
    void rememberPushPair(const ActorKey&,const ActorKey&,const Value&,const Value&,const Value&);
    void markEnvironmentTeamkiller(const ActorKey&,const ActorKey&,int);
    void recordMask(const ActorKey&,const ActorKey&,std::uint32_t,int count=1);
    void clearStun(const ActorKey&);
    Value completeBotCritical(Value&,const Value& admitted={});
    Value ramTrackLoss(const ActorKey&,const Value&,double);
    void advanceHazards(double);
    bool healthDamage(const ActorKey&,int,int,const ActorKey& attacker={},const Value& critical={},int display=-1,bool resolvePush=true,bool preserveRamMotion=false);
    void updateCapture();
    int dropCapture(const ActorKey&,const ActorKey& attacker={});
    void maybeFinish();
    Value& stats(const ActorKey&);
    void increment(const ActorKey&,const std::string&,std::int64_t amount=1);
    void interaction(const ActorKey&,const ActorKey&,const std::string&,std::int64_t amount=1);
    std::vector<ActorKey> directSpotters(const ActorKey&) const;
    std::vector<ActorKey> radioAssisters(const ActorKey&,const ActorKey&) const;
    bool invisibleFor(const ActorKey&) const;
    void recordDamage(const ActorKey&,const ActorKey&,int,const Value& before);
    void recordKill(const ActorKey&,const ActorKey&,int reason);
    void recordCritical(const ActorKey&,const ActorKey&,const Value&,const Value&);
    void mission(const ActorKey&,const ActorKey&,const std::string&,std::vector<Value>);
    void finalizeKillHealth(std::size_t begin);
    Value buildReceipts(int winner,const std::string&,int base_team);
};
}
