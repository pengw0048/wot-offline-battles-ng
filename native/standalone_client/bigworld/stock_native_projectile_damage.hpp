#pragma once
#include "stock_native_bot_armor_law.hpp"
#include "stock_native_bot_combat.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace stock_native_projectile_damage {
using Point=std::array<double,3>;
using Shell=stock_native_bot_armor::Shell;
using Material=stock_native_bot_armor::Material;
class Random {public:virtual ~Random()=default;virtual double gauss(double mean,double sigma)=0;virtual double uniform()=0;};
struct StunSpec {double radius=0.,duration=0.,factor=0.,guaranteed_duration=0.,damage_duration=0.,guaranteed_effect=0.,damage_effect=0.;};
struct FrozenShot {
 std::string projectile_id;std::int64_t shooter=0,fire_sequence=0;bool deadeye=false;Shell shell;
 double penetration_multiplier=1.,explosion_radius=0.,edge_damage_factor=.15;
 std::optional<double> device_damage_mean;
 std::optional<double> penetration_factor,damage_roll;std::optional<StunSpec> stun;
};
struct ImpactLayer {double distance=0.,cosine=0.;unsigned component=0;const Material* material=nullptr;bool use_lining=true;std::optional<Point> local_point,world_normal;
 ImpactLayer()=default;
 ImpactLayer(double d,double c,unsigned component_id,const Material* m,bool lining=true):distance(d),cosine(c),component(component_id),material(m),use_lining(lining){}
};
struct ArmorContact {int result=1;bool structural=false;std::size_t layer=0;double distance=0.,effective_armor=0.,piercing=0.,accumulated_loss=0.,angle_cos=0.;};
struct BlastContact {std::size_t layer=0;double damage=0.,nominal_armor=0.,distance=0.;Point point{},direction{};std::vector<std::size_t> through;std::vector<ImpactLayer> layers;};
struct CriticalLayer {
 double distance=0.;std::string name;bool structural=false;double armor=0.;
 std::optional<double> projectile_chance,explosion_chance;
 // Native chassis-local track witness and frozen descriptor zones. Absence
 // retains the source device-channel fallback without drawing another roll.
 bool chassis=false;std::optional<double> local_z,front_bound,rear_bound,bulk_factor;
 std::optional<int> damage_channel;
};
struct CriticalConfig {
 bool module_damage=true,crew_damage=true,internal_module_damage=true,deadeye=false,explosion=false;
 bool distance_filters=true;std::optional<bool> penetrated;
 double medkit_bonus=0.,engine_fire_chance=.15,min_engine_fire_damage=21.,fire_chance_factor=1.;
 int health=0;std::map<std::string,double> maxima;
};
struct CriticalOutcome {
 int damage=0;wot::offline_bot_step::CriticalState state;
 std::vector<wot::offline_bot_step::CriticalEvent> events;
 std::map<std::string,double> device_loss;std::set<std::string> crew_operations;
 bool ignite=false,changed=false;
};
// Layers include native exposed hits and only proved internal geometry hits.
CriticalOutcome resolveCritical(const FrozenShot&,const CriticalConfig&,
 const wot::offline_bot_step::CriticalState&,const std::vector<CriticalLayer>&,int hull_damage,Random&);
// Factor order: mobility, traverse, turret_speed, vision, speed, reload,
// aim_time, bloom_move, bloom_rotation, bloom_turret, dispersion.
struct StunConfig {double minimum_duration=0.,resist_duration=0.,resist_effect=0.;std::array<double,11> factors{};};
struct StunOutcome {std::int64_t end_ms=0,duration_ms=0;std::array<double,11> factors{};};
std::optional<StunOutcome> stun(const FrozenShot&,const StunSpec&,const StunConfig&,int damage,
 double distance,std::int64_t impact_ms,std::int64_t battle_end_ms);
struct BlastComponent {unsigned component=0;Point local_burst{},minimum{},maximum{};};
class BlastWorld {public:virtual ~BlastWorld()=default;
 // Returns only attached, loaded structural components; local_burst is the
 // exact world burst transformed into that component's local frame.
 virtual std::vector<BlastComponent> components(const Point& burst,double radius)=0;
 virtual Point toWorld(unsigned component,const Point& local)=0;
 virtual std::vector<ImpactLayer> vehicleRay(const Point& start,const Point& end)=0;
 virtual bool visible(const Point& scene_start,const Point& contact,const Point& burst)=0;
};
struct SeedRay {Point start{},end{};std::vector<ImpactLayer> layers;};
std::optional<BlastContact> chooseBlast(const FrozenShot&,const Point& burst,
 const Point& scene_start,double rolled_damage,double spall,BlastWorld&,const std::optional<SeedRay>& seed={});
double sampleShellValue(double mean,Random&);
double freezePenetration(FrozenShot&,Random&);
double freezeDamage(FrozenShot&,Random&);
// Splash draws once for each victim and shares that roll across its rays.
int sampleSplashDamage(const FrozenShot&,Random&);
std::optional<ArmorContact> resolveArmor(FrozenShot&,double range,double initial_loss,
 const std::vector<ImpactLayer>&,Random&);
int directDamage(const FrozenShot&,int armor_result,double nominal_armor,double spall);
std::optional<BlastContact> heBlast(const FrozenShot&,const Point& burst,const Point& start,
 const Point& end,const std::vector<ImpactLayer>&,double rolled_damage,double spall);
std::optional<double> firstRicochetMultiplier(const std::string& kind);
struct StickerSegment {std::uint64_t encoded=0;Point decoded_start{},decoded_end{};};
// Callers prove the decoded segment against the same component BSP before use.
std::optional<StickerSegment> encodeSticker(unsigned component,unsigned sticker,
 const Point& minimum,const Point& maximum,const Point& local_start,const Point& local_end);
struct TargetIdentity {bool human=false;std::int64_t id=0;};
struct Effect {
 TargetIdentity target;int damage=0,result=1,hull_damage=0;Point impact{};
 std::optional<Point> target_position;std::optional<int> potential_damage;
 std::optional<bool> structural_armor_hit,high_explosive;
 std::optional<CriticalOutcome> critical;std::optional<StunOutcome> stun;std::vector<std::string> crew_roster;
 std::optional<std::uint64_t> damage_sticker;
 std::int64_t critical_target_base_revision=0,critical_target_ack_sequence=0;
};
struct RicochetContact {Point impact{},world_normal{};std::int64_t resolved_time_ms=0;double checked_distance=0.,piercing_loss=0.,elapsed_delta=0.;};
struct Resolution {std::optional<Effect> direct;std::vector<Effect> splash;std::optional<RicochetContact> ricochet;};

struct Target {
 TargetIdentity identity;Point position{};double spall=1.;CriticalConfig config;
 wot::offline_bot_step::CriticalState critical;std::vector<std::string> crew_roster;
 std::optional<double> track_front,track_rear,track_bulk;
 StunConfig stun_config;
 std::int64_t critical_target_base_revision=0,critical_target_ack_sequence=0;
};
struct DirectEvidence {
 Point impact{},query_start{},query_end{},incoming{},trajectory_start{},trajectory_end{};
 double range_distance=0.,piercing_loss=0.,trajectory_start_time=0.,trajectory_end_time=0.,checked_distance=0.;
 bool has_trajectory_query=false;
 std::int64_t elapsed_ms=0,impact_server_ms=0,battle_end_ms=0;
 std::vector<ImpactLayer> collisions;std::optional<Point> world_normal;
};
class DamageWorld:public BlastWorld {public:
 // Pure native compartment proof: use the exact direct ray or finite HE cone;
 // exclude devices already represented by the supplied native layers.
 virtual std::vector<CriticalLayer> internal(const Target&,const Point& start,
     const Point& end,bool explosion,const std::vector<ImpactLayer>& covered)=0;
 virtual std::optional<std::uint64_t> sticker(const Target&,const Point&,const Point&,
     const std::vector<ImpactLayer>&,const std::optional<ArmorContact>&,int result)=0;
};
// Processes one proven direct hit. The caller owns the projectile RNG stream,
// provides historical target state/geometry, and publishes the returned DTO.
std::optional<Effect> direct(FrozenShot&,const Target&,const DirectEvidence&,DamageWorld&,Random&,
                            std::optional<RicochetContact>&);
// Called in record order for each alive non-direct victim; draws one victim
// roll before surface proof, preserving the fixed source's RNG ordering.
std::optional<Effect> splash(const FrozenShot&,const Target&,const Point& burst,
 const Point& scene_start,const Point& visual_impact,std::int64_t impact_server_ms,
 std::int64_t battle_end_ms,DamageWorld&,Random&);

}
