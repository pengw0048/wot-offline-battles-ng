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
    ArtilleryInput fallback=input;fallback.pitch_config.fixed_limits={-.6,-.1};
    assert(artillery_candidates(fallback).empty());
    fallback.aim_samples={{0.,50.,100.},{0.,60.,100.}};
    auto candidates=artillery_candidates(fallback);
    assert(!candidates.empty() && candidates.front().aim_token==0);
    ArtilleryController fallback_controller;
    assert(!fallback_controller.plan(fallback,0.).ready);
    fallback_controller.advance(0.,100,world);
    auto fallback_receipt=fallback_controller.plan(fallback,0.);
    assert(fallback_receipt.ready && fallback_receipt.candidate && fallback_receipt.candidate->aim_token==0);
    fallback.identity.target_descriptor_revision=1;
    assert(!fallback_controller.plan(fallback,.1).ready);
    ArtilleryReproof reproof;reproof.arc="low";reproof.hold_solution=fallback_receipt.candidate;
    WeaponFrame frame;frame.valid_pitch=true;HydraulicConfig hydraulic;
    assert(!artillery_reproof_solution(fallback,reproof,frame,hydraulic));
    fallback.pitch_config.fixed_limits={-1.57,1.57};
    candidates=artillery_candidates(fallback);
    assert(!candidates.empty() && !candidates.front().aim_token);
    fallback.maximum=1.;assert(artillery_candidates(fallback).empty());
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
    // Reproof owns the chassis anchor; a new barrel solve is permitted until
    // an exact launch intent freezes all eight source pose values.
    auto slewing=intent;slewing.intent.reset();auto moved_barrel=input;moved_barrel.pose.gun_pitch+=.01;
    assert(artillery_reproof_current(slewing,artillery,moved_barrel,1,true,.2));
    auto pending=intent;assert(!artillery_reproof_current(pending,artillery,moved_barrel,1,true,.2));
    // Deliberate bias/lead is frozen gunner error, not evidence of stale motion.
    auto biased=intent;biased.intent->solution.aim[0]+=25.;
    assert(!artillery_receipt_stale(biased,artillery,input,*planning.candidate,true,.2));
    auto changed=input;changed.target[0]+=2.;
    assert(artillery_receipt_stale(biased,artillery,changed,*planning.candidate,true,.2));
    auto predicted=intent;predicted.intent->solution.reference_velocity={2.,0.,0.};predicted.intent->solution.prediction_seconds=.5;
    auto live=input;live.canonical_motion=std::make_pair(Point{1.,0.,100.},Point{2.,0.,0.});
    assert(!artillery_receipt_stale(predicted,artillery,live,*planning.candidate,true,.2));

}
