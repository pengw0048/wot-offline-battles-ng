#include "stock_native_bot_contacts.hpp"
#include <cassert>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace stock_native_bot_contacts;
namespace {
struct FreeMotion:stock_native_bot_motion::World {
    using State=stock_native_bot_motion::State;
    using Vec=stock_native_bot_motion::Vec;
    using Status=stock_native_bot_motion::MotionStatus;
    double rotation_fraction(const State&,const Descriptor&,const Point&,double,double,const Vec&)override{return 1.;}
    bool pose_progress(const State&,const Point&,double,const Point&,double)override{return true;}
    bool turret_clear(const State&,const Point&,double,const Point&,double)override{return true;}
    bool rotation(const State&,const Descriptor&,const Point&,double,double,double,double,double,const Vec&)override{return true;}
    bool corridor(const State&,double,bool,bool,const std::optional<double>&)override{return true;}
    Status motion(const State&,const Descriptor&,const Point&,double,double,double,double,bool,bool)override{return Status::Clear;}
    void impact(State&,double,double)override{}
    void invalidate(ID,double,bool)override{}
    void report_contact(const State&,const Point&,const Point&,double,double)override{}
    void motion_finished(const State&,Status,double,double)override{}
    Vec slide(const State&,const Descriptor&,const Vec& move,double& f)override{f=1.;return move;}
    bool vertical(State&,const Descriptor&,const Point&,double,double)override{return false;}
};
struct FreeWorld:World {
    FreeMotion motion;
    int proofs=0;
    stock_native_bot_motion::World& motion_world(ID)override{return motion;}
    std::optional<Armor> armor(const Body&,const Body&,const std::array<double,3>&)override
    {++proofs;Armor a;a.value={{10.,10.}};a.tracks[0]={"leftTrack"};return a;}
};
void body(std::istream& in,Body& b)
{
    b.flags=offline_contact::Alive|offline_contact::Impulse|offline_contact::HasY;
    in>>b.id>>b.x>>b.y>>b.z>>b.yaw>>b.pitch>>b.roll;
    for(auto& v:b.shape)in>>v;
}
void scenarios()
{
    Descriptor descriptor;descriptor.drive.mass=25000.;descriptor.collision_shape={{1.5,3.5,-.8,2.}};
    descriptor.half_width=1.5;descriptor.half_length=3.5;
    for(auto& value:descriptor.drive.terrain)value=1.;descriptor.drive.t[0]=9.81;
    descriptor.side_full_y=1.;descriptor.side_min_y=0.;descriptor.side_full=1.;descriptor.side_min=1.;
    Motion first,second;first.id=1;second.id=2;
    first.position={{0.,0.,0.}};second.position={{0.,0.,6.5}};
    first.speed=12.;first.grounded_once=second.grounded_once=true;
    std::map<ID,Actor> actors;
    actors[1].motion=&first;actors[2].motion=&second;
    actors[1].descriptor=actors[2].descriptor=&descriptor;actors[1].team=1;actors[2].team=2;
    FreeWorld world;Contacts contacts(1000000);
    auto reports=contacts.settle(actors,{},python27_integer_order({1,2}),0.,4.,world);
    assert(reports.size()==1&&reports[0].bot==1&&reports[0].target==2&&reports[0].damage_bot>0);
    assert(world.proofs==1&&contacts.active_contacts().count({1,2}));
    assert(contacts.settle(actors,{},python27_integer_order({1,2}),0.,4.1,world).empty());
    assert(contacts.push_proofs().at({false,2}).attacker==1);
    // Cumulative momentum remains independent of an unavailable armour proof.
    Human human;human.network_id=3;human.body.id=1000003;human.body.team=2;
    human.body.shape=descriptor.collision_shape;human.body.mass=25000.;human.body.flags=offline_contact::Alive|offline_contact::HasY;
    human.body.x=100.;human.checkpoint_field_present=true;
    human.checkpoints={{1,7,{{50000.,25000.,100.,100.,0.}}}};
    const double old=first.speed;
    contacts.consume(actors,{human},world,5.);
    assert(first.speed==old+1.&&first.push_x==2.&&actors[1].push_acks.at(3).sequence==7);
    contacts.consume(actors,{human},world,5.);
    assert(first.speed==old+1.&&first.push_x==2.);
    human.checkpoints[0].sequence=8;human.checkpoints[0].totals[0]+=25000.;
    contacts.consume(actors,{human},world,5.);assert(first.push_x==3.);
    human.checkpoints.push_back(human.checkpoints[0]);
    contacts.consume(actors,{human},world,5.);assert(first.push_x==3.);
    // A current dead body terminates a historical transaction and retries the
    // same report sequence until the server high-water acknowledgement arrives.
    first.alive=false;first.push_x=first.push_z=0.;second.position[2]=100.;
    human.checkpoints.clear();Receipt receipt;receipt.sequence=19;receipt.bot=1;
    human.receipts={receipt};
    reports=contacts.settle(actors,{human},python27_integer_order({1,2,1000003}),0.,6.,world);
    assert(reports.size()==1&&reports[0].player_receipt==19&&reports[0].damage_bot==0);
    auto retry=contacts.settle(actors,{human},python27_integer_order({1,2,1000003}),0.,7.,world);
    assert(retry.size()==1&&retry[0].sequence==reports[0].sequence);
    human.acknowledged_ram_sequence=19;
    assert(contacts.settle(actors,{human},python27_integer_order({1,2,1000003}),0.,8.,world).empty());
    std::cout<<"ok\n";
}
}
int main(int argc,char**)
{
    if(argc>1){scenarios();return 0;}
    std::cout<<std::setprecision(17);std::string line;
    while(std::getline(std::cin,line)) {
        std::istringstream in(line);int op;in>>op;
        if(op==0) {
            double speed,ma,mb,aa,ab;Profile a,b;bool moving_a,moving_b;
            in>>speed>>ma>>mb>>aa>>ab>>a.spall>>b.spall>>a.bonus>>b.bonus>>moving_a>>moving_b;
            auto result=damage(speed,ma,mb,aa,ab,a,b,moving_a,moving_b);
            std::cout<<'['<<result[0]<<','<<result[1]<<"]\n";
        } else if(op==1) {
            Body a,b;body(in,a);body(in,b);Point p;for(auto& v:p)in>>v;
            auto hit=overlap(a,b);std::cout<<'[';
            if(hit)std::cout<<'['<<(*hit)[0]<<','<<(*hit)[1]<<','<<(*hit)[2]<<']';else std::cout<<"null";
            std::cout<<','<<vertical_overlap(a,b)<<','<<contains(a,p,.75)<<"]\n";
        } else if(op==2) {
            std::size_t n;in>>n;std::vector<ID> input(n);for(auto& v:input)in>>v;
            const auto result=python27_integer_order(input);std::cout<<'[';
            for(std::size_t i=0;i<result.size();++i){if(i)std::cout<<',';std::cout<<result[i];}
            std::cout<<"]\n";
        }
        if(!in){std::cerr<<"Malformed contact input\n";return 2;}
    }
}
