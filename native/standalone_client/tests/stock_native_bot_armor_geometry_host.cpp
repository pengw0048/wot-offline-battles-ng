#include "stock_native_bot_armor_geometry.hpp"
#include <iomanip>
#include <iostream>
#include <cmath>
using namespace stock_native_bot_armor;
struct Query{std::int64_t id;Point start,end;};
struct Fixture:Geometry {
    int mode=0;std::vector<Query> queries;Material track,plate,bad;std::vector<Material> unused;
    Fixture(){track.device="leftTrackHealth";track.armor=20.;track.vehicle_damage_factor=0.;plate.armor=40.;plate.vehicle_damage_factor=1.;bad.armor=NAN;bad.vehicle_damage_factor=1.;}
    bool ready(const Body&)override{return true;}
    Body physical_body(const Body&body)override{auto result=body;if(mode>=7){result.x+=.15;result.y+=.2;result.z-=.1;result.pitch+=.17;result.roll-=.09;result.shape[0]*=1.1;result.shape[1]*=.9;}return result;}
    Point native_point(const Point&p)override{return mode>=14?Point{{float(p[0]),float(p[1]),float(p[2])}}:p;}
    std::optional<Hull> hull(const Body&)override{if((mode%7)%2)return {};return Hull{{{-1.,0.,-2.}},{{1.,.8,2.}},{{0.,.3,0.}}};}
    std::vector<Layer> segment(const Body&b,const Point&start,const Point&end)override {
        queries.push_back({b.id,start,end});const auto physical=physical_body(b);const double reach=std::sqrt(physical.shape[0]*physical.shape[0]+physical.shape[1]*physical.shape[1])+1.;const auto law=mode%7;
        const double y=start[1];bool solid=true;
        if(law==1)solid=y>=.65&&y<=.85;
        if(law==2)solid=std::abs(end[2]-start[2])>.1;
        if(law==3)solid=b.id==1?y<.7:y>1.3;
        if(law==5)return {};
        if(!solid)return {{reach-.05,1.,0,&track}};
        if(law==4)return {{reach,1.,1,&plate},{reach+.1,1.,0,&track}};
        return {{reach-(law==6?.25:.05),1.,0,&track},{reach+.01,1.,1,&bad},{reach+.1,1.,1,&plate}};
    }
};
void write(const Point&p){std::cout<<'['<<p[0]<<','<<p[1]<<','<<p[2]<<']';}
Body read(){Body b;std::cin>>b.id>>b.x>>b.y>>b.z>>b.yaw>>b.pitch>>b.roll;for(auto&v:b.shape)std::cin>>v;return b;}
int main(){std::cout<<std::setprecision(17);int mode;while(std::cin>>mode){const auto a=read(),b=read();XZ normal;std::cin>>normal[0]>>normal[1];Fixture fixture;fixture.mode=mode;
    const auto result=contact_pair(a,b,normal,fixture);std::cout<<'[';
    if(result){std::cout<<"[["<<result->armor[0]<<','<<result->armor[1]<<"],[";for(unsigned i=0;i<2;++i){if(i)std::cout<<',';std::cout<<'[';bool first=true;for(const auto&t:result->tracks[i]){if(!first)std::cout<<',';first=false;std::cout<<'"'<<t<<'"';}std::cout<<']';}std::cout<<"]]";}else std::cout<<"null";
    std::cout<<",[";bool first=true;for(const auto&q:fixture.queries){if(!first)std::cout<<',';first=false;std::cout<<'['<<q.id<<',';write(q.start);std::cout<<',';write(q.end);std::cout<<']';}std::cout<<"],[";
    first=true;for(const auto&p:overlap_polygon(a,b)){if(!first)std::cout<<',';first=false;std::cout<<'['<<p[0]<<','<<p[1]<<']';}std::cout<<"]]\n";
}}
