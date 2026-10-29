#include "stock_native_projectile_damage.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
namespace stock_native_projectile_damage {
namespace {
using namespace stock_native_bot_armor;
std::vector<std::size_t> ordered(const std::vector<ImpactLayer>& layers){std::vector<std::size_t> index(layers.size());std::iota(index.begin(),index.end(),0);std::stable_sort(index.begin(),index.end(),[&](std::size_t a,std::size_t b){return layers[a].distance<layers[b].distance;});return index;}
bool structural(const Material* m){return m&&m->vehicle_damage_factor.value_or(1.)!=0.;}
bool blastStructural(const Material* m){return m&&m->vehicle_damage_factor&&std::isfinite(*m->vehicle_damage_factor)&&*m->vehicle_damage_factor>0.;}
int heDamage(const FrozenShot&s,double rolled,double armor,double fraction,double spall){
 const double d=std::max(0.,std::min(1.,fraction));const double factor=s.shell.explosion_damage_factor+(s.edge_damage_factor-s.shell.explosion_damage_factor)*d;
 const double liner=std::isfinite(spall)?std::max(1.,spall):1.;const double result=rolled*factor-s.shell.explosion_absorption_factor*armor*liner;
 if(!std::isfinite(result)||result>double(std::numeric_limits<int>::max()))throw std::domain_error("Projectile damage is out of bounds");return result>0.?int(result):0;
}
}
CriticalOutcome resolveCritical(const FrozenShot& shot,const CriticalConfig& config,
 const wot::offline_bot_step::CriticalState& before,const std::vector<CriticalLayer>& input,int hull_damage,Random& rng){
 CriticalOutcome out;out.state=before;out.damage=hull_damage;if(!config.module_damage)return out;
 std::optional<double> channel[2];bool drawn[2]={false,false};
 const auto roll=[&](int index)->std::optional<double>{if(drawn[index])return channel[index];drawn[index]=true;
  if(index==0)channel[index]=sampleShellValue(shot.shell.damage,rng);
  else if(shot.device_damage_mean){const double mean=*shot.device_damage_mean;channel[index]=mean*.75+mean*.5*rng.uniform();}
  return channel[index];};
 const double device_damage=roll(1).value_or(double(hull_damage));
 const bool deadeye=config.deadeye&&(shot.shell.kind=="ARMOR_PIERCING"||shot.shell.kind=="ARMOR_PIERCING_CR"||shot.shell.kind=="HOLLOW_CHARGE");
 std::vector<std::size_t> order(input.size());std::iota(order.begin(),order.end(),0);std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return input[a].distance<input[b].distance;});
 std::optional<double> stop,exit;if(config.distance_filters){int walls=0;for(auto i:order){const auto& hit=input[i];if(hit.structural&&hit.armor>0.){if(!stop)stop=hit.distance;if(++walls==2){exit=hit.distance;break;}}}if(!config.penetrated||*config.penetrated)stop.reset();}
 const std::set<std::string> devices={"engineHealth","ammoBayHealth","fuelTankHealth","radioHealth","gunHealth","turretRotatorHealth","surveyingDeviceHealth","leftTrackHealth","rightTrackHealth"};
 const std::set<std::string> crew={"commanderHealth","driverHealth","gunner1Health","gunner2Health","loader1Health","loader2Health","radioman1Health","radioman2Health"};
 std::set<std::string> seen;
 for(auto index:order){const auto& hit=input[index];const auto& name=hit.name;if(name.empty()||(stop&&hit.distance>*stop)||(exit&&hit.distance>*exit))continue;
  const bool is_crew=crew.count(name)!=0;if(!is_crew&&!devices.count(name))continue;if(!seen.insert(name).second)continue;
  double fallback=is_crew?(config.explosion?.15:.33):name=="ammoBayHealth"?.27:(name=="leftTrackHealth"||name=="rightTrackHealth")?1.:name=="gunHealth"?.33:.45;
  const auto chance_value=config.explosion?hit.explosion_chance:hit.projectile_chance;double chance=std::min(1.,chance_value.value_or(fallback)+(deadeye?.03:0.));
  if(is_crew){chance*=std::max(0.,1.-.5*config.medkit_bonus);if(config.crew_damage&&rng.uniform()<chance){auto role=name.substr(0,name.size()-6);out.crew_operations.insert(role);out.state.crew_ko.insert(role);}continue;}
  if(rng.uniform()>=chance)continue;double loss=device_damage;
  if(!config.explosion&&(name=="leftTrackHealth"||name=="rightTrackHealth")&&hit.chassis&&hit.local_z&&hit.front_bound&&hit.rear_bound&&hit.damage_channel&&(*hit.damage_channel==0||*hit.damage_channel==1)){
   const bool middle=*hit.local_z<*hit.front_bound&&*hit.local_z>*hit.rear_bound;
   if(!middle||(hit.bulk_factor&&std::isfinite(*hit.bulk_factor)&&*hit.bulk_factor>0.)){const auto channel_value=roll(*hit.damage_channel);if(channel_value)loss=*channel_value*(middle?1. / *hit.bulk_factor:1.);}
  }
  const auto found=config.maxima.find(name);const double maximum=found==config.maxima.end()?100.:found->second;auto& device=out.state.devices[name];device.maximum=maximum;
  const double operation=std::max(0.,std::min(std::max(1.,maximum),loss));if(operation>.0005)out.device_loss[name]=std::min(std::max(1.,maximum),out.device_loss[name]+operation);
  const double previous=device.hp.value_or(maximum),damage=std::max(0.,loss);device.hp=device.destroyed&&damage>0.?0.:std::max(0.,previous-damage);
  if(*device.hp<=0.){device.destroyed=true;device.critical=false;}else if(device.critical||*device.hp<=maximum*.5)device.critical=true;else device.critical=false;
  if(name=="ammoBayHealth"&&*device.hp<=0.){out.damage=config.health+10;out.state.ammo_rack_death=true;break;}
  if(*device.hp<previous&&!out.state.fire){if(name=="fuelTankHealth"&&*device.hp<=0.)out.state.fire=true;
   else if(name=="engineHealth"&&device_damage>=std::max(0.,config.min_engine_fire_damage)&&config.engine_fire_chance*config.fire_chance_factor>0.&&rng.uniform()<config.engine_fire_chance*config.fire_chance_factor)out.state.fire=true;}
 }
 out.ignite=out.state.fire&&!before.fire;out.events=wot::offline_bot_step::critical_events(before,out.state,config.explosion?"explosion":"shot");
 out.changed=!out.events.empty()||!out.device_loss.empty()||!out.crew_operations.empty();
 if(!out.changed){for(const auto& item:out.state.devices){const auto old=before.devices.find(item.first);if(old==before.devices.end()||old->second.hp!=item.second.hp||old->second.destroyed!=item.second.destroyed||old->second.critical!=item.second.critical){out.changed=true;break;}}}
 return out;
}
std::optional<StunOutcome> stun(const FrozenShot& shot,const StunSpec& spec,const StunConfig& config,int damage,double distance,std::int64_t impact_ms,std::int64_t battle_end_ms){
 if(shot.shell.kind!="HIGH_EXPLOSIVE"||!std::isfinite(distance)||distance<0.||distance>spec.radius||shot.shell.damage<=0.)return {};
 const double fraction=std::max(0.,std::min(1.,double(damage)/shot.shell.damage));
 const double duration=spec.duration*(spec.guaranteed_duration+spec.damage_duration*fraction)*(1.-config.resist_duration);
 if(!std::isfinite(duration)||duration<=0.||duration<config.minimum_duration)return {};
 const double strength=std::max(0.,std::min(1.,spec.factor*(spec.guaranteed_effect+spec.damage_effect*fraction)*(1.-config.resist_effect)));
 StunOutcome out;out.end_ms=std::min(battle_end_ms,impact_ms+std::int64_t(std::round(duration*1000.)));if(out.end_ms<=impact_ms)return {};out.duration_ms=out.end_ms-impact_ms;
 for(std::size_t i=0;i<out.factors.size();++i){const double factor=1.+(config.factors[i]-1.)*strength;const bool increasing=i>=5;
  if(!std::isfinite(factor)||factor<(increasing?1.:0.)||factor>(increasing?100.:1.))return {};out.factors[i]=std::round(factor*1.e6)/1.e6;}
 return out;
}
double sampleShellValue(double mean,Random& rng){if(!std::isfinite(mean))throw std::invalid_argument("Projectile roll mean invalid");if(mean<=0.)return 0.;for(;;){const double value=rng.gauss(mean,mean*.25/3.);if(!std::isfinite(value))throw std::invalid_argument("Projectile roll invalid");if(value>=mean*.75&&value<=mean*1.25)return value;}}
double freezePenetration(FrozenShot& shot,Random& rng){if(!shot.penetration_factor)shot.penetration_factor=sampleShellValue(1.,rng);return *shot.penetration_factor;}
double freezeDamage(FrozenShot& shot,Random& rng){if(!shot.damage_roll)shot.damage_roll=sampleShellValue(shot.shell.damage,rng);return *shot.damage_roll;}
int sampleSplashDamage(const FrozenShot& shot,Random& rng){return int(sampleShellValue(shot.shell.damage,rng));}
std::optional<ArmorContact> resolveArmor(FrozenShot& shot,double range,double initial_loss,const std::vector<ImpactLayer>& layers,Random& rng){
 if(layers.empty())return {};const double factor=freezePenetration(shot,rng);const double multiplier=shot.penetration_multiplier*factor;
 auto scaled=shot.shell;if(scaled.caliber==0.)scaled.caliber=100.;scaled.piercing_near*=multiplier;scaled.piercing_far*=multiplier;const double base=stock_native_bot_armor::piercing(scaled,range);
 double spent=initial_loss;std::optional<double> last;std::set<std::pair<unsigned,int>> once;
 for(auto index:ordered(layers)){const auto& layer=layers[index];if(shot.shell.kind=="HOLLOW_CHARGE"&&last)spent+=std::max(0.,base-spent)*std::min(1.,.5*std::max(0.,layer.distance-*last));
  if(!layer.material){if(base-spent<=0.)break;if(shot.shell.kind=="HOLLOW_CHARGE")last=layer.distance;continue;}
  const auto& material=*layer.material;const auto key=std::make_pair(layer.component,material.kind);if(material.once&&once.count(key))continue;
  const double armor=material.armor.value_or(0.);if(armor<0.)continue;const auto p=stock_native_bot_armor::penetration(scaled,range,armor,layer.cosine,spent,material,!(shot.shell.kind=="HOLLOW_CHARGE"&&last));
  ArmorContact c{p.result,structural(&material),index,layer.distance,p.effective,p.piercing,spent,layer.cosine};if(c.structural)return c;
  if(shot.shell.kind=="HIGH_EXPLOSIVE"){c.result=1;return c;}if(c.result!=2)return c;spent+=p.effective;if(base-spent<=0.){c.result=1;return c;}
  if(shot.shell.kind=="HOLLOW_CHARGE")last=layer.distance+armor*.001;if(material.once)once.insert(key);
 }return {};
}
int directDamage(const FrozenShot&s,int result,double nominal,double spall){if(!s.damage_roll)throw std::invalid_argument("Projectile damage roll missing");const auto rolled=int(*s.damage_roll);if(result==2)return rolled;if(s.shell.kind=="HIGH_EXPLOSIVE")return heDamage(s,rolled,nominal,0.,spall);return 0;}
std::optional<BlastContact> heBlast(const FrozenShot&s,const Point& burst,const Point& start,const Point& end,const std::vector<ImpactLayer>& layers,double rolled,double spall){
 if(s.shell.kind!="HIGH_EXPLOSIVE"||!std::isfinite(rolled)||s.explosion_radius<=0.)return {};Point delta{},direction{};double squared=0.;for(int i=0;i<3;++i){if(!std::isfinite(burst[i])||!std::isfinite(start[i])||!std::isfinite(end[i]))return {};delta[i]=end[i]-start[i];squared+=delta[i]*delta[i];}if(squared<=0.)return {};
 const double length=std::sqrt(squared);double along=0.;for(int i=0;i<3;++i){direction[i]=delta[i]/length;along+=(burst[i]-start[i])*direction[i];}
 std::vector<std::size_t> through;for(auto index:ordered(layers)){const auto& layer=layers[index];if(layer.distance<0.||layer.distance>length||layer.distance<along-.001)continue;
  Point point{};double distance2=0.;for(int i=0;i<3;++i){point[i]=start[i]+direction[i]*layer.distance;const auto d=point[i]-burst[i];distance2+=d*d;}const double distance=std::sqrt(distance2);
  if(distance>s.explosion_radius){if(blastStructural(layer.material))return {};continue;}through.push_back(index);if(!blastStructural(layer.material))continue;
  const auto armor=layer.material->armor;if(!armor||!std::isfinite(*armor)||*armor<0.)return {};
  BlastContact result;result.layer=index;result.damage=heDamage(s,rolled,*armor,distance/s.explosion_radius,layer.material->use_lining?spall:1.);result.nominal_armor=*armor;result.distance=distance;result.point=point;result.direction=direction;result.through=std::move(through);return result;
 }return {};
}
std::optional<BlastContact> chooseBlast(const FrozenShot& shot,const Point& burst,const Point& scene_start,double rolled,double spall,BlastWorld& world,const std::optional<SeedRay>& seed){
 if(shot.shell.kind!="HIGH_EXPLOSIVE"||shot.explosion_radius<=0.)return {};
 std::vector<BlastContact> candidates;std::vector<Point> points;
 auto collect=[&](const Point& start,const Point& end,const std::vector<ImpactLayer>& layers){auto hit=heBlast(shot,burst,start,end,layers,rolled,spall);if(hit){hit->layers=layers;candidates.push_back(std::move(*hit));}};
 if(seed){collect(seed->start,seed->end,seed->layers);Point d{};double norm=0.;for(int i=0;i<3;++i){d[i]=seed->end[i]-seed->start[i];norm+=d[i]*d[i];}if(norm>1.e-18){norm=std::sqrt(norm);Point point{};for(int i=0;i<3;++i)point[i]=burst[i]+d[i]/norm*shot.explosion_radius;points.push_back(point);}}
 std::vector<BlastComponent> components;try{components=world.components(burst,shot.explosion_radius);}catch(const std::exception&){}
 std::set<Point> seen_points;for(const auto& component:components){try{
  Point closest{},middle{};double distance=0.;bool valid=true;for(int i=0;i<3;++i){if(!std::isfinite(component.minimum[i])||!std::isfinite(component.maximum[i])||!std::isfinite(component.local_burst[i])||component.minimum[i]>component.maximum[i])valid=false;
   closest[i]=std::max(component.minimum[i],std::min(component.maximum[i],component.local_burst[i]));middle[i]=(component.minimum[i]+component.maximum[i])*.5;const double d=component.local_burst[i]-closest[i];distance+=d*d;}
  if(!valid||distance>shot.explosion_radius*shot.explosion_radius)continue;std::vector<Point> local{closest};
  for(int axis=0;axis<3;++axis)for(int side=0;side<2;++side){const double bound=side?component.maximum[axis]:component.minimum[axis];auto center=middle,projection=closest;center[axis]=bound;projection[axis]=bound;local.push_back(center);local.push_back(projection);}
  std::set<Point> local_seen;for(const auto& p:local)if(local_seen.insert(p).second){const Point world_point=world.toWorld(component.component,p);if(seen_points.insert(world_point).second)points.push_back(world_point);}
 }catch(const std::exception&){} }
 std::set<Point> directions;for(const auto& point:points){Point d{};double length=0.;for(int i=0;i<3;++i){d[i]=point[i]-burst[i];length+=d[i]*d[i];}if(length<=1.e-18)continue;length=std::sqrt(length);Point key{},start{},end{};
  for(int i=0;i<3;++i){d[i]/=length;key[i]=std::round(d[i]*1.e9)/1.e9;start[i]=burst[i]-d[i]*.001;end[i]=burst[i]+d[i]*shot.explosion_radius;}
  if(!directions.insert(key).second)continue;try{collect(start,end,world.vehicleRay(start,end));}catch(const std::exception&){}
 }
 std::stable_sort(candidates.begin(),candidates.end(),[](const BlastContact& a,const BlastContact& b){return a.damage!=b.damage?a.damage>b.damage:a.distance<b.distance;});
 for(const auto& candidate:candidates)try{if(world.visible(scene_start,candidate.point,burst))return candidate;}catch(const std::exception&){}return {};
}
std::optional<StickerSegment> encodeSticker(unsigned component,unsigned sticker,const Point& minimum,const Point& maximum,const Point& start,const Point& end){
 if(component==0||component>3||sticker>255)return {};Point delta{};double entry=0.,exit=1.;for(int i=0;i<3;++i){if(!std::isfinite(start[i])||!std::isfinite(end[i])||!std::isfinite(minimum[i])||!std::isfinite(maximum[i])||maximum[i]<=minimum[i])return {};
  delta[i]=end[i]-start[i];if(std::abs(delta[i])<=1.e-12){if(start[i]<minimum[i]||start[i]>maximum[i])return {};continue;}double a=(minimum[i]-start[i])/delta[i],b=(maximum[i]-start[i])/delta[i];if(a>b)std::swap(a,b);entry=std::max(entry,a);exit=std::min(exit,b);if(exit<entry)return {};}
 StickerSegment out;out.encoded=std::uint64_t(sticker)|(std::uint64_t(component)<<8);int quantized[2][3];
 for(int side=0;side<2;++side)for(int i=0;i<3;++i){const double p=start[i]+delta[i]*(side?exit:entry);const double ratio=std::max(0.,std::min(1.,(p-minimum[i])/(maximum[i]-minimum[i])));const auto q=unsigned(std::round(ratio*255.));quantized[side][i]=int(q);out.encoded|=std::uint64_t(q)<<(16+side*24+i*8);}
 if(quantized[0][0]==quantized[1][0]&&quantized[0][1]==quantized[1][1]&&quantized[0][2]==quantized[1][2])return {};
 for(int i=0;i<3;++i){const float low=float(minimum[i]),span=float(float(maximum[i])-low),unit=float(span*float(1./255.));const float a=float(low+float(unit*float(quantized[0][i]))),b=float(low+float(unit*float(quantized[1][i])));const float offset=float(float(b-a)*.01f);out.decoded_start[i]=double(float(a-offset));out.decoded_end[i]=double(float(b+offset));}return out;
}
std::optional<Effect> direct(FrozenShot& shot,const Target& target,const DirectEvidence& evidence,DamageWorld& world,Random& rng,std::optional<RicochetContact>& ricochet){
 ricochet.reset();if(evidence.collisions.empty())return {};Point delta{},direction{};double length2=0.;for(int i=0;i<3;++i){delta[i]=evidence.query_end[i]-evidence.query_start[i];length2+=delta[i]*delta[i];}const double length=std::sqrt(length2);if(length<=1.e-6)return {};
 for(int i=0;i<3;++i)direction[i]=delta[i]/length;double first=length;for(const auto& layer:evidence.collisions)first=std::min(first,layer.distance);first=std::max(0.,std::min(length,first));
 const double trace_distance=first+std::max(0.,shot.shell.caliber)/100.;Point trace_end{};for(int i=0;i<3;++i)trace_end[i]=evidence.query_start[i]+direction[i]*trace_distance;
 auto traced=evidence.collisions;if(shot.shell.kind!="HIGH_EXPLOSIVE"&&trace_distance>length+1.e-6){auto extended=world.vehicleRay(evidence.query_start,trace_end);if(!extended.empty())traced=std::move(extended);}
 std::vector<ImpactLayer> layers;for(const auto& layer:traced)if(layer.distance<=trace_distance+1.e-6)layers.push_back(layer);
 auto contact=resolveArmor(shot,evidence.range_distance,evidence.piercing_loss,layers,rng);const int result=contact?contact->result:1;
 std::optional<std::uint64_t> sticker;try{sticker=world.sticker(target,evidence.query_start,trace_end,layers,contact,result);}catch(const std::exception&){}
 const double roll=sampleShellValue(shot.shell.damage,rng);int damage=result==2?int(roll):0;const bool he=shot.shell.kind=="HIGH_EXPLOSIVE";
 double hit_distance=first;if(contact)hit_distance=std::max(0.,std::min(length,contact->distance));Point critical_point{};for(int i=0;i<3;++i)critical_point[i]=evidence.query_start[i]+direction[i]*hit_distance;
 std::optional<BlastContact> blast;Point explosion_direction=direction;if(he&&result!=2){Point scene_start=critical_point;double incoming_length=0.;for(double x:evidence.incoming)incoming_length+=x*x;if(incoming_length>1.e-18){incoming_length=std::sqrt(incoming_length);for(int i=0;i<3;++i)scene_start[i]-=evidence.incoming[i]/incoming_length*.002;}
  SeedRay seed{evidence.query_start,evidence.query_end,evidence.collisions};blast=chooseBlast(shot,critical_point,scene_start,int(roll),target.spall,world,seed);damage=blast?int(blast->damage):0;
  if(blast){critical_point=blast->point;explosion_direction=blast->direction;std::vector<ImpactLayer> selected;for(auto index:blast->through)selected.push_back(blast->layers[index]);layers=std::move(selected);}
  else {const double stop=contact?contact->distance:first;layers.erase(std::remove_if(layers.begin(),layers.end(),[&](const ImpactLayer& layer){return layer.distance>stop+1.e-6;}),layers.end());}
 }else if(he)damage=int(roll);
 const int hull_damage=damage;Effect effect;effect.target=target.identity;effect.impact=evidence.impact;effect.result=result;effect.hull_damage=hull_damage;effect.damage=result==0?0:damage;
 effect.potential_damage=std::max(0,std::min(5000,result==2?int(roll):int(shot.shell.damage)));effect.structural_armor_hit=contact&&contact->structural;effect.high_explosive=he;
 effect.damage_sticker=sticker;
 if(result!=0){CriticalConfig config=target.config;config.deadeye=shot.deadeye;config.explosion=he;config.distance_filters=!he;config.penetrated=he?std::optional<bool>{}:std::optional<bool>{result==2};
  std::vector<CriticalLayer> critical;for(const auto& hit:layers){CriticalLayer c;c.distance=hit.distance;c.structural=structural(hit.material);c.armor=hit.material?hit.material->armor.value_or(0.):0.;
   if(hit.material){c.name=hit.material->device;c.projectile_chance=hit.material->chance_projectile;c.explosion_chance=hit.material->chance_explosion;c.damage_channel=hit.material->damage_kind;}
   c.chassis=hit.component==0;if(hit.local_point)c.local_z=(*hit.local_point)[2];c.front_bound=target.track_front;c.rear_bound=target.track_rear;c.bulk_factor=target.track_bulk;critical.push_back(std::move(c));}
  if(config.internal_module_damage&&((!he&&result==2)||(he&&(result==2||blast)))){Point start=he?critical_point:evidence.query_start,end=trace_end;if(he)for(int i=0;i<3;++i)end[i]=start[i]+explosion_direction[i]*std::max(0.,shot.shell.caliber)/100.;
   try{auto inside=world.internal(target,start,end,he,layers);critical.insert(critical.end(),inside.begin(),inside.end());}catch(const std::exception&){}}
  auto outcome=resolveCritical(shot,config,target.critical,critical,effect.damage,rng);effect.damage=outcome.damage;
  if(outcome.changed){outcome.state.crew_roster={target.crew_roster.begin(),target.crew_roster.end()};effect.crew_roster=target.crew_roster;effect.critical=std::move(outcome);effect.critical_target_base_revision=target.critical_target_base_revision;effect.critical_target_ack_sequence=target.critical_target_ack_sequence;}
 }
 if(shot.stun&&(blast||(contact&&contact->structural))){const double distance=blast?blast->distance:0.;effect.stun=stun(shot,*shot.stun,target.stun_config,effect.damage,distance,evidence.impact_server_ms,evidence.battle_end_ms);}
 if(result==0&&contact){const auto& selected=layers[contact->layer];const auto normal=selected.world_normal?selected.world_normal:evidence.world_normal;if(normal){RicochetContact r;r.impact=evidence.impact;r.world_normal=*normal;r.resolved_time_ms=evidence.elapsed_ms;r.checked_distance=evidence.checked_distance;r.piercing_loss=std::max(evidence.piercing_loss,contact->accumulated_loss);
   if(!evidence.has_trajectory_query){ricochet=r;return effect;}
   if(evidence.trajectory_end_time<evidence.trajectory_start_time)throw std::invalid_argument("Projectile ricochet time order invalid");
   const double cf=contact->distance/length,ff=first/length;if(cf+1.e-6<ff)throw std::invalid_argument("Projectile ricochet precedes impact");const double fraction_delta=std::max(0.,cf-ff);double trajectory_length2=0.;for(int i=0;i<3;++i){const double d=evidence.trajectory_end[i]-evidence.trajectory_start[i];r.impact[i]=evidence.trajectory_start[i]+d*cf;trajectory_length2+=d*d;}
   r.elapsed_delta=std::max(0.,(evidence.trajectory_end_time-evidence.trajectory_start_time)*fraction_delta);r.resolved_time_ms+=std::int64_t(std::round(r.elapsed_delta*1000.));r.checked_distance+=std::sqrt(trajectory_length2)*fraction_delta;ricochet=r;}}
 return effect;
}
std::optional<Effect> splash(const FrozenShot& shot,const Target& target,const Point& burst,const Point& scene_start,const Point& visual_impact,std::int64_t impact_ms,std::int64_t battle_end_ms,DamageWorld& world,Random& rng){
 if(shot.shell.kind!="HIGH_EXPLOSIVE"||shot.explosion_radius<=0.)return {};const int rolled=sampleSplashDamage(shot,rng);const auto contact=chooseBlast(shot,burst,scene_start,rolled,target.spall,world);if(!contact)return {};
 Effect effect;effect.target=target.identity;effect.impact=visual_impact;effect.target_position=target.position;effect.hull_damage=int(contact->damage);effect.damage=effect.hull_damage;effect.result=effect.damage>0?2:1;
 if(effect.damage>0){CriticalConfig config=target.config;config.deadeye=shot.deadeye;config.explosion=true;config.distance_filters=false;config.penetrated.reset();std::vector<ImpactLayer> layers;for(auto index:contact->through)layers.push_back(contact->layers[index]);
  std::vector<CriticalLayer> critical;for(const auto& hit:layers){CriticalLayer c;c.distance=hit.distance;c.structural=structural(hit.material);c.armor=hit.material?hit.material->armor.value_or(0.):0.;if(hit.material){c.name=hit.material->device;c.projectile_chance=hit.material->chance_projectile;c.explosion_chance=hit.material->chance_explosion;c.damage_channel=hit.material->damage_kind;}critical.push_back(std::move(c));}
  Point end{};for(int i=0;i<3;++i)end[i]=contact->point[i]+contact->direction[i]*std::max(0.,shot.shell.caliber)/100.;if(config.internal_module_damage)try{auto inside=world.internal(target,contact->point,end,true,layers);critical.insert(critical.end(),inside.begin(),inside.end());}catch(const std::exception&){}
  auto result=resolveCritical(shot,config,target.critical,critical,effect.damage,rng);effect.damage=result.damage;if(result.changed){result.state.crew_roster={target.crew_roster.begin(),target.crew_roster.end()};effect.crew_roster=target.crew_roster;effect.critical=std::move(result);effect.critical_target_base_revision=target.critical_target_base_revision;effect.critical_target_ack_sequence=target.critical_target_ack_sequence;}}
 if(shot.stun)effect.stun=stun(shot,*shot.stun,target.stun_config,effect.damage,contact->distance,impact_ms,battle_end_ms);
 if(effect.damage<=0&&!effect.stun)return {};return effect;
}
std::optional<double> firstRicochetMultiplier(const std::string& kind){if(kind=="ARMOR_PIERCING"||kind=="ARMOR_PIERCING_CR")return .75;if(kind=="HOLLOW_CHARGE")return 1.;return {};}
}
