#pragma once
#include "stock_native_projectile_chord.hpp"
#include "stock_native_world_pipeline.hpp"
namespace stock_native_projectile {
class ShotScene {
    stock_native_world::Registry& registry_;
    stock_native_world::Scene& scene_;
    struct Contact;
    struct Step;
    std::optional<Contact> catalog(const Point&,const Point&,std::optional<double>);
    std::optional<double> registeredExit(const stock_native_world::MaterialHit&,const Point&,const Point&);
    stock_native_world::QueryStatus material(const Point&,const Point&,stock_native_world::MaterialHit&);
    std::optional<stock_native_world::MaterialKey> transparent(const stock_native_world::MaterialHit&,bool trees);
    bool destroy(const stock_native_world::MaterialHit&,double,double,std::vector<stock_native_world::Destruction>&);
    Step one(const Point&,const Point&,const Point&,const damage::FrozenShot&,double,std::vector<stock_native_world::Destruction>&);
public:
    ShotScene(stock_native_world::Registry&r,stock_native_world::Scene&s):registry_(r),scene_(s){}
    SceneResult trace(FlightMeta&,const Point&,const Point&,const Point&,double now,
        damage::Random&,std::vector<stock_native_world::Destruction>&);
};
}
