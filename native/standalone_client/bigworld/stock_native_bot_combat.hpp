#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <map>
#include <deque>
#include <set>
#include <string>
#include <vector>

namespace wot::offline_bot_step {
using CombatPoint = std::array<double,3>;
enum class TerminalCause { none, drowning, overturn };
struct Device {
    std::optional<double> hp, maximum, regeneration;
    bool destroyed=false, critical=false;
    std::optional<double> wire_maximum;
};
struct CriticalState {
    std::map<std::string,Device> devices;
    std::set<std::string> crew_roster, crew_ko;
    bool fire=false, ammo_rack_death=false;
    double fire_elapsed=0, fire_timer=0;
};
enum class CriticalStat { reload, dispersion, aim_time, turret_speed, mobility, vision, signal };
double critical_factor(const CriticalState&,CriticalStat);
enum class CriticalEventKind { device, crew, fire, ammo_rack };
enum class CriticalStatus { normal, critical, destroyed, fire_on, fire_off };
struct CriticalEvent {
    CriticalEventKind kind;
    std::string name;
    CriticalStatus old_status=CriticalStatus::normal, status=CriticalStatus::normal;
    std::string cause;
};
std::vector<CriticalEvent> critical_events(const CriticalState& before,
    const CriticalState& after, const std::string& cause);
struct CombatState {
    bool alive=true, drowning=false, drowned=false, overturned=false;
    bool has_critical_payload=false;
    int health=0, display_health=0, death_reason=0, overturn_level=0;
    int movement_direction=0, rotation_direction=0;
    double speed=0, turn_speed=0, pitch=0, roll=0;
    double drown_check=0, drown_time=0, water_depth=-1;
    double overturn_check=0, overturn_time=0;
    CombatPoint position{};
    bool has_target=false, has_reposition=false;
    int maximum_health=1;
    std::int64_t stun_end_server_time_ms=0;
    double stun_until_equipment_time=0;
    CriticalState critical;
    std::set<std::string> descriptor_crew_roster;
};
struct CombatResult {
    TerminalCause terminal=TerminalCause::none;
    bool water_sampled=false;
    std::vector<CriticalEvent> events;
};
class CombatWorld {
public:
    virtual ~CombatWorld()=default;
    virtual double water_depth(const CombatPoint&)=0;
};
// The caller processes a terminal result immediately using its typed critical
// state before it advances the next actor; no terminal event may be discarded.
CombatResult advance_drowning(CombatState&, const CombatPoint& turret_offset,
                              double elapsed, CombatWorld&);
CombatResult advance_overturn(CombatState&, double elapsed);
void terminal_critical(CombatState&);
struct CriticalTickResult { bool changed=false; int fire_damage=0; std::vector<CriticalEvent> events;
    std::optional<CriticalState> fire_before,fire_after; };
CriticalTickResult advance_critical(CombatState&, double elapsed, double now,
                                    double repair_factor, double repair_speed_factor,
                                    bool advance_fire=true);
enum class EquipmentAction { extinguish_fire, repair_devices, restore_crew };
struct EquipmentEffect {
    EquipmentAction action;
    std::string selected;
    bool all=false;
    bool clear_stun=false;
    std::int64_t stun_base_end_server_time_ms=0;
    std::vector<CriticalEvent> events;
};
bool apply_equipment(CombatState&, const EquipmentEffect&);
enum class EquipmentKind { extinguisher, repairkit, medkit, fuel, rpm_limiter, stimulator, other };
struct EquipmentState {
    EquipmentKind kind=EquipmentKind::other;
    int uses_left=0;
    double ready_at=0, cooldown_seconds=0, reaction_seconds=0;
    bool autoactivate=false, repair_all=false, active=false;
    double bonus_value=0, crew_level_increase=0, fire_starting_chance_factor=1;
    double engine_power_factor=1, turret_rotation_speed_factor=1, engine_hp_loss_per_second=0;
    std::uint64_t contract_revision=0;
    std::optional<double> auto_pending_since, ai_pending_since;
};
struct EquipmentPassives {
    double fire_starting_chance_factor=1,repair_bonus=0,medkit_bonus=0,crew_level_increase=0;
    double engine_power_factor=1,turret_rotation_speed_factor=1,engine_hp_loss_per_second=0;
};
EquipmentPassives equipment_passives(const std::vector<EquipmentState>&);
std::optional<EquipmentEffect> poll_bot_equipment(EquipmentState&, CombatState&,
                                                   double now, bool stunned);
struct CombatRecord {
    int health=0;
    bool alive=false;
    CriticalState critical;
    std::int64_t stun_end_server_time_ms=0;
};
struct CombatReplayStep {
    double elapsed=0, now=0;
    bool advance_fire=false;
    std::vector<EquipmentEffect> equipment;
};
struct CombatPublication {
    std::int64_t sequence=0;
    CombatRecord combat;
    std::vector<CombatReplayStep> steps;
};
struct ServerCombat {
    CombatRecord record;
    std::int64_t revision=0,base_revision=0,acknowledged_sequence=0;
    std::optional<std::int64_t> server_tick;
};
struct CombatLedger {
    CombatRecord server_combat, published_combat;
    std::deque<CombatPublication> pending;
    std::vector<CombatReplayStep> unpublished_steps;
    std::int64_t next_sequence=0,acknowledged_sequence=0,revision=0,base_revision=0,server_tick=-1;
    bool authority_handoff_pending=false;
    void initialize(const CombatState&,std::int64_t revision,std::int64_t base,std::int64_t acknowledged);
    bool mark_publication(const CombatState&);
    bool reconcile(CombatState&,const ServerCombat&,double repair_factor,double repair_speed_factor);
};
CombatRecord combat_record(const CombatState&);
bool same_combat(const CombatRecord&,const CombatRecord&);

struct SiegeConfig { bool available=false; double enable_seconds=0, disable_seconds=0, damaged_factor=0; };
struct SiegeState {
    int mode=0;
    double remaining=0, total=0, intent_elapsed=0;
    std::optional<bool> intent;
};
struct SiegeCommand {
    double throttle=0, turn=0;
    bool brake=false, fire_allowed=false, movement_intent=false;
    CombatPoint position{}, destination{};
    bool target_alive=false;
};
struct SiegeResult { bool locked=false, descriptor_changed=false; };
SiegeResult advance_siege(SiegeState&, const SiegeConfig&, double elapsed);
SiegeResult update_siege_intent(SiegeState&, const SiegeConfig&, SiegeCommand&,
    double elapsed, double speed, bool burst_active, bool engine_destroyed,
    bool engine_damaged);
} // namespace wot::offline_bot_step
