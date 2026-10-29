#include "../bigworld/stock_native_bot_environment.hpp"
#include <cassert>
using namespace wot::offline_bot_step;
struct World:ImpactWorld {
 int material=0,interior=0;
 std::vector<Point> ground_witnesses() override{return {{0.,0.,0.}};}
 std::vector<ImpactHit> material_segment(const Point&,const Point&) override {++material;return {{1.,"engineHealth"},{2.,"fuelTankHealth"}};}
 std::vector<ImpactHit> interior_segment(const Point&,const Point&) override {++interior;return {{0.,"driverHealth"}};}
};
int main(){
 World world;CombatState state;state.alive=true;state.health=state.maximum_health=1000;
 state.descriptor_crew_roster={"driver","commander"};
 state.critical.devices["engineHealth"]={100.,100.,50.,false,false,100.};
 state.critical.devices["fuelTankHealth"]={50.,100.,50.,false,false,100.};
 VerticalState vertical;vertical.air_lateral_x=2.;EnvironmentTotals totals;
 auto impact=apply_environment_impact(1,state,vertical,20.,true,true,{},Point{0.,1.,0.},world,totals);
 assert(impact.damage==300 && state.health==700 && vertical.slide_speed==2. && vertical.air_lateral_x==0.);
 assert(world.material==5 && world.interior==5 && state.critical.devices["engineHealth"].destroyed);
 assert(state.critical.devices["fuelTankHealth"].destroyed && state.critical.fire && state.critical.crew_ko.count("driver"));
 assert(totals.fall_damage[1]==300 && totals.fire_provenance.count(1) && !totals.critical.empty());
 CombatState low;low.health=low.maximum_health=100;VerticalState skid;skid.air_lateral_x=12.;
 auto combined=apply_environment_impact(2,low,skid,0.,false,false,{},Point{0.,1.,0.},world,totals);
 assert(combined.damage==6 && low.health==94 && skid.slide_speed==12.);
}
