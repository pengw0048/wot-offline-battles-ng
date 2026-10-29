#include "stock_native_bot_turret_obstacles.hpp"
#include <iomanip>
#include <iostream>
#include <cassert>
#include <cmath>
using namespace stock_native_bot_turret;
Point point(){Point out;for(auto&v:out)std::cin>>v;return out;}
Component component(){Component out;out.bounds.minimum=point();out.bounds.maximum=point();out.offset=point();return out;}
void write(const Point&p){std::cout<<'['<<p[0]<<','<<p[1]<<','<<p[2]<<']';}
void write(const Box&b){std::cout<<'[';write(b.center);std::cout<<",[";bool first=true;for(const auto&a:b.axes){if(!first)std::cout<<',';first=false;write(a);}std::cout<<"]]";}
struct Hits:DetachedHits {
    std::vector<std::pair<Point,Point>> queries;
    std::vector<double> segment(Owner,bool gun,const Point&a,const Point&b)override {queries.push_back({a,b});return gun?std::vector<double>{1.,5.}:std::vector<double>{2.,4.,8.};}
};
int main(int argc,char**){std::cout<<std::setprecision(17);
    if(argc>1){
        Component box{{{{-1.,-1.,-1.}},{{1.,1.,1.}}},{}};DetachedDescriptor detached{box,box};MovingDescriptor moving{box,box};
        Obstacles obstacles;Record row;row.owner={false,2};row.landed=true;row.duration=1.;row.created_time_ms=1000.;row.rest={{0.,0.,0.}};
        assert(obstacles.put(row,detached));assert(!obstacles.put(row,detached));
        stock_native_world::TurretSweep sweep;sweep.actor=1;sweep.start.position={{0.,0.,-5.}};sweep.end.position={{0.,0.,5.}};
        assert(!obstacles.blocks(sweep,moving,1999.));assert(obstacles.blocks(sweep,moving,2000.));
        row.motion_sequence=1;row.support=Owner{false,1};assert(obstacles.put(row,detached));assert(!obstacles.blocks(sweep,moving,2000.));assert(obstacles.navigation(2000.).empty());
        row.motion_sequence=2;row.support.reset();row.has_body=true;assert(obstacles.put(row,detached));assert(!obstacles.blocks(sweep,moving,2000.));
        row.motion_sequence=3;row.has_body=false;row.landed=false;assert(obstacles.put(row,detached));assert(obstacles.active()==0);
        bool rejected=false;try{componentSweeps(box,{{NAN,0.,0.}},{},{},{},{});}catch(...){rejected=true;}assert(rejected);
        std::cout<<"ok\n";return 0;
    }
    int operation;while(std::cin>>operation){
        if(operation==2) {
            Record record;record.owner={false,7};record.landed=true;record.created_time_ms=1000.;record.duration=1.;
            record.rest=point();record.rest_attitude=point();double now,begin;std::cin>>now>>begin;
            DetachedDescriptor descriptor{component(),component()};const auto start=point(),end=point();
            Obstacles obstacles;assert(obstacles.put(record,descriptor));Hits hits;const auto distance=obstacles.block_distance(start,end,now,hits,begin);
            std::cout<<'[';if(distance)std::cout<<*distance;else std::cout<<"null";std::cout<<",[";bool first=true;
            for(const auto&q:hits.queries){if(!first)std::cout<<',';first=false;std::cout<<'[';write(q.first);std::cout<<',';write(q.second);std::cout<<']';}std::cout<<"]]\n";continue;
        }
        const auto shape=component();const auto start=point(),a=point(),end=point(),b=point();
        int has_pivot;std::cin>>has_pivot;const auto pivot_position=point();double delta;std::cin>>delta;
        const auto pivot=has_pivot?std::optional<std::pair<Point,double>>{{pivot_position,delta}}:std::nullopt;
        const auto boxes=componentSweeps(shape,start,a,end,b,pivot);
        if(operation==0){std::cout<<'[';bool first=true;for(const auto&box:boxes){if(!first)std::cout<<',';first=false;write(box);}std::cout<<"]\n";}
        else{const auto other=component();const auto p=point(),angle=point();const auto obstacle=worldBox(other,p,angle);std::cout<<leavesOverlap(worldBox(shape,start,a),worldBox(shape,end,b),boxes,obstacle)<<'\n';}
    }
}
