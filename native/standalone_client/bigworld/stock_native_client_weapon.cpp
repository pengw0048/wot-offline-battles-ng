#include "stock_native_client_weapon.hpp"
#include <algorithm>
#include <cmath>

namespace stock_native_client_weapon {
bool Gun::select(int index,bool instant){
    if(ammunition.empty())return false;
    index=std::max(0,std::min(index,int(ammunition.size())-1));pending.reset();
    if(index==shot)return false;
    const double remaining=std::max(0.,reload_time),duration=std::max(0.,reload_duration);
    shot=index;load_started=false;reload_duration=config.reload;
    if(instant&&ammunition[index]>0){
        if(remaining<=0.){clip=std::min(config.clip_size,ammunition[index]);reload_time=0.;}
        else {clip=0;reload_time=reload_duration*(duration>0.?std::max(0.,std::min(1.,remaining/duration)):1.);}
    }else {clip=0;reload_time=config.reload;}
    return true;
}
bool Gun::request(int index){
    if(ammunition.empty())return false;
    index=std::max(0,std::min(index,int(ammunition.size())-1));
    if(ammunition[index]<=0)return false;
    if(shot<int(ammunition.size())&&ammunition[shot]<=0)return select(index);
    pending=index==shot?std::nullopt:std::optional<int>(index);return false;
}
bool Gun::partialReload(){
    if(config.clip_size<=1||ammunition.empty()||shot>=int(ammunition.size())||ammunition[shot]<=0)return false;
    auto next=pending;
    if(next){if(*next<0||*next>=int(ammunition.size())||ammunition[*next]<=0)next.reset();pending.reset();}
    if(!next&&(clip>=config.clip_size||(clip<=0&&reload_time>0.)))return false;
    if(next)shot=*next;clip=0;reload_time=reload_duration=config.reload;load_started=false;return true;
}
bool Gun::ready(bool live)const{return live&&!ammunition.empty()&&reload_time<=0.&&burst_remaining<=0&&shot<int(ammunition.size())&&clip>0&&ammunition[shot]>0;}
bool Gun::begin(int count){
    if(!ready())return false;count=std::min({count,clip,ammunition[shot]});if(count<=0)return false;
    burst_total=burst_remaining=count;return true;
}
bool Gun::commit(bool final,double factor){
    if(burst_remaining<=0||final!=(burst_remaining==1)||shot>=int(ammunition.size())||ammunition[shot]<=0||clip<=0)return false;
    const int previous=shot;--ammunition[shot];--clip;
    const double jump=config.base_dispersion*(final?config.after_shot:config.after_shot_in_burst);
    dispersion=std::min(std::sqrt(dispersion*dispersion+jump*jump),config.base_dispersion*15.);
    --burst_remaining;if(!final){reload_time=0.;return true;}burst_total=0;
    if(clip>0&&ammunition[shot]>0){reload_time=reload_duration=config.clip_reload;return true;}
    const auto next=pending;pending.reset();
    if(next&&*next>=0&&*next<int(ammunition.size())&&ammunition[*next]>0)shot=*next;
    else if(ammunition[shot]<=0)for(std::size_t offset=1;offset<=ammunition.size();++offset){const int candidate=int((previous+offset)%ammunition.size());if(ammunition[candidate]>0){shot=candidate;break;}}
    clip=0;reload_time=reload_duration=config.reload*std::max(0.,factor);if(shot!=previous)load_started=false;return true;
}
bool Gun::cancel(double factor){if(burst_remaining<=0)return false;burst_remaining=burst_total=0;reload_time=reload_duration=clip>0?config.clip_reload:config.reload*std::max(0.,factor);return true;}
void Gun::tick(double dt,bool live,double move,double rotation,double turret,const std::array<double,3>&factors,double dispersion_factor,double aim_factor){
    dt=std::max(0.,dt);
    const double m=move*factors[0]*config.bloom_move,r=rotation*factors[1]*config.bloom_rotation,t=turret*factors[2]*config.bloom_turret;
    const double target=config.base_dispersion*std::sqrt(1.+m*m+r*r+t*t)*std::max(0.,dispersion_factor);
    if(dispersion>target)dispersion=target+(dispersion-target)*std::exp(-dt/std::max(config.aim_time*std::max(0.,aim_factor),.1));
    else dispersion=std::min(dispersion+(target-dispersion)*.2,5.);
    if(!live){load_started=false;return;}load_started=true;if(reload_time<=0.)return;
    reload_time-=dt;if(reload_time<=0.){reload_time=0.;if(clip==0&&shot<int(ammunition.size()))clip=std::min(config.clip_size,ammunition[shot]);}
}
bool Gun::rescale(double factor){
    if(reload_time<=0.||clip>0||reload_duration<=0.)return false;
    const double duration=config.reload*std::max(0.,factor);if(std::abs(duration-reload_duration)<=1.e-9)return false;
    const double fraction=reload_duration>0.?std::max(0.,std::min(1.,reload_time/reload_duration)):1.;reload_duration=duration;reload_time=duration*fraction;return true;
}
Point scatter(const Point&direction,double radius,double angle){
    const double length=std::sqrt(direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2]);if(length<=0.)return direction;
    Point d={direction[0]/length,direction[1]/length,direction[2]/length},axis{};
    if(std::abs(d[0])<=std::abs(d[1])&&std::abs(d[0])<=std::abs(d[2]))axis[0]=1.;else if(std::abs(d[1])<=std::abs(d[2]))axis[1]=1.;else axis[2]=1.;
    Point tangent={d[1]*axis[2]-d[2]*axis[1],d[2]*axis[0]-d[0]*axis[2],d[0]*axis[1]-d[1]*axis[0]};
    const double norm=std::sqrt(tangent[0]*tangent[0]+tangent[1]*tangent[1]+tangent[2]*tangent[2]);for(auto&v:tangent)v/=norm;
    const Point up={d[1]*tangent[2]-d[2]*tangent[1],d[2]*tangent[0]-d[0]*tangent[2],d[0]*tangent[1]-d[1]*tangent[0]};
    Point out=d;if(radius>0.){const double x=(radius*std::cos(angle))/radius,y=(radius*std::sin(angle))/radius;for(unsigned i=0;i<3;++i)out[i]=d[i]*std::cos(radius)+(tangent[i]*x+up[i]*y)*std::sin(radius);}
    return out;
}
}
