#include "stock_native_projectile_owner.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace stock_native_projectile;
struct World:OwnerWorld {
    bool block=false,fail=false;
    int confirmations=0,resolutions=0;
    std::vector<Record> targets;
    std::vector<Record> records()override{return targets;}
    CollisionQuery collide(const Record&,const Point&,const Point&,const std::optional<Pose>&)override{return {};}
    std::optional<double> turretBlock(const Chord&)override{return {};}
    SceneResult scenery(const Cursor&,FlightMeta&,const Point&,const Point&,const Point&)override{if(fail)throw std::runtime_error("local query failure");return block?SceneResult{.25,false,"wall"}:SceneResult{};}
    damage::Resolution resolve(Flight&,const Cursor&,const TerminalData&)override{++resolutions;return {};}
    void confirmed(const Launch&)override{++confirmations;}
    std::vector<stock_native_world::Destruction> takeDestructions()override{return {};}
};
Launch launch(const char*id){Launch l;l.id=id;l.vehicle="ussr:T-34";l.source_signature="frozen";l.shooter=1;l.sequence=l.group_sequence=1;l.burst_count=1;l.velocity=l.segment_velocity={0,0,100};l.gravity=10;l.maximum_distance=10000;l.maximum_ms=1000;l.penetration_factor=1;l.shot.shell.kind="ARMOR_PIERCING";return l;}
int main(){
    {
        World measured;Record target;target.key="player:1";target.id=1;
        target.human=target.ready=target.started=target.alive=true;target.current.position={0,0,5};
        measured.targets.push_back(target);Owner observed(measured);assert(observed.observe(1,0,0,0));
        assert(observed.launch(launch("measured"),0));assert(observed.advance(.05));
        const auto flying=observed.metrics();assert(flying.cursor.active==1&&flying.cursor.chords>0);
        measured.block=true;assert(observed.advance(.1));const auto hit=observed.metrics();
        assert(hit.cursor.active==0&&hit.cursor.chords>flying.cursor.chords&&hit.cursor.terminals==1);
        assert(hit.chord.scans>0&&hit.chord.candidates>0&&hit.terminal_proposals==1);
        assert(observed.advance(.2));const auto idle=observed.metrics();
        assert(idle.cursor.chords==hit.cursor.chords&&idle.cursor.terminals==hit.cursor.terminals);
        assert(idle.chord.scans==hit.chord.scans&&idle.chord.candidates==hit.chord.candidates);
        assert(idle.cursor.active==0&&idle.cursor.debt_before==0&&idle.cursor.debt_after==0);
    }
    World world;Owner owner(world);assert(owner.observe(7,0,0,0));auto l=launch("shell");assert(owner.launch(l,0));assert(owner.launch(l,0));assert(owner.metrics().launches==1);
    assert(owner.advance(.1));auto dispatch=owner.dispatches();assert(dispatch.size()==1&&dispatch[0].kind=="progress");const auto frozen=dispatch[0].progress[0];assert(frozen.checked_ms==100);assert(owner.acknowledge(dispatch[0].serial,true));
    assert(owner.advance(.2));dispatch=owner.dispatches();assert(dispatch.size()==1&&dispatch[0].progress[0].checked_ms==frozen.checked_ms);owner.acknowledge(dispatch[0].serial,true);
    assert(owner.advance(1.1));dispatch=owner.dispatches();assert(dispatch.size()==1&&dispatch[0].kind=="progress");owner.acknowledge(dispatch[0].serial,false);assert(owner.flights().at("shell").terminal&&!owner.flights().at("shell").awaiting);
    l.checked_ms=100;l.checked_distance=frozen.distance;assert(owner.reconcile({l},{l.id},1.1));assert(owner.advance(1.11));dispatch=owner.dispatches();assert(dispatch.size()==1&&dispatch[0].kind=="resolve");assert(dispatch[0].proposal->outcome=="expired"&&dispatch[0].proposal->frontier.base_ms==100);const auto serial=dispatch[0].serial;assert(owner.acknowledge(serial,false));
    assert(owner.advance(1.12));dispatch=owner.dispatches();assert(dispatch.size()==1&&dispatch[0].proposal->frontier.checked_ms==1000);assert(owner.acknowledge(dispatch[0].serial,true));assert(owner.flights().at("shell").awaiting);
    assert(owner.reconcile({l},{l.id},1.12));assert(!owner.flights().at("shell").awaiting);assert(owner.advance(1.13));dispatch=owner.dispatches();assert(dispatch.size()==1);owner.acknowledge(dispatch[0].serial,true);
    assert(owner.reconcile({}, {},1.14));assert(owner.flights().empty());assert(owner.launch(l,1.15));assert(owner.flights().empty());
    assert(owner.observe(8,2000,2,2));world.block=true;l=launch("impact");l.launch_ms=2000;assert(owner.launch(l,2));owner.advance(2.1);dispatch=owner.dispatches();assert(dispatch.size()==1&&dispatch[0].proposal&&dispatch[0].proposal->outcome=="impact");assert(world.resolutions==1);owner.acknowledge(dispatch[0].serial,false);owner.advance(2.2);assert(world.resolutions==1);assert(owner.terminalEvent(l.id,8));assert(owner.launch(l,2.2));assert(owner.flights().empty());
    assert(owner.observe(9,3000,3,3));world.block=false;world.fail=true;l=launch("failed");l.launch_ms=3000;assert(owner.launch(l,3));owner.advance(3.1);dispatch=owner.dispatches();assert(dispatch.size()==1&&dispatch[0].proposal->outcome=="expired"&&!dispatch[0].proposal->direct);assert(owner.metrics().local_failures==1);
    auto bad=l;bad.velocity[1]=1;assert(!same_launch(l,bad));bad=l;bad.sequence=2;assert(!valid(bad));bad=l;bad.penetration_factor=std::nan("");assert(!valid(bad));
    std::cout<<"Native projectile owner lifecycle passed\n";
}
