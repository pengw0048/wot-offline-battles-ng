#include "stock_native_bot_artillery.hpp"
#include "stock_native_bot_weapon.hpp"
#include <algorithm>
#include <cmath>
#include <tuple>

namespace wot::offline_bot_step {
namespace {
double distance(const Point& a,const Point& b) {
    const double x=a[0]-b[0],y=a[1]-b[1],z=a[2]-b[2];return std::sqrt(x*x+y*y+z*z);
}
bool equal(const ArcKey& a,const ArcKey& b) {return !(a<b) && !(b<a);}
void erase(std::deque<ArcKey>& values,const ArcKey& key) {
    values.erase(std::remove_if(values.begin(),values.end(),[&](const ArcKey& k){return equal(k,key);}),values.end());
}
bool finite(const Point& p) {return std::all_of(p.begin(),p.end(),[](double x){return std::isfinite(x);});}
}
bool ArcKey::operator<(const ArcKey& b) const {
    return std::tie(actor,target,fire_sequence,descriptor_revision,target_kind,shell,arc_mask,source_position,source_angles,
        origin,yaw,pitch,speed,gravity,maximum,flight_time)<
        std::tie(b.actor,b.target,b.fire_sequence,b.descriptor_revision,b.target_kind,b.shell,b.arc_mask,b.source_position,
        b.source_angles,b.origin,b.yaw,b.pitch,b.speed,b.gravity,b.maximum,b.flight_time);
}
ArcQueue::ArcQueue(std::size_t jobs,double success,double failure,double age,bool remote,
    std::size_t waiting,double slop):max_jobs_(std::max<std::size_t>(1,jobs)),
    max_waiting_(std::max(max_jobs_,waiting)),success_ttl_(std::max(.05,success)),
    failure_ttl_(std::max(.05,failure)),max_age_(std::max(.25,age)),
    target_slop_(std::max(0.,slop)),remote_impact_(remote) {}
void ArcQueue::reset() {jobs_.clear();waiting_.clear();order_.clear();waiting_order_.clear();results_.clear();}
void ArcQueue::discard(const ArcKey& key) {
    jobs_.erase(key);waiting_.erase(key);results_.erase(key);erase(order_,key);erase(waiting_order_,key);
}
void ArcQueue::complete(const ArcKey& key,double now,std::optional<ArcCandidate> candidate,const std::string& reason) {
    const auto it=jobs_.find(key);
    ArcResult result;result.ready=true;result.completed=now;result.candidate=std::move(candidate);
    if (result.candidate) result.reason="clear";
    else if (!reason.empty()) result.reason=reason;
    else if (it!=jobs_.end() && it->second.blocks && it->second.all_hits) result.reason="world_blocked";
    else result.reason="unresolved";
    result.local_blockage=it!=jobs_.end() && it->second.blocks==it->second.candidates.size() &&
        it->second.blocks && it->second.all_hits && it->second.all_local && reason.empty();
    results_[key]={now+(result.candidate?success_ttl_:failure_ttl_),std::move(result)};
    jobs_.erase(key);erase(order_,key);
}
void ArcQueue::purge(double now) {
    for (auto it=results_.begin();it!=results_.end();) {
        if (it->second.deadline<=now) it=results_.erase(it); else ++it;
    }
    const auto active=order_;
    for (const auto& key:active) {
        const auto it=jobs_.find(key);
        if (it==jobs_.end()) erase(order_,key);
        else if (now-it->second.created>max_age_) complete(key,now,std::nullopt,"timeout");
    }
    const auto queued=waiting_order_;
    for (const auto& key:queued) {
        const auto it=waiting_.find(key);
        if (it==waiting_.end()) erase(waiting_order_,key);
        else if (now-it->second.created>max_age_) {
            waiting_.erase(it);erase(waiting_order_,key);
            ArcResult result;result.ready=true;result.completed=now;result.reason="unresolved";results_[key]={now+failure_ttl_,result};
        }
    }
}
ArcResult ArcQueue::result(const ArcKey& key,double now) {
    purge(now);const auto it=results_.find(key);return it==results_.end()?ArcResult{}:it->second.result;
}
bool ArcQueue::is_pending(const ArcKey& key,double now) {
    purge(now);return jobs_.count(key) || waiting_.count(key);
}
ArcResult ArcQueue::request(const ArcKey& key,std::vector<ArcCandidate> candidates,Point target,double now) {
    auto cached=result(key,now);if (cached.ready) return cached;
    if (jobs_.count(key) || waiting_.count(key)) return {};
    candidates.erase(std::remove_if(candidates.begin(),candidates.end(),
        [](const ArcCandidate& c){return c.path.size()<2;}),candidates.end());
    if (candidates.empty()) {ArcResult empty;empty.ready=true;empty.completed=now;empty.reason="no_candidate";
        results_[key]={now+failure_ttl_,empty};return empty;}
    Job job;job.created=now;job.target=target;job.candidates=std::move(candidates);
    if (jobs_.size()>=max_jobs_) {
        if (waiting_.size()<max_waiting_) {waiting_[key]=std::move(job);waiting_order_.push_back(key);}
    } else {jobs_[key]=std::move(job);order_.push_back(key);}
    return {};
}
std::size_t ArcQueue::advance(double now,std::size_t budget,ArcWorld& world) {
    purge(now);
    while (jobs_.size()<max_jobs_ && !waiting_order_.empty()) {
        const auto key=waiting_order_.front();waiting_order_.pop_front();
        const auto it=waiting_.find(key);if (it==waiting_.end()) continue;
        if (now-it->second.created>max_age_) {
            waiting_.erase(it);ArcResult failure;failure.ready=true;failure.completed=now;failure.reason="unresolved";
            results_[key]={now+failure_ttl_,failure};continue;
        }
        jobs_[key]=std::move(it->second);waiting_.erase(it);order_.push_back(key);
    }
    std::size_t used=0;
    while (used<budget && !order_.empty()) {
        const auto key=order_.front();order_.pop_front();
        auto it=jobs_.find(key);if (it==jobs_.end()) continue;
        auto& job=it->second;auto& candidate=job.candidates[job.candidate];
        if (job.chord+1>=candidate.path.size()) {complete(key,now,candidate);continue;}
        Point hit{};ArcQuery result=ArcQuery::failed;
        try {result=world.segment(candidate.path[job.chord],candidate.path[job.chord+1],hit);} catch (...) {}
        ++used;
        if (result==ArcQuery::clear) {
            ++job.chord;if (job.chord+1>=candidate.path.size()) complete(key,now,candidate);
            else order_.push_back(key);
            continue;
        }
        if (result==ArcQuery::hit && finite(hit)) {
            if (remote_impact_ && candidate.arc=="exact_launch") {
                const auto& a=candidate.path[job.chord];const auto& b=candidate.path[job.chord+1];
                double length=0,dot=0;for (int i=0;i<3;++i) {const double d=b[i]-a[i];length+=d*d;dot+=(hit[i]-a[i])*d;}
                if (length>0.) {
                    const double f=dot/length;Point closest{};
                    for (int i=0;i<3;++i) closest[i]=a[i]+f*(b[i]-a[i]);
                    if (f>=-.0001 && f<=1.0001 && distance(hit,closest)<=.1 && distance(hit,candidate.path.front())>25.) {
                        ArcCandidate receipt=candidate;receipt.path.resize(job.chord+1);receipt.path.push_back(hit);
                        receipt.terminal_impact=hit;complete(key,now,std::move(receipt));continue;
                    }
                }
            }
            if (job.chord==candidate.path.size()-2 && distance(hit,job.target)<=target_slop_) {
                complete(key,now,candidate);continue;
            }
            job.all_local=job.all_local && distance(hit,candidate.path.front())<=25.;
        } else {job.all_hits=false;job.all_local=false;}
        ++job.blocks;++job.candidate;job.chord=0;
        if (job.candidate>=job.candidates.size()) complete(key,now,std::nullopt);
        else order_.push_back(key);
    }
    return used;
}

std::vector<ArcCandidate> artillery_candidates(const ArtilleryInput& input) {
    std::vector<ArcCandidate> result;
    if (!input.source_available || !input.valid_pitch || !finite(input.origin) ||
        input.speed<=1. || std::abs(input.gravity)<=.01 || input.maximum<=1.) return result;
    Point target=input.target;target[1]+=1.;
    const double pi=std::acos(-1.);
    for (bool high : {false,true}) {
        const auto solution=offline_simulation::weapons::ballistic_intercept(input.origin,target,input.velocity,
            input.speed,std::abs(input.gravity),-pi*.5,pi*.5,high,20.);
        if (!solution.valid || solution.time>20. || input.speed*solution.time>input.maximum+1.e-6 ||
            (!result.empty() && std::abs(solution.pitch-result.back().pitch)<1.e-5)) continue;
        const double yaw=std::atan2(solution.aim[0]-input.origin[0],solution.aim[2]-input.origin[2]);
        AimIntent intent;intent.world_yaw=yaw;intent.world_pitch=solution.pitch;
        const auto local=local_aim_intent(input.pose,intent);
        const auto limits=offline_simulation::weapons::pitch_limits(local.raw_yaw,input.pitch_config);
        if (local.raw_pitch<limits[0]-.0001 || local.raw_pitch>limits[1]+.0001) continue;
        ArcCandidate c;c.arc=high?"high":"low";c.aim=solution.aim;c.origin=input.origin;
        c.yaw=yaw;c.pitch=solution.pitch;c.flight_time=solution.time;c.speed=input.speed;
        c.gravity=std::abs(input.gravity);c.maximum=input.maximum;
        const int count=std::max(1,static_cast<int>(std::ceil(c.flight_time/.2)));
        for (int i=0;i<=count;++i) c.path.push_back(offline_simulation::weapons::ballistic_position(
            c.origin,c.yaw,c.pitch,c.speed,c.gravity,c.flight_time*double(i)/count));
        result.push_back(std::move(c));
    }
    return result;
}
std::optional<ArcCandidate> artillery_launch_candidate(const ArcKey& key,double step) {
    if (!(key.speed>1.) || !(key.gravity>.01) || !(key.maximum>1.) || !(key.flight_time>0.) ||
        key.flight_time>20. || key.speed*key.flight_time>key.maximum+1.e-6 || !finite(key.origin) ||
        !std::isfinite(key.yaw) || !std::isfinite(key.pitch) || !std::isfinite(key.flight_time)) return std::nullopt;
    ArcCandidate c;c.arc="exact_launch";c.origin=key.origin;c.yaw=key.yaw;c.pitch=key.pitch;
    c.speed=key.speed;c.gravity=key.gravity;c.maximum=key.maximum;c.flight_time=key.flight_time;
    const double h=std::cos(c.pitch);
    c.velocity={std::sin(c.yaw)*h*c.speed,std::sin(c.pitch)*c.speed,std::cos(c.yaw)*h*c.speed};
    step=std::max(.04,std::min(.20,step));
    c.proof_maximum_step=step;
    const int count=std::max(1,static_cast<int>(std::ceil(c.flight_time/step)));
    for (int i=0;i<=count;++i) c.path.push_back(offline_simulation::weapons::ballistic_position(
        c.origin,c.yaw,-c.pitch,c.speed,c.gravity,c.flight_time*double(i)/count));
    return c;
}
void ArtilleryController::reset() {planning_.reset();launch_.reset();planning_keys_.clear();launch_keys_.clear();receipts_.clear();rejected_.clear();}
ArcResult ArtilleryController::plan(const ArtilleryInput& input,double now) {
    auto key=input.identity;
    key.source_position=input.pose.position;key.source_angles={input.pose.yaw,input.pose.pitch,input.pose.roll};
    key.fire_sequence=key.descriptor_revision=0;key.origin={};key.yaw=key.pitch=key.speed=key.gravity=key.maximum=key.flight_time=0.;
    key.arc_mask=0;
    const auto slot=std::make_tuple(key.actor,key.target_kind,key.target,key.shell);
    const auto previous=planning_keys_.find(slot);
    const auto rejected=rejected_.find(slot);
    if (rejected!=rejected_.end()) {
        const auto active_launch=launch_keys_.find(key.actor);
        bool active=active_launch!=launch_keys_.end() && active_launch->second.target_kind==key.target_kind &&
            active_launch->second.target==key.target && active_launch->second.shell==key.shell;
        if (previous!=planning_keys_.end()) {
            const auto cached=planning_.result(previous->second,now);
            active=active || planning_.is_pending(previous->second,now) || (cached.ready && cached.candidate.has_value());
        }
        if ((now>=rejected->second.until && !active) || !equal(key,rejected->second.key) ||
            input.identity.fire_sequence!=rejected->second.fire_sequence || distance(input.target,rejected->second.target)>1.5)
            rejected_.erase(rejected);
        else key.arc_mask=rejected->second.mask;
    }
    if (previous!=planning_keys_.end() && previous->second.arc_mask==key.arc_mask) {
        const auto& p=previous->second;
        double moved=0.,turned=0.;for (int i=0;i<3;++i) {
            const double d=key.source_position[i]-p.source_position[i];moved+=d*d;
            const double a=key.source_angles[i]-p.source_angles[i];turned=std::max(turned,std::abs(std::atan2(std::sin(a),std::cos(a))));
        }
        if (moved<=.05*.05 && turned<=.001) key=p;
    }
    if (previous!=planning_keys_.end() && !equal(previous->second,key)) planning_.discard(previous->second);
    planning_keys_[slot]=key;
    auto value=planning_.result(key,now);
    if (!value.ready && !planning_.is_pending(key,now)) {
        auto candidates=artillery_candidates(input);
        candidates.erase(std::remove_if(candidates.begin(),candidates.end(),[&](const ArcCandidate& c) {
            return (c.arc=="low" && (key.arc_mask&1)) || (c.arc=="high" && (key.arc_mask&2));
        }),candidates.end());
        value=planning_.request(key,std::move(candidates),input.target,now);
    }
    if (value.ready && value.candidate) {
        const auto arc=value.candidate->arc;value.candidate.reset();
        for (const auto& current:artillery_candidates(input)) if (current.arc==arc) {value.candidate=current;break;}
    }
    return value;
}
void ArtilleryController::reject_launch_arc(const ArtilleryInput& input,const std::string& arc,
    std::optional<Point> planned,double now) {
    auto key=input.identity;
    key.source_position=input.pose.position;key.source_angles={input.pose.yaw,input.pose.pitch,input.pose.roll};
    key.fire_sequence=key.descriptor_revision=0;key.origin={};key.yaw=key.pitch=key.speed=key.gravity=key.maximum=key.flight_time=0.;key.arc_mask=0;
    const auto slot=std::make_tuple(key.actor,key.target_kind,key.target,key.shell);
    const auto previous=planning_keys_.find(slot);
    if (previous!=planning_keys_.end()) {planning_.discard(previous->second);planning_keys_.erase(previous);}
    if ((arc!="low" && arc!="high") || !planned || distance(input.target,*planned)>1.5) return;
    int mask=0;const auto old=rejected_.find(slot);
    if (old!=rejected_.end() && equal(old->second.key,key) && old->second.fire_sequence==input.identity.fire_sequence &&
        distance(input.target,old->second.target)<=1.5) mask=old->second.mask;
    mask|=arc=="low"?1:2;
    rejected_[slot]={key,input.target,input.identity.fire_sequence,mask,now+2.5};
}
void ArtilleryController::cancel_launch(std::int64_t actor) {
    const auto previous=launch_keys_.find(actor);if (previous==launch_keys_.end()) return;
    launch_.discard(previous->second);receipts_.erase(previous->second);launch_keys_.erase(previous);
}
ArcResult ArtilleryController::request_launch(ArcKey key,ArcCandidate candidate,double now) {
    key.source_position={};key.source_angles={};key.descriptor_revision=0;key.arc_mask=0;
    const auto previous=launch_keys_.find(key.actor);
    if (previous!=launch_keys_.end() && !equal(previous->second,key)) cancel_launch(key.actor);
    launch_keys_[key.actor]=key;
    const auto pinned=receipts_.find(key);
    if (pinned!=receipts_.end()) {ArcResult result;result.ready=true;result.candidate=pinned->second;result.reason="clear";return result;}
    auto result=launch_.result(key,now);
    if (!result.ready) result=launch_.request(key,{candidate},candidate.path.empty()?candidate.origin:candidate.path.back(),now);
    if (result.ready && result.candidate) receipts_[key]=*result.candidate;
    return result;
}
std::size_t ArtilleryController::advance(double now,std::size_t budget,ArcWorld& world) {
    if (!budget) return 0;
    const bool l=launch_.pending()>0,p=planning_.pending()>0;
    const auto pin=[&]() {
        for (const auto& item:launch_keys_) {const auto result=launch_.result(item.second,now);
            if (result.ready && result.candidate) receipts_[item.second]=*result.candidate;}
    };
    if (!l) return planning_.advance(now,budget,world);
    if (!p) {const auto used=launch_.advance(now,budget,world);pin();return used;}
    const auto quota=std::max<std::size_t>(1,budget/2);
    auto used=launch_.advance(now,quota,world);pin();
    used+=planning_.advance(now,budget-quota,world);
    if (used<budget) {used+=launch_.advance(now,budget-used,world);pin();}
    if (used<budget) used+=planning_.advance(now,budget-used,world);
    return used;
}
bool artillery_reproof_current(ArtilleryIntentState& state,ArtilleryController& controller,
    const ArtilleryInput& input,std::int64_t next,bool target_alive,double now) {
    if (!state.reproof) return false;
    const auto& r=*state.reproof;
    const auto angle=[](double a,double b){return std::abs(std::atan2(std::sin(a-b),std::cos(a-b)));};
    const bool invalid=!target_alive || r.target_kind!=input.identity.target_kind || r.target_id!=input.identity.target ||
        r.shell!=input.identity.shell || r.fire_sequence!=next || r.speed!=input.speed ||
        r.gravity!=input.gravity || r.maximum!=input.maximum || distance(r.source.position,input.pose.position)>.05 ||
        angle(r.source.yaw,input.pose.yaw)>.001 || angle(r.source.pitch,input.pose.pitch)>.001 ||
        angle(r.source.roll,input.pose.roll)>.001 || angle(r.source.turret_yaw,input.pose.turret_yaw)>.001 ||
        angle(r.source.gun_pitch,input.pose.gun_pitch)>.001 || now>r.deadline+1.e-9;
    if (invalid) {state={};controller.cancel_launch(input.identity.actor);return false;}
    return true;
}
std::optional<ArcCandidate> artillery_reproof_solution(const ArtilleryInput& input,
    const ArtilleryReproof& reproof,const WeaponFrame& frame,const HydraulicConfig& hydraulic) {
    if (reproof.arc!="low" && reproof.arc!="high") return std::nullopt;
    double latency=std::max(0.,reproof.proof_latency);
    const bool adaptive=reproof.proof_chords>=1 && reproof.proof_chords<=500 &&
        reproof.proof_maximum_step>=.04 && reproof.proof_maximum_step<=.20;
    const double per_chord=adaptive?latency/reproof.proof_chords:0.;
    const double pi=std::acos(-1.);
    offline_simulation::weapons::BallisticSolution solution;
    for (int i=0;i<(adaptive?8:1);++i) {
        Point predicted=input.target;predicted[1]+=1.;
        for (int axis=0;axis<3;++axis) predicted[axis]+=input.velocity[axis]*latency;
        solution=offline_simulation::weapons::ballistic_intercept(input.origin,predicted,input.velocity,
            input.speed,input.gravity,-pi*.5,pi*.5,reproof.arc=="high",20.);
        if (!solution.valid) return std::nullopt;
        const int chords=adaptive?std::max(1,static_cast<int>(std::ceil(solution.time/reproof.proof_maximum_step))):0;
        const double next=chords*per_chord;
        if (!adaptive || std::abs(next-latency)<1.e-8) break;
        latency=next;
    }
    if (!solution.valid || solution.time>20. || input.speed*solution.time>input.maximum+1.e-6) return std::nullopt;
    const double yaw=std::atan2(solution.aim[0]-input.origin[0],solution.aim[2]-input.origin[2]);
    if (!world_solution_reachable(frame,hydraulic,input.pitch_config,yaw,solution.pitch)) return std::nullopt;
    ArcCandidate result;result.arc=reproof.arc;result.aim=solution.aim;result.origin=input.origin;
    result.yaw=yaw;result.pitch=solution.pitch;result.flight_time=solution.time;
    result.speed=input.speed;result.gravity=input.gravity;result.maximum=input.maximum;
    return result;
}
bool begin_artillery_intent(ArtilleryIntentState& state,ArtilleryController& controller,
    const ArtilleryInput& input,const ArcCandidate& solution,std::int64_t next,std::int64_t round,
    double speed,double dispersion,bool aligned,bool target_alive,double now) {
    const auto direction=shot_direction(input.pose);
    const double yaw=std::atan2(direction[0],direction[2]);
    const double pitch=-std::atan2(direction[1],std::max(1.e-12,std::hypot(direction[0],direction[2])));
    const double yaw_error=std::atan2(std::sin(solution.yaw-yaw),std::cos(solution.yaw-yaw));
    if (!target_alive || std::abs(speed)>.05 || !aligned || std::abs(yaw_error)>1.e-7 ||
        std::abs(solution.pitch-pitch)>1.e-7) return false;
    if (!artillery_reproof_current(state,controller,input,next,target_alive,now)) {
        ArtilleryReproof r;r.source=input.pose;r.target_kind=input.identity.target_kind;r.target_id=input.identity.target;
        r.shell=input.identity.shell;r.fire_sequence=next;r.speed=input.speed;r.gravity=input.gravity;r.maximum=input.maximum;
        r.created=now;r.deadline=now+60.;r.absolute_deadline=now+120.;state.reproof=r;
    } else if (state.reproof->attempts) state.reproof->deadline=std::min(state.reproof->absolute_deadline,now+60.);
    offline_simulation::weapons::BurstEdge edge;edge.shot_seq=edge.group_seq=next;edge.count=1;edge.final_round=true;
    const auto shot=dispersed_shot_angles(round,input.identity.actor,edge,input.pose,dispersion);
    ArtilleryIntent intent;intent.solution=solution;intent.target_position=input.target;
    intent.yaw=shot[0];intent.pitch=shot[1];intent.created=now;state.intent=intent;return true;
}
bool artillery_receipt_stale(ArtilleryIntentState& state,ArtilleryController& controller,
    const ArtilleryInput& input,const ArcCandidate& receipt,bool target_alive,double now) {
    if (!state.intent || !state.reproof) return true;
    Point target=input.target;target[1]+=1.;
    for (int i=0;i<3;++i) target[i]+=input.velocity[i]*receipt.flight_time;
    if (!finite(target) || !finite(state.intent->solution.aim)) {
        state={};controller.cancel_launch(input.identity.actor);return true;
    }
    if (distance(target,state.intent->solution.aim)<=1.5+1.e-9) return false;
    if (!artillery_reproof_current(state,controller,input,input.identity.fire_sequence+1,target_alive,now)) return true;
    auto& r=*state.reproof;r.proof_latency=std::max(0.,now-state.intent->created);
    r.proof_chords=receipt.path.size()>1 && receipt.path.size()-1<=500 &&
        receipt.proof_maximum_step>=.04 && receipt.proof_maximum_step<=.20 ? static_cast<int>(receipt.path.size()-1) : 0;
    r.proof_maximum_step=r.proof_chords ? receipt.proof_maximum_step : 0.;
    ++r.attempts;r.deadline=std::min(r.absolute_deadline,now+60.);r.arc=state.intent->solution.arc;
    r.hold_solution=state.intent->solution;
    state.intent.reset();controller.cancel_launch(input.identity.actor);return true;
}
ArtilleryProof prove_artillery_launch(ArtilleryIntentState& state,ArtilleryController& controller,
    const ArtilleryInput& input,const ArcCandidate& intended,std::int64_t next,std::int64_t round,
    double vehicle_speed,double dispersion,bool aligned,bool target_alive,double now,double splash,int team,
    const std::vector<LaneBody>& roster,DirectWorld& world) {
    ArtilleryProof out;
    if (state.intent && !artillery_reproof_current(state,controller,input,next,target_alive,now)) state.intent.reset();
    if (!state.intent && !begin_artillery_intent(state,controller,input,intended,next,round,
        vehicle_speed,dispersion,aligned,target_alive,now)) return out;
    const auto direction=shot_direction(input.pose);
    const double world_yaw=std::atan2(direction[0],direction[2]);
    const double world_pitch=-std::atan2(direction[1],std::max(1.e-12,std::hypot(direction[0],direction[2])));
    const double delta=std::atan2(std::sin(state.intent->solution.yaw-world_yaw),std::cos(state.intent->solution.yaw-world_yaw));
    if (!aligned || std::abs(delta)>1.e-7 || std::abs(state.intent->solution.pitch-world_pitch)>1.e-7) return out;
    if (!input.source_available || !finite(input.origin) || !finite(input.barrel_point)) return out;
    try {if (world.barrel_under_water(input.barrel_point)) return out;} catch (const std::exception&) {return out;}
    auto key=input.identity;key.fire_sequence=next;key.origin=input.origin;
    key.yaw=state.intent->yaw;key.pitch=state.intent->pitch;key.speed=input.speed;
    key.gravity=input.gravity;key.maximum=input.maximum;key.flight_time=state.intent->solution.flight_time;
    const auto candidate=artillery_launch_candidate(key);
    if (!candidate) {state={};controller.cancel_launch(input.identity.actor);out.status=ArtilleryProofStatus::failed;return out;}
    const auto result=controller.request_launch(key,*candidate,now);
    if (!result.ready) return out;
    out.reason=result.reason;out.completed=result.completed;
    if (!result.candidate) {
        controller.reject_launch_arc(input,result.reason=="world_blocked"?state.intent->solution.arc:"",
                                     state.intent->target_position,now);
        controller.cancel_launch(input.identity.actor);state={};out.status=ArtilleryProofStatus::failed;return out;
    }
    if (artillery_receipt_stale(state,controller,input,*result.candidate,target_alive,now)) {
        out.status=ArtilleryProofStatus::stale;return out;
    }
    const auto lane=prove_friendly_path(result.candidate->path,team,splash,roster,world);
    if (!lane.clear) {
        out.shot_yaw=state.intent->yaw;
        out.status=ArtilleryProofStatus::friendly_blocked;out.has_blocker=lane.has_blocker;
        if (lane.has_blocker) {out.blocker_id=lane.blocker.id;out.blocker_team=lane.blocker.team;
            out.blocker_kind=lane.blocker.kind;
            out.blocker_position=lane.blocker.position;out.blocker_radius=lane.blocker.radius;}
        controller.cancel_launch(input.identity.actor);state={};return out;
    }
    offline_simulation::weapons::LaunchRecord launch;
    launch.actor.kind=offline_simulation::ActorKind::Bot;launch.actor.id=input.identity.actor;
    launch.edge.shot_seq=launch.edge.group_seq=next;launch.edge.count=1;launch.edge.shell=input.identity.shell;launch.edge.final_round=true;
    launch.pose.position=input.pose.position;launch.pose.yaw=input.pose.yaw;launch.pose.pitch=input.pose.pitch;launch.pose.roll=input.pose.roll;
    launch.yaw=key.yaw;launch.pitch=key.pitch;launch.has_origin=true;launch.artillery=true;
    launch.origin=key.origin;launch.velocity=result.candidate->velocity;launch.gravity=key.gravity;
    launch.maximum_distance=key.maximum;launch.maximum_time_ms=20000;
    const int kind=key.target_kind=="bot"?1:key.target_kind=="human"?2:0;
    launch.proof={double(key.actor),double(kind),double(key.target),double(key.shell),double(key.fire_sequence),
        key.origin[0],key.origin[1],key.origin[2],key.yaw,key.pitch,key.speed,key.gravity,key.maximum,key.flight_time};
    out.status=ArtilleryProofStatus::ready;out.launch=std::move(launch);return out;
}
} // namespace wot::offline_bot_step
