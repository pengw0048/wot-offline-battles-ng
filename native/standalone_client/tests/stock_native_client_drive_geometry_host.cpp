#include "stock_native_client_drive_geometry.hpp"
#include "stock_native_bot_python.hpp"
#include <cassert>
#include <iostream>
using namespace stock_native_client_drive;
namespace world=stock_native_world;
namespace p=stock_native_bot_python;
struct Scene final:world::Scene {
    bool ready=false;std::uint64_t revision=0;unsigned calls=0;
    world::QueryStatus segment(const Point&a,const Point&b,std::uint16_t,const world::Filter&keep,world::Hit&out)override{
        world::Surface skin{71,0,7,1},floor{0,0,0,0};
        if(ready&&b[1]<=1.&&a[1]>=1.&&(!keep||keep(skin))){out.point={a[0],1.,a[2]};out.normal={0.,1.,0.};out.surface=skin;return world::QueryStatus::Found;}
        if(b[1]<=0.&&a[1]>=0.&&(!keep||keep(floor))){out.point={a[0],0.,a[2]};out.normal={0.,1.,0.};out.surface=floor;return world::QueryStatus::Found;}return world::QueryStatus::Miss;}
    world::QueryStatus material(const Point&,const Point&,const Point&,world::MaterialHit&)override{return world::QueryStatus::Miss;}
    world::QueryStatus water(const Point&,const Point&,double&)override{return world::QueryStatus::Miss;}
    world::Admission refresh(const world::Key&key,world::Registry&r)override{++calls;if(r.isolated(key))return world::Admission::Isolated;if(!ready)return world::Admission::Pending;const auto found=r.baked_instances.find(key);if(found==r.baked_instances.end())return world::Admission::Pending;
        auto value=found->second;value.admission=world::Admission::Admitted;r.put(key,std::move(value));return world::Admission::Admitted;}
    bool unresolved(const world::Key&,std::vector<world::Box>&)override{return false;}
    bool destroy(const world::Destruction&)override{return false;}
    bool treeChunks(const std::vector<world::Box>&,std::vector<std::uint32_t>&)override{return true;}
    bool turretSweep(const world::TurretSweep&)override{return true;}
    std::uint64_t streamRevision()const override{return revision;}
};
void script(PyObject*dict,const char*source){p::Ref result(PyRun_String(source,Py_file_input,dict,dict));}
int main(){Py_Initialize();{
    p::Ref tree_pair(Py_BuildValue("(ii)",1,7)),module_triple(Py_BuildValue("(iii)",1,7,73));
    assert(visibleTreeKey(tree_pair.get())==world::Key(1,7));assert(!visibleTreeKey(module_triple.get()));
    auto sensor=p::dict();p::set(sensor.get(),"__builtins__",p::retain(PyEval_GetBuiltins()));
    script(sensor.get(),"g_offh_destr_contact_bins={}\ng_offh_destr_instances={}\ng_offh_destr_spatial_revision=0\ng_offh_destr_spatial_published_revision=0\ng_offh_destr_spatial_cell_generations={}\n");
    // The DTO retains the cache's original descriptor spelling; the live
    // sensor key uses its normalized resource spelling.
    world::Registry registry;world::Instance value;value.kind=world::Kind::Fragile;value.filename="Exact/Fragile.model";value.descriptor_valid=true;
    world::Box box;box.center={0.,0.,0.};box.axes={{1.,0.,0.},{0.,1.,0.},{0.,0.,1.}};value.boxes={box};registry.putBaked({1,7},value);registry.skin.destroyed.insert({1,7,-1});
    Scene scene;LocalGeometry sync;world::Pipeline pipeline(registry,scene,0);SceneGround ground(pipeline);const std::array<Point,2> region{{{-2.,0.,-2.},{2.,0.,2.}}};
    assert(sync.sync(sensor.get(),registry,scene,region));assert(registry.instances.empty());assert(ground.column(0.,0.,-1.,2.,{})==0.);
    scene.ready=true;++scene.revision;
    script(sensor.get(),"g_offh_destr_contact_bins={(0,0):set([(1,7)])}\ng_offh_destr_instances={(1,7):dict(kind='fragile',filename='exact/fragile.model',boxes=[((0.,0.,0.),((1.,0.,0.),(0.,1.,0.),(0.,0.,1.)),None)])}\ng_offh_destr_spatial_revision=1\ng_offh_destr_spatial_published_revision=1\ng_offh_destr_spatial_cell_generations={(0,0):1}\n");
    assert(sync.sync(sensor.get(),registry,scene,region));assert(registry.instances.count({1,7}));assert(registry.skin.contact_bins.at({0,0}).count({1,7}));assert(ground.column(0.,0.,-1.,2.,{})==0.);
    assert(registry.instances.at({1,7}).filename=="Exact/Fragile.model");
    // Spelling is normalized only for identity comparison, never before
    // forwarding the original filename to the case-sensitive resource cache.
    for(const char* spelling:{"  EXACT\\FRAGILE.MODEL  ","exact/fragile.model"}){
        auto rows=p::value(sensor.get(),"g_offh_destr_instances");p::Ref id(Py_BuildValue("(ii)",1,7));auto* row=PyDict_GetItem(rows.get(),id.get());p::set(row,"filename",p::text(spelling));++scene.revision;
        assert(sync.sync(sensor.get(),registry,scene,region));
        assert(registry.instances.at({1,7}).filename=="Exact/Fragile.model");
    }
    {
        auto rows=p::value(sensor.get(),"g_offh_destr_instances");p::Ref id(Py_BuildValue("(ii)",1,7));auto* row=PyDict_GetItem(rows.get(),id.get());
        for(const char* different:{"exact/other.model","different/fragile.model"}){
            p::set(row,"filename",p::text(different));++scene.revision;bool rejected=false;
            try{sync.sync(sensor.get(),registry,scene,region);}catch(const p::Error&){rejected=true;}
            assert(rejected);
        }
        p::set(row,"filename",p::text("exact/fragile.model"));p::set(row,"kind",p::text("falling"));++scene.revision;bool rejected=false;
        try{sync.sync(sensor.get(),registry,scene,region);}catch(const p::Error&){rejected=true;}
        assert(rejected);p::set(row,"kind",p::text("fragile"));assert(sync.sync(sensor.get(),registry,scene,region));
    }
    const auto calls=scene.calls;assert(!sync.sync(sensor.get(),registry,scene,region));assert(scene.calls==calls);
    script(sensor.get(),"g_offh_destr_spatial_revision=2\ng_offh_destr_spatial_published_revision=2\ng_offh_destr_spatial_cell_generations[(100,100)]=1\n");
    assert(!sync.sync(sensor.get(),registry,scene,region));assert(scene.calls==calls);
    value.kind=world::Kind::Falling;value.filename="exact/column.model";registry.putBaked({1,8},value);
    script(sensor.get(),"g_offh_destr_instances[(1,8)]=dict(kind='falling',filename='exact/column.model',boxes=[((0.,0.,0.),((1.,0.,0.),(0.,1.,0.),(0.,0.,1.)),None)])\ng_offh_destr_contact_bins[(0,0)].add((1,8))\ng_offh_destr_spatial_revision=3\ng_offh_destr_spatial_published_revision=3\ng_offh_destr_spatial_cell_generations[(0,0)]=2\n");
    assert(sync.sync(sensor.get(),registry,scene,region));
    script(sensor.get(),"g_offh_destr_instances[(1,8)]['boxes']=[((10.,0.,0.),((0.,2.,0.),(1.,0.,0.),(0.,0.,1.)),None)]\ng_offh_destr_contact_bins[(0,0)].remove((1,8))\ng_offh_destr_contact_bins[(1,0)]=set([(1,8)])\ng_offh_destr_spatial_revision=4\ng_offh_destr_spatial_published_revision=4\ng_offh_destr_spatial_cell_generations[(0,0)]=3\ng_offh_destr_spatial_cell_generations[(1,0)]=1\n");
    assert(sync.sync(sensor.get(),registry,scene,region));assert(registry.instances.at({1,8}).boxes[0].center[0]==10.);
    assert(registry.instances.at({1,8}).boxes[0].axes[0][1]==2.);assert(!registry.skin.contact_bins.at({0,0}).count({1,8}));assert(registry.skin.contact_bins.at({1,0}).count({1,8}));
    script(sensor.get(),"g_offh_destr_isolated_slots=set([(1,8)])\ng_offh_destr_spatial_cell_generations[(0,0)]=4\n");
    assert(sync.sync(sensor.get(),registry,scene,region));assert(!registry.instances.count({1,8}));
    script(sensor.get(),"g_offh_destr_isolated_slots.clear()\ng_offh_destr_spatial_cell_generations[(0,0)]=5\n");++scene.revision;
    assert(sync.sync(sensor.get(),registry,scene,region));assert(registry.instances.count({1,8}));assert(!registry.skin.isolated_slots.count({1,8}));
    // A pre-existing native quarantine is not owned by the visible mirror.
    registry.skin.isolated_slots.insert({1,7});
    script(sensor.get(),"g_offh_destr_isolated_slots.add((1,7))\ng_offh_destr_spatial_cell_generations[(0,0)]=6\n");
    assert(sync.sync(sensor.get(),registry,scene,region));
    script(sensor.get(),"g_offh_destr_isolated_slots.clear()\ng_offh_destr_spatial_cell_generations[(0,0)]=7\n");
    assert(sync.sync(sensor.get(),registry,scene,region));assert(registry.skin.isolated_slots.count({1,7}));
    scene.ready=false;++scene.revision;script(sensor.get(),"g_offh_destr_instances.clear()\ng_offh_destr_contact_bins.clear()\n");
    assert(sync.sync(sensor.get(),registry,scene,region));assert(!registry.instances.count({1,7}));
    // Initial DTO isolation also belongs to the visible snapshot; streaming
    // may retire it later without retiring independent native quarantines.
    world::Registry cold;value.kind=world::Kind::Falling;cold.putBaked({1,8},value);cold.skin.isolated_slots.insert({1,8});scene.ready=true;
    script(sensor.get(),"g_offh_destr_isolated_slots=set([(1,8)])\ng_offh_destr_spatial_cell_generations[(0,0)]=8\n");
    LocalGeometry seeded;seeded.seedIsolation(sensor.get());assert(seeded.sync(sensor.get(),cold,scene,region));
    script(sensor.get(),"g_offh_destr_isolated_slots.clear()\ng_offh_destr_spatial_cell_generations[(0,0)]=9\n");
    assert(seeded.sync(sensor.get(),cold,scene,region));assert(!cold.skin.isolated_slots.count({1,8}));assert(cold.instances.count({1,8}));
    assert(sync.reuses()==2);std::cout<<"Native player local geometry lifecycle tests passed\n";
}Py_Finalize();}
