#include "pch.hpp"
#include "stock_native_bot_services.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stock_native_bot {
namespace {
ID plannerId(Key key){return key.kind==offline_simulation::ActorKind::Human?1000000+key.id:key.id;}
geometry::Pose geomPose(const step::AimPose& p){geometry::Pose q;q.position=p.position;q.yaw=p.yaw;q.pitch=p.pitch;q.roll=p.roll;
 q.turret_yaw=p.turret_yaw;q.gun_pitch=p.gun_pitch;q.native_components=false;q.native_hydraulics=false;return q;}
Point plain(const BW::Vector3& p){return {p.x,p.y,p.z};}
BW::Vector3 native(const Point& p){return BW::Vector3(float(p[0]),float(p[1]),float(p[2]));}
}
class NativeServices::WeaponAdapter final:public step::WeaponRuntimeWorld {
 NativeServices& owner_;
 NativeWeaponWorld& world(){if(!owner_.weapon_world_)throw std::logic_error("Native weapon services are unbound");return *owner_.weapon_world_;}
public:
 explicit WeaponAdapter(NativeServices& owner):owner_(owner){}
 void bind(Key source,const std::optional<step::WeaponTarget>& target,const stock_native_bot_armor::Shell* shell)override {
  const ID target_id=target?plannerId({target->target.kind=="human"?offline_simulation::ActorKind::Human:offline_simulation::ActorKind::Bot,target->target.id}):0;
  world().bind(plannerId(source),target_id,target?geomPose(target->pose):geometry::Pose{},shell);
 }
 std::optional<step::WeaponTarget> target(const std::string& kind,ID id)override {
  const Key key{kind=="human"?offline_simulation::ActorKind::Human:offline_simulation::ActorKind::Bot,id};
  const auto it=owner_.entities_.find(plannerId(key));if(it==owner_.entities_.end())return {};
  auto value=it->second.target;value.available=it->second.available;value.alive=it->second.alive;return value;
 }
 std::optional<Point> barrel_point(Key key,const step::AimPose& pose)override {
  const auto row=owner_.entities_.find(plannerId(key));if(row==owner_.entities_.end()||!row->second.available)return {};
  const auto vehicle=owner_.vehicles_.find(key);if(!vehicle||!vehicle->ready)return {};
  const auto& gun=vehicle->components[3];if(!gun.bounds_available)return {};
  step::GunMount mount{vehicle->hull_offset,vehicle->turret_offset,vehicle->gun_offset};
  const auto origin=step::shot_origin(pose,mount),direction=step::shot_direction(pose);Point end;
  for(int i=0;i<3;++i)end[i]=origin[i]+direction[i]*gun.maximum[2];return end;
 }
 bool source_available(Key key)override {const auto row=owner_.entities_.find(plannerId(key));
  const auto vehicle=owner_.vehicles_.find(key);return row!=owner_.entities_.end()&&row->second.available&&vehicle&&vehicle->ready;}
 std::vector<step::LaneBody> lane_bodies(Key source)override {
  std::vector<step::LaneBody> result;for(const auto& pair:owner_.entities_){const auto& row=pair.second;step::LaneBody b;
   b.id=row.key.id;b.kind=row.key.kind==offline_simulation::ActorKind::Human?"human":"bot";b.team=row.team;
   b.source=row.key==source;b.alive=row.alive;b.available=row.available;b.position=row.target.pose.position;b.radius=row.radius;result.push_back(std::move(b));}
  return result;
 }
 bool turret_clear(const step::AimPose&a,const step::AimPose&b)override{return world().turret_clear(a,b);}
 bool barrel_under_water(const Point&p)override{return world().barrel_under_water(p);}
 bool vehicle_segment(const step::LaneBody& body,const Point&a,const Point&b)override {auto mapped=body;
  mapped.id=plannerId({body.kind=="human"?offline_simulation::ActorKind::Human:offline_simulation::ActorKind::Bot,body.id});return world().vehicle_segment(mapped,a,b);}
 bool debris_segment(const Point&a,const Point&b)override{return world().debris_segment(a,b);}
 bool static_segment_blocked(const Point&a,const Point&b)override{return world().static_segment_blocked(a,b);}
 bool wreck_segment_blocked(const Point&a,const Point&b)override{return world().wreck_segment_blocked(a,b);}
 std::optional<double> nominal_damage(const Point&a,const Point&b)override{return world().nominal_damage(a,b);}
 step::ArcQuery segment(const Point&a,const Point&b,Point&hit)override{return world().segment(a,b,hit);}
};
NativeServices::NativeServices(stock_native_world::Registry& registry,stock_native_world::Scene& scene,geometry::Vehicles& vehicles):
 registry_(registry),scene_(scene),vehicles_(vehicles),queries_(registry,scene,128),adapter_(new WeaponAdapter(*this)){}
NativeServices::~NativeServices()=default;
void NativeServices::install(ServiceEntity row){
 if(row.key.id<=0||row.mode<0||row.mode>1)throw std::invalid_argument("Invalid native service entity");
 const auto id=plannerId(row.key);
 row.target.target.id=row.key.id;row.target.target.kind=row.key.kind==offline_simulation::ActorKind::Human?"human":"bot";
 row.target.samples=row.aim_samples[row.mode];vehicles_.install(row.key,row.geometry[row.mode]);entities_[id]=std::move(row);
 updatePose(entities_.at(id),entities_.at(id).target.pose,entities_.at(id).alive);
 if(runtime_ && entities_.at(id).key.kind==offline_simulation::ActorKind::Bot){const auto actor=runtime_->actors().find(id);
  if(actor!=runtime_->actors().end()){const auto& a=actor->second;
   if(entities_.at(id).mode!=a.mode)modeChanged(id,a.modes[a.mode]);
   const auto& gun=runtime_->weapons().at({offline_simulation::ActorKind::Bot,id}).aim;
   step::AimPose p;p.position=a.motion.position;p.yaw=a.motion.yaw;p.pitch=a.motion.pitch;p.roll=a.motion.roll;
   p.turret_yaw=gun.turret_yaw;p.gun_pitch=gun.gun_pitch;p.aim_yaw=a.weapon.aim_yaw;actorChanged(a,p);}}
 if(weapon_world_)weapon_world_->freezeWrecks();
}
void NativeServices::remove(Key key){const auto id=plannerId(key);entities_.erase(id);rows_.erase(id);vehicles_.remove(key);if(weapon_world_)weapon_world_->freezeWrecks();}
const ServiceEntity* NativeServices::find(Key key)const {const auto it=entities_.find(plannerId(key));return it!=entities_.end()&&it->second.key==key?&it->second:nullptr;}
void NativeServices::recordReady(Key key,bool ready){auto& entity=entities_[plannerId(key)];entity.key=key;entity.record_ready=ready;}
void NativeServices::bind(Runtime& runtime){if(runtime_&&runtime_!=&runtime)throw std::logic_error("Native services already bound");
 runtime_=&runtime;context_.reset(new WeaponWorldContext{scene_,vehicles_,rows_,turrets_,runtime.physics(),server_time_ms_,{}});
 weapon_world_.reset(new NativeWeaponWorld(*context_));for(const auto& row:runtime.actors()){
  const auto& a=row.second;const auto& gun=runtime.weapons().at({offline_simulation::ActorKind::Bot,row.first}).aim;
  step::AimPose p;p.position=a.motion.position;p.yaw=a.motion.yaw;p.pitch=a.motion.pitch;p.roll=a.motion.roll;
  p.turret_yaw=gun.turret_yaw;p.gun_pitch=gun.gun_pitch;p.aim_yaw=a.weapon.aim_yaw;actorChanged(a,p);}}
void NativeServices::updatePose(ServiceEntity& e,const step::AimPose& pose,bool alive){
 e.target.pose=pose;e.target.target.position=pose.position;e.target.alive=alive;e.alive=alive;
 GeometryRow row;row.key=e.key;row.pose=geomPose(pose);row.pose.siege=e.mode==1?2:0;row.team=e.team;row.alive=alive;row.available=e.available;
 row.pose.native_components=true;row.pose.native_hydraulics=true;
 rows_[plannerId(e.key)]=row;
}
void NativeServices::updateHuman(Key key,const step::WeaponTarget& target,int team,bool alive,bool available,double radius){
 if(key.kind!=offline_simulation::ActorKind::Human)throw std::invalid_argument("Expected human identity");
 auto found=entities_.find(plannerId(key));if(found==entities_.end())throw std::invalid_argument("Unknown native human entity");
 auto& e=found->second;e.target=target;e.target.target.kind="human";e.target.target.id=key.id;
 e.target.samples=e.aim_samples[e.mode];e.team=team;e.available=available;e.radius=radius;updatePose(e,target.pose,alive);
}
step::WeaponRuntimeWorld& NativeServices::weaponWorld(){return *adapter_;}
step::ImpactWorld& NativeServices::impactWorld(ID id){
 if(!runtime_)throw std::logic_error("Native services are unbound");const auto actor=runtime_->actors().find(id);
 if(actor==runtime_->actors().end())throw std::invalid_argument("Unknown impact actor");
 const auto& a=actor->second;const auto& gun=runtime_->weapons().at({offline_simulation::ActorKind::Bot,id}).aim;
 step::AimPose p;p.position=a.motion.position;p.yaw=a.motion.yaw;p.pitch=a.motion.pitch;p.roll=a.motion.roll;
 p.turret_yaw=gun.turret_yaw;p.gun_pitch=gun.gun_pitch;p.aim_yaw=a.weapon.aim_yaw;actorChanged(a,p);impact_actor_=id;return *this;
}
std::optional<contacts::Armor> NativeServices::armor(const contacts::Body&a,const contacts::Body&b,const std::array<double,3>& normal){
 if(!weapon_world_)throw std::logic_error("Native services are unbound");const auto plates=stock_native_bot_armor::contact_pair(a,b,{normal[0],normal[1]},*weapon_world_);
 if(!plates)return {};return contacts::Armor{plates->armor,plates->tracks};
}
void NativeServices::actorChanged(const Actor& actor,const step::AimPose& pose){
 auto it=entities_.find(actor.motion.id);if(it==entities_.end()) {ServiceEntity pending;
  pending.key={offline_simulation::ActorKind::Bot,actor.motion.id};pending.mode=actor.mode;pending.available=false;
  pending.target.target.kind="bot";pending.target.target.id=actor.motion.id;
  it=entities_.emplace(actor.motion.id,std::move(pending)).first;}
 auto& e=it->second;e.team=actor.team;e.target.target.velocity={std::sin(pose.yaw)*actor.motion.speed,0.,std::cos(pose.yaw)*actor.motion.speed};
 e.target.target.kind="bot";e.target.target.id=actor.motion.id;e.target.descriptor_revision=actor.weapon.descriptor_revision;
 updatePose(e,pose,actor.motion.alive);
}
void NativeServices::modeChanged(ID id,const Mode&){
 if(!runtime_)throw std::logic_error("Native services are unbound");auto it=entities_.find(id);auto actor=runtime_->actors().find(id);
 if(actor==runtime_->actors().end())return;
 if(it==entities_.end()){ServiceEntity pending;pending.key={offline_simulation::ActorKind::Bot,id};pending.available=false;
  it=entities_.emplace(id,std::move(pending)).first;}
 auto& e=it->second;e.mode=actor->second.mode;e.target.samples=e.aim_samples[e.mode];
 if(e.available)vehicles_.install(e.key,e.geometry[e.mode]);
 e.target.descriptor_revision=actor->second.weapon.descriptor_revision;
 const auto row=rows_.find(id);if(row!=rows_.end())row->second.pose.siege=e.mode==1?2:0;
}
void NativeServices::beginCallback(double now,double server_time_ms){now_=now;server_time_ms_=server_time_ms;
 if(!context_||!weapon_world_)throw std::logic_error("Native services are unbound");context_->server_time_ms=server_time_ms;weapon_world_->freezeWrecks();}
bool NativeServices::prewarmDestructibles(const Actor& actor,double){
 const auto record=entities_.find(actor.motion.id);if(record==entities_.end()||!record->second.record_ready)return false;
 return prewarmChunks(actor.motion.position[0],actor.motion.position[2],actor.motion.yaw);
}
bool NativeServices::prewarmPlayerDestructibles(const Human& human,double){
 return prewarmChunks(human.contacts.body.x,human.contacts.body.z,human.contacts.body.yaw);
}
bool NativeServices::prewarmChunks(double x,double z,double yaw){
 const double px=float(x),pz=float(z);
 const double sine=std::sin(yaw),cosine=std::cos(yaw);
 const auto chunk=[](double x,double z)->std::optional<std::uint32_t>{x=std::floor(double(float(x))*.01)+127.;z=std::floor(double(float(z))*.01)+127.;
  if(!std::isfinite(x)||!std::isfinite(z)||x<0.||z<0.||x>253.||z>253.)return {};
  return (std::uint32_t(x)<<8)|std::uint32_t(z);};
 const auto current=chunk(px,pz);if(!current)return false;std::map<std::uint32_t,std::pair<double,double>> ids;
 ids[*current]={0.,0.};const auto mapped=chunk(px+sine*6.,pz+cosine*6.);if(mapped)ids.emplace(*mapped,std::make_pair(0.,0.));
 for(double x:{-100.,0.,100.})for(double z:{-100.,0.,100.}){const auto id=chunk(px+x,pz+z);if(!id)continue;
  const std::pair<double,double> rank{x*sine+z*cosine,-std::abs(x*cosine-z*sine)};
  const auto found=ids.find(*id);if(found==ids.end())ids.emplace(*id,rank);else if(rank>found->second)found->second=rank;}
 std::vector<std::uint32_t> order;for(const auto& row:ids)order.push_back(row.first);
 std::sort(order.begin(),order.end(),[&](std::uint32_t a,std::uint32_t b){if((a==*current)!=(b==*current))return a==*current;
  const auto& x=ids.at(a);const auto& y=ids.at(b);if(x.first!=y.first)return x.first>y.first;if(x.second!=y.second)return x.second>y.second;return a<b;});
 if(!scene_.warmTreeChunks(order))return false;
 for(auto id:order)if(registry_.skin.isolated_chunks.count(id)||!registry_.ready_tree_chunks.count(id))return false;
 return !order.empty();
}
void NativeServices::scanDestructibles(const motion::State& motion){
 if(!runtime_||!motion.alive||std::abs(motion.speed)<1.)return;
 const auto actor=runtime_->actors().find(motion.id);const auto record=entities_.find(motion.id);
 if(actor==runtime_->actors().end()||record==entities_.end()||!record->second.available)return;
 Point position{double(float(motion.position[0])),double(float(motion.position[1])),double(float(motion.position[2]))};
 const double sine=std::sin(motion.yaw),cosine=std::cos(motion.yaw);
 const auto chunk=[](double x,double z)->std::optional<std::uint32_t>{
  x=std::floor(double(float(x))*.01)+127.;z=std::floor(double(float(z))*.01)+127.;
  if(!std::isfinite(x)||!std::isfinite(z)||x<0.||z<0.||x>253.||z>253.)return {};
  return (std::uint32_t(x)<<8)|std::uint32_t(z);
 };
 std::set<std::uint32_t> ids;const auto add=[&](double x,double z){const auto value=chunk(x,z);if(value)ids.insert(*value);};
 add(position[0],position[2]);const double direction=motion.speed>=0.?6.:-6.;
 add(position[0]+sine*direction,position[2]+cosine*direction);
 if(registry_.catalog_available)for(double x:{-100.,0.,100.})for(double z:{-100.,0.,100.})add(position[0]+x,position[2]+z);
 std::vector<std::uint32_t> chunks(ids.begin(),ids.end());if(chunks.empty()||!scene_.warmTreeChunks(chunks))return;
 const auto& descriptor=actor->second.descriptor;offline_destructible::Pose hull;
 hull.position=position;hull.yaw=motion.yaw;hull.minimum=descriptor.hull_minimum;hull.maximum=descriptor.hull_maximum;
 stock_native_world::ActorInputs inputs;inputs.mass=descriptor.drive.mass;inputs.now=now_;inputs.yaw=motion.yaw;
 inputs.speed=motion.speed;inputs.commit_enabled=true;queries_.actor(inputs);
 queries_.occupiedScan(hull,motion.speed,chunks);
}
bool NativeServices::outgoingLane(const observation::Actor& source,const observation::Actor& target,double now){
 if(!runtime_ || source.key.kind!=offline_simulation::ActorKind::Bot)return false;
 return runtime_->outgoingLane(source.key.id,target,now);
}
bool NativeServices::staticClear(const Point&a,const Point&b){stock_native_world::Hit hit;return scene_.segment(a,b,128,{},hit)==stock_native_world::QueryStatus::Miss;}
std::optional<double> NativeServices::support(double x,double z,double hint){double start=hint+2.;for(int i=0;i<4;++i){stock_native_world::Hit hit;
 if(queries_.ground({x,start,z},{x,-1000.,z},hit)!=stock_native_world::QueryStatus::Found)return {};
 if(hit.normal[1]>.5)return hit.point[1];const double next=hit.point[1]-.05;if(next<=-999.99)return {};start=next;}return {};}
std::optional<double> NativeServices::ground(double x,double z,double hint){return support(x,z,hint);}
double NativeServices::waterDepth(const Point&p){double distance=-1.;const Point a{p[0],p[1]+20.,p[2]},b{p[0],p[1]-5.,p[2]};
 const auto status=scene_.water(a,b,distance);return status==stock_native_world::QueryStatus::Found&&distance>=0.?20.-distance:-1.;}
std::vector<Point> NativeServices::ground_witnesses(){std::vector<Point> result;if(!runtime_)return result;
 const auto it=runtime_->actors().find(impact_actor_);if(it==runtime_->actors().end())return result;const auto& a=it->second;const auto& p=a.motion.position;
 const double width=a.descriptor.collision_shape[0],length=a.descriptor.collision_shape[1],yaw=a.motion.yaw,c=std::cos(yaw),s=std::sin(yaw);
 for(const auto& offset:std::array<std::array<double,2>,5>{{{0.,0.},{-.8*width,-.8*length},{-.8*width,.8*length},{.8*width,-.8*length},{.8*width,.8*length}}}){
  const double x=p[0]+c*offset[0]+s*offset[1],z=p[2]-s*offset[0]+c*offset[1];const auto y=support(x,z,p[1]);
  if(y&&std::abs(*y-p[1])<=.25)result.push_back({x,*y,z});}return result;
}
std::vector<step::ImpactHit> NativeServices::material_segment(const Point&a,const Point&b){std::vector<step::ImpactHit> result;
 const auto row=rows_.find(impact_actor_);if(row==rows_.end()||!row->second.available)return result;
 auto frozen=row->second.pose;frozen.native_components=false;frozen.native_hydraulics=false;
 for(const auto& hit:vehicles_.intersections(row->second.key,frozen,a,b))if(hit.material&&!hit.material->device.empty())result.push_back({hit.distance,hit.material->device});return result;
}
std::vector<step::ImpactHit> NativeServices::interior_segment(const Point&a,const Point&b){
 const auto row=rows_.find(impact_actor_);const auto record=entities_.find(impact_actor_);
 if(row==rows_.end()||record==entities_.end()||!row->second.available)return {};
 const auto vehicle=vehicles_.find(row->second.key);if(!vehicle||!vehicle->ready)return {};
 auto frozen=row->second.pose;frozen.native_components=false;frozen.native_hydraulics=false;
 const auto f=geometry::frames(*vehicle,frozen);std::array<step::InternalSegment,4> segments{};
 for(int i=0;i<4;++i){if(i>=2&&vehicle->detached)continue;const auto& root=i==0?f.chassis_inverse:f.body_inverse;
  segments[i]={plain(f.component_inverse[i].applyPoint(root.applyPoint(native(a)))),plain(f.component_inverse[i].applyPoint(root.applyPoint(native(b)))),true};}
 const double length=std::sqrt(std::pow(a[0]-b[0],2)+std::pow(a[1]-b[1],2)+std::pow(a[2]-b[2],2));
 return step::internal_hits(record->second.interior[record->second.mode],segments,length);
}
}
