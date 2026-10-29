#include "../bigworld/stock_native_bot_artillery.hpp"
#include "../bigworld/stock_native_bot_weapon.hpp"
#include <cassert>
#include <vector>
using namespace wot::offline_bot_step;
struct World : ArcWorld {
    int calls=0;bool block=false;
    ArcQuery segment(const Point& a,const Point& b,Point& hit) override {
        ++calls;if (!block) return ArcQuery::clear;
        for (int i=0;i<3;++i) hit[i]=(a[i]+b[i])*.5;
        return ArcQuery::hit;
    }
};
struct Scene : DirectWorld {
    bool barrel_under_water(const Point&) override {return false;}
    bool vehicle_segment(const LaneBody&,const Point&,const Point&) override {return false;}
    bool debris_segment(const Point&,const Point&) override {return false;}
};
int main() {
    ArcQueue queue(1,2.5,.25,60.,false);World world;
    ArcCandidate candidate;candidate.arc="low";candidate.path={{0.,0.,0.},{10.,0.,0.},{20.,0.,0.}};
    ArcKey a;a.actor=1;ArcKey b;b.actor=2;
    assert(!queue.request(a,{candidate},{20.,0.,0.},0.).ready);
    assert(!queue.request(b,{candidate},{20.,0.,0.},0.).ready);
    assert(queue.advance(0.,1,world)==1 && !queue.result(a,0.).ready);
    assert(queue.advance(0.,1,world)==1 && queue.result(a,0.).candidate);
    assert(!queue.result(b,0.).ready);
    assert(queue.advance(0.,2,world)==2 && queue.result(b,0.).candidate);
    ArcQueue exact(1,.35,.25,40.,true);
    candidate.arc="exact_launch";candidate.path={{0.,0.,0.},{30.,0.,0.},{40.,0.,0.}};
    assert(!exact.request(a,{candidate},{40.,0.,0.},0.).ready);
    assert(exact.advance(0.,1,world)==1);world.block=true;
    assert(exact.advance(0.,1,world)==1);
    auto result=exact.result(a,0.);
    assert(result.candidate && result.candidate->terminal_impact && (*result.candidate->terminal_impact)[0]==35.);
    queue.reset();assert(!queue.result(a,0.).ready);
    world.block=false;ArtilleryController artillery;ArtilleryInput input;
    input.identity.actor=1;input.identity.target=2;input.identity.target_kind="human";
    input.source_available=input.valid_pitch=true;input.target={0.,0.,100.};
    input.speed=500.;input.gravity=9.81;input.maximum=1000.;
    input.pitch_config.fixed_pitch=true;input.pitch_config.fixed_limits={-1.57,1.57};
    auto planning=artillery.plan(input,0.);assert(!planning.ready);
    assert(artillery.advance(0.,100,world)>0);
    planning=artillery.plan(input,0.);assert(planning.ready && planning.candidate);
    input.pose.gun_pitch=planning.candidate->pitch;
    ArtilleryIntentState intent;Scene scene;
    auto proof=prove_artillery_launch(intent,artillery,input,*planning.candidate,1,22,0.,.1,true,true,0.,0.,1,{},scene);
    assert(proof.status==ArtilleryProofStatus::pending);
    assert(artillery.advance(.1,100,world)>0);
    proof=prove_artillery_launch(intent,artillery,input,*planning.candidate,1,22,0.,.1,true,true,.1,0.,1,{},scene);
    assert(proof.status==ArtilleryProofStatus::ready && proof.launch && proof.launch->proof.size()==14);
}
