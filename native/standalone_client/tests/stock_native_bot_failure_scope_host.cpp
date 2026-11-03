#include "stock_native_bot_planner.hpp"
#include <cassert>
#include <cmath>
#include <string>
using namespace stock_native_bot_planner;
struct WorldTest:World {
    bool pending=false;
    bool direction_uncertain()const override{return pending;}
    bool direction(std::int64_t,double,const Optional<double>&max,double,const Optional<double>&) override {return max.present&&max.value<=2.;}
    bool pose(std::int64_t,double,bool) override{return false;}
    Navigation navigate(const Input&,const Order&,const Point&) override{return {};}
};
int main(){
    Planner planner;Input in;in.body.id=1;in.body.half_length=3.5;in.body.half_width=1.7;in.body.lower_y=-.8;in.body.upper_y=2.;in.dt=.2;in.decision_horizon=.4;
    Navigation nav;nav.target={{0.,0.,30.}};Optional<double> arrival;WorldTest world;
    auto setup=[&]{auto&s=planner.actor(in).driver;s.clock=1.;s.stuck_time=2.;s.recovery_time=.5;s.recovery_side=1.;Objective o;o.goal=o.order_goal=nav.target;o.best=o.order_best=30.;o.at=-10.;s.objective.set(o);s.translation_progress_at.set(-10.);};
    setup();planner.remember_forecast(1,3.14159265358979323846,in.body.position,8.);
    auto out=planner.drive(in,nav,nav.target,true,false,arrival,{},world);assert(out.recovery=="short_reverse_escape");
    planner.forget(1);setup();planner.remember_contact(1,3.14159265358979323846,in.body.position,5.);
    planner.remember_forecast(1,3.14159265358979323846,in.body.position,8.);
    out=planner.drive(in,nav,nav.target,true,false,arrival,{},world);assert(out.recovery=="short_forward_escape");
    planner.forget(1);setup();planner.remember_contact(1,3.14159265358979323846,in.body.position,5.);
    in.body.position[1]=1.;out=planner.drive(in,nav,nav.target,true,false,arrival,{},world);assert(out.recovery=="short_reverse_escape");
    planner.forget(1);in.body.position={{0.,0.,0.}};Body wreck=in.body;wreck.id=2;wreck.alive=false;wreck.position={{0.,0.,6.5}};Order order;order.move.set(nav.target);
    world.pending=true;planner.decide(in,order,{wreck},world);assert(!planner.actor(in).wreck_history.at(2).exhausted);
    world.pending=false;planner.decide(in,order,{wreck},world);assert(planner.actor(in).wreck_history.at(2).exhausted);
}
