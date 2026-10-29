#include "stock_native_player_contacts.hpp"
#include <cassert>
#include <iostream>
using namespace stock_native_player_contacts;
namespace w=stock_native_world;
struct Fake:World {
    TreeStatus identities[3]={TreeStatus::Other,TreeStatus::Other,TreeStatus::Other};
    w::ContactResult tree_result,proposal,committed;
    unsigned tree_calls=0,proposal_calls=0,commit_calls=0;
    TreeStatus tree(const w::Key& key)override{return identities[key.second-1];}
    w::ContactResult trees(const Token&,const Receipt&,double)override{++tree_calls;return tree_result;}
    w::ContactResult catalog(const Descriptor&,const Receipt&,double,bool commit)override{if(commit){++commit_calls;return committed;}++proposal_calls;return proposal;}
};
Request request(){Request out;auto&r=out.receipt;r.player=1;r.sequence=1;r.token={{1,1,-1}};r.dt=.05;r.end={0.,0.,.1};r.speed=2.;out.descriptor=Descriptor{};return out;}
Token token(unsigned mask){Token out;for(unsigned i=0;i<3;++i)if(mask&(1u<<i))out.emplace(1,i+1,i==2?73:-1);return out;}
w::ContactResult result(int status,unsigned mask,bool pending=false){w::ContactResult out;out.status=static_cast<w::ContactStatus>(status);out.contacts=token(mask);out.pending=pending;return out;}
struct Scene:w::Scene {
    std::vector<w::Destruction> effects;
    w::QueryStatus segment(const Point&,const Point&,std::uint16_t,const w::Filter&,w::Hit&)override{return w::QueryStatus::Miss;}
    w::QueryStatus material(const Point&,const Point&,const Point&,w::MaterialHit&)override{return w::QueryStatus::Miss;}
    w::QueryStatus water(const Point&,const Point&,double&)override{return w::QueryStatus::Miss;}
    w::Admission refresh(const w::Key&,w::Registry&)override{return w::Admission::Pending;}
    bool unresolved(const w::Key&,std::vector<w::Box>&)override{return false;}
    bool destroy(const w::Destruction& event)override{effects.push_back(event);return true;}
    bool treeChunks(const std::vector<w::Box>&,std::vector<std::uint32_t>&)override{return true;}
    bool turretSweep(const w::TurretSweep&)override{return true;}
};
struct Trees:TreeAuthority {
    TreeStatus classify(const w::Key&)override{return TreeStatus::Other;}
    w::ContactResult commit(const Token&,const Point&,const Point&,double,double,double)override{return {};}
};
void add(w::Registry& registry,double y){w::Instance item;item.kind=w::Kind::Fragile;item.admission=w::Admission::Admitted;
    item.filename="exact/prop";item.origin={0.,y,0.};w::Box box;box.center=item.origin;
    box.axes={{{.2,0.,0.}},{{0.,.2,0.}},{{0.,0.,.2}}};item.boxes={box};item.scaled_health[-1]=item.reference_health[-1]=1.;
    item.item_scale=1.;item.descriptor_valid=item.native_name_exact=true;registry.put({1,1},item);}
int main(int argc,char**argv){
    if(argc==2&&std::string(argv[1])=="oracle"){
        unsigned requested,tm,pm,cm;int t0,t1,ts,ps,cs,pending_proposal,pending_commit;
        while(std::cin>>requested>>t0>>t1>>ts>>tm>>ps>>pm>>pending_proposal>>cs>>cm>>pending_commit){
            Fake fake;fake.identities[0]=static_cast<TreeStatus>(t0);fake.identities[1]=static_cast<TreeStatus>(t1);
            fake.tree_result=result(ts,tm);fake.proposal=result(ps,pm,pending_proposal);fake.committed=result(cs,cm,pending_commit);
            auto row=request();row.receipt.token=token(requested);Owner owner;auto values=owner.update({row},1.,fake);
            std::cout<<(values.empty()?-1:values[0].accepted?1:0)<<' '<<fake.tree_calls<<' '<<fake.proposal_calls<<' '<<fake.commit_calls<<'\n';
        }return 0;
    }
    Fake fake;fake.proposal=fake.committed=result(int(w::ContactStatus::Crushed),1);Owner owner;auto row=request();
    auto values=owner.update({row},1.,fake);assert(values.size()==1&&values[0].accepted&&fake.proposal_calls==1&&fake.commit_calls==1);
    const auto serial=values[0].serial;assert(owner.update({row},1.01,fake).size()==1&&fake.proposal_calls==1);
    assert(owner.acknowledge(serial,1.01));assert(owner.update({row},1.25,fake).empty());
    assert(owner.update({row},1.26,fake).size()==1&&fake.commit_calls==1);assert(owner.acknowledge(serial,1.26));
    row.receipt.start[0]=1.;assert(owner.update({row},1.27,fake).size()==1&&fake.commit_calls==2);
    for(int i=2;i<=1026;++i){row.receipt.sequence=i;assert(owner.update({row},2.,fake).size()==1);}
    assert(owner.cached()==1024&&!owner.acknowledge(serial,3.));
    auto malformed=row;malformed.receipt.pitch=.611;assert(owner.update({malformed},3.,fake).empty());
    // Real catalog proposal/commit retains zero physical speed despite a
    // powered eligibility cap, and records only one accepted effect.
    for(bool vertical_rotation:{false,true}){
        w::Registry registry;registry.catalog_available=true;registry.unit_vehicle_mass=1000.;Scene scene;Trees trees;PipelineWorld world(registry,scene,trees);Owner actual;
        auto r=request();r.receipt.speed=0.;r.receipt.end={0.,0.,0.};auto&d=*r.descriptor;
        d.body_minimum={-1.,0.,-2.};d.body_maximum={1.,2.,2.};d.mass=25000.;d.drive_forward=20.;d.drive_reverse=-8.;d.rotation_speed=.7;
        if(vertical_rotation){r.receipt.end={0.,10.,0.};r.receipt.end_yaw=.2;r.receipt.dt=.1;}
        add(registry,vertical_rotation?7.5:1.);auto output=actual.update({r},1.,world);
        assert(output.size()==1&&output[0].accepted&&scene.effects.size()==1);
        if(!vertical_rotation)assert(scene.effects[0].speed==0.);
        assert(actual.update({r},2.,world).size()==1&&scene.effects.size()==1);
    }
    std::cout<<"Native player contacts passed\n";
}
