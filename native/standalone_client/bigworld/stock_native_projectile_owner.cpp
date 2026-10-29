#include "stock_native_projectile_owner.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>
namespace stock_native_projectile {
namespace {
bool finite(const Point&p){return std::all_of(p.begin(),p.end(),[](double x){return std::isfinite(x);});}
double magnitude(const Point&p){return std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);}
std::optional<std::pair<Point,Point>> reflect(const Point& impact,const Point& velocity,const Point& normal){
    const double norm=magnitude(normal);if(!finite(velocity)||!finite(normal)||norm<=1e-12)return {};
    Point unit;double projection=0;for(unsigned i=0;i<3;++i){unit[i]=normal[i]/norm;projection+=velocity[i]*unit[i];}
    Point reflected;double side=0;for(unsigned i=0;i<3;++i){reflected[i]=velocity[i]-2*projection*unit[i];side+=reflected[i]*unit[i];}
    const double speed=magnitude(reflected);if(!finite(reflected)||speed<=1e-6||speed>3000.)return {};
    Point start;for(unsigned i=0;i<3;++i)start[i]=impact[i]+(side>=0?1.:-1.)*unit[i]*.002;
    return std::make_pair(start,reflected);
}
}
bool valid(const Launch&l){
    if(l.id.empty()||l.vehicle.empty()||l.vehicle.size()>128||l.shooter<=0||l.sequence<=0||l.shell_index<0||l.shell_index>9||l.group_sequence<=0||l.burst_index<0||l.burst_count<1||l.burst_count>64||l.burst_index>=l.burst_count||l.sequence!=l.group_sequence+l.burst_index||l.segment<0||l.segment>1||l.maximum_ms<=0||l.maximum_ms>20000||l.launch_ms<0||l.segment_ms<0||l.segment_ms>l.maximum_ms||(l.segment==1&&l.segment_ms>=l.maximum_ms)||l.checked_ms<l.segment_ms||l.checked_ms>l.maximum_ms||l.gravity<=0||l.maximum_distance<=0||l.splash_radius<0||l.penetration_factor<0||l.checked_distance<0||l.checked_distance>l.maximum_distance+.1||l.piercing_loss<0)return false;
    for(double v:{l.gravity,l.maximum_distance,l.splash_radius,l.penetration_factor,l.multiplier,l.checked_distance,l.piercing_loss})if(!std::isfinite(v))return false;
    for(const auto*p:{&l.origin,&l.velocity,&l.range_origin,&l.segment_origin,&l.segment_velocity})if(!finite(*p))return false;
    const auto factor=l.segment?damage::firstRicochetMultiplier(l.shot.shell.kind):std::optional<double>(1.);
    return factor&&l.multiplier==*factor&&(l.segment!=0||(l.segment_ms==0&&l.segment_origin==l.origin&&l.segment_velocity==l.velocity));
}
bool same_launch(const Launch&a,const Launch&b){
    return std::tie(a.id,a.vehicle,a.source_signature,a.shooter,a.sequence,a.group_sequence,a.burst_index,a.burst_count,a.shell_index,a.origin,a.velocity,a.range_origin,a.gravity,a.maximum_distance,a.splash_radius,a.penetration_factor,a.maximum_ms,a.launch_ms,a.he)==std::tie(b.id,b.vehicle,b.source_signature,b.shooter,b.sequence,b.group_sequence,b.burst_index,b.burst_count,b.shell_index,b.origin,b.velocity,b.range_origin,b.gravity,b.maximum_distance,b.splash_radius,b.penetration_factor,b.maximum_ms,b.launch_ms,b.he);
}
bool Owner::setEpoch(std::int64_t epoch,double now){
    if(epoch<0||!std::isfinite(now))return false;if(epoch_&&*epoch_==epoch)return true;
    if(!cursors_.reset(std::max(now,cursors_.now())))return false;epoch_=epoch;flights_.clear();terminals_.clear();outbox_.clear();terminal_fence_.clear();resolution_fence_.clear();terminal_fifo_.clear();resolution_fifo_.clear();next_progress_=now;last_now_=now;return true;
}
bool Owner::observe(std::optional<std::int64_t> epoch,std::optional<std::int64_t> server,double local,double now){
    if(!std::isfinite(now)||!std::isfinite(local)||local<0||(server&&*server<0)||(epoch&&*epoch<0))return false;
    if(server&&(!server_ms_||*server>=*server_ms_)){server_ms_=server;server_local_=local;}
    return !epoch||setEpoch(*epoch,now);
}
std::optional<std::int64_t> Owner::serverTime(double now)const{return server_ms_?std::optional<std::int64_t>(*server_ms_+std::int64_t(std::max(0.,now-server_local_)*1000.)):std::nullopt;}
Flight* Owner::install(const Launch&incoming){
    if(!valid(incoming)){++metrics_.rejected;return nullptr;}
    auto row=flights_.find(incoming.id);
    if(row==flights_.end()){
        Flight flight;flight.canonical=incoming;flight.meta.shot=incoming.shot;flight.meta.shot.projectile_id=incoming.id;flight.meta.shot.shooter=incoming.shooter;flight.meta.shot.fire_sequence=incoming.sequence;flight.meta.shot.penetration_factor=incoming.penetration_factor;flight.meta.shot.penetration_multiplier=incoming.multiplier;flight.meta.range_origin=incoming.range_origin;flight.meta.shooter_key=world_.shooterKey(incoming);flight.anchor=world_.localLaunchTime(incoming,last_now_);flight.meta.piercing_loss=incoming.piercing_loss;
        row=flights_.emplace(incoming.id,std::move(flight)).first;
    }else{
        auto&flight=row->second;auto&current=flight.canonical;
        if(!same_launch(current,incoming)||(!world_.pipelined(current)&&incoming.segment<current.segment)||incoming.segment>current.segment+1){++metrics_.rejected;return nullptr;}
        if(world_.pipelined(current)&&incoming.segment<current.segment){world_.confirmed(incoming);return &flight;}
        if(incoming.segment>current.segment){
            current.segment=incoming.segment;current.segment_ms=incoming.segment_ms;current.segment_origin=incoming.segment_origin;current.segment_velocity=incoming.segment_velocity;current.multiplier=incoming.multiplier;flight.meta.shot.penetration_multiplier=incoming.multiplier;flight.meta.shot.damage_roll.reset();
            flight.awaiting=false;flight.terminal.reset();flight.terminal_cursor.reset();flight.wire_frozen=false;flight.destructibles.clear();
        }
        if(incoming.checked_ms>=current.checked_ms){current.checked_ms=incoming.checked_ms;flight.acked_distance=incoming.checked_distance;flight.acked_loss=incoming.piercing_loss;current.checked_distance=std::max(current.checked_distance,incoming.checked_distance);flight.meta.piercing_loss=std::max(flight.meta.piercing_loss,incoming.piercing_loss);
            if(flight.progress_pending&&incoming.checked_ms>=flight.progress_pending->checked_ms)flight.progress_pending.reset();
            if(!cursors_.contains({current.id,current.segment})&&!flight.terminal&&!flight.awaiting){current.checked_distance=incoming.checked_distance;flight.meta.piercing_loss=incoming.piercing_loss;}
        }
    }
    world_.confirmed(incoming);return &row->second;
}
Cursor Owner::snapshot(Flight&flight,double now){
    const auto&l=flight.canonical;
    if(!flight.anchor){const auto estimated=serverTime(now);const double age=estimated?std::max(0.,double(*estimated-l.launch_ms-l.segment_ms)/1000.):0.;flight.anchor=std::max(0.,now-age)-l.segment_ms/1000.;}
    Cursor c;c.key={l.id,l.segment};c.origin=l.segment_origin;c.velocity=l.segment_velocity;c.gravity={0.,-l.gravity,0.};c.launch_time=*flight.anchor+l.segment_ms/1000.;c.max_time=std::max(.001,(l.maximum_ms-l.segment_ms)/1000.);c.max_distance=l.maximum_distance;c.cursor_time=std::max(c.launch_time,std::min(now,c.launch_time+std::max<std::int64_t>(0,l.checked_ms-l.segment_ms)/1000.));c.distance=l.checked_distance;c.position=trajectory(c.origin,c.velocity,c.gravity,c.cursor_time-c.launch_time);return c;
}
void Owner::fence(const std::string&id,bool resolution){auto&set=resolution?resolution_fence_:terminal_fence_;auto&fifo=resolution?resolution_fifo_:terminal_fifo_;if(!set.insert(id).second)return;fifo.push_back(id);while(fifo.size()>8192){set.erase(fifo.front());fifo.pop_front();}}
bool Owner::admit(const Launch&l,double now,bool restore,std::optional<double> local_anchor){
    if(!epoch_)return false;if(terminal_fence_.count(l.id)||resolution_fence_.count(l.id))return true;
    last_now_=std::max(last_now_,now);auto*flight=install(l);if(!flight)return false;if(local_anchor&&!flight->anchor)flight->anchor=local_anchor;if(restore)flight->canonical_seen=true;if(flight->continuation)return true;const Key key{l.id,flight->canonical.segment};if(cursors_.contains(key))return true;
    if(flight->terminal){if(restore)flight->awaiting=false;return true;}
    auto c=snapshot(*flight,now);const bool restored=restore||l.segment!=0;const bool ok=restored?cursors_.restore(c,restore?now:std::max(now,c.cursor_time)):cursors_.launch(c,std::max(now,c.cursor_time));
    if(!ok){++metrics_.rejected;return false;}
    flight->meta.presentation_offsets=world_.presentationOffsets(l,now);if(restored)++metrics_.restores;else ++metrics_.launches;
    if(history_.samples().empty()){Poses poses;for(const auto&r:world_.records())if(world_.historyRecord(r))poses[r.key]=r.current;history_.sample(now,std::move(poses));}
    return true;
}
bool Owner::launch(const Launch&l,double now,std::optional<double> local_anchor){return admit(l,now,false,local_anchor);}
bool Owner::discard(const std::string&id){auto row=flights_.find(id);if(row==flights_.end())return false;cursors_.remove({id,row->second.canonical.segment});for(auto at=outbox_.begin();at!=outbox_.end();){bool owns=at->second.proposal&&at->second.proposal->frontier.projectile==id;for(const auto&f:at->second.progress)owns=owns||f.projectile==id;if(owns)at=outbox_.erase(at);else ++at;}flights_.erase(row);terminals_.erase(id);return true;}
bool Owner::localFailure(const std::string&id){if(!discard(id))return false;fence(id,false);++metrics_.local_failures;return true;}
bool Owner::reconcile(const std::vector<Launch>&rows,const std::set<std::string>&reported,double now){
    std::set<std::string> active;for(const auto&l:rows){if(terminal_fence_.count(l.id)||resolution_fence_.count(l.id))continue;active.insert(l.id);admit(l,now,true);}
    for(auto at=flights_.begin();at!=flights_.end();){auto&f=at->second;if(!active.count(at->first)&&f.terminal&&(!world_.pipelined(f.canonical)||f.canonical_seen)){if(f.awaiting&&!f.terminal->ricochet&&!reported.count(at->first))fence(at->first,true);terminals_.erase(at->first);at=flights_.erase(at);}else ++at;}return true;
}
bool Owner::terminalEvent(const std::string&id,std::optional<std::int64_t>epoch){
    if(id.empty()||(epoch&&epoch_&&*epoch!=*epoch_))return false;fence(id,false);const auto row=flights_.find(id);if(row!=flights_.end())cursors_.remove({id,row->second.canonical.segment});else cursors_.remove({id,0});flights_.erase(id);terminals_.erase(id);return true;
}
Frontier Owner::frontier(const Flight&f,const Cursor&c)const{
    Frontier out;out.projectile=f.canonical.id;out.base_ms=world_.pipelined(f.canonical)?std::max(f.canonical.checked_ms,f.queued_ms):f.canonical.checked_ms;
    out.checked_ms=std::max(out.base_ms,f.canonical.segment_ms+std::int64_t(std::round(std::max(0.,c.cursor_time-c.launch_time)*1000.)));
    out.distance=std::max({0.,c.distance,f.canonical.checked_distance,f.acked_distance,f.queued_distance});out.loss=std::max({0.,f.meta.piercing_loss,f.acked_loss,f.queued_loss});out.penetration_factor=f.meta.shot.penetration_factor.value_or(1.);out.destructibles=f.destructibles;return out;
}
std::optional<Terminal> Owner::chord(const Cursor&c,const Chord&chord){
    const auto row=flights_.find(c.key.projectile);if(row==flights_.end())return Terminal{"callback_error",0};std::optional<TerminalData> data;
    try{auto result=resolver_.trace(c,row->second.meta,chord,data);auto destroyed=world_.takeDestructions();row->second.destructibles.insert(row->second.destructibles.end(),destroyed.begin(),destroyed.end());if(data)terminals_[c.key.projectile]=std::move(*data);return result;}
    catch(...){auto destroyed=world_.takeDestructions();row->second.destructibles.insert(row->second.destructibles.end(),destroyed.begin(),destroyed.end());++metrics_.local_failures;return Terminal{"callback_error",0};}
}
void Owner::terminal(const Cursor&c,const Terminal&t){
    const auto row=flights_.find(c.key.projectile);if(row==flights_.end())return;auto&f=row->second;
    Proposal proposal;proposal.outcome=t.reason=="max_distance"?"miss":"expired";proposal.impact=c.position;
    const auto hit=terminals_.find(c.key.projectile);std::optional<TerminalData> data;if(hit!=terminals_.end()){data=std::move(hit->second);terminals_.erase(hit);}
    if(t.reason=="impact"&&data){
        proposal.outcome="impact";proposal.impact=data->impact;proposal.hit_vehicle=bool(data->target);
        try{
            const auto resolution=world_.resolve(f,c,*data);proposal.direct=resolution.direct;proposal.splash=resolution.splash;
            if(proposal.hit_vehicle&&!proposal.direct){
                for(const auto&r:world_.records())if(r.key==*data->target&&!r.alive){proposal.wreck=damage::TargetIdentity{r.human,r.id};break;}
                if(!proposal.wreck){proposal.outcome="expired";proposal.hit_vehicle=false;proposal.splash.clear();++metrics_.local_failures;}
            }
            if(proposal.direct&&proposal.direct->result==0&&f.canonical.segment==0&&resolution.ricochet){
                const auto&contact=*resolution.ricochet;const auto multiplier=damage::firstRicochetMultiplier(f.meta.shot.shell.kind);
                const auto checked=frontier(f,c);const auto when=contact.resolved_time_ms;const double travelled=contact.checked_distance;
                if(multiplier&&when<f.canonical.maximum_ms&&travelled<f.canonical.maximum_distance){
                    Point incoming;const double elapsed=std::max(0.,c.cursor_time-c.launch_time)+std::max(0.,contact.elapsed_delta);for(unsigned i=0;i<3;++i)incoming[i]=c.velocity[i]+c.gravity[i]*elapsed;
                    const auto reflected=reflect(contact.impact,incoming,contact.world_normal);
                    if(reflected){proposal.ricochet=true;proposal.impact=contact.impact;proposal.segment_origin=reflected->first;proposal.segment_velocity=reflected->second;proposal.multiplier=*multiplier;proposal.frontier=checked;proposal.frontier.checked_ms=when;proposal.frontier.distance=std::max(checked.distance,travelled);proposal.frontier.loss=std::max(checked.loss,contact.piercing_loss);}
                }
            }
        }catch(...){proposal.outcome="expired";proposal.direct.reset();proposal.splash.clear();proposal.wreck.reset();proposal.hit_vehicle=false;proposal.ricochet=false;++metrics_.local_failures;}
    }
    f.terminal=std::move(proposal);f.terminal_cursor=c;f.awaiting=false;f.wire_frozen=false;++metrics_.terminal_proposals;
}
void Owner::pending(){
    if(!epoch_)return;
    for(auto&entry:flights_){auto&f=entry.second;if(!f.terminal||f.awaiting||(f.progress_pending&&!world_.pipelined(f.canonical))||!f.terminal_cursor)continue;
        bool queued=false;for(const auto&d:outbox_)if(d.second.proposal&&d.second.proposal->frontier.projectile==entry.first){queued=true;break;}if(queued)continue;
        if(!f.wire_frozen){
            const auto previous=f.terminal->frontier;f.terminal->frontier=frontier(f,*f.terminal_cursor);
            if(f.terminal->ricochet){f.terminal->frontier.checked_ms=previous.checked_ms;f.terminal->frontier.distance=std::max(f.terminal->frontier.distance,previous.distance);f.terminal->frontier.loss=std::max(f.terminal->frontier.loss,previous.loss);}
            f.wire_frozen=true;
        }
        Dispatch d;d.serial=next_serial_++;d.epoch=*epoch_;d.kind=f.terminal->ricochet?"ricochet":"resolve";d.proposal=f.terminal;outbox_.emplace(d.serial,std::move(d));
    }
}
void Owner::progress(){
    if(!epoch_)return;
    // Transport is synchronous at the outer boundary. An unacknowledged
    // dispatch remains the exact same request until that boundary answers.
    for(const auto&row:outbox_)if(row.second.kind=="progress")return;
    std::vector<Frontier> all;std::set<std::string> active;
    for(const auto&c:cursors_.snapshot()){
        const auto row=flights_.find(c.key.projectile);if(row==flights_.end())continue;const auto&f=row->second;active.insert(row->first);
        auto p=f.progress_pending?*f.progress_pending:frontier(f,c);p.checked_ms=std::min(f.canonical.maximum_ms,p.checked_ms);all.push_back(std::move(p));
    }
    for(const auto&row:flights_)if(row.second.progress_pending&&!active.count(row.first))all.push_back(*row.second.progress_pending);
    for(std::size_t at=0;at<all.size();at+=30){Dispatch d;d.serial=next_serial_++;d.epoch=*epoch_;d.kind="progress";d.progress.assign(all.begin()+at,all.begin()+std::min(all.size(),at+30));outbox_.emplace(d.serial,std::move(d));}
}
void Owner::continuations(double now){for(auto&entry:flights_){auto&f=entry.second;if(!f.continuation)continue;const auto l=*f.continuation;const double start=f.anchor.value_or(now)+l.segment_ms/1000.;if(start>now)continue;auto c=snapshot(f,now);if(cursors_.restore(c,std::max(now,c.cursor_time))){f.continuation.reset();++metrics_.restores;}else {f.continuation.reset();Proposal failure;failure.outcome="expired";failure.impact=l.segment_origin;f.terminal=failure;f.terminal_cursor=c;f.awaiting=false;f.wire_frozen=false;++metrics_.local_failures;++metrics_.terminal_proposals;}}}
bool Owner::sample(double now){if(!std::isfinite(now))return false;Poses poses;for(const auto&r:world_.records())if(world_.historyRecord(r))poses[r.key]=r.current;history_.sample(now,std::move(poses));return true;}
bool Owner::advance(double now,bool force){
    last_advance_={};
    if(!std::isfinite(now)||now<cursors_.now())return false;
    last_now_=now;continuations(now);pending();auto records=world_.records();Poses poses;for(const auto&r:records)if(world_.historyRecord(r))poses[r.key]=r.current;history_.sample(now,std::move(poses));
    const auto states=cursors_.snapshot();const auto budget=std::min<std::size_t>(256,std::max({std::size_t(32),states.size()*2,cursors_.sustainable(1./15.)}));resolver_.begin(records,states,now,budget);
    const bool advanced=cursors_.advance(now,*this,budget);
    if(advanced){
        const auto& cursor=cursors_.metrics();const auto& chord=resolver_.metrics();
        last_advance_.cursor=cursor;last_advance_.chord=chord;
        metrics_.cursor.chords+=cursor.chords;metrics_.cursor.terminals+=cursor.terminals;
        metrics_.chord.scans+=chord.scans;metrics_.chord.candidates+=chord.candidates;
        metrics_.cursor.active=cursors_.size();
        metrics_.cursor.debt_before=cursor.debt_before;metrics_.cursor.debt_after=cursor.debt_after;
    }
    pending();
    if(force||now>=next_progress_){if(now>=next_progress_){if(next_progress_<=0)next_progress_=now;const auto intervals=std::floor((std::max(0.,now-next_progress_)+1e-9)/.1)+1;next_progress_+=intervals*.1;}progress();}
    return advanced;
}
std::vector<Dispatch> Owner::dispatches()const{std::vector<Dispatch> out;for(const auto&row:outbox_)out.push_back(row.second);return out;}
bool Owner::acknowledge(std::uint64_t serial,bool sent,std::optional<double> now){
    const auto row=outbox_.find(serial);if(row==outbox_.end())return false;const auto d=row->second;outbox_.erase(row);
    if(!sent||!epoch_||d.epoch!=*epoch_)return true;
    if(d.proposal){const auto found=flights_.find(d.proposal->frontier.projectile);if(found!=flights_.end()&&found->second.terminal){auto&f=found->second;f.awaiting=true;if(d.proposal->ricochet&&world_.pipelined(f.canonical)){auto next=f.canonical;const auto&p=*d.proposal;next.segment=1;next.segment_ms=p.frontier.checked_ms;next.checked_ms=p.frontier.checked_ms;next.checked_distance=world_.wireValue(p.frontier.distance);next.piercing_loss=world_.wireValue(p.frontier.loss);for(unsigned i=0;i<3;++i){next.segment_origin[i]=world_.wireValue(p.segment_origin[i]);next.segment_velocity[i]=world_.wireValue(p.segment_velocity[i]);}next.multiplier=p.multiplier;
                f.canonical=next;f.meta.shot.penetration_multiplier=next.multiplier;f.meta.shot.damage_roll.reset();f.meta.piercing_loss=next.piercing_loss;f.queued_ms=next.checked_ms;f.queued_distance=next.checked_distance;f.queued_loss=next.piercing_loss;f.terminal.reset();f.terminal_cursor.reset();f.awaiting=false;f.wire_frozen=false;f.destructibles.clear();f.continuation=next;
            }}}
    else for(const auto&p:d.progress){const auto found=flights_.find(p.projectile);if(found==flights_.end())continue;auto&f=found->second;
        if(world_.pipelined(f.canonical)){f.queued_ms=p.checked_ms;f.queued_distance=world_.wireValue(p.distance);f.queued_loss=world_.wireValue(p.loss);const auto n=std::min(p.destructibles.size(),f.destructibles.size());f.destructibles.erase(f.destructibles.begin(),f.destructibles.begin()+n);}
        else if(!f.progress_pending){f.progress_pending=p;const auto n=std::min(p.destructibles.size(),f.destructibles.size());f.destructibles.erase(f.destructibles.begin(),f.destructibles.begin()+n);}}
    continuations(now.value_or(last_now_));return true;
}
}
