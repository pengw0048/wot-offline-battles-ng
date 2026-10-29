#pragma once
#include "stock_native_client_drive_runtime.hpp"
namespace stock_native_client_drive {
struct LegacyPlane {step::SupportPlane plane;double pitch=0.,roll=0.,slope=0.,contact_residual=0.;Vec downhill{};};
struct LegacySamples {std::optional<double>highest,center;std::optional<LegacyPlane>plane;};
std::optional<LegacyPlane> sampledPlayerPlane(const std::array<std::optional<double>,5>&,
    double yaw,double length,double width,double maximum_residual);
std::optional<LegacyPlane> sampledPlayerSupport(const std::array<std::optional<double>,5>&,
    double yaw,double length,double width);
struct LegacyImpact {Point position{};double speed=0.;};
// The hydraulic/excluded-trial path retains the player's five-column
// upper-support face and straddle laws. It deliberately does not use the
// worker's different support-repair/continuous-path basic vertical owner.
class LegacySupport {
    stock_native_world::Pipeline&pipeline_;
    const PlayerDescriptor&descriptor_;
    double plane_epsilon_,pitch_limit_;
    struct Sample {double x,z,yaw;std::optional<LegacyPlane>plane;};
    std::optional<Sample>sample_;
    std::optional<LegacyPlane>sampleRaw(const Point&,double yaw);
public:
    LegacySupport(stock_native_world::Pipeline&,const PlayerDescriptor&,double plane_epsilon,double pitch_limit);
    std::optional<double>column(double x,double z,double hint,std::optional<double>maximum={});
    std::optional<double>nearGround(double x,double z,double hint);
    LegacySamples terrain(const PlayerState&,const Point&,double yaw,
        std::optional<double>maximum={},std::optional<double>follow_gap={});
    std::optional<step::SupportPlane>samplePlane(const Point&,double yaw);
    void groundPose(PlayerState&,const Point&,double yaw);
    std::vector<LegacyImpact>vertical(PlayerState&,Point&,double yaw,double elapsed,
        std::optional<Point>tick={});
};
}
