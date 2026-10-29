#include "stock_native_bot_navigation_runtime.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
using namespace stock_native_bot_navigation;
struct Scene final:World {
    std::optional<double> ground(double x,double z,double)override{return .01*x+.02*z;}
    Proof corridor(const Point&,const Point&,double)override{return Proof::Clear;}
    Proof navigation(const Point&,const Point&,double,double,double,std::optional<double>)override{return Proof::Clear;}
};
struct Queue final:SearchQueue {
    Grid&grid;explicit Queue(Grid&g):grid(g){}
    std::shared_ptr<Search> submit(const Point&a,const Point&b,double now,unsigned,bool,const Penalties&,const std::set<Edge>&,const Path&,std::optional<std::pair<Point,Point>>)override
    {auto job=std::make_shared<Search>();job->submitted_at=now;job->start=a;job->goal=b;job->hull_revision=grid.hull_revision;return job;}
    void cancel(const std::vector<std::shared_ptr<Search>>&values)override{for(const auto&job:values){job->done=true;job->result.clear();}}
    void advance(double,unsigned)override{}
    void close()override{}
};
Point readPoint(){Point p;std::cin>>p[0]>>p[1]>>p[2];return p;}
void point(const std::optional<Point>&p){if(p)std::cout<<'['<<(*p)[0]<<','<<(*p)[1]<<','<<(*p)[2]<<']';else std::cout<<"null";}
int main(){std::cout<<std::setprecision(17);Scene scene;offline_navigation::MapInput map;std::cin>>map.width>>map.height>>map.cell_size>>map.origin_x>>map.origin_z;
    map.has_bounds=true;for(auto&v:map.bounds)std::cin>>v;for(int n=0;n<map.width*map.height;++n){int present;double height;unsigned links,hazard;std::cin>>present>>height>>links>>hazard;
        map.heights.push_back(present?height:std::numeric_limits<double>::quiet_NaN());map.links.push_back(links);map.hazards.push_back(hazard);}
    double grade=std::tan(25.*3.14159265358979323846/180.);Grid grid(map,scene,grade,grade);Navigator nav(grid,std::make_unique<Queue>(grid));NavigationRuntime runtime(nav);
    int count;std::cin>>count;for(int n=0;n<count;++n){NavigationActor a;int alive,join;std::cin>>a.id>>a.team>>a.slot>>alive;a.position=readPoint();std::cin>>a.yaw>>a.half_length>>a.half_width>>join;
        a.alive=alive;a.collision_half_length=a.half_length;a.collision_half_width=a.half_width;a.local_route_id="r";a.local_route={{-20.,0.},{20.,0.}};a.local_route_index=1;a.local_route_join=join;
        runtime.actors[a.id]=a;Strategy order;order.route_id="r";order.route_index=1;order.route_join=join;order.route_anchor=Point{-20.,0.,0.};runtime.orders[a.id]=order;}
    std::string operation;while(std::cin>>operation){std::int64_t id;std::cin>>id;auto current=readPoint(),goal=readPoint();double now;int join;std::cin>>now>>join;
        auto strategy=runtime.orders.at(id);strategy.route_join=join;runtime.orders[id]=strategy;runtime.actors.at(id).position=current;
        if(operation=="bind"){auto result=runtime.laneBinding(id,{runtime.actors.at(id).team,"r"},goal,strategy);std::cout<<'['<<result.first<<','<<result.second<<",[";bool comma=false;
            for(const auto&entry:runtime.actors){if(comma)std::cout<<',';comma=true;const auto&a=entry.second;std::cout<<'['<<a.id<<','<<a.lane_desired<<','<<a.row_desired<<','<<(a.lane_group?1:0)<<']';}std::cout<<"]]";
        }else if(operation=="goal"){auto result=runtime.laneGoal(id,current,goal,strategy,now,join);std::cout<<'[';point(result.first);std::cout<<','<<result.second.first<<','<<result.second.second<<']';}
        else if(operation=="lane")point(runtime.laneTarget(id,current,goal,readPoint(),strategy,now));
        else if(operation=="radio"){std::array<double,4> area;for(auto&v:area)std::cin>>v;strategy.move_area=area;strategy.team_command="cmd";point(runtime.radioGoal(id,current,goal,strategy));}
        else if(operation=="occupied"){std::cout<<'[';point(runtime.occupiedTarget(id,current,goal,now,join));std::cout<<','<<runtime.actors.at(id).route_wreck_blocked<<']';}
        else if(operation=="nav"){auto result=runtime.target(id,current,goal,strategy,now);std::cout<<'[';point(result.target);std::cout<<','<<result.stop_at_target<<','<<std::quoted(result.status)<<',';point(result.progress);std::cout<<']';}
        else if(operation=="alive"){runtime.actors.at(id).alive=join;std::cout<<"null";}
        else return 2;
        std::cout<<'\n';}
}
