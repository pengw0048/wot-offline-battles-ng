#include "stock_native_bot_navigation.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
using namespace stock_native_bot_navigation;
struct Scene final:World {
    int policy=0,calls=0;
    std::optional<double> ground(double x,double z,double)override
    {++calls;if(policy==3)return {};return .01*x+.02*z;}
    Proof corridor(const Point&,const Point&,double)override
    {++calls;return policy==1?Proof::Blocked:policy==2?Proof::Unknown:Proof::Clear;}
    Proof navigation(const Point&,const Point&,double,double,double,std::optional<double>)override
    {++calls;return policy==1?Proof::Blocked:policy==2?Proof::Unknown:Proof::Clear;}
};
Point readPoint(){Point p;std::cin>>p[0]>>p[1]>>p[2];return p;}
void point(const Point&p){std::cout<<'['<<p[0]<<','<<p[1]<<','<<p[2]<<']';}
template<class T>void optional(const std::optional<T>&v){if(v)std::cout<<*v;else std::cout<<"null";}
int main()
{
    std::cout<<std::setprecision(17);Scene scene;offline_navigation::MapInput input;
    std::cin>>input.width>>input.height>>input.cell_size>>input.origin_x>>input.origin_z;
    input.has_bounds=true;for(auto&v:input.bounds)std::cin>>v;
    for(int n=0;n<input.width*input.height;++n){int present;double height;unsigned links,hazard;
        std::cin>>present>>height>>links>>hazard;input.heights.push_back(present?height:std::numeric_limits<double>::quiet_NaN());
        input.links.push_back(links);input.hazards.push_back(hazard);}
    const double grade=std::tan(25.*3.14159265358979323846/180.);Grid grid(input,scene,grade,grade);
    std::string operation;
    while(std::cin>>operation){
        if(operation=="geometry") {auto a=readPoint(),b=readPoint();double yaw,length,width;std::cin>>yaw>>length>>width;
            auto c=grid.cellFor(a);auto corridor=grid.bakedCorridor(a,b);auto local=grid.localCorridor(a);
            std::cout<<'['<<c[0]<<','<<c[1]<<',';optional(grid.height(c));std::cout<<','<<corridor.clear<<',';optional(corridor.hazards);
            std::cout<<','<<grid.segmentHazard(a,b,4)<<','<<grid.motionHazard(a,b,5)<<','<<grid.pointHazard(a,3)<<','<<grid.hazardNear(a,1)
                <<','<<grid.hullPoseClear(a,yaw,length,width)<<','<<grid.linkCount(c)<<','<<grid.clearancePenalty(c)<<',';
            if(local)std::cout<<'['<<local->first<<','<<local->second<<']';else std::cout<<"null";std::cout<<']';
        }else if(operation=="proof") {auto a=readPoint(),b=readPoint();int policy,review;std::cin>>policy>>review;scene.policy=policy;scene.calls=0;
            grid.invalidateNativeReview();grid.beginFrame();if(review)grid.reviewNativeCorridor(a,b);
            bool supported=grid.supportedSlope(a,b),segment=grid.segmentClear(a,b),dry=grid.drySegment(a,b,0.);
            std::cout<<'['<<supported<<','<<segment<<','<<dry<<','<<grid.proof_deferred<<']';
        }else if(operation=="local") {auto a=readPoint(),b=readPoint();double side,minimum;std::cin>>side>>minimum>>scene.policy;
            grid.invalidateNativeReview();grid.beginFrame();auto result=grid.safeLocal(a,b,0.,{},side,{},minimum);if(result)point(*result);else std::cout<<"null";
        }else if(operation=="hulls") {int count;double padding;std::cin>>count>>padding;std::vector<Hull> hulls;
            for(int n=0;n<count;++n){Hull h;std::cin>>h.id>>h.x>>h.z>>h.yaw>>h.half_length>>h.half_width;hulls.push_back(h);}
            bool changed=grid.setStaticHulls(hulls,padding);auto a=readPoint(),b=readPoint();std::cout<<'['<<changed<<','<<grid.hull_revision<<','<<grid.static_hull_edges.size()<<','<<grid.segmentPenalty(a,b,0.)<<']';
        }else if(operation=="path") {int count;std::cin>>count;Path path;for(int n=0;n<count;++n)path.push_back(readPoint());
            std::cout<<'[';optional(grid.clearanceExposure(path));std::cout<<','<<Grid::preservesClimb(path,0,path.size()-1)<<','<<grid.preservesClearance(path,0,path.size()-1)<<']';
        }else return 2;
        std::cout<<'\n';
    }
}
