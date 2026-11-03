#include "stock_native_bot_strategy.hpp"
#include "stock_native_bot_ammo.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <tuple>

namespace stock_native_bot_strategy {
namespace {
constexpr double pi=3.14159265358979323846;
double distance(const Point&a,const Point&b){const double dx=a[0]-b[0],dz=a[2]-b[2];return std::sqrt(dx*dx+dz*dz);}
double delta(double a,double b){double d=a-b;while(d>pi)d-=2*pi;while(d<-pi)d+=2*pi;return d;}
double role(const Profile&p,const char*name){auto i=p.roles.find(name);return i==p.roles.end()?0.:i->second;}
Point point(const std::array<double,2>&p,double y){return {p[0],y,p[1]};}
}
int selectShell(const Profile&p,const Contact&,const Personality&)
{
    stock_native_bot_ammo::Profile input;input.vehicle=p.vehicle_name;input.gun=p.gun_name;
    input.class_tag=p.class_tag;input.manual=p.ammo_manual;input.ratios=p.ammo_ratios;
    int count=0;for(const auto& shell:p.shells) {
        input.shells.push_back({shell.index,shell.kind,shell.name});count=std::max(count,shell.index+1);
    }
    const auto allocation=stock_native_bot_ammo::allocate(input,0,count);
    return allocation.order.empty()?0:allocation.order.front();
}

void Director::clear(){agents.clear();bases.clear();contacts_.clear();}
void Director::forget(std::int64_t id){agents.erase(id);}
void Director::observe(int team,const Contact&value)
{
    if(team==value.team)return;auto&contacts=contacts_[team];auto found=contacts.find(value.id);
    if(value.visible){auto c=value;c.health=std::max(0.,c.health);c.max_health=std::max(1.,c.max_health);c.armor=std::max(0.,c.armor);c.speed=std::max(0.,c.speed);contacts[value.id]=std::move(c);}
    else if(found!=contacts.end()){found->second.visible=false;found->second.shootable=std::set<std::int64_t>{};}
}
std::vector<Contact*> Director::known(int team,double now)
{
    auto&source=contacts_[team];std::vector<Contact*> result;
    for(auto i=source.begin();i!=source.end();)if(now-i->second.last_seen>7.||i->second.health<=0.)i=source.erase(i);else{result.push_back(&i->second);++i;}return result;
}
Contact* Director::choose(Agent&a,const Point&p,double yaw,double now)
{
    Contact*best=nullptr;double best_score=-1.e18;const auto&profile=a.profile;const auto&personality=a.personality;
    for(auto*c:known(a.team,now)){
        if(c->visible&&c->shootable&&!c->shootable->count(a.id))continue;int focus=0;for(const auto&pair:agents)if(pair.second.team==a.team&&pair.second.target==c->id)++focus;
        const int desired=c->health>=1800.?3:(c->health>=900.||c->class_tag=="heavyTank"||c->class_tag=="AT-SPG")?2:1;
        if(focus>=desired&&a.target!=c->id)continue;const double d=distance(p,c->position),age=std::max(0.,now-c->last_seen),mobility=std::max(role(profile,"scout"),role(profile,"flanker"));
        if(d>std::max(340.,std::min(560.,profile.desired_range*2.+mobility*300.)))continue;
        const double health=c->health/std::max(c->max_health,1.),bearing=std::atan2(c->position[0]-p[0],c->position[2]-p[2]);
        double score=c->visible?90.:std::max(4.,42.-age*6.);score+=(1.-health)*38.;score-=std::abs(d-profile.desired_range)/std::max(profile.desired_range,50.)*(14.-personality.aggression*5.);
        score-=std::abs(delta(bearing,yaw))/pi*12.;if(focus<desired)score+=focus*personality.teamwork*4.;else score-=(focus-desired+1)*(10.+(1.-personality.teamwork)*7.);
        if(a.target==c->id)score+=18.;if(c->class_tag=="lightTank"||c->class_tag=="SPG")score+=4.*personality.initiative;if(c->visible&&c->class_tag=="SPG")score+=48.;
        if(score>best_score){best_score=score;best=c;}}
    a.target=best?std::optional<std::int64_t>(best->id):std::nullopt;return best;
}
Point Director::route(Agent&a,const Point&p,double speed,double yaw,double now)
{
    if(!a.has_route){auto b=bases.find(a.team==1?2:1);return b==bases.end()?p:point(b->second,p[1]);}if(a.waypoints.empty())return p;
    if(!a.route_started){int nearest=0;double best=distance(p,point(a.waypoints[0],p[1]));
        for(std::size_t n=1;n<a.waypoints.size();++n){const double d=distance(p,point(a.waypoints[n],p[1]));if(d<best){best=d;nearest=int(n);}}
        if(a.route_entry_fixed&&a.waypoints.size()>1)nearest=1;
        if(nearest==0&&a.waypoints.size()>1)nearest=1;
        while(nearest+1<int(a.waypoints.size())&&distance(p,point(a.waypoints[nearest],p[1]))<=6.)++nearest;
        if(speed>.5)while(nearest+1<int(a.waypoints.size())){const auto&w=a.waypoints[nearest];if(std::abs(delta(std::atan2(w[0]-p[0],w[1]-p[2]),yaw))<=1.75)break;++nearest;}
        a.waypoint_index=nearest;a.route_started=true;a.route_join_anchor=p;a.route_join_index=nearest;}
    const bool has_previous=a.route_observation && a.observed_route_id==a.route_id &&
        now>a.route_observed_at && now-a.route_observed_at<=2.;
    const auto previous=a.route_observation;a.route_observation=p;a.route_observed_at=now;a.observed_route_id=a.route_id;
    int i=std::min(a.waypoint_index,int(a.waypoints.size())-1);
    while(i+1<int(a.waypoints.size())) {
        const auto target=point(a.waypoints[i],p[1]);bool reached=distance(p,target)<=6.;
        if(!reached && has_previous) {
            const double dx=p[0]-(*previous)[0],dz=p[2]-(*previous)[2],length=dx*dx+dz*dz;
            if(length>.000001) {
                const double f=std::max(0.,std::min(1.,((target[0]-(*previous)[0])*dx+(target[2]-(*previous)[2])*dz)/length));
                reached=std::hypot(target[0]-((*previous)[0]+f*dx),target[2]-((*previous)[2]+f*dz))<=6.;
            }
        }
        if(!reached)break;++i;
    }
    a.waypoint_index=i;return point(a.waypoints.at(i),p[1]);
}
Point Director::fallback(const Agent&a,const Point&p)const
{if(!a.has_route||a.waypoints.empty())return p;return point(a.waypoints[std::max(0,std::min(a.waypoint_index-1,int(a.waypoints.size())-1))],p[1]);}
Point Director::anchor(const Agent&a,const Point&p)const
{if(!a.has_route||a.waypoints.empty())return p;const int i=std::max(0,std::min(a.waypoint_index,int(a.waypoints.size())-1));if(a.route_join_index==i&&a.route_join_anchor)return *a.route_join_anchor;
return point(a.waypoints[std::max(0,std::min(i-1,int(a.waypoints.size())-1))],p[1]);}
std::optional<double> Director::hullAngle(const Agent&a)const
{const auto&p=a.profile;if(p.class_tag=="AT-SPG"||p.class_tag=="SPG"||(p.dominant_role!="brawler"&&p.dominant_role!="support")||(p.armor<60.&&role(p,"brawler")<.55))return {};
const double angle=12.+a.personality.caution*18.;return (a.seed&1)?angle:-angle;}
Decision Director::order(std::int64_t id,const Point&p,double yaw,double speed,double health,double max_health,double now)
{
    auto&a=agents.at(id);a.position=p;a.health_fraction=health/std::max(max_health,1.);auto*contact=choose(a,p,yaw,now);const Point goal=route(a,p,speed,yaw,now);const auto&profile=a.profile;const auto&personality=a.personality;
    Decision out;out.aim.set(goal);out.face.set(goal);out.move.set(goal);out.desired_range=profile.desired_range;out.fire_range=profile.fire_range;out.route_id=a.has_route?a.route_id:"direct";
    out.route_index=a.waypoint_index;out.route_anchor=anchor(a,p);out.route_join=a.route_join_index==a.waypoint_index;
    if(contact){const double d=distance(p,contact->position);double allies=1.,enemies=0.;
        for(const auto&pair:agents){const auto&other=pair.second;if(other.id!=a.id&&other.team==a.team&&other.position&&distance(*other.position,p)<=185.)allies+=std::max(.25,other.health_fraction);}
        for(const auto*enemy:known(a.team,now))if(distance(enemy->position,contact->position)<=185.)enemies+=std::max(.3,enemy->health/std::max(enemy->max_health,1.));
        out.force_balance=allies-enemies;out.target.set(contact->id);out.aim.set(contact->position);out.face=out.aim;auto hull=hullAngle(a);
        if(hull){out.hull_angle_degrees.set(*hull);const double dx=contact->position[0]-p[0],dz=contact->position[2]-p[2];if(std::sqrt(dx*dx+dz*dz)>=.1){const double angle=*hull*pi/180.,c=std::cos(angle),s=std::sin(angle);out.face.set({p[0]+dx*c-dz*s,contact->position[1],p[2]+dx*s+dz*c});}}
        out.fire_allowed=contact->visible;out.shell_index=selectShell(profile,*contact,personality);
        if(contact->visible){out.mode="engage";const double close=.52+personality.aggression*.12,far=1.02+personality.caution*.28;
            if(out.force_balance<-.65&&personality.caution+.18>personality.aggression&&profile.dominant_role!="brawler"){out.move.set(fallback(a,p));out.mode="withdraw";}
            else if(d>profile.desired_range*far){if(!out.route_id.empty())out.mode="advance";else{out.move=out.aim;out.mode="advance_contact";}}
            else if(d<profile.desired_range*close){if(profile.dominant_role!="brawler"||personality.caution>.62){out.move.set(fallback(a,p));out.mode="withdraw";}else out.move.set(p);}
            else if(role(profile,"flanker")>=.68&&d<profile.fire_range*1.15&&out.force_balance>=-.35&&personality.initiative>.38){const double dx=p[0]-contact->position[0],dz=p[2]-contact->position[2],len=std::sqrt(dx*dx+dz*dz),sign=(a.seed&1)?-1.:1.;out.move.set(p);
                if(len>=.1){const double forward=profile.desired_range*.72,lateral=std::min(95.,profile.desired_range*.38);out.move.set({contact->position[0]+dx/len*forward+dz/len*lateral*sign,p[1],contact->position[2]+dz/len*forward-dx/len*lateral*sign});}out.mode="flank";}
            else out.move.set(p);
        }else{out.mode="investigate";if(personality.initiative+personality.aggression>1.05)out.move=out.aim;}}
    if(a.health_fraction<.20+personality.caution*.18){out.move.set(fallback(a,p));out.mode="withdraw";}return out;
}
}
