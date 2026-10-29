#include "stock_native_bot_navigator.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
using namespace stock_native_bot_navigation;
struct Scene final:World {
    int policy=0;
    std::optional<double> ground(double x,double z,double)override{return policy==3?std::nullopt:std::optional<double>(.01*x+.02*z);}
    Proof corridor(const Point&,const Point&,double)override{return policy==1?Proof::Blocked:policy==2?Proof::Unknown:Proof::Clear;}
    Proof navigation(const Point&,const Point&,double,double,double,std::optional<double>)override{return policy==1?Proof::Blocked:policy==2?Proof::Unknown:Proof::Clear;}
};
struct Queue final:SearchQueue {
    int mode=0;std::int64_t next=0;Grid&grid;std::vector<std::shared_ptr<Search>> jobs;
    explicit Queue(Grid&g):grid(g){}
    std::shared_ptr<Search> submit(const Point&a,const Point&b,double now,unsigned,bool,const Penalties&,const std::set<Edge>&,const Path&,std::optional<std::pair<Point,Point>>)override
    {auto job=std::make_shared<Search>();job->id=++next;job->hull_revision=grid.hull_revision;job->submitted_at=now;job->start=a;job->goal=b;jobs.push_back(job);return job;}
    void cancel(const std::vector<std::shared_ptr<Search>>&values)override{for(const auto&job:values){job->done=true;job->status=1;job->result.clear();}}
    void advance(double,unsigned)override{if(mode)for(auto&job:jobs)if(!job->done){job->done=true;job->result=mode==1?Path{job->start,job->goal}:Path{};}}
    void close()override{cancel(jobs);jobs.clear();}
};
Point readPoint(){Point p;std::cin>>p[0]>>p[1]>>p[2];return p;}
void point(const std::optional<Point>&p){if(p)std::cout<<'['<<(*p)[0]<<','<<(*p)[1]<<','<<(*p)[2]<<']';else std::cout<<"null";}
int main()
{
    std::cout<<std::setprecision(17);Scene scene;offline_navigation::MapInput input;std::cin>>input.width>>input.height>>input.cell_size>>input.origin_x>>input.origin_z;
    input.has_bounds=true;for(auto&v:input.bounds)std::cin>>v;
    for(int n=0;n<input.width*input.height;++n){int present;double height;unsigned links,hazard;std::cin>>present>>height>>links>>hazard;
        input.heights.push_back(present?height:std::numeric_limits<double>::quiet_NaN());input.links.push_back(links);input.hazards.push_back(hazard);}
    const double grade=std::tan(25.*3.14159265358979323846/180.);Grid grid(input,scene,grade,grade);auto queue=std::make_unique<Queue>(grid);auto* control=queue.get();Navigator nav(grid,std::move(queue));
    std::string operation;while(std::cin>>operation){
        std::int64_t id;std::cin>>id;auto current=readPoint(),goal=readPoint();double now;std::cin>>now;std::string kind,identity;std::cin>>kind>>identity;
        int movement;double yaw;std::cin>>movement>>yaw>>scene.policy>>control->mode;PathKey key{kind,identity,{},kind=="local"?std::optional<std::int64_t>(id):std::nullopt};
        std::optional<Point> result;bool verdict=false;
        if(operation=="next")result=nav.nextTarget(id,current,goal,key,now,{}, {}, {},movement);
        else if(operation=="direct")result=nav.observeDirect(id,current,goal,key,now,movement);
        else if(operation=="retained")result=nav.retainedTarget(id,current,goal,key,now,movement,yaw);
        else if(operation=="hard")verdict=nav.reportHardContact(id,current,goal,yaw,now);
        else if(operation=="blocked")verdict=nav.reportBlockedStep(id,current,goal,now);
        else if(operation=="replan")verdict=nav.requestReplan(id,current,now);
        else if(operation=="clear")verdict=nav.clearBlockedContact(id);
        else if(operation=="review")verdict=nav.reportBlockedPlan(current,goal);
        else if(operation=="remember"){nav.rememberRuntimeTarget(id,current,goal,readPoint(),now);}
        else return 2;
        std::cout<<'[';point(result);std::cout<<','<<verdict<<',';auto s=nav.states.find(id);
        if(s==nav.states.end())std::cout<<"null";
        else{const auto&state=s->second;std::cout<<'['<<std::quoted(state.status)<<','<<state.terminal<<','<<state.index<<','<<state.generation<<','<<state.macro_replans<<','<<state.blocked_replans<<','<<state.replan_active<<','<<state.temporary_stalled<<',';
            point(state.local_target);std::cout<<',';point(state.last_target);std::cout<<',';point(state.controlled_shallow_target);std::cout<<',';point(nav.driverProgressTarget(id,current,state.last_target.value_or(current),now));std::cout<<']';}
        std::cout<<']'<<'\n';
    }
}
