#include "stock_native_world_pipeline.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

using namespace stock_native_world;
namespace g=offline_destructible;
namespace {
Box box(double x,double z=0.,int material=-1) {
    Box out;out.center={{x,1.,z}};out.axes={{{.4,0.,0.}},{{0.,1.,0.}},{{0.,0.,.4}}};out.material=material;return out;
}
Instance instance(Kind kind,double x,double health=1.,int material=-1) {
    Instance out;out.kind=kind;out.admission=Admission::Admitted;out.filename="exact/model";
    out.boxes={box(x,0.,material)};out.origin={{x,0.,0.}};out.scaled_health[material]=health;out.reference_health[material]=health;
    out.item_scale=1.;out.descriptor_valid=true;out.native_name_exact=true;out.tree_health=10.;return out;
}
class NativeScene:public Scene {
public:
    struct Plane {double x;Surface surface;};
    std::vector<Plane> planes;
    std::vector<Destruction> effects;
    std::vector<Key> refreshed;
    std::vector<std::uint32_t> chunks;
    std::vector<Box> tree_sweeps;
    std::map<Key,Instance> admitted;
    bool query_failure=false,turret_clear=true,destroy_accept=true,add_replacement=false;
    unsigned queries=0,material_queries=0;
    QueryStatus segment(const Point & a,const Point & b,std::uint16_t,const Filter & filter,Hit & out) override {
        ++queries;if(query_failure)return QueryStatus::Failed;
        if(a[0]==b[0]&&a[2]==b[2]) {
            if(a[1]>=0.&&b[1]<=0.){out={};out.point={{a[0],0.,a[2]}};out.normal={{0.,1.,0.}};return QueryStatus::Found;}
            return QueryStatus::Miss;
        }
        bool found=false;double nearest=2.;
        for(const auto & plane:planes) {
            if(b[0]==a[0])continue;
            const double t=(plane.x-a[0])/(b[0]-a[0]);if(t<0.||t>1.||t>nearest)continue;
            if(filter&&!filter(plane.surface))continue;
            found=true;nearest=t;out={};out.surface=plane.surface;
            for(unsigned i=0;i<3;++i)out.point[i]=float(a[i]+(b[i]-a[i])*t);
            out.normal={{-1.,0.,0.}};
        }
        return found?QueryStatus::Found:QueryStatus::Miss;
    }
    QueryStatus material(const Point &,const Point &,const Point & near,MaterialHit & out) override {
        ++material_queries;
        for(const auto & plane:planes)if(std::abs(plane.x-near[0])<.01&&plane.surface.material>=71&&plane.surface.material<=86) {
            out={};out.hit.point=near;out.hit.normal={{-1.,0.,0.}};out.hit.surface=plane.surface;out.filename="exact/model";return QueryStatus::Found;
        }
        return QueryStatus::Miss;
    }
    QueryStatus water(const Point &,const Point &,double & value) override {value=-1.;return QueryStatus::Miss;}
    Admission refresh(const Key & key,Registry & registry) override {
        refreshed.push_back(key);const auto row=admitted.find(key);if(row==admitted.end())return Admission::Pending;
        registry.put(key,row->second);return row->second.admission;
    }
    bool unresolved(const Key &,std::vector<Box> &) override {return false;}
    bool destroy(const Destruction & order) override {
        if(!destroy_accept)return false;effects.push_back(order);
        if(add_replacement)planes.push_back({2.05,{87,0,order.key.second,order.key.first}});
        return true;
    }
    bool treeChunks(const std::vector<Box> & boxes,std::vector<std::uint32_t> & out) override {tree_sweeps=boxes;out=chunks;return true;}
    bool turretSweep(const TurretSweep &) override {return turret_clear;}
};
ActorInputs actor() {ActorInputs out;out.mass=1000.;out.speed=20.;out.now=1.;out.commit_enabled=true;return out;}
g::Pose pose() {g::Pose out;out.position={{0.,0.,0.}};out.minimum={{-1.,0.,-2.}};out.maximum={{1.,2.,2.}};return out;}
void filter_and_events() {
    Registry registry;NativeScene scene;Pipeline pipeline(registry,scene,0);pipeline.actor(actor());registry.catalog_available=true;registry.unit_vehicle_mass=1000.;
    registry.put({1,7},instance(Kind::Fragile,2.));scene.planes={{2.,{73,0,7,1}}};
    Hit hit;assert(pipeline.horizontal({{0.,1.,0.}},{{4.,1.,0.}},hit)==QueryStatus::Found);
    bool crushed=false;assert(pipeline.resolve({{0.,1.,0.}},{{4.,1.,0.}},hit,crushed,true)==1);
    assert(crushed&&scene.effects.size()==1&&registry.journal.size()==1);
    assert(pipeline.horizontal({{0.,1.,0.}},{{4.,1.,0.}},hit)==QueryStatus::Miss);
    assert(!pipeline.commit({1,7,-1},{{2.,1.,0.}},20.));
    scene.planes.push_back({3.,{73,0,7,2}});
    assert(pipeline.horizontal({{0.,1.,0.}},{{4.,1.,0.}},hit)==QueryStatus::Found&&hit.surface.chunk==2);
    scene.planes={{2.,{87,0,7,1}}};assert(pipeline.horizontal({{0.,1.,0.}},{{4.,1.,0.}},hit)==QueryStatus::Found);
    registry.acknowledge(registry.journal[0].sequence);assert(registry.journal.empty());assert(registry.destroyed({1,7,-1}));
}
void backing_and_replacement() {
    for(bool replacement:{false,true}) {
        Registry registry;NativeScene scene;Pipeline pipeline(registry,scene,0);pipeline.actor(actor());registry.catalog_available=true;registry.unit_vehicle_mass=1000.;
        registry.put({1,7},instance(Kind::Fragile,2.));scene.planes={{2.,{73,0,7,1}}};
        if(!replacement)scene.planes.push_back({2.1,{0,0,99,1}});else scene.add_replacement=true;
        Hit hit;assert(pipeline.horizontal({{0.,1.,0.}},{{4.,1.,0.}},hit)==QueryStatus::Found);
        bool crushed=false;assert(pipeline.resolve({{0.,1.,0.}},{{4.,1.,0.}},hit,crushed,true)==0);
        assert(crushed&&scene.effects.size()==1&&registry.destroyed({1,7,-1}));
    }
}
void kinetic_and_cold() {
    Registry registry;NativeScene scene;Pipeline pipeline(registry,scene,0);auto inputs=actor();inputs.speed=0.;inputs.allow_kinetic=true;inputs.has_kinetic_speed=true;inputs.kinetic_speed=20.;pipeline.actor(inputs);
    registry.catalog_available=true;registry.unit_vehicle_mass=1000.;
    auto item=instance(Kind::Fragile,2.);registry.putBaked({1,7},item);scene.admitted[{1,7}]=item;scene.planes={{2.,{73,0,7,1}}};
    Hit hit;assert(pipeline.horizontal({{0.,1.,0.}},{{4.,1.,0.}},hit)==QueryStatus::Found);
    bool crushed=false;assert(pipeline.resolve({{0.,1.,0.}},{{4.,1.,0.}},hit,crushed,false)==2);
    assert(!crushed&&scene.effects.empty()&&scene.refreshed.size()==1);
    scene.planes.push_back({2.1,{0,0,99,1}});assert(pipeline.resolve({{0.,1.,0.}},{{4.,1.,0.}},hit,crushed,false)==0);
    scene.query_failure=true;assert(pipeline.horizontal({{0.,1.,0.}},{{4.,1.,0.}},hit)==QueryStatus::Failed);
}
void catalog_proposal_and_siblings() {
    Registry registry;NativeScene scene;Pipeline pipeline(registry,scene,0);pipeline.actor(actor());registry.catalog_available=true;registry.unit_vehicle_mass=1000.;
    registry.put({1,7},instance(Kind::Fragile,0.));registry.put({1,8},instance(Kind::Fragile,.5,10000.));
    auto physical=pose();const auto proposal=pipeline.catalogMotion(physical,20.,.1,true,&physical,false,true);
    assert(proposal.status==ContactStatus::Hard&&proposal.requires_commit&&proposal.contacts.count({1,7,-1}));
    assert(scene.effects.empty()&&registry.journal.empty());
    const auto committed=pipeline.catalogMotion(physical,20.,.1,true,&physical);
    assert(committed.status==ContactStatus::Hard&&committed.accepted_now&&scene.effects.size()==1);
    assert(registry.destroyed({1,7,-1})&&!registry.destroyed({1,8,-1}));
}
void dependencies() {
    Registry registry;NativeScene scene;Pipeline pipeline(registry,scene,0);pipeline.actor(actor());
    auto structure=instance(Kind::Structure,0.,1.,73);
    structure.reference_health={{73,1.},{74,100.},{75,50.},{76,1.}};
    structure.dependencies={{73,{74}}};structure.inverse_dependencies={{74,{75,76}}};
    registry.put({1,7},structure);assert(pipeline.commit({1,7,73},{{0.,1.,0.}},20.));
    assert(registry.destroyed({1,7,73})&&registry.destroyed({1,7,74})&&registry.destroyed({1,7,76}));
    assert(!registry.destroyed({1,7,75})&&scene.effects.size()==3);
    assert(scene.effects[0].material==73&&scene.effects[1].material==74&&scene.effects[2].material==76);
}
void trees_and_turrets() {
    Registry registry;NativeScene scene;Pipeline pipeline(registry,scene,0);pipeline.actor(actor());
    auto tree=instance(Kind::Tree,0.);registry.put({1,7},tree);registry.ready_tree_chunks.insert(1);scene.chunks={1};
    g::Chunk chunk;g::Item item;item.index=7;item.position={{0.,0.,0.}};item.tree=item.named=true;chunk.items[7]=item;chunk.origins[{0,0}]={7};registry.geometry.chunk(1,chunk);
    const auto before=pipeline.trees({{0.,0.,-1.}},0.,{{0.,0.,1.}},0.,{{-1.,0.,-1.}},{{1.,2.,1.}},2.,.2,0.,false);
    assert(before.status==ContactStatus::Crushed&&before.requires_commit&&scene.effects.empty());
    const auto after=pipeline.trees({{0.,0.,-1.}},0.,{{0.,0.,1.}},0.,{{-1.,0.,-1.}},{{1.,2.,1.}},2.,.2,0.,true,&before.contacts);
    assert(after.status==ContactStatus::Crushed&&after.accepted_now&&scene.effects.size()==1&&scene.effects[0].yaw==0.);
    assert(pipeline.trees({{0.,0.,0.}},0.,{{0.,0.,0.}},0.,{{-1.,0.,-1.}},{{1.,2.,1.}},2.,.3).status==ContactStatus::Hard);
    MotionRequest motion;TurretSweep sweep;motion.turret=&sweep;scene.turret_clear=false;
    const auto blocked=pipeline.resolveMotion(motion);assert(blocked.turret_blocked&&blocked.status==ContactStatus::Hard);
}
void tree_vector_boundaries() {
    Registry registry;NativeScene scene;Pipeline pipeline(registry,scene,0);pipeline.actor(actor());
    MotionRequest request;const Point raw{{12345.678951,0.,23456.789052}};request.geometry_position=raw;
    for(unsigned i=0;i<3;++i)request.world.position[i]=float(raw[i]);
    request.world.yaw=.7;request.world.speed=2.1;request.world.dt=.04;
    request.world.bounds[0]=-1.;request.world.bounds[1]=1.;request.world.bounds[2]=request.world.bounds[3]=2.;request.world.heights[0]=0.;request.world.heights[1]=2.;
    request.body_minimum=request.hull_minimum={{-1.,0.,-2.}};request.body_maximum=request.hull_maximum={{1.,2.,2.}};
    assert(pipeline.resolveMotion(request).status==ContactStatus::Clear);
    const double distance=request.world.speed*request.world.dt;
    const Point start{{float(raw[0]),float(raw[1]),float(raw[2])}},end{{float(raw[0]+std::sin(.7)*distance),0.,float(raw[2]+std::cos(.7)*distance)}};
    const auto expected=g::tree_sweep(start,.7,end,.7,request.hull_minimum,request.hull_maximum,0.);
    assert(expected.size()==scene.tree_sweeps.size());for(unsigned i=0;i<expected.size();++i)assert(expected[i].center==scene.tree_sweeps[i].center&&expected[i].axes==scene.tree_sweeps[i].axes);
}
void occupied_scan_without_interval() {
    Registry registry;NativeScene scene;Pipeline pipeline(registry,scene,0);pipeline.actor(actor());
    auto tree=instance(Kind::Tree,0.);auto column=instance(Kind::Falling,.5);column.boxes.clear();
    registry.put({1,7},tree);registry.put({1,8},column);scene.admitted[{1,7}]=tree;scene.admitted[{1,8}]=column;
    registry.ready_tree_chunks.insert(1);g::Chunk chunk;
    for(int index:{7,8}) {g::Item item;item.index=index;item.position={{index==7?0.:.5,0.,0.}};item.tree=index==7;item.named=true;
        chunk.items[index]=item;chunk.origins[{0,0}].push_back(index);}
    registry.geometry.chunk(1,chunk);auto body=pose();body.minimum={{-1.,0.,-1.}};body.maximum={{1.,2.,1.}};
    assert(pipeline.occupiedScan(body,.99,{1}).status==ContactStatus::Clear&&scene.effects.empty());
    const auto result=pipeline.occupiedScan(body,-1.,{1});assert(result.status==ContactStatus::Crushed&&result.contacts.size()==2&&scene.effects.size()==2);
    assert(scene.effects[0].yaw==3.14159265358979323846&&scene.effects[1].yaw==3.14159265358979323846);
    assert(pipeline.occupiedScan(body,-1.,{1}).status==ContactStatus::Clear&&scene.effects.size()==2);
}
void powered_cap_preserves_real_speed() {
    for(bool spawn:{false,true}){
        Registry registry;NativeScene scene;Pipeline pipeline(registry,scene,0);auto inputs=actor();inputs.speed=0.;inputs.has_kinetic_speed=true;inputs.kinetic_speed=20.;pipeline.actor(inputs);registry.catalog_available=true;registry.unit_vehicle_mass=1000.;
        registry.put({1,7},instance(spawn?Kind::Fragile:Kind::Falling,0.));auto physical=pose();
        const auto result=pipeline.catalogMotion(physical,0.,.04,true,&physical,spawn);
        assert(result.status==ContactStatus::Crushed&&result.used_kinetic_speed&&scene.effects.size()==1&&scene.effects[0].speed==0.);
    }
}
}
int main(int argc,char ** argv) {
    if(argc==2&&std::string(argv[1])=="kinetic") {
        int kind;double mass,speed,health,correction,unit;
        while(std::cin>>kind>>mass>>speed>>health>>correction>>unit) {
            Registry registry;NativeScene scene;Pipeline pipeline(registry,scene,0);auto inputs=actor();inputs.mass=mass;pipeline.actor(inputs);registry.unit_vehicle_mass=unit;
            auto row=instance(kind==0?Kind::Structure:kind==1?Kind::Fragile:Kind::Falling,0.,health,kind==0?73:-1);row.kinetic_correction=correction;registry.put({1,7},row);
            std::cout<<pipeline.crushable({1,7,kind==0?73:-1},speed)<<'\n';
        }
        return 0;
    }
    filter_and_events();backing_and_replacement();kinetic_and_cold();catalog_proposal_and_siblings();dependencies();trees_and_turrets();tree_vector_boundaries();occupied_scan_without_interval();powered_cap_preserves_real_speed();
    std::cout<<"Native world pipeline tests passed\n";
}
