#pragma once
#include "stock_native_world_pipeline.hpp"
#include "stock_native_bot_navigation.hpp"

#include <optional>

namespace stock_native_bot_turret {
using Point=stock_native_world::Point;
using Box=stock_native_world::Box;
struct Bounds {Point minimum{},maximum{};};
struct Owner {
    bool human=false;
    std::int64_t id=0;
    bool operator<(const Owner& other)const{return std::tie(human,id)<std::tie(other.human,other.id);}
    bool operator==(const Owner& other)const{return human==other.human&&id==other.id;}
};
struct Component {Bounds bounds;Point offset{};};
struct MovingDescriptor {
    Component chassis,hull;
    bool pivot_available=false,rotation_around_center=true;
    double track_center=0.;
};
struct DetachedDescriptor {Component turret,gun;};
struct Record {
    Owner owner;
    std::optional<Owner> support;
    std::int64_t motion_sequence=0;
    double created_time_ms=0.,duration=0.;
    bool landed=false,has_body=false;
    Point rest{},attitude{},spin{};
    std::optional<Point> rest_attitude;
};
class DetachedHits {
public:
    virtual ~DetachedHits()=default;
    // Endpoints are already in the exact descriptor component frame and
    // rounded at the original Vector3 boundary. The native BSP owner returns
    // all segment distances, matching localHitTest's ordered result shape.
    virtual std::vector<double> segment(Owner,bool gun,const Point&,const Point&)=0;
};
Box worldBox(const Component&,const Point& position,const Point& attitude);
bool leavesOverlap(const Box& initial,const Box& final,const std::vector<Box>&,
    const Box& obstacle);
std::vector<Box> componentSweeps(const Component&,const Point& start,
    const Point& start_attitude,const Point& end,const Point& end_attitude,
    const std::optional<std::pair<Point,double>>& pivot={});
class Obstacles {
    struct Entry {Record record;DetachedDescriptor descriptor;Point attitude;double radius,settles_at;std::vector<Box> boxes;};
    std::map<Owner,Entry> entries_;
public:
    // Input rows are already decoded at the native event-ingress boundary.
    // A bad/stale row is local; it never invalidates another accepted turret.
    bool put(Record,const DetachedDescriptor&);
    void remove(Owner owner){entries_.erase(owner);}
    bool blocks(const stock_native_world::TurretSweep&,const MovingDescriptor&,
        double server_time_ms)const;
    std::optional<double> block_distance(const Point&,const Point&,double server_time_ms,
        DetachedHits&,std::optional<double> start_time_ms={})const;
    std::vector<stock_native_bot_navigation::Hull> navigation(double server_time_ms)const;
    void clear(){entries_.clear();}
    std::size_t active()const{return entries_.size();}
};
}
