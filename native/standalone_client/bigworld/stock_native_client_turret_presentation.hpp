#pragma once
#include "stock_native_detached_turret_runtime.hpp"
#include <deque>
#include <memory>
namespace stock_native_client_turret_presentation {
using Point=stock_native_detached_turret::Point;
using Frame=stock_native_detached_turret::Frame;
using Row=stock_native_detached_turret::Row;
struct Tuning {double gravity=0.,flight_step=0.,minimum_delay=0.,initial_delay=0.,delay_decay=0.;};
struct Pose {Point position{},attitude{};std::shared_ptr<const Frame> body;};
Pose poseAt(const Row&,double elapsed,const Tuning&);
class Buffer {
public:
    struct Sample {double time=0.;std::shared_ptr<const Frame> frame;};
private:
    Tuning tuning_;
    std::deque<Sample> samples_;
    std::optional<double> cursor_,last_render_,arrival_;
    double anchor_=0.,delay_=0.;
public:
    explicit Buffer(Tuning value):tuning_(value){}
    bool push(const Row&,double now,double age);
    std::optional<Pose> pose(double now);
    const std::deque<Sample>&samples()const{return samples_;}
    const std::optional<double>&cursor()const{return cursor_;}
    double delay()const{return delay_;}
};
}
