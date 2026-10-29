#pragma once
#include "stock_native_bot_navigator.hpp"

namespace stock_native_bot_navigation {
enum class RayStatus {Miss,Found,Failed};
struct PlanningRay {RayStatus status=RayStatus::Miss;double height=0.;};
struct DirectionSample {
    bool clear=false,collision=false,water=false,probe_failed=false,deferred=false,slope_valid=true;
    double slope=0.;
    std::string reason;
};
class CandidateWorld {
public:
    virtual ~CandidateWorld()=default;
    // Descriptor-bearing calls use the release navigation filter (ignore
    // proved intact fragile foliage and released BSP skins). Passive calls
    // use the raw world/ground filter. A missing ray is never a made-up plane.
    virtual PlanningRay ground(const Point&,const Point&,bool descriptor)=0;
    virtual RayStatus segment(const Point&,const Point&,bool descriptor)=0;
    virtual double waterDepth(const Point&)=0;
    virtual double rotationFraction(std::int64_t actor,const Point&,double from,
        double to,bool allow_wreck_push)=0;
};
struct CandidateDescriptor {
    bool present=false;
    double collision_half_width=0.;
};
struct RotationRefusal {Point origin{};double yaw=0.,delta=0.,until=0.;};
struct CandidateState {
    std::int64_t id=0;
    Point position{};
    double yaw=0.,speed=0.,now=0.,water_depth=-1.,half_length=0.,half_width=0.;
    std::map<int,RotationRefusal> rejected_rotations;
};
struct BakeProof {double vehicle_half_width=0.,maximum_clearance_radius=0.;bool present=false;};
DirectionSample probeDirection(CandidateWorld&,const Point&,double yaw,double speed,
    const CandidateDescriptor&,std::optional<double> maximum_distance={},std::optional<double> half_width={});
bool probeClear(const DirectionSample&);
class CandidateProbe {
    Grid& grid_;
    Navigator& navigator_;
    CandidateWorld& world_;
    CandidateState& state_;
    CandidateDescriptor descriptor_;
    BakeProof bake_;
    std::map<std::pair<double,std::optional<double>>,DirectionSample> samples_;
public:
    CandidateProbe(Grid&,Navigator&,CandidateWorld&,CandidateState&,CandidateDescriptor,BakeProof);
    // Cache lifetime is one planner decision, never a realized-motion receipt.
    std::optional<bool> corridor(double yaw,bool wet_escape,bool allow_shallow,
        bool hazard_only,std::optional<double> maximum={});
    bool direction(double yaw,std::optional<double> maximum={},std::optional<double> implicit_horizon={});
    bool pose(double yaw,bool allow_wreck_push=false);
    const auto& samples()const{return samples_;}
};
}
