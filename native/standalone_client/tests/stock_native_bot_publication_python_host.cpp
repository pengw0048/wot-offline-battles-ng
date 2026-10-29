#include "../bigworld/stock_native_bot_publication_python.hpp"
#include <Python.h>
using namespace stock_native_bot;
static PyObject* publish(PyObject*,PyObject*){
    Output out;out.state.rows={{1,2,3}};out.state.sample_us=100;out.state.edge_sample_us=90;out.state.edge_revision=7;
    offline_simulation::weapons::LaunchRecord l;l.actor={offline_simulation::ActorKind::Bot,1};l.edge={9,8,1,3,2,false,.03};l.launch_time_us=95;l.pose.position={{1,2,3}};l.pose.yaw=.1;l.pose.pitch=.2;l.pose.roll=.3;l.yaw=.4;l.pitch=.5;l.class_tag="SPG";l.has_origin=true;l.origin={{4,5,6}};l.artillery=true;l.velocity={{7,8,9}};l.gravity=9.81;l.maximum_distance=1200;l.maximum_time_ms=8000;l.has_shells_before=true;l.shells_before=15;l.proof={1,2,4,2,9,4,5,6,.4,.5,500,9.81,1200,2.2};out.state.launches.push_back(l);
    contacts::Report r;r.bot=1;r.target=2;r.sequence=8;r.damage_bot=4;r.damage_target=6;r.positions={{1,2,3,4}};r.bot_tracks={"leftTrackHealth"};r.closing_speed=7;out.state.reports.push_back(r);r.target_human=true;r.player_receipt=12;out.state.reports.push_back(r);
    out.pushes.push_back({5,2,1,true,false});out.hazards={1};out.environment_fires={2};out.falls={{{1,20}}};out.critical.push_back({1,"collision",4,2});
    observation::Observation obs;observation::Contact c;c.observing_team=1;c.target_team=2;c.target={offline_simulation::ActorKind::Human,2};c.visible=c.fresh=true;c.time_left=.5;c.radio_recipients.push_back({{offline_simulation::ActorKind::Bot,1},.4});c.visible_by_bot_ids={1};c.shootable_by_bot_ids={1};c.position={{10,20,30}};c.health=50;c.max_health=100;c.class_tag="heavyTank";c.armor=80;obs.contacts.push_back(c);obs.player_vision_ranges.push_back({2,350});obs.radio_links.push_back({{offline_simulation::ActorKind::Human,2},{{offline_simulation::ActorKind::Bot,1}}});out.observation=obs;
    return packNativeOutputs({out},100);
}
static PyMethodDef methods[]={{"publish",publish,METH_NOARGS,nullptr},{nullptr,nullptr,0,nullptr}};
extern "C" void initpublication_test(){Py_InitModule("publication_test",methods);}
