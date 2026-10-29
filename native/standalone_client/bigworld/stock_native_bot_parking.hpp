#pragma once
#include "stock_native_bot_navigation.hpp"

namespace stock_native_bot_parking {
using Point=stock_native_bot_navigation::Point;
struct Plan {
    std::string catalog,map,zone,cell,source,vehicle,side;
    Point point{},face{};
    double radius=2.,clearance=0.;
    bool operator==(const Plan&b)const{return std::tie(catalog,map,zone,cell,point)==std::tie(b.catalog,b.map,b.zone,b.cell,b.point);}
    bool operator!=(const Plan&b)const{return !(*this==b);}
};
struct Candidate {Plan plan;int index=0,priority=0;double centre_distance=0.;};
struct Area {bool circle=false;std::array<double,4> bounds{};double radius=0.;};
struct Catalog {
    bool manual=false;
    std::vector<Candidate> candidates;
    std::map<std::string,Area> areas;
};
struct Occupied {std::optional<int> team;Point point{};double radius=0.;};
bool available(const Point&,double clearance,int team,const std::vector<Point>&excluded,const std::vector<Occupied>&);
class Parking {
    const offline_navigation::MapInput& map_;
    std::array<std::array<int,2>,8> directions_;
public:
    bool usable(int)const;
    Point point(int)const;
    std::map<std::int64_t,Catalog> catalogs;
    Parking(const offline_navigation::MapInput&,std::array<std::array<int,2>,8> directions);
    std::optional<int> closest(const Point&)const;
    bool clear(int,double radius)const;
    std::vector<double> distances(const Point&)const;
    bool displaced(std::int64_t,const Plan&,const Point&,bool airborne,bool overturned)const;
    std::optional<Plan> select(std::int64_t,int team,const Point&origin,const std::vector<Point>&excluded,
        const std::vector<Occupied>&occupied,const std::optional<std::string>&preferred={})const;
};
}
