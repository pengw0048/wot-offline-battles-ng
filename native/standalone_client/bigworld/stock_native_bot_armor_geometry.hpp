#pragma once
#include "stock_native_bot_armor_law.hpp"
#include "offline_contact_roster.h"
#include <array>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace stock_native_bot_armor {
using Point=std::array<double,3>;
using XZ=std::array<double,2>;
using Body=offline_contact::Body;
struct Hull {Point minimum{},maximum{},mount{};};
class Geometry {
public:
    virtual ~Geometry()=default;
    virtual bool ready(const Body&)=0;
    // The contact envelope is frozen separately from the actual body matrix
    // used by the native component query (including hydraulic displacement).
    virtual Body physical_body(const Body& body){return body;}
    virtual Point native_point(const Point& point){return point;}
    virtual std::optional<Hull> hull(const Body&)=0;
    virtual std::vector<Layer> segment(const Body&,const Point&,const Point&)=0;
};
struct Plates {std::array<double,2> armor{};std::array<std::vector<std::string>,2> tracks;};
std::vector<XZ> overlap_polygon(const Body&,const Body&);
std::vector<double> sample_heights(double low,double high,double hit);
std::array<double,2> hull_interval(const Hull&,double y,double pitch,double roll);
std::optional<Plates> contact_pair(const Body&,const Body&,const XZ& normal,Geometry&);
}
