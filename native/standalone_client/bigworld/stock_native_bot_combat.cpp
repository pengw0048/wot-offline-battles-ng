#include "stock_native_bot_combat.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wot::offline_bot_step {
namespace {
void canonicalize(CriticalState&);
CriticalStatus device_status(const Device* d) {
    if (!d) return CriticalStatus::normal;
    if (d->destroyed) return CriticalStatus::destroyed;
    if (d->critical) return CriticalStatus::critical;
    if (!d->hp || *d->hp<=0.) return CriticalStatus::destroyed;
    if (d->maximum && *d->hp<=*d->maximum*.5) return CriticalStatus::critical;
    return CriticalStatus::normal;
}
bool critical_changed(const CriticalState& a,const CriticalState& b) {
    if (a.fire!=b.fire || a.ammo_rack_death!=b.ammo_rack_death || a.crew_ko!=b.crew_ko) return true;
    std::set<std::string> names;
    for (const auto& p:a.devices) if (p.second.hp || p.second.destroyed || p.second.critical) names.insert(p.first);
    for (const auto& p:b.devices) if (p.second.hp || p.second.destroyed || p.second.critical) names.insert(p.first);
    for (const auto& name:names) {
        const auto x=a.devices.find(name),y=b.devices.find(name);
        if (x==a.devices.end() || y==b.devices.end()) return true;
        if (x->second.hp!=y->second.hp || x->second.destroyed!=y->second.destroyed ||
            x->second.critical!=y->second.critical) return true;
    }
    return false;
}
void extinguish(CriticalState& critical) {
    if (!critical.fire) return;
    critical.fire=false;
    const auto found=critical.devices.find("fuelTankHealth");
    if (found!=critical.devices.end() && found->second.regeneration &&
        found->second.hp && *found->second.hp<*found->second.regeneration) {
        found->second.hp=found->second.regeneration;
        found->second.destroyed=false;found->second.critical=true;
    }
}
}
double critical_factor(const CriticalState& state,CriticalStat stat) {
    std::set<std::string> roles;
    for (auto role:state.crew_ko) {
        while (!role.empty() && role.back()>='0' && role.back()<='9') role.pop_back();
        roles.insert(role);
    }
    const double fit=.57+.43*1.1,role_out=.57,commander_out=.57+.43;
    const double time=fit/role_out,speed=role_out/fit,commander_time=fit/commander_out,commander_speed=commander_out/fit;
    double value=1.;const bool commander=roles.count("commander")!=0;
    if (stat==CriticalStat::reload) {if(roles.count("loader"))value*=time;if(commander)value*=commander_time;}
    else if (stat==CriticalStat::dispersion || stat==CriticalStat::aim_time) {if(roles.count("gunner"))value*=time;if(commander)value*=commander_time;}
    else if (stat==CriticalStat::turret_speed) {if(roles.count("gunner"))value*=speed;if(commander)value*=commander_speed;}
    else if (stat==CriticalStat::mobility) {if(roles.count("driver"))value*=speed;if(commander)value*=commander_speed;}
    else if (stat==CriticalStat::vision) {if(commander)value*=speed;if(roles.count("radioman"))value*=speed;}
    else if (stat==CriticalStat::signal) {if(roles.count("radioman"))value*=speed;if(commander)value*=commander_speed;}
    const char* name=stat==CriticalStat::reload?"ammoBayHealth":
        (stat==CriticalStat::dispersion || stat==CriticalStat::aim_time)?"gunHealth":
        stat==CriticalStat::turret_speed?"turretRotatorHealth":stat==CriticalStat::mobility?"engineHealth":
        stat==CriticalStat::vision?"surveyingDeviceHealth":"radioHealth";
    const auto found=state.devices.find(name);
    if (found!=state.devices.end()) {
        const auto& d=found->second;
        if (d.destroyed) {
            if (stat==CriticalStat::turret_speed || stat==CriticalStat::mobility) value*=0.;
            else if (stat==CriticalStat::vision || stat==CriticalStat::signal) value*=.25;
        } else if (d.hp && (d.critical || (*d.hp>0. && d.maximum && *d.hp<=*d.maximum*.5))) {
            value*=stat==CriticalStat::reload || stat==CriticalStat::dispersion || stat==CriticalStat::aim_time ? 2. : .5;
        }
    }
    return value;
}
std::vector<CriticalEvent> critical_events(const CriticalState& before,
    const CriticalState& after,const std::string& cause) {
    std::set<std::string> names;
    for (const auto& p:before.devices) if (p.second.hp || p.second.destroyed || p.second.critical) names.insert(p.first);
    for (const auto& p:after.devices) if (p.second.hp || p.second.destroyed || p.second.critical) names.insert(p.first);
    std::vector<CriticalEvent> events;
    for (const auto& name:names) {
        const auto a=before.devices.find(name),b=after.devices.find(name);
        const auto old=a==before.devices.end() || !a->second.hp ? CriticalStatus::normal : device_status(&a->second);
        const auto now=device_status(b==after.devices.end()?nullptr:&b->second);
        if (old!=now) events.push_back({CriticalEventKind::device,name,old,now,cause});
    }
    for (const auto& name:after.crew_ko) if (!before.crew_ko.count(name))
        events.push_back({CriticalEventKind::crew,name,CriticalStatus::normal,CriticalStatus::destroyed,cause});
    for (const auto& name:before.crew_ko) if (!after.crew_ko.count(name))
        events.push_back({CriticalEventKind::crew,name,CriticalStatus::destroyed,CriticalStatus::normal,cause});
    if (before.fire!=after.fire) events.push_back({CriticalEventKind::fire,"",before.fire?CriticalStatus::fire_on:CriticalStatus::fire_off,
        after.fire?CriticalStatus::fire_on:CriticalStatus::fire_off,cause});
    if (!before.ammo_rack_death && after.ammo_rack_death)
        events.push_back({CriticalEventKind::ammo_rack,"",CriticalStatus::normal,CriticalStatus::destroyed,cause});
    return events;
}
void terminal_critical(CombatState& s) {
    extinguish(s.critical);
    s.critical.crew_roster.clear();
    for (auto& entry:s.critical.devices) entry.second.critical=false;
    for (const char* name : {"engineHealth","ammoBayHealth","fuelTankHealth","radioHealth",
        "gunHealth","turretRotatorHealth","surveyingDeviceHealth","leftTrackHealth","rightTrackHealth"}) {
        auto& device=s.critical.devices[name];device.hp=0.;device.destroyed=true;device.critical=false;
    }
    if (!s.descriptor_crew_roster.empty()) {s.critical.crew_roster=s.descriptor_crew_roster;s.critical.crew_ko=s.descriptor_crew_roster;}
}
bool apply_equipment(CombatState& s,const EquipmentEffect& effect) {
    auto& c=s.critical;
    bool changed=false;
    if (effect.action==EquipmentAction::extinguish_fire) {
        if (c.fire) {extinguish(c);c.fire_elapsed=c.fire_timer=0.;changed=true;}
    } else if (effect.action==EquipmentAction::restore_crew) {
        if (effect.all) {changed=!c.crew_ko.empty();c.crew_ko.clear();}
        else changed=c.crew_ko.erase(effect.selected)!=0;
    } else {
        std::string selected=effect.selected;
        if (!selected.empty() && (selected.size()<6 || selected.substr(selected.size()-6)!="Health")) selected+="Health";
        for (auto& entry:c.devices) {
            auto& d=entry.second;
            if ((!effect.all && entry.first!=selected) || !d.maximum || (!d.hp && !d.destroyed)) continue;
            if (d.destroyed || d.hp.value_or(*d.maximum)<*d.maximum) {
                d.hp=d.maximum;d.destroyed=d.critical=false;changed=true;
            }
        }
    }
    if (effect.clear_stun && effect.stun_base_end_server_time_ms>0 &&
        s.stun_end_server_time_ms==effect.stun_base_end_server_time_ms) {
        s.stun_end_server_time_ms=0;changed=true;
    }
    if (changed) {c.crew_roster.clear();canonicalize(c);}
    return changed;
}
std::optional<EquipmentEffect> poll_bot_equipment(EquipmentState& equipment,CombatState& state,
                                                   double now,bool stunned) {
    bool eligible=false;
    EquipmentEffect effect;
    if (equipment.kind==EquipmentKind::extinguisher) {
        if (!equipment.autoactivate) return std::nullopt;
        eligible=state.critical.fire;effect.action=EquipmentAction::extinguish_fire;
    } else if (equipment.kind==EquipmentKind::repairkit) {
        effect.action=EquipmentAction::repair_devices;effect.all=equipment.repair_all;
        bool damaged=false,worth=false;
        for (const auto& entry:state.critical.devices) {
            const auto& d=entry.second;
            if (d.destroyed) {damaged=worth=true;break;}
            if (d.critical) {damaged=true;
                if (entry.first=="ammoBayHealth" || entry.first=="engineHealth" || entry.first=="gunHealth") worth=true;}
        }
        eligible=equipment.repair_all && damaged && worth;
    } else if (equipment.kind==EquipmentKind::medkit) {
        effect.action=EquipmentAction::restore_crew;effect.all=equipment.repair_all;
        eligible=(stunned || !state.critical.crew_ko.empty()) && (equipment.repair_all || stunned);
        effect.clear_stun=stunned;effect.stun_base_end_server_time_ms=state.stun_end_server_time_ms;
    } else {equipment.ai_pending_since.reset();return std::nullopt;}
    auto& pending=equipment.kind==EquipmentKind::extinguisher ? equipment.auto_pending_since : equipment.ai_pending_since;
    if (!eligible || equipment.uses_left==0 || now<equipment.ready_at) {pending.reset();return std::nullopt;}
    if (!pending) {
        pending=now;
        if (equipment.kind!=EquipmentKind::extinguisher || equipment.reaction_seconds>0.) return std::nullopt;
    }
    if (equipment.kind==EquipmentKind::extinguisher) {
        if (now-*pending+1.e-9<std::max(0.,equipment.reaction_seconds)) return std::nullopt;
    } else if (now<=*pending+1.e-9) return std::nullopt;
    if (equipment.uses_left>0) --equipment.uses_left;
    equipment.ready_at=now+std::max(0.,equipment.cooldown_seconds);
    equipment.auto_pending_since.reset();equipment.ai_pending_since.reset();
    if (effect.clear_stun && effect.stun_base_end_server_time_ms<=0)
        throw std::runtime_error("Bot medkit stun base is invalid");
    const auto before=state.critical;
    const bool applied=apply_equipment(state,effect);
    effect.events=critical_events(before,state.critical,"repair");
    if (!applied && !effect.clear_stun) throw std::runtime_error("Bot equipment effect could not be applied");
    if (effect.clear_stun && state.stun_end_server_time_ms==0) state.stun_until_equipment_time=now;
    return effect;
}
EquipmentPassives equipment_passives(const std::vector<EquipmentState>& values) {
    EquipmentPassives result;
    for (const auto& e:values) {
        if (e.kind==EquipmentKind::extinguisher) result.fire_starting_chance_factor*=std::max(0.,e.fire_starting_chance_factor);
        else if (e.kind==EquipmentKind::repairkit && e.uses_left!=0) result.repair_bonus+=e.bonus_value;
        else if (e.kind==EquipmentKind::medkit && e.uses_left!=0) result.medkit_bonus+=e.bonus_value;
        result.crew_level_increase+=e.crew_level_increase;
        if (e.kind==EquipmentKind::fuel || (e.kind==EquipmentKind::rpm_limiter && e.active))
            result.engine_power_factor*=std::max(0.,e.engine_power_factor);
        if (e.kind==EquipmentKind::fuel) result.turret_rotation_speed_factor*=std::max(0.,e.turret_rotation_speed_factor);
        if (e.kind==EquipmentKind::rpm_limiter && e.active) result.engine_hp_loss_per_second+=std::max(0.,e.engine_hp_loss_per_second);
    }
    return result;
}
namespace {
double rounded(double value,int digits) {
    long double scale=1.;for (int i=0;i<digits;++i) scale*=10.;
    return static_cast<double>(std::round(static_cast<long double>(value)*scale)/scale);
}
void canonicalize(CriticalState& state) {
    for (auto& pair:state.devices) if (pair.second.hp) {
        auto& d=pair.second;
        const double maximum=std::max(1.,rounded(d.maximum.value_or(std::max(1.,std::round(*d.hp))),3));
        d.wire_maximum=maximum;d.hp=std::max(0.,std::min(rounded(*d.hp,3),maximum));
    }
    state.fire_elapsed=rounded(state.fire_elapsed,6);state.fire_timer=rounded(state.fire_timer,6);
}
void apply_record(CombatState& state,const CombatRecord& record) {
    state.health=std::max(0,std::min(record.health,state.maximum_health));
    state.alive=record.alive && state.health>0;state.display_health=state.health;
    state.critical=record.critical;state.has_critical_payload=true;state.stun_end_server_time_ms=record.stun_end_server_time_ms;
    if (!state.alive) {state.speed=0.;state.movement_direction=state.rotation_direction=0;state.has_target=false;}
}
}
CombatRecord combat_record(const CombatState& s) {
    CombatRecord out;out.health=std::max(0,s.health);out.alive=s.alive;out.critical=s.critical;
    out.critical.fire_elapsed=rounded(out.critical.fire_elapsed,6);out.critical.fire_timer=rounded(out.critical.fire_timer,6);
    out.stun_end_server_time_ms=std::max<std::int64_t>(0,s.stun_end_server_time_ms);
    for (auto& pair:out.critical.devices) if (pair.second.hp) {
        const double maximum=std::max(1.,rounded(pair.second.wire_maximum.value_or(pair.second.maximum.value_or(std::max(1.,std::round(*pair.second.hp)))),3));
        pair.second.hp=std::max(0.,std::min(rounded(*pair.second.hp,3),maximum));
    }
    return out;
}
bool same_combat(const CombatRecord& a,const CombatRecord& b) {
    if (a.health!=b.health || a.alive!=b.alive || a.stun_end_server_time_ms!=b.stun_end_server_time_ms ||
        rounded(a.critical.fire_elapsed,6)!=rounded(b.critical.fire_elapsed,6) ||
        rounded(a.critical.fire_timer,6)!=rounded(b.critical.fire_timer,6) ||
        a.critical.fire!=b.critical.fire || a.critical.ammo_rack_death!=b.critical.ammo_rack_death ||
        a.critical.crew_ko!=b.critical.crew_ko || a.critical.crew_roster!=b.critical.crew_roster) return false;
    std::set<std::string> names;
    for (const auto& p:a.critical.devices) if (p.second.hp || p.second.destroyed || p.second.critical) names.insert(p.first);
    for (const auto& p:b.critical.devices) if (p.second.hp || p.second.destroyed || p.second.critical) names.insert(p.first);
    for (const auto& name:names) {
        const auto x=a.critical.devices.find(name),y=b.critical.devices.find(name);
        if (x==a.critical.devices.end() || y==b.critical.devices.end()) return false;
        const auto& left=x->second;const auto& right=y->second;
        if (left.hp.has_value()!=right.hp.has_value() || left.destroyed!=right.destroyed || device_status(&left)!=device_status(&right)) return false;
        if (left.hp && (rounded(*left.hp,3)!=rounded(*right.hp,3) ||
            rounded(left.wire_maximum.value_or(left.maximum.value_or(std::max(1.,std::round(*left.hp)))),3)!=
            rounded(right.wire_maximum.value_or(right.maximum.value_or(std::max(1.,std::round(*right.hp)))),3))) return false;
    }
    return true;
}
void CombatLedger::initialize(const CombatState& state,std::int64_t r,std::int64_t base,std::int64_t acknowledged) {
    if (r<0 || base<0 || base>r || acknowledged<0) throw std::invalid_argument("Invalid combat ledger baseline");
    server_combat=published_combat=combat_record(state);pending.clear();unpublished_steps.clear();
    next_sequence=acknowledged_sequence=acknowledged;revision=r;base_revision=base;server_tick=-1;authority_handoff_pending=false;
}
bool CombatLedger::mark_publication(const CombatState& state) {
    const auto record=combat_record(state);if (same_combat(record,published_combat)) return false;
    CombatPublication publication;publication.sequence=++next_sequence;publication.combat=record;
    publication.steps=std::move(unpublished_steps);unpublished_steps.clear();pending.push_back(std::move(publication));
    published_combat=record;return true;
}
bool CombatLedger::reconcile(CombatState& state,const ServerCombat& incoming,double repair_factor,double repair_speed) {
    if (incoming.server_tick && *incoming.server_tick<server_tick) return false;
    const auto& incoming_record=incoming.record;
    if (incoming.revision<0 || incoming.base_revision<0 || incoming.base_revision>incoming.revision ||
        incoming.acknowledged_sequence<0 || incoming_record.stun_end_server_time_ms<0 ||
        !std::isfinite(incoming_record.critical.fire_elapsed) || !std::isfinite(incoming_record.critical.fire_timer) ||
        incoming_record.critical.fire_elapsed<0. || incoming_record.critical.fire_elapsed>10. || incoming_record.critical.fire_timer<0. ||
        incoming_record.critical.fire_timer>=1. || (!incoming_record.critical.fire && (incoming_record.critical.fire_elapsed!=0. || incoming_record.critical.fire_timer!=0.)))
        throw std::invalid_argument("Invalid server combat contract");
    CombatState candidate=state;apply_record(candidate,incoming_record);const auto record=combat_record(candidate);
    const auto found=std::find_if(pending.begin(),pending.end(),[&](const CombatPublication& p){return p.sequence==incoming.acknowledged_sequence;});
    const bool acknowledged=incoming.acknowledged_sequence>acknowledged_sequence && found!=pending.end();
    const bool handoff_new=authority_handoff_pending && incoming.base_revision>base_revision;
    const bool handoff_overlap=authority_handoff_pending && incoming.base_revision==base_revision &&
        incoming.acknowledged_sequence>acknowledged_sequence && (incoming.acknowledged_sequence>next_sequence ||
        (acknowledged && !same_combat(found->combat,record)));
    const bool handoff_reset=handoff_new || handoff_overlap;
    if (incoming.revision<revision || incoming.base_revision<base_revision || incoming.acknowledged_sequence<acknowledged_sequence ||
        (incoming.acknowledged_sequence>next_sequence && !handoff_reset)) throw std::invalid_argument("Server combat revision moved backwards");
    if (handoff_reset) {
        if (incoming.revision<=revision) throw std::invalid_argument("Server combat handoff is inconsistent");
        apply_record(state,record);server_combat=published_combat=record;pending.clear();unpublished_steps.clear();
        next_sequence=acknowledged_sequence=incoming.acknowledged_sequence;revision=incoming.revision;base_revision=incoming.base_revision;
        authority_handoff_pending=false;if (incoming.server_tick)server_tick=*incoming.server_tick;return true;
    }
    if (incoming.base_revision==base_revision) {
        if (incoming.acknowledged_sequence==acknowledged_sequence) {
            if (incoming.revision!=revision || !same_combat(record,server_combat)) throw std::invalid_argument("Server changed combat without ack");
        } else {
            if (!acknowledged || !same_combat(found->combat,record) || incoming.revision<=revision)
                throw std::invalid_argument("Server combat acknowledgement is inconsistent");
            while (!pending.empty() && pending.front().sequence<=incoming.acknowledged_sequence) pending.pop_front();
            authority_handoff_pending=false;
        }
        server_combat=record;acknowledged_sequence=incoming.acknowledged_sequence;revision=incoming.revision;
        if (incoming.server_tick)server_tick=*incoming.server_tick;return false;
    }
    if (incoming.revision<incoming.base_revision || incoming.revision<=revision)
        throw std::invalid_argument("New combat base has no canonical revision");
    if (incoming.acknowledged_sequence!=acknowledged_sequence && !acknowledged)
        throw std::invalid_argument("New combat base has an unknown acknowledgement");
    std::vector<CombatReplayStep> replay;
    for (const auto& p:pending) if (p.sequence>incoming.acknowledged_sequence)
        replay.insert(replay.end(),p.steps.begin(),p.steps.end());
    replay.insert(replay.end(),unpublished_steps.begin(),unpublished_steps.end());
    apply_record(state,record);server_combat=published_combat=record;pending.clear();unpublished_steps.clear();
    next_sequence=acknowledged_sequence=incoming.acknowledged_sequence;revision=incoming.revision;base_revision=incoming.base_revision;
    authority_handoff_pending=false;
    for (const auto& step:replay) {
        for (const auto& effect:step.equipment) apply_equipment(state,effect);
        advance_critical(state,step.elapsed,step.now,repair_factor,repair_speed,step.advance_fire);
    }
    if (!same_combat(combat_record(state),record)) unpublished_steps=std::move(replay);
    if (incoming.server_tick)server_tick=*incoming.server_tick;return true;
}
CriticalTickResult advance_critical(CombatState& s,double dt,double now,
                                    double repair_factor,double repair_speed_factor,bool advance_fire) {
    CriticalTickResult out;
    if (!s.alive || s.health<=0 || dt<=0.) return out;
    auto& c=s.critical;
    const auto before_repair=c;
    for (auto& entry:c.devices) {
        auto& d=entry.second;
        if (!d.destroyed || !d.regeneration || !d.hp || (entry.first=="fuelTankHealth" && c.fire)) continue;
        double factor=std::max(0.,repair_factor)/.57*repair_speed_factor;
        if (factor<=0.) factor=1.;
        const bool track=entry.first.find("Track")!=std::string::npos || entry.first.find("track")!=std::string::npos;
        const double seconds=(track?12.:21.6)/factor;
        const double previous=*d.hp;
        if (*d.hp<*d.regeneration) d.hp=std::min(*d.regeneration,*d.hp+*d.regeneration/std::max(.1,seconds)*dt);
        if (*d.hp>=*d.regeneration) {d.destroyed=false;d.critical=true;}
        out.changed=out.changed || *d.hp!=previous || !d.destroyed;
    }
    out.events=critical_events(before_repair,c,"repair");
    if (!c.fire || !advance_fire) {if(out.changed){c.crew_roster.clear();canonicalize(c);}return out;}
    const auto before_fire=c;
    const double elapsed=std::max(0.,std::min(10.,c.fire_elapsed));
    const double timer=std::max(0.,std::min(.999999,c.fire_timer));
    const double started=now-std::min(10.,elapsed+dt),end=started+10.;
    const double active=std::max(0.,std::min(now,end)-std::max(now-dt,started));
    const double ticks=timer+active;
    const int count=static_cast<int>(std::floor(ticks+1.e-9));
    c.fire_timer=count>0 ? std::max(0.,ticks-count) : ticks;
    const int damage=std::max(1,static_cast<int>(s.maximum_health*.05));
    for (int i=0;i<count;++i) {
        out.fire_damage+=damage;
        if (out.fire_damage>=s.health) break;
        bool terminal=false;
        for (const char* name : {"ammoBayHealth","engineHealth","fuelTankHealth","radioHealth","turretRotatorHealth"}) {
            const auto it=c.devices.find(name);if (it==c.devices.end() || !it->second.maximum) continue;
            auto& d=it->second; const double old=std::max(0.,d.hp.value_or(*d.maximum));
            if (old<=0.) continue;
            d.hp=std::max(0.,old-10.);
            if (*d.hp<=0.) {d.destroyed=true;d.critical=false;}
            else if (d.critical || *d.hp<=*d.maximum*.5) d.critical=true;
            else d.critical=false;
            out.changed=out.changed || *d.hp!=old;
            if (std::string(name)=="ammoBayHealth" && *d.hp<=0.) {c.ammo_rack_death=true;terminal=true;break;}
        }
        if (terminal) {out.fire_damage=std::max(out.fire_damage,s.health);break;}
    }
    if (now>=end) extinguish(c);
    if (c.fire) {c.fire_elapsed=std::min(10.,elapsed+dt);c.fire_timer=std::max(0.,std::min(.999999,c.fire_timer));}
    else c.fire_elapsed=c.fire_timer=0.;
    const auto fire_events=critical_events(before_fire,c,"fire");
    if (critical_changed(before_fire,c)) {out.events=fire_events;out.changed=true;out.fire_before=before_fire;out.fire_after=c;}
    if (out.fire_damage>0) {
        s.health=std::max(0,s.health-out.fire_damage);s.display_health=s.health;s.alive=s.health>0;
        if (!s.alive) {const auto before_terminal=c;terminal_critical(s);
            out.events=critical_events(before_terminal,c,"fire");c.fire_elapsed=c.fire_timer=0.;s.has_reposition=false;
            s.death_reason=1;s.speed=0.;s.movement_direction=s.rotation_direction=0;s.has_target=false;}
    }
    out.changed=out.changed || out.fire_damage>0;
    if (out.changed) {if(s.alive && (critical_changed(before_repair,before_fire) || out.fire_after))c.crew_roster.clear();canonicalize(c);}
    else {c.fire_elapsed=rounded(c.fire_elapsed,6);c.fire_timer=rounded(c.fire_timer,6);}
    return out;
}
CombatResult advance_drowning(CombatState& s, const CombatPoint& turret,
                              double dt, CombatWorld& world) {
    CombatResult result;
    if (!s.alive || s.health<=0 || dt<=0.) return result;
    s.drown_check+=dt;
    if (s.drown_check<.30) return result;
    const double elapsed=s.drown_check;s.drown_check=0.;
    const double depth=world.water_depth(s.position);
    if (!std::isfinite(depth)) throw std::invalid_argument("Water depth is not finite");
    s.water_depth=depth;result.water_sampled=true;
    const double y=std::cos(s.pitch)*(std::sin(s.roll)*turret[0]+std::cos(s.roll)*turret[1])-
                   std::sin(s.pitch)*turret[2];
    if (depth<0. || depth<=y) {s.drown_time=0.;s.drowning=false;return result;}
    s.drowning=true;s.drown_time+=elapsed;
    if (s.drown_time<=10.) return result;
    s.display_health=std::max(0,s.health);s.health=0;s.alive=false;s.death_reason=5;
    s.drowned=true;s.drown_time=10.;s.drowning=false;s.has_reposition=false;
    s.speed=0.;s.movement_direction=s.rotation_direction=0;s.has_target=false;
    const auto before_terminal=s.critical;terminal_critical(s);
    result.events=critical_events(before_terminal,s.critical,"drowning");
    result.terminal=TerminalCause::drowning;return result;
}
CombatResult advance_overturn(CombatState& s,double dt) {
    CombatResult result;
    if (!s.alive || s.health<=0 || dt<=0.) return result;
    const double up=std::max(-1.,std::min(1.,std::cos(s.pitch)*std::cos(s.roll)));
    const double pi=std::acos(-1.);
    const int level=up<=std::cos(80.*pi/180.) ? 2 : up<=std::cos(70.*pi/180.) ? 1 : 0;
    if (!level) {s.overturn_check=s.overturn_time=0.;s.overturn_level=0;s.overturned=false;return result;}
    s.overturn_check+=dt;
    if (s.overturn_check+.000001<.10) return result;
    if (level!=s.overturn_level) {s.overturn_level=level;s.overturn_time=0.;}
    s.overturned=level==2;
    if (level!=2) {s.overturn_time=0.;return result;}
    s.movement_direction=s.rotation_direction=0;s.turn_speed=0.;s.overturn_time+=dt;
    if (s.overturn_time+.000001<30.) return result;
    s.speed=0.;s.health=0;s.alive=false;s.display_health=0;s.death_reason=7;
    s.overturn_time=30.;s.has_reposition=false;s.has_target=false;
    const auto before_terminal=s.critical;terminal_critical(s);
    result.events=critical_events(before_terminal,s.critical,"overturn");
    result.terminal=TerminalCause::overturn;return result;
}
SiegeResult advance_siege(SiegeState& s,const SiegeConfig& c,double dt) {
    if (!c.available) {s.mode=0;s.remaining=s.total=0.;return {false,true};}
    if (s.mode!=1 && s.mode!=3) {s.remaining=s.total=0.;return {};}
    s.remaining=std::max(0.,s.remaining-dt);
    if (s.remaining>1.e-9) return {true,false};
    s.mode=s.mode==1?2:0;s.remaining=s.total=0.;s.intent=s.mode==2;s.intent_elapsed=0.;
    return {true,true};
}
SiegeResult update_siege_intent(SiegeState& s,const SiegeConfig& c,SiegeCommand& command,
    double dt,double speed,bool burst,bool engine_destroyed,bool engine_damaged) {
    if (!c.available) return {};
    if (burst) {s.intent_elapsed=0.;return {};}
    const bool switching=s.mode==1 || s.mode==3;
    if (switching) command.fire_allowed=false;
    const double dx=command.position[0]-command.destination[0],dy=command.position[1]-command.destination[1],
                 dz=command.position[2]-command.destination[2];
    const bool desired=command.target_alive && !(command.movement_intent && std::sqrt(dx*dx+dy*dy+dz*dz)>35.);
    if (!s.intent || *s.intent!=desired) {s.intent=desired;s.intent_elapsed=0.;return {switching,false};}
    if ((s.mode==1 && desired) || (s.mode==3 && !desired)) {s.intent_elapsed=0.;return {true,false};}
    s.intent_elapsed+=dt;
    if ((desired && s.mode==2) || (!desired && s.mode==0) || s.intent_elapsed+1.e-9<(desired?.30:.80)) return {};
    if (engine_destroyed) {s.intent_elapsed=0.;return {};}
    if (std::abs(speed)>.05) {command.throttle=command.turn=0.;command.brake=true;command.fire_allowed=false;return {};}
    const int wanted=desired?2:0,switch_to=desired?1:3;
    const double total=(desired?c.enable_seconds:c.disable_seconds)*(engine_damaged?c.damaged_factor:1.);
    if (!(total>0.) || !std::isfinite(total)) throw std::invalid_argument("Siege duration is invalid");
    if (s.mode==0 || s.mode==2) {s.mode=switch_to;s.remaining=s.total=total;}
    else {
        if (!(s.total>0.)) throw std::invalid_argument("Siege transition total is missing");
        const double remaining=std::min(std::max(0.,s.remaining),s.total);
        const double progress=std::max(0.,std::min(1.,(s.total-remaining)/s.total));
        s.remaining=progress*total;s.total=s.remaining<=1.e-9?0.:total;
        s.mode=s.remaining<=1.e-9?wanted:switch_to;
        if (s.mode==wanted) s.remaining=0.;
    }
    command.fire_allowed=false;s.intent_elapsed=0.;return {true,true};
}
} // namespace wot::offline_bot_step
