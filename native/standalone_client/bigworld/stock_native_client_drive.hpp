#pragma once
#include "stock_native_bot_step_state.hpp"
#include "stock_native_world_pipeline.hpp"
#include <cstdint>

namespace stock_native_client_drive {
namespace step=wot::offline_bot_step;
using Point=step::Point;
// The visible player's sampled support has a separate owner from Bot driving.
// Its horizontal control/contact order remains the exact player adapter's order.
enum class GroundVerdict {OutsideBand,Wall,AboveFlat,Support};
struct GroundLayer {double height=0.,up=0.;GroundVerdict verdict=GroundVerdict::Support;};
class Ground {
public:
    virtual ~Ground()=default;
    virtual std::optional<double> initial(double x,double z,double hint)=0;
    virtual std::optional<double> column(double x,double z,double low,double high,
        std::optional<double> flat_high)=0;
    virtual std::vector<GroundLayer> layers()const{return {};}
};
class SceneGround final:public Ground {
    stock_native_world::Pipeline& pipeline_;
    std::vector<GroundLayer> layers_;
public:
    explicit SceneGround(stock_native_world::Pipeline& value):pipeline_(value){}
    std::vector<GroundLayer> layers()const override{return layers_;}
    std::optional<double> initial(double,double,double)override;
    std::optional<double> column(double,double,double,double,std::optional<double>)override;
};
struct Input {
    step::VerticalState state;
    Point tick_position{};
    std::optional<Point> motion_position;
    double elapsed=0.,turret_yaw=0.,height_delta=0.,speed_delta=0.;
};
struct Probe {double x=0.,z=0.,low=0.,high=0.;std::optional<double> direct,retained,flat;std::vector<GroundLayer> layers;};
struct Output {
    step::VerticalState state;
    step::VerticalOutcome outcome;
    std::vector<step::SuspensionContact> contacts;
    std::optional<step::SupportPlane> plane;
    double plane_residual=0.;std::size_t plane_contacts=0;
    std::vector<Probe> probes;
};
struct Statistics {std::uint64_t steps=0,columns=0,python_business_callbacks=0;};
class Owner {
    step::SuspensionDescriptor descriptor_;
    step::VerticalMemory memory_;
    Statistics statistics_;
public:
    explicit Owner(step::SuspensionDescriptor);
    Output advance(const Input&,Ground&);
    std::array<Point,2> region(const Input&)const;
    const step::VerticalMemory& memory()const{return memory_;}
    void memory(step::VerticalMemory value){memory_=std::move(value);}
    const Statistics& statistics()const{return statistics_;}
};
}
