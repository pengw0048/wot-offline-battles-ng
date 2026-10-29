#include "stock_native_destructible_runtime.hpp"

#include <cassert>
#include <iomanip>
#include <iostream>
#include <string>

using namespace stock_native_destructible;
namespace {
class Native:public Backend {
public:
    SnapshotStatus availability=SnapshotStatus::Ready;
    Parameters parameters;
    std::vector<std::string> events;
    unsigned transforms=0;
    bool loaded=true,retire_on_ram=false,invalidate_on_ram=false;
    Runtime * runtime=nullptr;
    std::uint64_t incarnation=1;double replay_pitch=-1.;
    Native() {
        parameters.scale=1.;parameters.height=6.;parameters.mass=100.;parameters.stiffness=300.;parameters.spring_angle=.2;
        parameters.spring_resist=50.;parameters.air_resist=2.;
    }
    bool current()const override{return loaded;}
    SnapshotStatus snapshot(const Destruction &,Parameters & out)override{out=parameters;return availability;}
    bool fallPitch(const Destruction &,double,double & pitch,std::uint32_t & flags)override{pitch=1.5707963267948966;flags=0;return true;}
    bool position(const Body &,double)override{++transforms;return loaded;}
    bool geometry(const Destruction & event,const ChildEffect & callback)override {
        events.push_back("geometry");if(event.kind==Kind::Structure){auto child=event;child.material=74;return callback(child);}return true;
    }
    bool present(const Destruction &,int,PresentationPhase phase)override {
        events.push_back(std::to_string(int(phase)));
        if(retire_on_ram&&phase==PresentationPhase::Ram)runtime->clear();
        if(invalidate_on_ram&&phase==PresentationPhase::Ram)loaded=false;
        return true;
    }
    std::uint64_t generation(const Destruction&)const override{return incarnation;}
    bool replay(const Destruction&,const Body*body)override{if(body)replay_pitch=body->pitch;return true;}
};
Destruction order(Kind kind,int item=1) {Destruction out;out.kind=kind;out.key={1,std::uint32_t(item)};out.material=kind==Kind::Structure?73:-1;out.speed=2.4;out.yaw=.123;out.now=1.;return out;}
void lifecycle() {
    Native native;Runtime runtime(native);native.runtime=&runtime;
    assert(runtime.destroy(order(Kind::Fragile)));
    assert((native.events==std::vector<std::string>{"0","2","3","geometry","7"}));
    assert(!runtime.destroy(order(Kind::Fragile))&&runtime.counters().accepted==1&&runtime.counters().mod_callbacks==0);
    native.events.clear();assert(runtime.destroy(order(Kind::Structure,2)));
    assert((native.events==std::vector<std::string>{"0","3","geometry","0","3"}));
    auto shot=order(Kind::Fragile,3);shot.shot=true;native.events.clear();assert(runtime.destroy(shot));
    assert((native.events==std::vector<std::string>{"0","2","4","7"}));
    runtime.update(.01,1.19);assert(native.events.back()=="7");runtime.update(.01,1.2);assert(native.events.back()=="geometry");
    native.availability=SnapshotStatus::Missing;assert(runtime.destroy(order(Kind::Falling,4)));assert(runtime.counters().queued==1&&runtime.bodies().empty());
    native.availability=SnapshotStatus::Ready;runtime.mappingChanged(10.);assert(runtime.bodies().size()==1&&native.transforms==1);
    const auto first=runtime.bodies()[0];assert(first.simulation_time==.016666&&first.pitch>0.);
    runtime.update(.1,1.3);assert(runtime.bodies()[0].simulation_time>.11&&native.transforms==2);
    assert(runtime.counters().body_steps==6);
    native.availability=SnapshotStatus::Missing;auto queued=order(Kind::Fragile,6);queued.shot=true;assert(runtime.destroy(queued));
    native.availability=SnapshotStatus::Ready;native.events.clear();runtime.mappingChanged(20.);
    assert((native.events==std::vector<std::string>{"0","2","4","7"}));
    runtime.update(0.,20.19);assert(native.events.back()=="7");runtime.update(0.,20.2);assert(native.events.back()=="geometry");
    const auto present_calls=runtime.counters().present_calls;const auto pitch=runtime.bodies()[0].pitch;
    ++native.incarnation;runtime.mappingChanged(20.3);assert(runtime.counters().replays==runtime.counters().accepted&&native.replay_pitch==pitch&&runtime.counters().present_calls==present_calls);
    const auto replays=runtime.counters().replays;runtime.mappingChanged(20.4);assert(runtime.counters().replays==replays);
    runtime.clear();native.retire_on_ram=true;assert(!runtime.destroy(order(Kind::Fragile,5))&&runtime.bodies().empty());
    // close() can synchronously re-enter through the approved stock effect
    // seam. Retirement invalidates current() while the pinned runtime and
    // its containers remain alive; geometry and sound must not follow it.
    Native retired;Runtime retained(retired);retired.runtime=&retained;retired.invalidate_on_ram=true;
    assert(!retained.destroy(order(Kind::Fragile,7)));
    assert((retired.events==std::vector<std::string>{"0","2","3"}));
    assert(retained.counters().accepted==0&&!retained.acceptedData(order(Kind::Fragile,7)));
    assert(!retained.destroy(order(Kind::Fragile,8)));
    retained.mappingChanged(2.);retained.update(.1,2.);
    assert(retired.events.size()==3&&retired.transforms==0&&retained.bodies().empty());
}
}
int main(int argc,char ** argv) {
    std::cout<<std::setprecision(17);
    if(argc==2&&std::string(argv[1])=="body") {
        Body body;double dt;int pending;
        while(std::cin>>body.pitch>>body.pitch_speed>>body.inertia>>body.mass>>body.height>>body.spring_angle>>body.stiffness>>body.spring_resist>>body.air_resist>>body.pitch_constraint>>body.simulation_time>>body.bury_depth>>dt>>pending) {
            body.touchdown_pending=pending;const auto result=moveBody(body,dt);
            std::cout<<result.active<<' '<<result.touchdown<<' '<<body.pitch<<' '<<body.pitch_speed<<' '<<body.simulation_time<<'\n';
        }
        return 0;
    }
    if(argc==2&&std::string(argv[1])=="encode") {double yaw,pitch;while(std::cin>>yaw>>pitch)std::cout<<encodedYaw(yaw)<<' '<<encodedTreePitch(pitch)<<'\n';return 0;}
    lifecycle();std::cout<<"Native destructible runtime tests passed\n";
}
