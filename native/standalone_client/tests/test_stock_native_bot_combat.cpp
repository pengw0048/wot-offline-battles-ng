#include "../bigworld/stock_native_bot_combat.hpp"
#include <cassert>
#include <cmath>
using namespace wot::offline_bot_step;
struct World : CombatWorld { double depth=3.; int samples=0;
    double water_depth(const CombatPoint&) override {++samples;return depth;} };
int main() {
    World world; CombatState bot; bot.health=100;
    auto result=advance_drowning(bot,{0.,2.,0.},.2,world);
    assert(!result.water_sampled && world.samples==0);
    result=advance_drowning(bot,{0.,2.,0.},.1,world);
    assert(result.water_sampled && bot.drowning && bot.drown_time>.299);
    world.depth=-1.; advance_drowning(bot,{0.,2.,0.},.3,world);
    assert(!bot.drowning && bot.drown_time==0.);
    world.depth=3.; bot.drown_time=10.;
    result=advance_drowning(bot,{0.,2.,0.},.3,world);
    assert(result.terminal==TerminalCause::drowning && !bot.alive && bot.display_health==100);
    CombatState tipped; tipped.health=100; tipped.roll=std::acos(-1.)*.5;
    result=advance_overturn(tipped,.05); assert(!tipped.overturned);
    result=advance_overturn(tipped,.05); assert(tipped.overturned);
    tipped.overturn_time=29.95; result=advance_overturn(tipped,.05);
    assert(result.terminal==TerminalCause::overturn && tipped.death_reason==7);
    SiegeConfig config{true,2.,1.3,2.}; SiegeState siege;
    SiegeCommand command;command.target_alive=true;command.fire_allowed=true;
    auto mode=update_siege_intent(siege,config,command,.1,0.,false,false,false);
    assert(!mode.locked && siege.mode==0);
    mode=update_siege_intent(siege,config,command,.3,0.,false,false,false);
    assert(mode.locked && mode.descriptor_changed && siege.mode==1 && siege.remaining==2.);
    mode=advance_siege(siege,config,2.);
    assert(mode.locked && mode.descriptor_changed && siege.mode==2);
    CombatState repair;repair.health=repair.maximum_health=100;
    repair.critical.devices["engineHealth"]={0.,100.,50.,true,false,100.};
    auto tick=advance_critical(repair,1.,1.,.57,1.);
    assert(tick.changed && std::abs(*repair.critical.devices["engineHealth"].hp-2.315)<1.e-12);
    CombatState burning;burning.health=burning.maximum_health=100;burning.critical.fire=true;
    burning.critical.devices["ammoBayHealth"]={100.,100.,50.,false,false,100.};
    burning.critical.devices["engineHealth"]={50.,100.,50.,false,true,100.};
    burning.critical.devices["fuelTankHealth"]={10.,100.,50.,false,true,100.};
    tick=advance_critical(burning,1.,1.,.57,1.);
    assert(tick.fire_damage==5 && burning.health==95 &&
        *burning.critical.devices["ammoBayHealth"].hp==90. &&
        *burning.critical.devices["engineHealth"].hp==40. &&
        burning.critical.devices["fuelTankHealth"].destroyed);
    EquipmentEffect extinguish;extinguish.action=EquipmentAction::extinguish_fire;
    assert(apply_equipment(burning,extinguish));
    assert(!burning.critical.fire && *burning.critical.devices["fuelTankHealth"].hp==50.);
    EquipmentState kit;kit.kind=EquipmentKind::repairkit;kit.uses_left=1;kit.repair_all=true;
    burning.critical.devices["engineHealth"].destroyed=true;
    assert(!poll_bot_equipment(kit,burning,1.,false));
    assert(!poll_bot_equipment(kit,burning,1.,false));
    assert(poll_bot_equipment(kit,burning,1.1,false) && kit.uses_left==0);
    assert(*burning.critical.devices["engineHealth"].hp==100.);
    CriticalState damage;
    damage.crew_ko={"gunner1","commander"};
    damage.devices["gunHealth"]={40.,100.,50.,false,true,100.};
    const double expected=((.57+.43*1.1)/.57)*((.57+.43*1.1)/(.57+.43))*2.;
    assert(std::abs(critical_factor(damage,CriticalStat::dispersion)-expected)<1.e-12);
    damage.devices["turretRotatorHealth"]={0.,100.,50.,true,false,100.};
    assert(critical_factor(damage,CriticalStat::turret_speed)==0.);
    CombatState lineage;lineage.health=lineage.maximum_health=100;lineage.alive=true;
    lineage.critical.devices["engineHealth"]={0.,100.,50.,true,false,100.};
    CombatLedger ledger;ledger.initialize(lineage,0,0,0);
    CombatReplayStep slice;slice.elapsed=1.;slice.now=1.;slice.advance_fire=false;
    advance_critical(lineage,1.,1.,.57,1.,false);ledger.unpublished_steps.push_back(slice);
    assert(ledger.mark_publication(lineage) && ledger.next_sequence==1);
    ServerCombat hit;hit.record=combat_record(lineage);hit.record.health=90;hit.record.critical.devices["engineHealth"].hp=0.;
    hit.revision=1;hit.base_revision=1;hit.acknowledged_sequence=0;
    assert(ledger.reconcile(lineage,hit,.57,1.));
    assert(lineage.health==90 && ledger.next_sequence==0 && ledger.unpublished_steps.size()==1);
    assert(std::abs(*lineage.critical.devices["engineHealth"].hp-2.315)<1.e-12);
    assert(ledger.mark_publication(lineage) && ledger.next_sequence==1);
    ServerCombat ack;ack.record=ledger.pending.front().combat;ack.revision=2;ack.base_revision=1;ack.acknowledged_sequence=1;
    assert(!ledger.reconcile(lineage,ack,.57,1.) && ledger.pending.empty() && ledger.acknowledged_sequence==1);
}
