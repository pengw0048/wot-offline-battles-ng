#pragma once
#include "stock_native_projectile_cursor.hpp"
namespace stock_native_projectile {
struct Pose {Point position{};double yaw=0,pitch=0,roll=0,turret_yaw=0,gun_pitch=0;int siege=0;};
using Poses=std::map<std::string,Pose>;
struct PoseSample {double at=0;Poses poses;};
class History {
public:
    void sample(double,Poses);
    std::optional<Pose> at(const std::string&,double) const;
    std::optional<std::vector<double>> sweep(const std::string&,double,double) const;
    const std::deque<PoseSample>& samples()const{return samples_;}
    void clear(){samples_.clear();}
private:std::deque<PoseSample> samples_;
};
// A conservative index preserves the incoming record order. Missing poses stay
// in every candidate set, so history failures cannot silently swallow a shell.
class SpatialIndex {
public:
    bool build(const History&,const std::vector<std::string>& records,
        const std::vector<Cursor>&,double now,std::size_t maximum_chords,double presentation_lag=0);
    std::vector<std::string> candidates(const Point&,const Point&,double,double) const;
    void clear();
private:
    bool valid_=false;
    double floor_=0,ceiling_=0;
    std::vector<std::string> records_;
    std::set<std::string> fallback_;
    std::map<std::pair<int,int>,std::set<std::string>> bins_;
};
double angle_delta(double from,double to);
bool expanded_bounds(const Point&,const Point&,const Point&,double);
double point_segment_distance_sq(const Point&,const Point&,const Point&);
}
