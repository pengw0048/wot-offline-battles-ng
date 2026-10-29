#include "stock_native_bot_strategy.hpp"
#include <iostream>
#include <iomanip>
using namespace stock_native_bot_strategy;
void point(const Point&p){std::cout<<"["<<p[0]<<","<<p[1]<<","<<p[2]<<"]";}
int main(){Director director;std::cout<<std::setprecision(17);std::string op;while(std::cin>>op){
if(op=="base"){int team;std::array<double,2>p;std::cin>>team>>p[0]>>p[1];director.bases[team]=p;}
else if(op=="agent"){Agent a;int n;std::cin>>a.id>>a.team>>a.seed>>a.profile.class_tag>>a.profile.dominant_role>>a.profile.desired_range>>a.profile.fire_range>>a.profile.armor>>a.personality.aggression>>a.personality.caution>>a.personality.initiative>>a.personality.teamwork>>a.has_route>>a.route_id>>n;
for(int i=0;i<n;++i){std::array<double,2>p;std::cin>>p[0]>>p[1];a.waypoints.push_back(p);}std::cin>>n;for(int i=0;i<n;++i){std::string role;double value;std::cin>>role>>value;a.profile.roles[role]=value;}std::cin>>n;for(int i=0;i<n;++i){Shell s;std::cin>>s.index>>s.kind>>s.penetration>>s.damage;a.profile.shells.push_back(s);}director.agents[a.id]=a;}
else if(op=="contact"){int team,n;Contact c;std::cin>>team>>c.id>>c.team>>c.position[0]>>c.position[1]>>c.position[2]>>c.health>>c.max_health>>c.armor>>c.speed>>c.last_seen>>c.class_tag>>c.visible>>n;if(n>=0){c.shootable.emplace();for(int i=0;i<n;++i){std::int64_t id;std::cin>>id;c.shootable->insert(id);}}director.observe(team,c);}
else if(op=="order"){std::int64_t id;Point p;double yaw,speed,health,max_health,now;std::cin>>id>>p[0]>>p[1]>>p[2]>>yaw>>speed>>health>>max_health>>now;const auto d=director.order(id,p,yaw,speed,health,max_health,now);
std::cout<<"[";if(d.target.present)std::cout<<d.target.value;else std::cout<<"null";std::cout<<",";point(d.aim.value);std::cout<<",";point(d.move.value);std::cout<<",";point(d.face.value);
std::cout<<","<<d.fire_allowed<<",\""<<d.mode<<"\","<<d.shell_index<<","<<d.desired_range<<","<<d.fire_range<<",\""<<d.route_id<<"\","<<d.route_index<<",";point(d.route_anchor);std::cout<<","<<d.route_join<<","<<d.force_balance<<",";if(d.hull_angle_degrees.present)std::cout<<d.hull_angle_degrees.value;else std::cout<<"null";std::cout<<"]\n";}
}}
