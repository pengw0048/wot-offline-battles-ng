#include "../bigworld/stock_native_player_environment.hpp"
#include <cassert>
#include <cmath>
using namespace stock_native_player_environment;
struct Water:World {double depth=0.;Point sampled{};std::optional<Pose> bodyPose(const Player&)override{return {};};double waterDepth(const Point& p)override{sampled=p;return depth;}};
int main(){Metadata m;m.minimum={-1.,0.,-2.};m.maximum={1.,2.,2.};m.mount={0.,1.,0.};m.crew={"driver","commander"};m.maxima["engineHealth"]=100.;
 Pose pose;auto sample=hullSample(m,pose);assert(sample&&sample->first[1]==1.&&sample->second==2.);pose.roll=1.5707963267948966;sample=hullSample(m,pose);assert(sample&&std::abs(sample->first[1]+1.)<1.e-12&&std::abs(sample->second-2.)<1.e-12);
 Player p;p.id=2;p.input_sequence=7;p.descriptor=m;p.critical.devices["engineHealth"].hp=25.;p.critical.devices["engineHealth"].maximum=100.;p.critical.crew_ko.insert("driver");p.critical.fire=true;
 Owner owner;Water water;assert(!owner.advance(.2,true,{p},water));water.depth=2.;auto b=owner.advance(.1,true,{p},water);assert(b&&b->sequence==1&&b->observations[0].level==1);owner.acknowledge(1,false);water.depth=2.01;b=owner.advance(.31,true,{p},water);assert(b&&b->sequence==1&&b->observations[0].level==2);const auto& q=*b->observations[0].drowning;assert(q.state.fire&&q.state.crew_ko.count("commander")&&q.state.devices.at("engineHealth").destroyed);owner.acknowledge(1,true);assert(owner.sequence()==1&&owner.elapsed()>.009&&owner.elapsed()<.011);
}
