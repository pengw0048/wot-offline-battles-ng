#include "stock_native_client_drive.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace stock_native_client_drive;
namespace world=stock_native_world;
struct Scene final:world::Scene {
    struct Layer {double height;Point normal;world::Surface surface;};
    std::vector<Layer> layers;bool failed=false;
    world::QueryStatus segment(const Point&a,const Point&b,std::uint16_t,const world::Filter&keep,world::Hit&out)override{
        if(failed)return world::QueryStatus::Failed;
        for(const auto&layer:layers)if(b[1]<=layer.height&&layer.height<=a[1]&&(!keep||keep(layer.surface))){out.point={a[0],layer.height,a[2]};out.normal=layer.normal;out.surface=layer.surface;return world::QueryStatus::Found;}
        return world::QueryStatus::Miss;}
    world::QueryStatus material(const Point&,const Point&,const Point&,world::MaterialHit&)override{return world::QueryStatus::Miss;}
    world::QueryStatus water(const Point&,const Point&,double&)override{return world::QueryStatus::Miss;}
    world::Admission refresh(const world::Key&,world::Registry&)override{return world::Admission::Admitted;}
    bool unresolved(const world::Key&,std::vector<world::Box>&)override{return false;}
    bool destroy(const world::Destruction&)override{return false;}
    bool treeChunks(const std::vector<world::Box>&,std::vector<std::uint32_t>&)override{return true;}
    bool turretSweep(const world::TurretSweep&)override{return true;}
};
int main(){
    world::Registry registry;Scene scene;world::Pipeline pipeline(registry,scene,0);SceneGround ground(pipeline);
    world::Surface ordinary;ordinary.material=0;
    scene.layers={{1.,{1.,0.,0.},ordinary},{0.,{0.,1.,0.},ordinary}};
    assert(ground.column(0.,0.,-1.,2.,{})==0.);assert(ground.layers().size()==2);
    assert(ground.layers()[0].verdict==GroundVerdict::Wall);
    scene.layers={{1.,{0.,1.,0.},ordinary},{.5,{.5,1.,0.},ordinary},{0.,{0.,1.,0.},ordinary}};
    assert(ground.column(0.,0.,-1.,2.,.2)==.5);assert(ground.layers()[0].verdict==GroundVerdict::AboveFlat);
    world::Surface broken;broken.chunk=1;broken.item=7;broken.material=71;
    world::Surface damaged=broken;damaged.material=87;
    registry.skin.contact_bins[{0,0}].insert({1,7});registry.skin.destroyed.insert({1,7,-1});
    scene.layers={{1.,{0.,1.,0.},broken},{.7,{0.,1.,0.},damaged},{0.,{0.,1.,0.},ordinary}};
    assert(ground.column(0.,0.,-1.,2.,{}).value()==.7);assert(ground.layers().size()==1);
    scene.layers={{1.,{0.,0.,0.},ordinary}};bool malformed=false;try{ground.column(0.,0.,-1.,2.,{});}catch(const std::runtime_error&){malformed=true;}assert(malformed);
    scene.failed=true;bool failed=false;try{ground.column(0.,0.,-1.,2.,{});}catch(const std::runtime_error&){failed=true;}assert(failed);scene.failed=false;
    scene.layers={{20.,{0.,1.,0.},ordinary},{0.,{0.,1.,0.},ordinary}};assert(ground.initial(0.,0.,100.)==0.);
    std::cout<<"Native client support scene tests passed\n";
}
