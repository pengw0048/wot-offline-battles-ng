#include "stock_native_bot_candidate.hpp"
#include <algorithm>
#include <cmath>

namespace stock_native_bot_navigation {
namespace {
constexpr double pi=3.14159265358979323846;
const double max_grade=std::tan(25.*pi/180.);
double angle(double value){while(value>pi)value-=2*pi;while(value<-pi)value+=2*pi;return value;}
double distance(const Point&a,const Point&b){const double x=a[0]-b[0],z=a[2]-b[2];return std::sqrt(x*x+z*z);}
DirectionSample failed(bool collision,double slope,const char*reason,bool query=false,bool valid=true)
{DirectionSample sample;sample.collision=collision;sample.slope=slope;sample.reason=reason;sample.probe_failed=query;sample.slope_valid=valid;return sample;}
}
DirectionSample probeDirection(CandidateWorld&world,const Point&position,double yaw,double speed,
    const CandidateDescriptor&descriptor,std::optional<double>maximum,std::optional<double>width)
{
    const double half_width=width.value_or(descriptor.present?descriptor.collision_half_width:2.2);
    if(!std::isfinite(half_width)||half_width<=0.)return failed(true,0.,"");
    try{
        const double water=world.waterDepth(position);const bool wet=water>.90;
        double far=std::abs(speed)>5.?20.:15.;
        if(maximum){if(!std::isfinite(*maximum))return failed(true,0.,"");far=std::min(far,std::max(.5,*maximum));}
        const double near=std::min(8.,far),sine=std::sin(yaw),cosine=std::cos(yaw);double previous_y=position[1],previous_distance=0.,largest=0.,slope=0.;
        for(const auto&pair:std::array<std::pair<double,double>,2>{{{.7,near},{1.5,far}}}){
            const double height=pair.first,d=pair.second,nx=position[0]+sine*d,nz=position[2]+cosine*d,run=d-previous_distance;
            double next_y=previous_y;
            if(run>0.){const double up=std::max(4.5,run*max_grade+.02),down=std::max(5.,run*max_grade+.02);PlanningRay ground;
                try{ground=world.ground({nx,previous_y+up,nz},{nx,previous_y-down,nz},descriptor.present);}
                catch(...){return failed(true,99.,"planning_ground_query_failed",true,false);}
                if(ground.status==RayStatus::Failed)return failed(true,99.,"planning_ground_query_failed",true,false);
                if(ground.status==RayStatus::Miss)return failed(false,99.,"planning_ground_missing",false,false);
                next_y=ground.height;const double depth=world.waterDepth({nx,next_y,nz});
                if((wet&&depth>water+.10)||(!wet&&depth>.90)){auto result=failed(false,0.,"");result.water=true;return result;}
                const double delta=next_y-previous_y;slope=delta/std::max(.1,run);if(std::abs(slope)>std::abs(largest))largest=slope;
                if(std::abs(delta)>run*max_grade)return failed(false,slope,"planning_grade");}
            for(double offset:{-half_width,0.,half_width}){
                const Point start{position[0]+cosine*offset,position[1]+height,position[2]-sine*offset};
                const Point end{nx+cosine*offset,next_y+height,nz-sine*offset};RayStatus collision;
                try{collision=world.segment(start,end,descriptor.present);}catch(...){return failed(true,slope,"planning_probe_failed",true);}
                if(collision==RayStatus::Failed)return failed(true,slope,"planning_probe_failed",true);
                if(collision==RayStatus::Found)return failed(true,slope,"planning_collision");}
            previous_y=next_y;previous_distance=d;}
        DirectionSample result;result.clear=true;result.slope=largest;return result;
    }catch(...){return failed(true,0.,"",true);}
}
bool probeClear(const DirectionSample&sample)
{return sample.clear&&!sample.collision&&!sample.water&&std::abs(sample.slope)<=max_grade;}
CandidateProbe::CandidateProbe(Grid&grid,Navigator&navigator,CandidateWorld&world,CandidateState&state,CandidateDescriptor descriptor,BakeProof bake):
    grid_(grid),navigator_(navigator),world_(world),state_(state),descriptor_(descriptor),bake_(bake){}
std::optional<bool> CandidateProbe::corridor(double yaw,bool wet,bool shallow,bool hazard_only,std::optional<double>maximum)
{
    if(wet||!bake_.present||bake_.vehicle_half_width<2.15||bake_.maximum_clearance_radius<3.)return {};
    if(!grid_.nearest(grid_.cellFor(state_.position),0))return {};
    double d=std::max(1.,grid_.map.cell_size);if(maximum)d=std::min(d,std::max(0.,*maximum));
    const Point end{state_.position[0]+std::sin(yaw)*d,state_.position[1],state_.position[2]+std::cos(yaw)*d};
    if(grid_.motionHazard(state_.position,end,9u|(shallow?0u:4u)))return false;
    if(hazard_only||grid_.segmentClear(state_.position,end))return true;return {};
}
bool CandidateProbe::direction(double yaw,std::optional<double>maximum,std::optional<double>implicit)
{
    const bool bounded=bool(maximum);if(!maximum)maximum=implicit;
    auto advisory=corridor(yaw,grid_.pointHazard(state_.position,4)||state_.water_depth>.90,
        navigator_.controlledShallowStep(state_.id,state_.position,yaw),false,maximum);
    if(advisory&&!*advisory)return false;if(advisory&&*advisory&&!bounded)return true;
    const double normal=angle(yaw);const auto key=std::make_pair(std::round(normal*1.e4)/1.e4,
        maximum?std::optional<double>(std::round(*maximum*100.)/100.):std::nullopt);
    auto found=samples_.find(key);if(found==samples_.end())found=samples_.emplace(key,probeDirection(world_,state_.position,yaw,state_.speed,descriptor_,maximum)).first;
    return found->second.deferred?!bounded:probeClear(found->second);
}
bool CandidateProbe::pose(double yaw,bool allow_wreck_push)
{
    const double delta=angle(yaw-state_.yaw);
    for(auto i=state_.rejected_rotations.begin();i!=state_.rejected_rotations.end();){const auto&refusal=i->second;
        if(state_.now>=refusal.until||distance(state_.position,refusal.origin)>.08||std::abs(angle(state_.yaw-refusal.yaw))>.02)i=state_.rejected_rotations.erase(i);
        else{if(delta*i->first>0.&&std::abs(delta)>=std::abs(refusal.delta))return false;++i;}}
    if(world_.rotationFraction(state_.id,state_.position,state_.yaw,yaw,allow_wreck_push)<1.)return false;
    if(!grid_.hullPoseClear(state_.position,state_.yaw,state_.half_length,state_.half_width))return true;
    return grid_.hullPoseClear(state_.position,yaw,state_.half_length,state_.half_width);
}
}
