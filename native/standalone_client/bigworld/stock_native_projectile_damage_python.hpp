#pragma once
#include "stock_native_projectile_damage.hpp"
struct _object;
namespace stock_native_projectile_damage {
struct ShotDescriptor {FrozenShot shot;std::optional<StunSpec> stun;};
struct LaunchDescriptor {double speed=0.,gravity=0.,maximum=0.,raw_radius=0.;FrozenShot shot;bool explosion_factors=false;};
struct TargetDescriptor {
 CriticalConfig critical;
 std::optional<double> front_bound,rear_bound,bulk_factor;
 double stun_resistance_duration=0.,stun_resistance_effect=0.,spall=1.;
 std::vector<std::string> crew_roster;
};
// config.json he_tuning: operator overrides of the descriptor HE damage and absorption factors.
struct HeTuning {std::optional<double> splash_fraction,armor_factor;};
// Borrows the current round's tuning; null reads the combat_rules module overrides (the hidden worker's Python round).
void installHeTuning(const HeTuning* tuning);
// Forgets the borrowed tuning only while it is still this owner's.
void releaseHeTuning(const HeTuning* tuning);
// Installation-only readers call the reviewed fixed-source descriptor producers.
bool readShotDescriptor(_object* shot,ShotDescriptor&);
bool readLaunchDescriptor(_object* shot,LaunchDescriptor&);
bool readTargetDescriptor(_object* descriptor,_object* critical_descriptor,TargetDescriptor&,
                          _object* accepted_critical_profile=nullptr);
bool readStunConfig(_object* config,StunConfig&);
// Hot ingress only reads record plain dictionaries and native entity fields.
bool readProjectileTargetState(_object* battle,_object* record,_object* entity,
 const TargetDescriptor&,Target&);
}
