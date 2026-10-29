#include "stock_native_projectile_scene.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
using namespace stock_native_projectile;
namespace w=stock_native_world;
struct Rng:damage::Random{int draws=0;double gauss(double mean,double)override{++draws;return mean;}double uniform()override{++draws;return .5;}};
struct Scene:w::Scene {
    struct Surface {double z;w::MaterialHit hit;bool dynamic=false;};
    std::vector<Surface> surfaces;std::vector<w::Destruction> accepted;
    bool failed=false;
    w::QueryStatus query(const Point&a,const Point&b,const w::Filter&keep,bool material,w::MaterialHit&out){
        if(failed)return w::QueryStatus::Failed;
        const Surface* best=nullptr;for(const auto&s:surfaces)if((material||!s.dynamic)&&s.z>=a[2]-1e-7&&s.z<=b[2]+1e-7&&(!keep||keep(s.hit.hit.surface))&&(!best||s.z<best->z))best=&s;
        if(!best)return w::QueryStatus::Miss;out=best->hit;return w::QueryStatus::Found;
    }
    w::QueryStatus segment(const Point&a,const Point&b,std::uint16_t,const w::Filter&keep,w::Hit&out)override{w::MaterialHit value;auto q=query(a,b,keep,false,value);out=value.hit;return q;}
    w::QueryStatus material(const Point&a,const Point&b,const Point&near,w::MaterialHit&out)override{if(failed)return w::QueryStatus::Failed;const Surface*best=nullptr;for(const auto&s:surfaces)if(s.z>=a[2]-1e-7&&s.z<=b[2]+1e-7&&(!best||std::abs(s.z-near[2])<std::abs(best->z-near[2])))best=&s;if(!best)return w::QueryStatus::Miss;out=best->hit;return w::QueryStatus::Found;}
    w::QueryStatus materialFiltered(const Point&a,const Point&b,const Point&,const w::Filter&keep,w::MaterialHit&out)override{return query(a,b,keep,true,out);}
    w::QueryStatus water(const Point&,const Point&,double&)override{return w::QueryStatus::Miss;}
    w::Admission refresh(const w::Key&,w::Registry&)override{return w::Admission::Admitted;}
    bool unresolved(const w::Key&,std::vector<w::Box>&)override{return false;}
    bool destroy(const w::Destruction&d)override{accepted.push_back(d);return true;}
    bool treeChunks(const std::vector<w::Box>&,std::vector<std::uint32_t>&)override{return true;}
    bool turretSweep(const w::TurretSweep&)override{return true;}
    void add(w::Registry&r,unsigned id,w::Kind kind,double z,double health,bool dynamic=false,const char*filename="exact/model"){
        w::Instance item;item.kind=kind;item.admission=w::Admission::Admitted;item.descriptor_valid=item.native_name_exact=true;item.filename=filename;item.tree_health=health;item.scaled_health[-1]=health;item.item_scale=1;
        w::Box box;box.center={0,0,z};box.axes={{1,0,0},{0,1,0},{0,0,.25}};item.boxes={box};r.put({1,id},item);
        Surface s;s.z=z-.25;s.dynamic=dynamic;s.hit.filename=filename;s.hit.hit.point={0,0,s.z};s.hit.hit.normal={0,0,-1};s.hit.hit.surface={71,0,id,1};surfaces.push_back(s);
    }
    void wall(double z){Surface s;s.z=z;s.hit.hit.point={0,0,z};s.hit.hit.normal={0,0,-1};s.hit.hit.surface={1,0,0,0};surfaces.push_back(s);}
};
FlightMeta shot(const char*kind="ARMOR_PIERCING"){FlightMeta f;f.shot.shell.kind=kind;f.shot.shell.piercing_near=100;f.shot.shell.piercing_far=100;f.shot.shell.maximum_distance=100;f.shot.penetration_factor=1;return f;}
int main(){
    {w::Registry registry;registry.catalog_available=true;Scene scene;scene.add(registry,1,w::Kind::Tree,2,10);scene.wall(4);ShotScene query(registry,scene);Rng rng;auto f=shot("HIGH_EXPLOSIVE");std::vector<w::Destruction> events;const auto result=query.trace(f,{0,0,0},{0,0,10},{0,0,1},1,rng,events);assert(std::abs(result.world_distance-4)<1e-6&&events.empty()&&scene.accepted.empty()&&f.piercing_loss==0);}
    {w::Registry registry;registry.catalog_available=true;Scene scene;scene.add(registry,2,w::Kind::Fragile,2,19);scene.wall(4);ShotScene query(registry,scene);Rng rng;auto f=shot();std::vector<w::Destruction> events;const auto result=query.trace(f,{0,0,0},{0,0,10},{0,0,1},1,rng,events);assert(std::abs(result.world_distance-4)<1e-5&&events.size()==1&&events[0].shot&&f.piercing_loss==25&&registry.journal.empty());}
    for(const char*kind:{"HIGH_EXPLOSIVE","HOLLOW_CHARGE"}){w::Registry registry;registry.catalog_available=true;Scene scene;scene.add(registry,2,w::Kind::Fragile,2,1);ShotScene query(registry,scene);Rng rng;auto f=shot(kind);std::vector<w::Destruction> events;const auto result=query.trace(f,{0,0,0},{0,0,10},{0,0,1},1,rng,events);assert(result.stopped_by_destructible&&result.stop_reason=="shell_family"&&events.size()==1&&f.piercing_loss==0);}
    {w::Registry registry;registry.catalog_available=true;Scene scene;scene.add(registry,3,w::Kind::Fragile,2,19,true);scene.wall(5);ShotScene query(registry,scene);Rng rng;auto f=shot();std::vector<w::Destruction> events;const auto result=query.trace(f,{0,0,0},{0,0,10},{0,0,1},1,rng,events);assert(std::abs(result.world_distance-5)<1e-5&&events.size()==1&&f.piercing_loss==25);}
    {w::Registry registry;registry.catalog_available=true;Scene scene;scene.add(registry,4,w::Kind::Falling,2,100,true,"content/environment/env413_streetlamp/normal/lod0/pole.model");scene.wall(5);ShotScene query(registry,scene);Rng rng;auto f=shot("HIGH_EXPLOSIVE");std::vector<w::Destruction> events;const auto result=query.trace(f,{0,0,0},{0,0,10},{0,0,1},1,rng,events);assert(std::abs(result.world_distance-5)<1e-5&&events.size()==1&&f.piercing_loss==0);}
    {w::Registry registry;registry.catalog_available=true;Scene scene;scene.add(registry,5,w::Kind::Fragile,2,19);ShotScene query(registry,scene);Rng rng;auto f=shot();f.shot.shell.piercing_near=f.shot.shell.piercing_far=25;std::vector<w::Destruction> events;const auto result=query.trace(f,{0,0,0},{0,0,10},{0,0,1},1,rng,events);assert(result.stop_reason=="penetration_exhausted"&&std::abs(result.world_distance-1.75)<1e-6&&events.size()==1);}
    {w::Registry registry;Scene scene;scene.failed=true;ShotScene query(registry,scene);Rng rng;auto f=shot();std::vector<w::Destruction> events;bool failed=false;try{query.trace(f,{0,0,0},{0,0,10},{0,0,1},1,rng,events);}catch(const std::runtime_error&){failed=true;}assert(failed&&events.empty());}
    std::cout<<"Native projectile scene boundary tests passed\n";
}
