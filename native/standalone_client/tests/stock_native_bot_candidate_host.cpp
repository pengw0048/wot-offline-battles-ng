#include "stock_native_bot_candidate.hpp"
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace stock_native_bot_navigation;
struct Scene final:CandidateWorld {
    int policy=0,queries=0;double gx=0,gz=0,base=0,water=-1.;
    PlanningRay ground(const Point&a,const Point&,bool)override {++queries;if(policy==2)return {RayStatus::Failed,0.};if(policy==3)return {};return {RayStatus::Found,gx*a[0]+gz*a[2]+base};}
    RayStatus segment(const Point&,const Point&,bool)override{++queries;return policy==1?RayStatus::Found:policy==4?RayStatus::Failed:RayStatus::Miss;}
    double waterDepth(const Point&p)override{return water+(policy==5?p[2]*.04:0.);}
    double rotationFraction(std::int64_t,const Point&,double,double,bool)override{return 1.;}
};
int main(){std::cout<<std::setprecision(17);Point p;double yaw,speed,width,maximum;int bounded;
    Scene scene;while(std::cin>>p[0]>>p[1]>>p[2]>>yaw>>speed>>width>>bounded>>maximum>>scene.policy>>scene.gx>>scene.gz>>scene.base>>scene.water){
        scene.queries=0;auto sample=probeDirection(scene,p,yaw,speed,{true,width},bounded?std::optional<double>(maximum):std::nullopt);
        std::cout<<'['<<sample.clear<<','<<sample.collision<<','<<sample.water<<','<<sample.slope<<','<<sample.probe_failed<<','<<sample.slope_valid<<','<<std::quoted(sample.reason)<<','<<scene.queries<<']'<<'\n';}}
