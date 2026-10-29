#include "stock_native_player_environment.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <exception>
namespace stock_native_player_environment {
namespace s=wot::offline_bot_step;
namespace {
Point transform(Point p,const Pose& q){const auto sr=std::sin(q.roll),cr=std::cos(q.roll),sp=std::sin(q.pitch),cp=std::cos(q.pitch),sy=std::sin(q.yaw),cy=std::cos(q.yaw);
 const double x=cr*p[0]-sr*p[1],y=sr*p[0]+cr*p[1],z=p[2];const double yp=cp*y-sp*z,zp=sp*y+cp*z;
 return {q.position[0]+cy*x+sy*zp,q.position[1]+yp,q.position[2]-sy*x+cy*zp};}
bool finite(double v){return std::isfinite(v);}
}
std::optional<std::pair<Point,double>> hullSample(const Metadata& d,const Pose& pose){
 for(int i=0;i<3;++i)if(!finite(d.minimum[i])||!finite(d.maximum[i])||!finite(d.mount[i])||!finite(pose.position[i])||d.maximum[i]<=d.minimum[i])return {};
 if(!finite(pose.yaw)||!finite(pose.pitch)||!finite(pose.roll))return {};Point center{};double bottom=std::numeric_limits<double>::infinity(),top=-bottom;
 for(int mask=0;mask<8;++mask){Point p;for(int i=0;i<3;++i)p[i]=((mask>>i)&1?d.maximum[i]:d.minimum[i])+d.mount[i];p=transform(p,pose);for(int i=0;i<3;++i)center[i]+=p[i]/8.;bottom=std::min(bottom,p[1]);top=std::max(top,p[1]);}
 if(!finite(top-bottom)||top<=bottom)return {};center[1]=bottom;return std::make_pair(center,top-bottom);
}
std::optional<Proposal> drowningProposal(const Critical& before,const Metadata& meta){
 auto after=before;for(auto& item:after.devices)item.second.critical=false;
 for(const char* name:{"engineHealth","ammoBayHealth","fuelTankHealth","radioHealth","gunHealth","turretRotatorHealth","surveyingDeviceHealth","leftTrackHealth","rightTrackHealth"}){
  auto& device=after.devices[name];const auto max=meta.maxima.find(name);if(max!=meta.maxima.end())device.maximum=max->second;device.hp=0.;device.destroyed=true;device.critical=false;}
 after.crew_ko.insert(meta.crew.begin(),meta.crew.end());auto events=s::critical_events(before,after,"drowning");
 bool changed=!events.empty()||before.crew_ko!=after.crew_ko;
 if(!changed)for(const auto& item:after.devices){auto old=before.devices.find(item.first);if(old==before.devices.end()||old->second.hp!=item.second.hp||old->second.destroyed!=item.second.destroyed||old->second.critical!=item.second.critical){changed=true;break;}}
 if(!changed)return {};return Proposal{std::move(after),std::move(events)};
}
bool Owner::willSample(double dt,bool authority)const{return authority&&finite(dt)&&dt>0.&&elapsed_+dt>=.3;}
std::optional<Batch> Owner::advance(double dt,bool authority,const std::vector<Player>& players,World& world){
 if(!authority||!finite(dt)||dt<=0.)return {};elapsed_+=dt;if(elapsed_<.3)return {};elapsed_=std::fmod(elapsed_,.3);
 Batch result;result.sequence=sequence_+1;std::vector<const Player*> ordered;for(const auto& player:players)ordered.push_back(&player);
 std::sort(ordered.begin(),ordered.end(),[](const Player*a,const Player*b){return std::to_string(a->id)<std::to_string(b->id);});
 for(const Player* player:ordered){if(player->id<=0)continue;Pose pose=player->pose;
  try{pose=world.bodyPose(*player).value_or(pose);}catch(const std::exception&){}
  const auto sample=hullSample(player->descriptor,pose);if(!sample)continue;
  double depth=-1.;try{depth=world.waterDepth(sample->first);}catch(const std::exception&){}if(!finite(depth))continue;const int level=depth>sample->second?2:depth>sample->second*.5?1:0;
  Observation observation;observation.id=player->id;observation.input_sequence=std::max<std::int64_t>(0,player->input_sequence);observation.level=level;
  if(level==2)observation.drowning=drowningProposal(player->critical,player->descriptor);result.observations.push_back(std::move(observation));}
 return result;
}
}
