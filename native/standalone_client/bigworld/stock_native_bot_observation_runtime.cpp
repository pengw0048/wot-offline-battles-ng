#include "stock_native_bot_observation_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stock_native_bot_observation {
namespace {
constexpr double pi=3.14159265358979323846;
std::string kind(Key key){return key.kind==offline_simulation::ActorKind::Human?"human":"bot";}
Key key(const std::string&name,ID id){return {name=="human"?offline_simulation::ActorKind::Human:offline_simulation::ActorKind::Bot,id};}
TeamKey teamKey(int team,Key target){return {team,kind(target),target.id};}
Key targetKey(const TeamKey&t){return key(std::get<1>(t),std::get<2>(t));}
Key targetKey(const LaneKey&t){return key(std::get<1>(t),std::get<2>(t));}
Key sourceKey(ID id){return {offline_simulation::ActorKind::Bot,id};}
double distance(const Point&a,const Point&b){return std::hypot(a[0]-b[0],a[2]-b[2]);}
double wrap(double angle){while(angle>pi)angle-=2*pi;while(angle<-pi)angle+=2*pi;return angle;}
std::uint64_t positive(ID value){return value<0?std::uint64_t(-(value+1))+1:std::uint64_t(value);}
double rounded(double value){return std::round(value*1.e6)/1.e6;}
bool modes(const std::string&s,std::initializer_list<const char*>values){for(auto*v:values)if(s==v)return true;return false;}
double range(const Actor&a){return a.target.class_tag=="SPG"?2500.:585.;}
bool wireOrder(Key a,Key b){return std::make_pair(kind(a),a.id)<std::make_pair(kind(b),b.id);}
perception::Pose frozenPose(const Actor&a){perception::Pose out;out.position=a.pose.position;out.values={a.pose.position[0],a.pose.position[1],a.pose.position[2],a.pose.yaw,a.pose.pitch,a.pose.roll,a.pose.aim_yaw,a.pose.turret_yaw,a.pose.gun_pitch,a.speed,0.,0.,0.};out.mask=(1u<<10)-1;return out;}
void overlayPose(Actor&a,const perception::Pose&p){a.pose.position=p.position;a.target.position=p.position;
    double* fields[]={&a.pose.yaw,&a.pose.pitch,&a.pose.roll,&a.pose.aim_yaw,&a.pose.turret_yaw,&a.pose.gun_pitch,&a.speed};for(unsigned i=0;i<7;++i)if(p.mask&(1u<<(i+3)))*fields[i]=p.values[i+3];a.target.speed=a.speed;}
struct Exhausted {};
struct Budget {
    World&world;int remaining=40;
    std::map<Point,std::optional<double>> heights;
    void spend(){if(remaining<=0)throw Exhausted{};--remaining;}
    bool clear(const Point&a,const Point&b){spend();return world.staticClear(a,b);}
    double water(const Point&p){spend();return world.waterDepth(p);}
    std::optional<double> ground(double x,double z,double hint){const Point key{x,z,hint};auto found=heights.find(key);if(found!=heights.end())return found->second;spend();auto value=world.ground(x,z,hint);heights[key]=value;return value;}
};
std::optional<weapon::AimPose> surface(const Actor&source,const Point&p,const Descriptor&d,Budget&budget)
{
    const double length=std::max(3.,std::abs(d.hull_minimum[2])+std::abs(d.hull_maximum[2])),width=std::max(2.,std::abs(d.hull_minimum[0])+std::abs(d.hull_maximum[0]));
    const double sy=std::sin(source.pose.yaw),cy=std::cos(source.pose.yaw);
    const auto front=budget.ground(p[0]+sy*length*.5,p[2]+cy*length*.5,p[1]);
    const auto rear=budget.ground(p[0]-sy*length*.5,p[2]-cy*length*.5,p[1]);
    const auto right=budget.ground(p[0]+cy*width*.5,p[2]-sy*width*.5,p[1]);
    const auto left=budget.ground(p[0]-cy*width*.5,p[2]+sy*width*.5,p[1]);
    const auto center=budget.ground(p[0],p[2],p[1]);if(!front||!rear||!right||!left||!center)return {};
    const double long_mid=(*front+*rear)*.5,side_mid=(*right+*left)*.5;
    if(std::max({std::abs(long_mid-side_mid),std::abs(*center-long_mid),std::abs(*center-side_mid)})>.35)return {};
    const double forward=(*front-*rear)/length,lateral=(*right-*left)/width;
    if(std::atan(std::hypot(forward,lateral))*180./pi>24.)return {};
    auto out=source.pose;out.position={p[0],*center,p[2]};out.pitch=-std::atan2(*front-*rear,length);out.roll=std::atan2(lateral,std::hypot(1.,forward));return out;
}
std::optional<double> exposure(const std::vector<weapon::AimSample>&samples,const weapon::AimPose&pose,const std::vector<Point>&origins,Budget&budget)
{
    if(samples.empty()||origins.empty())return {};std::vector<Point>points;for(const auto&s:samples)points.push_back(weapon::vehicle_aim_point(s,pose));double worst=0.;
    for(const auto&origin:origins){double visible=0.;for(const auto&p:points)if(budget.clear(origin,p))visible+=1.;worst=std::max(worst,visible/points.size());}return worst;
}
bool gunLane(const Actor&source,const weapon::AimPose&pose,const Descriptor&descriptor,const Actor&target,const Descriptor&other,Budget&budget)
{
    if(!descriptor.mount||!other.valid)return false;const Point origin=weapon::shot_origin(pose,*descriptor.mount);
    for(std::size_t i=0;i<std::min<std::size_t>(2,other.samples.size());++i){const auto point=weapon::vehicle_aim_point(other.samples[i],target.pose);weapon::AimIntent intent;intent.world_yaw=std::atan2(point[0]-origin[0],point[2]-origin[2]);intent.world_pitch=-std::atan2(point[1]-origin[1],std::hypot(point[0]-origin[0],point[2]-origin[2]));const auto local=weapon::local_aim_intent(pose,intent);
        if(!(descriptor.valid_pitch||source.pitch_locked))continue;const auto limits=source.pitch_locked?std::array<double,2>{source.static_pitch,source.static_pitch}:offline_simulation::weapons::pitch_limits(local.raw_yaw,descriptor.aim);
        if(local.raw_pitch<limits[0]||local.raw_pitch>limits[1]||local.raw_yaw<descriptor.minimum_yaw||local.raw_yaw>descriptor.maximum_yaw)continue;
        auto aimed=pose;aimed.turret_yaw=local.raw_yaw;aimed.gun_pitch=local.raw_pitch;if(budget.clear(weapon::shot_origin(aimed,*descriptor.mount),point))return true;
    }return false;
}
}
Runtime::Runtime(weapon::CombatSharedRuntime&shared,perception::Perception&perception,decision::Decisions&decisions,nav::Grid&grid,World&world):shared_(shared),perception_(perception),decisions_(decisions),grid_(grid),world_(world){}
void Runtime::replaceActors(std::vector<Actor>actors){actors_.clear();for(auto&actor:actors)if(!actors_.emplace(actor.key,std::move(actor)).second)throw std::invalid_argument("Duplicate tactical actor");}
double Runtime::phase(const LaneKey&value){const auto bucket=(positive(std::get<0>(value))%29*31+positive(std::get<2>(value))%29*17+(std::get<1>(value)=="human"?11:0))%29;return double(bucket)/29.;}
bool Runtime::current(const CoverJob&job)const
{
    const auto source=actors_.find(sourceKey(job.id));if(source==actors_.end()||!source->second.target.alive||source->second.target.team!=job.source.target.team)return false;
    const auto remembered=perception_.remembered().find({job.source.target.team,job.target.key});return remembered!=perception_.remembered().end()&&remembered->second.until>now_;
}
Schedule Runtime::begin(double now,bool refresh,bool publish,bool fixed,double control)
{
    now_=now;refresh_=refresh;publish_=publish;fixed_=fixed;control_seconds_=control;incoming_budget_=refresh?1:0;team_visibility_.clear();entries_.clear();priorities_.clear();cover_jobs_.clear();
    Schedule s;s.collect_observation=publish&&refresh&&now>=next_observation_;s.cycle_time=next_lane_;s.shot_lane_refresh_due=now>=next_lane_;
    s.cover_reserved=refresh&&!cover_queue_.empty()&&now+1.e-9>=cover_queue_.front().ready_at&&current(cover_queue_.front());
    s.refresh_shot_lanes=refresh&&!s.cover_reserved&&(s.shot_lane_refresh_due||(next_lane_>0.&&now+1.+1.e-9>=next_lane_));
    s.collect_cover_jobs=refresh&&publish&&cover_queue_.empty()&&now>=next_cover_;schedule_=s;return s;
}
void Runtime::observeHumans()
{
    if(!schedule_.collect_observation&&!schedule_.refresh_shot_lanes)return;
    for(const auto&row:perception_.observations()){const TeamKey k=teamKey(row.first.team,row.first.target);const auto actor=actors_.find(row.first.target);if(actor==actors_.end())continue;
        auto&entry=entries_[k];entry.target=actor->second;overlayPose(entry.target,row.second.pose);entry.visible=row.second.visible;entry.players=row.second.human_observers;entry.bots=row.second.bot_observers;
        if(row.second.visible)team_visibility_[k]=true;}
}
void Runtime::observeActor(ID id,const std::vector<decision::Target>&contacts,const stock_native_bot_planner::Command&command)
{
    const auto&source=actors_.at(sourceKey(id));const decision::Target*close=nullptr;double closest=std::numeric_limits<double>::infinity();
    for(const auto&cached:contacts){const Key target=key(cached.identity.kind,cached.identity.id);const TeamKey k=teamKey(source.target.team,target);
        if(cached.fresh_visible)team_visibility_[k]=true;
        if(schedule_.refresh_shot_lanes&&cached.direct_visible&&cached.alive&&cached.team!=source.target.team){const double d=distance(source.pose.position,cached.position);if(d<=50.&&d<closest){close=&cached;closest=d;}}
        if(!schedule_.collect_observation)continue;Actor observed;observed.key=target;observed.target=cached;observed.pose.position=cached.position;if(cached.observed_pose)overlayPose(observed,*cached.observed_pose);
        const auto live=actors_.find(target);if(live!=actors_.end()){observed.target.alive=live->second.target.alive;observed.target.health=live->second.target.health;observed.target.max_health=live->second.target.max_health;observed.target.team=live->second.target.team;if(cached.fresh_visible){observed.pose=live->second.pose;observed.speed=live->second.speed;observed.target.position=observed.pose.position;}}
        if(cached.fresh_visible)perception_.rememberPose({source.target.team,target},frozenPose(observed));auto&entry=entries_[k];entry.target=observed;entry.visible=entry.visible||cached.visible;if(cached.direct_visible)entry.bots.insert(id);
    }
    if(close)priorities_[{id,close->identity.kind,close->identity.id}]=-1;
    if(command.target.present){for(const auto&contact:contacts)if(contact.id==command.target.value){priorities_[{id,contact.identity.kind,contact.identity.id}]=command.fire_allowed?0:1;break;}}
}
void Runtime::queueCover(ID id,const decision::Target*target,const stock_native_bot_planner::Command&command)
{
    if(!schedule_.collect_cover_jobs||!target||!target->visible)return;
    if(!modes(command.mode,{"take_cover","cover_hold","cover_peek","cover_return","under_fire_withdraw","low_health_retreat","crossfire_withdraw"})&&!(command.fire_allowed&&modes(command.mode,{"engage","advance_contact","jiggle_forward","jiggle_back"})))return;
    const auto source=actors_.find(sourceKey(id));if(source==actors_.end())return;Actor observed;observed.key=key(target->identity.kind,target->identity.id);observed.target=*target;observed.pose.position=target->position;if(target->observed_pose)overlayPose(observed,*target->observed_pose);
    const auto live=actors_.find(observed.key);if(target->fresh_visible&&live!=actors_.end())observed=live->second;
    CoverJob job;job.id=id;job.source=source->second;job.target=observed;job.route=command.move;cover_jobs_.push_back(std::move(job));
}
void Runtime::enqueue(const LaneKey&value,double cycle)
{auto deadline=deadlines_.find(value);if((deadline!=deadlines_.end()&&deadline->second==cycle)||work_set_.count(value))return;work_.push_back(value);work_set_.insert(value);}
void Runtime::prepareLanes(double cycle)
{
    if(!work_cycle_||*work_cycle_!=cycle){work_.clear();work_set_.clear();enqueued_targets_.clear();source_signature_.clear();selected_cursors_.clear();work_cursor_=0;work_cycle_=cycle;}
    std::map<int,std::vector<ID>>sources{{1,{}},{2,{}}};for(const auto&row:actors_)if(row.first.kind==offline_simulation::ActorKind::Bot&&row.second.target.alive&&sources.count(row.second.target.team))sources[row.second.target.team].push_back(row.first.id);
    if(sources!=source_signature_){source_signature_=sources;for(const auto&t:enqueued_targets_)for(ID id:sources[std::get<0>(t)])enqueue({id,std::get<1>(t),std::get<2>(t)},cycle);}
    for(const auto&row:team_visibility_)if(row.second&&!enqueued_targets_.count(row.first)&&sources.count(std::get<0>(row.first))){enqueued_targets_.insert(row.first);for(ID id:sources[std::get<0>(row.first)])enqueue({id,std::get<1>(row.first),std::get<2>(row.first)},cycle);}
}
std::optional<bool> Runtime::incoming(const Actor&source,const Actor&target)
{
    const auto own=descriptors_.find(source.key),enemy=descriptors_.find(target.key);if(own==descriptors_.end()||enemy==descriptors_.end()||!own->second.valid||!enemy->second.valid||!enemy->second.mount||own->second.samples.empty())return {};
    const auto origin=weapon::shot_origin(target.pose,*enemy->second.mount);for(std::size_t i=0;i<std::min<std::size_t>(2,own->second.samples.size());++i)if(world_.staticClear(origin,weapon::vehicle_aim_point(own->second.samples[i],source.pose)))return true;return false;
}
std::size_t Runtime::serviceLanes()
{
    const double cycle=schedule_.cycle_time,window=cycle-1.;prepareLanes(cycle);if(work_set_.empty())return 0;
    const int maximum=fixed_?16:64;int materialized=0,budget=maximum;std::set<LaneKey>attempted;
    auto valid=[&](const LaneKey&k){auto s=actors_.find(sourceKey(std::get<0>(k))),t=actors_.find(targetKey(k));return s!=actors_.end()&&t!=actors_.end()&&s->second.target.alive&&t->second.target.alive&&s->second.target.team!=t->second.target.team;};
    auto attempt=[&](const LaneKey&k){if(attempted.count(k)||!work_set_.count(k))return;attempted.insert(k);auto deadline=deadlines_.find(k);if((deadline!=deadlines_.end()&&deadline->second==cycle)||!valid(k)){work_set_.erase(k);return;}
        const auto&source=actors_.at(sourceKey(std::get<0>(k)));const auto&target=actors_.at(targetKey(k));if(!team_visibility_[teamKey(source.target.team,target.key)])return;
        auto cached=shared_.lanes.find(k);if(cached!=shared_.lanes.end()&&cached->second.at>window+1.e-9){deadlines_[k]=cycle;work_set_.erase(k);return;}
        const auto priority=priorities_.find(k);const bool urgent=priority!=priorities_.end()&&priority->second<0;if(now_+1.e-9<window+phase(k)&&!urgent)return;
        const double d=distance(source.pose.position,target.pose.position);const bool incoming_due=incoming_budget_>0;
        if(d>range(source)&&!incoming_due){shared_.lanes[k]={now_,false};deadlines_[k]=cycle;work_set_.erase(k);return;}
        if(materialized>=maximum)return;if(incoming_due){--incoming_budget_;try{const auto value=incoming(source,target);if(value)incoming_[k]={now_,*value};}catch(const std::exception&){}}
        ++materialized;if(d>range(source))shared_.lanes[k]={now_,false};else{if(budget<=0)return;--budget;shared_.lanes[k]={now_,world_.outgoingLane(source,target,now_)};}
        deadlines_[k]=cycle;work_set_.erase(k);
        if(shared_.lanes.size()>1024){std::vector<std::pair<double,LaneKey>>old;for(const auto&row:shared_.lanes)old.push_back({row.second.at,row.first});std::stable_sort(old.begin(),old.end());for(std::size_t i=0;i<256;++i){shared_.lanes.erase(old[i].second);deadlines_.erase(old[i].second);}}
    };
    std::map<int,std::vector<LaneKey>>selected;for(const auto&row:priorities_)if(work_set_.count(row.first))selected[row.second].push_back(row.first);
    for(const auto&group:selected){auto values=group.second;std::size_t first=0;const auto cursor=selected_cursors_.find(group.first);if(cursor!=selected_cursors_.end()){auto found=std::upper_bound(values.begin(),values.end(),cursor->second);if(found!=values.end())first=std::size_t(found-values.begin());}
        for(std::size_t i=0;i<values.size()&&materialized<maximum;++i){const auto&k=values[(first+i)%values.size()];attempt(k);selected_cursors_[group.first]=k;}}
    if(!work_.empty()){const auto start=work_cursor_%work_.size();std::size_t checked=0;while(checked<work_.size()&&materialized<maximum){attempt(work_[(start+checked)%work_.size()]);++checked;}work_cursor_=(start+checked)%work_.size();}
    if(work_.size()>std::max<std::size_t>(64,work_set_.size()*2)){std::deque<LaneKey>active;for(const auto&k:work_)if(work_set_.count(k))active.push_back(k);work_=std::move(active);work_cursor_=work_.empty()?0:work_cursor_%work_.size();}
    std::size_t pending=0;for(const auto&k:work_set_)if(valid(k)){const auto&source=actors_.at(sourceKey(std::get<0>(k)));if(team_visibility_[teamKey(source.target.team,targetKey(k))])++pending;}return pending;
}
std::vector<CoverCandidate> Runtime::cover(const CoverJob&job)
{
    const Actor&source=job.source;const auto own=descriptors_.find(source.key),other=descriptors_.find(job.target.key);if(own==descriptors_.end()||other==descriptors_.end()||!own->second.valid)return {};const auto&descriptor=own->second;
    std::vector<weapon::AimSample>samples;for(std::size_t i=0;i<std::min<std::size_t>(2,descriptor.samples.size());++i)samples.push_back(descriptor.samples[i]);unsigned sides=0;for(std::size_t i=2;i<descriptor.samples.size()&&sides<2;++i)if(!descriptor.samples[i].turret){samples.push_back(descriptor.samples[i]);++sides;}if(samples.empty())return {};
    const double width=descriptor.hull_maximum[0]-descriptor.hull_minimum[0],length=descriptor.hull_maximum[2]-descriptor.hull_minimum[2];if(std::min(width,length)<=0.)return {};
    std::vector<Actor>threats{job.target};const auto&current=source.pose.position;const double main=std::atan2(job.target.pose.position[0]-current[0],job.target.pose.position[2]-current[2]);std::vector<std::tuple<double,std::string,ID,Actor>>ranked;
    for(const auto&row:perception_.remembered())if(row.first.team==source.target.team&&row.second.until>now_&&row.first.target!=job.target.key){Actor value;value.key=row.first.target;overlayPose(value,row.second.pose);const auto&p=value.pose.position;const double bearing=std::atan2(p[0]-current[0],p[2]-current[2]);ranked.push_back({-(1.+std::abs(wrap(bearing-main)))/std::max(1.,distance(p,current)),kind(value.key),value.key.id,value});}
    std::sort(ranked.begin(),ranked.end(),[](const auto&a,const auto&b){return std::tie(std::get<0>(a),std::get<1>(a),std::get<2>(a))<std::tie(std::get<0>(b),std::get<1>(b),std::get<2>(b));});if(!ranked.empty())threats.push_back(std::get<3>(ranked.front()));
    std::vector<Point>origins;for(const auto&threat:threats){auto d=descriptors_.find(threat.key);if(d==descriptors_.end()||!d->second.valid||!d->second.mount)return {};origins.push_back(weapon::shot_origin(threat.pose,*d->second.mount));}
    const auto phase=cover_phases_[job.id]++;std::vector<std::array<double,2>>offsets{{0.,0.}};const double radius=std::max(width,length)*2.;for(double scale:{1.,2.})for(unsigned i=0;i<8;++i){const double angle=i*pi/4.;offsets.push_back({std::cos(angle)*radius*scale,std::sin(angle)*radius*scale});}
    const double away_x=current[0]-job.target.pose.position[0],away_z=current[2]-job.target.pose.position[2],d=std::hypot(away_x,away_z);if(d<2.)return {};const Point away{away_x/d,0.,away_z/d},right{away[2],0.,-away[0]};
    const double route_x=job.route[0]-current[0],route_z=job.route[2]-current[2],route_length=std::hypot(route_x,route_z);std::vector<CoverCandidate>out;Budget budget{world_,40,{}};
    for(std::size_t i=0;i<4;++i){const auto&offset=offsets[(phase+i)%offsets.size()];try{Point point{current[0]+away[0]*offset[0]+right[0]*offset[1],current[1],current[2]+away[2]*offset[0]+right[2]*offset[1]};auto ground=budget.ground(point[0],point[2],point[1]);if(!ground)continue;point[1]=*ground;const double water=budget.water(point);if(water>.90||!grid_.segmentClear(current,point))continue;
        const auto pose=surface(source,point,descriptor,budget);if(!pose)continue;const auto hidden=exposure(samples,*pose,origins,budget);if(!hidden||*hidden>.5)continue;const int side=phase%2?-1:1;Point peek{point[0]+right[0]*width*1.5*side,point[1],point[2]+right[2]*width*1.5*side};const auto peek_ground=budget.ground(peek[0],peek[2],peek[1]);if(!peek_ground)continue;peek[1]=*peek_ground;
        if(budget.water(peek)>.90||!grid_.segmentClear(point,peek)||!grid_.segmentClear(peek,point))continue;const auto peek_pose=surface(source,peek,descriptor,budget);if(!peek_pose||!gunLane(source,*peek_pose,descriptor,job.target,other->second,budget))continue;
        const auto exposed=exposure(samples,*peek_pose,origins,budget);auto mid_pose=*pose;for(unsigned a=0;a<3;++a)mid_pose.position[a]=(current[a]+point[a])*.5;const std::vector<weapon::AimSample>centres(samples.begin(),samples.begin()+std::min<std::size_t>(2,samples.size()));const auto approach=exposure(centres,mid_pose,origins,budget);if(!exposed||!approach)continue;
        CoverCandidate value;value.id=std::to_string(job.id)+":"+std::to_string(std::int64_t(std::round(point[0]/4.)))+":"+std::to_string(std::int64_t(std::round(point[2]/4.)))+":"+std::to_string(side);value.position=point;value.peek_position=peek;const double mx=point[0]-current[0],mz=point[2]-current[2];value.travel_distance=std::hypot(mx,mz);value.route_alignment=.5;if(value.travel_distance>.1&&route_length>.1)value.route_alignment=std::max(0.,std::min(1.,.5+.5*(mx*route_x+mz*route_z)/(value.travel_distance*route_length)));
        value.enemy_occlusion=1.-*hidden;value.exposure=(*hidden+*exposed+*approach)/3.;value.slope=std::max(std::abs(pose->pitch),std::abs(pose->roll))*180./pi;value.water=water;unsigned nearby=0;for(const auto&ally:job.allies){const double dist=distance(point,ally);if(dist>.5&&dist<13.)++nearby;}value.ally_congestion=std::min(1.,nearby/3.);out.push_back(value);
    }catch(const Exhausted&){break;}}
    return out;
}
Observation Runtime::pack()
{
    Observation out;const double age=1.+control_seconds_;std::map<TeamKey,std::set<ID>>incoming;
    for(auto i=incoming_.begin();i!=incoming_.end();){const auto source=actors_.find(sourceKey(std::get<0>(i->first)));if(source==actors_.end()||!source->second.target.alive||now_-i->second.at>age){i=incoming_.erase(i);continue;}if(i->second.clear)incoming[teamKey(source->second.target.team,targetKey(i->first))].insert(source->first.id);++i;}
    for(const auto&lane:shared_.lanes){if(std::get<1>(lane.first)!="bot"||!lane.second.clear||now_-lane.second.at>age)continue;const auto enemy=actors_.find(sourceKey(std::get<0>(lane.first))),own=actors_.find(sourceKey(std::get<2>(lane.first)));if(enemy==actors_.end()||own==actors_.end()||!enemy->second.target.alive||!own->second.target.alive||enemy->second.target.team==own->second.target.team||enemy->second.target.class_tag=="SPG")continue;const auto k=teamKey(own->second.target.team,enemy->first);if(entries_.count(k))incoming[k].insert(own->first.id);}
    auto radio=perception_.control().radio_actors();std::sort(radio.begin(),radio.end(),[](const auto&a,const auto&b){return wireOrder(a.key,b.key);});
    for(auto&row:entries_){const auto k=row.first;auto&entry=row.second;const auto remembered=perception_.remembered().find({std::get<0>(k),targetKey(k)});Contact result;result.observing_team=std::get<0>(k);result.target=targetKey(k);result.target_team=entry.target.target.team;
        result.time_left=remembered==perception_.remembered().end()?0.:std::max(0.,std::min(12.,remembered->second.until-now_));result.visible=result.time_left>0.;if(result.visible)overlayPose(entry.target,remembered->second.pose);result.fresh=result.visible&&(!entry.players.empty()||!entry.bots.empty());if(!result.fresh)entry.shootable.clear();
        if(result.visible){result.visible_by_player_ids.assign(entry.players.begin(),entry.players.end());result.visible_by_bot_ids.assign(entry.bots.begin(),entry.bots.end());for(const auto&actor:radio)if(actor.team==result.observing_team){const auto contact=perception_.control().recipient_contact(actor.key,result.target,now_);if(contact.remaining>0.)result.radio_recipients.push_back({actor.key,rounded(std::min(result.time_left,contact.remaining))});}}
        for(ID id:entry.shootable){const auto actor=decisions_.actors.find(id);const decision::Identity identity{kind(result.target),result.target.id};if(actor!=decisions_.actors.end()){auto rejected=actor->second.gun_rejected.find(identity);if(rejected!=actor->second.gun_rejected.end()&&now_<rejected->second)continue;}if(perception_.control().recipient_contact(sourceKey(id),result.target,now_).fresh)result.shootable_by_bot_ids.push_back(id);}
        if(result.fresh){const auto values=incoming.find(k);if(values!=incoming.end())result.threatened_bot_ids.assign(values->second.begin(),values->second.end());}result.time_left=rounded(result.time_left);result.position=entry.target.pose.position;result.health=std::max(0,int(entry.target.target.health));result.max_health=std::max(1,int(entry.target.target.max_health));result.class_tag=entry.target.target.class_tag;result.armor=std::max(0.,entry.target.target.armor);out.contacts.push_back(std::move(result));
    }
    for(const auto&actor:radio)if(actor.key.kind==offline_simulation::ActorKind::Human){RadioLink link;link.actor=actor.key;for(const auto&ally:radio)if(ally.key!=actor.key&&perception_.control().radio_connected(actor.key,ally.key))link.allies.push_back(ally.key);out.radio_links.push_back(std::move(link));auto live=actors_.find(actor.key);if(live!=actors_.end()&&live->second.target.alive)out.player_vision_ranges.push_back({actor.key.id,actor.view});}
    out.affordances=std::move(cover_results_);cover_results_.clear();return out;
}
std::optional<Observation> Runtime::finish()
{
    if(schedule_.refresh_shot_lanes){const auto pending=serviceLanes();if(schedule_.shot_lane_refresh_due&&pending==0)next_lane_=now_+1.;}
    if(schedule_.collect_observation)for(const auto&lane:shared_.lanes){auto source=actors_.find(sourceKey(std::get<0>(lane.first)));if(source==actors_.end()||!source->second.target.alive||!lane.second.clear||now_-lane.second.at>1.+control_seconds_+1.e-9)continue;const auto k=teamKey(source->second.target.team,targetKey(lane.first));auto found=entries_.find(k);if(team_visibility_[k]&&found!=entries_.end())found->second.shootable.insert(source->first.id);}
    std::optional<Observation> output;if(schedule_.collect_observation){output=pack();next_observation_=now_+.4;}
    if(schedule_.collect_cover_jobs){next_cover_=now_+1.;std::sort(cover_jobs_.begin(),cover_jobs_.end(),[](const auto&a,const auto&b){return a.id<b.id;});if(!cover_jobs_.empty()){const auto start=cover_cursor_%cover_jobs_.size(),count=std::min<std::size_t>(3,cover_jobs_.size());cover_cursor_=(start+count)%cover_jobs_.size();for(std::size_t i=0;i<count;++i){auto job=cover_jobs_[(start+i)%cover_jobs_.size()];job.ready_at=now_+.5*double(i)/count;for(const auto&row:actors_)if(row.first.kind==offline_simulation::ActorKind::Bot&&row.second.target.alive&&row.second.target.team==job.source.target.team)job.allies.push_back(row.second.pose.position);cover_queue_.push_back(std::move(job));}}}
    if(refresh_&&!cover_queue_.empty()&&now_+1.e-9>=cover_queue_.front().ready_at){auto job=cover_queue_.front();cover_queue_.erase(cover_queue_.begin());if(current(job)){job.source=actors_.at(sourceKey(job.id));overlayPose(job.target,perception_.remembered().at({job.source.target.team,job.target.key}).pose);try{auto candidates=cover(job);if(!candidates.empty())cover_results_.push_back({job.id,job.target.key.id,kind(job.target.key),std::move(candidates)});}catch(const std::exception&){}}}
    return output;
}
}
