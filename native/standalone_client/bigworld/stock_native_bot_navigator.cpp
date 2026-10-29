#include "stock_native_bot_navigator.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace stock_native_bot_navigation {
namespace {
constexpr double pi=3.14159265358979323846,arrival=1.5;
double distance(const Point&a,const Point&b){const double x=a[0]-b[0],z=a[2]-b[2];return std::sqrt(x*x+z*z);}
double angle(double a){while(a>pi)a-=2*pi;while(a<-pi)a+=2*pi;return a;}
std::string cellIdentity(const Cell&c){return std::to_string(c[0])+","+std::to_string(c[1]);}
bool advances(const Point&current,const std::optional<Point>&target,const Point&goal)
{if(!target)return false;return ((*target)[0]-current[0])*(goal[0]-current[0])+((*target)[2]-current[2])*(goal[2]-current[2])>0.
    &&distance(*target,goal)+.2<distance(current,goal);}
}
Navigator::Navigator(Grid&g,std::unique_ptr<SearchQueue> jobs):jobs_(jobs?std::move(jobs):std::make_unique<Jobs>(g)),grid(g){}
PathKey Navigator::derived(const std::string&kind,std::int64_t owner,const std::string&part,const PathKey&base)
{
    std::ostringstream identity;identity<<part.size()<<':'<<part<<base.kind.size()<<':'<<base.kind<<base.identity.size()<<':'<<base.identity;
    if(base.owner)identity<<'@'<<*base.owner;return {kind,identity.str(),base.kind,owner};
}
bool Navigator::clearance(const PathKey&key)
{return key.kind=="route"||key.kind=="route_join"||key.kind=="join"||key.kind=="recovery"||key.kind=="local"||
    (key.kind=="continue"&&(key.base_kind=="route"||key.base_kind=="route_join"));}
void Navigator::clearTemporary(NavigatorState&s){s.local_episode.reset();s.visited.clear();s.visited_order.clear();s.temporary_stalled=false;}
LocalIntent Navigator::intent(const NavigatorState&s,const Point&goal){return {s.request_key,s.generation,goal};}
void Navigator::cancelSearches(const std::vector<CacheKey>&keys)
{
    std::vector<std::shared_ptr<Search>> jobs;for(const auto&key:keys){auto i=searches_.find(key);if(i==searches_.end())continue;
        jobs.push_back(i->second);searches_.erase(i);}jobs_->cancel(jobs);
}
void Navigator::cancelBot(std::int64_t id,const std::optional<CacheKey>&keep,const std::string&kind)
{
    std::vector<CacheKey> keys;for(const auto&item:searches_)
        if(item.first.path.owner==id&&(!keep||item.first!=*keep)&&(kind.empty()||item.first.path.kind==kind))keys.push_back(item.first);
    cancelSearches(keys);
}
void Navigator::close(){jobs_->close();searches_.clear();}
void Navigator::invalidateNativePlanning()
{
    grid.invalidateNativeReview();std::vector<CacheKey> keys;for(const auto&i:searches_)keys.push_back(i.first);
    cancelSearches(keys);paths.clear();
}
void Navigator::beginFrame(double elapsed)
{
    ++frame_;frame_open_=true;grid.beginFrame();credit_=std::min(384.,credit_+std::max(0.,elapsed)*960.);
    frame_budget_=std::min(384u,std::max(96u,unsigned(credit_)));
}
void Navigator::endFrame(){frame_open_=false;grid.endFrame();}
void Navigator::advance(double now)
{
    if(!frame_open_&&(!automatic_at_||std::abs(now-*automatic_at_)>=.000001)){
        const double elapsed=automatic_at_?std::max(0.,now-*automatic_at_):.1;automatic_at_=now;++frame_;grid.beginFrame();
        credit_=std::min(384.,credit_+elapsed*960.);frame_budget_=std::min(384u,std::max(96u,unsigned(credit_)));}
    if(processed_==frame_)return;processed_=frame_;jobs_->advance(now,frame_budget_);
    std::vector<std::pair<CacheKey,std::shared_ptr<Search>>> completed;
    for(const auto&entry:searches_)if(entry.second->done)completed.push_back(entry);
    for(const auto&entry:completed)finish(entry.first,entry.second,now);
    if(paths.size()>96){std::vector<std::pair<double,CacheKey>> ordered;for(const auto&p:paths)ordered.push_back({p.second.at,p.first});
        std::sort(ordered.begin(),ordered.end());const auto count=ordered.size()-80;for(std::size_t i=0;i<count;++i)paths.erase(ordered[i].second);}
}
void Navigator::finish(const CacheKey&key,const std::shared_ptr<Search>&search,double now)
{
    auto found=searches_.find(key);if(found==searches_.end()||found->second!=search)return;searches_.erase(found);
    if(!search->result.empty()&&search->hull_revision!=std::int64_t(grid.hull_revision)&&grid.pathCrossesStaticHull(search->result))return;
    paths[key]={std::make_shared<const Path>(search->result),now,std::uint64_t(search->hull_revision)};
}
Penalties Navigator::active(std::map<std::int64_t,TimedPenalties>&all,std::int64_t id,double now)
{
    Penalties result;auto found=all.find(id);if(found==all.end())return result;
    for(auto i=found->second.begin();i!=found->second.end();)if(now>=i->second.first)i=found->second.erase(i);
        else{result[i->first]=i->second.second;++i;}
    if(found->second.empty())all.erase(found);return result;
}
Penalties Navigator::penalties(std::optional<std::int64_t>id,double now)
{if(!id)return {};auto result=active(failed_,*id,now);for(const auto&item:active(macro_edges_,*id,now))result[item.first]=item.second;return result;}
void Navigator::tick(double now)
{
    advance(now);if(!housekeeping_at_||now-*housekeeping_at_>=1.){housekeeping_at_=now;grid.pruneFailed(now);
        std::vector<std::int64_t> ids;for(const auto&p:failed_)ids.push_back(p.first);for(auto id:ids)active(failed_,id,now);
        ids.clear();for(const auto&p:macro_edges_)ids.push_back(p.first);for(auto id:ids)active(macro_edges_,id,now);}
}
void Navigator::fallbackMode(std::int64_t id,const std::string&mode)
{
    if(mode.empty()||mode=="safe_direct"){auto i=states.find(id);if(i!=states.end()){i->second.pending_since.reset();clearTemporary(i->second);}}
    if(mode.empty())fallback_modes_.erase(id);else fallback_modes_[id]=mode;
}
bool Navigator::segmentPenalized(std::int64_t id,const Point&a,const Point&b,double now)
{
    auto s=states.find(id);if(s!=states.end()&&s->second.local_retired&&now<s->second.local_retired->second&&distance(b,s->second.local_retired->first)<=arrival)return true;
    return grid.pathHasEdgePenalty({a,b},penalties(id,now));
}
void Navigator::resetMacro(NavigatorState&s,const Point&current,const std::optional<Point>&target,double now,const std::string&kind)
{s.macro_at=now;s.macro_kind=kind;s.macro_position=current;s.macro_target=target;s.macro_path_key=s.path_key;s.macro_index=s.index;}
bool Navigator::startMacro(std::int64_t id,NavigatorState&s,const Point&current,const Point&target,double now)
{
    auto escape=grid.safeLocal(current,target,now,{},id%2?1.:-1.,penalties(id,now),.42);
    const auto edges=grid.edges(current,target);if(edges.empty()&&!escape)return false;
    if(escape)s.macro_escape=std::make_pair(*escape,now+4.);else macro_edges_[id][edges.front()]={now+4.,240.};
    ++s.generation;++s.macro_replans;s.path_key.reset();s.index=0;s.recovery_start=current;s.replan_active=!escape;
    s.macro_replan_active=true;s.pending_since=now-.6;s.controlled_shallow_target.reset();cancelBot(id);resetMacro(s,current,target,now);return true;
}
void Navigator::finishMacro(std::int64_t id,NavigatorState&s)
{
    macro_edges_.erase(id);s.macro_escape.reset();s.macro_replan_active=false;s.replan_active=false;s.recovery_start.reset();s.path_key.reset();s.index=0;cancelBot(id);
}
bool Navigator::observeMacro(std::int64_t id,NavigatorState&s,const Point&current,const Point&goal,double now)
{
    auto target=s.last_target;const bool temporary=s.local_episode==intent(s,goal);
    if(temporary){const auto cell=grid.cellFor(current);const bool fresh=s.visited.insert(cell).second;
        if(fresh){s.visited_order.push_back(cell);while(s.visited_order.size()>2048){s.visited.erase(s.visited_order.front());s.visited_order.pop_front();}}
        if(fresh||s.macro_kind!="temporary"||distance(current,goal)<=arrival){s.temporary_stalled=false;resetMacro(s,current,goal,now,"temporary");}
        else if(!s.temporary_stalled&&now-s.macro_at>=12.){s.temporary_stalled=true;++s.macro_replans;}return s.temporary_stalled;}
    if(s.macro_replan_active&&!s.macro_escape&&active(macro_edges_,id,now).empty()){
        finishMacro(id,s);resetMacro(s,current,target,now);return false;}
    if(!target||distance(current,*target)<=arrival||distance(current,goal)<=arrival){resetMacro(s,current,target,now);return false;}
    if(!s.macro_target||s.macro_path_key!=s.path_key||s.macro_index!=s.index||s.macro_kind!="path"||distance(*s.macro_target,*target)>2.){
        resetMacro(s,current,target,now);return false;}
    const double progress=distance(s.macro_position,*target)-distance(current,*target);s.macro_target=target;
    if(progress>=.2){if(s.macro_replan_active)finishMacro(id,s);resetMacro(s,current,target,now);return false;}
    if(now-s.macro_at>=12.)return startMacro(id,s,current,*target,now);return false;
}
void Navigator::rememberLocal(NavigatorState&s,const Point&target,const Point&goal,const Point&start,double now)
{
    if(s.local_target!=target){s.local_best_heading.reset();s.local_progress_at=now;s.local_best_distance=distance(start,target);}
    s.local_target=target;s.local_intent=intent(s,goal);s.local_episode=s.local_intent;s.local_start=start;
}
bool Navigator::localPassed(const NavigatorState&s,const Point&current,const Point&target)const
{
    if(!s.local_start)return false;const double dx=target[0]-(*s.local_start)[0],dz=target[2]-(*s.local_start)[2],length=std::hypot(dx,dz);
    if(length<.1)return false;const double px=current[0]-target[0],pz=current[2]-target[2];return px*dx+pz*dz>=0.&&std::abs(px*dz-pz*dx)<=length*grid.map.cell_size;
}
void Navigator::recordRefusal(NavigatorState&s,const Point&current,double now)
{
    if(!s.retry_progress||s.retry_progress->request!=s.request_key)s.retry_progress=RetryProgress{s.request_key,now,{}, {grid.cellFor(current)},0,0};
    s.retry_progress->failures=std::min(2,s.retry_progress->failures+1);
}
bool Navigator::retryStalled(NavigatorState&s,const Point&current,const Point&goal,double now,bool movement)
{
    if(!s.retry_progress)return false;auto&tracker=*s.retry_progress;
    if(!movement||tracker.request!=s.request_key||distance(current,goal)<=arrival){s.retry_progress.reset();return false;}
    const auto cell=grid.cellFor(current);if(!tracker.visited.count(cell)){if(tracker.visited.size()>=2048)tracker.visited.clear();tracker.visited.insert(cell);tracker.at=now;tracker.failures=0;return false;}
    if(tracker.failures<2||now-tracker.at<12.||now-tracker.replan_at.value_or(tracker.at)<12.)return false;
    tracker.replan_at=now;++tracker.replans;tracker.failures=0;return true;
}
std::optional<Point> Navigator::retainedLocal(std::int64_t id,NavigatorState&s,const Point&current,const Point&goal,double now)
{
    auto target=s.local_target;const bool matches=s.local_intent==intent(s,goal);
    if(target&&matches&&(distance(current,*target)<=arrival||localPassed(s,current,*target)))s.local_completed=std::make_pair(*target,intent(s,goal));
    if(target&&s.last_target.value_or(*target)==*target&&matches&&distance(current,*target)>arrival&&!localPassed(s,current,*target)&&!segmentPenalized(id,current,*target,now)){
        const auto revision=grid.proof_revision;const bool clear=grid.drySegment(current,*target,now),deferred=revision!=grid.proof_revision||grid.proof_deferred;
        if(!clear&&!deferred){s.local_end_reason="unsafe";recordRefusal(s,current,now);}
        else{const double remaining=distance(current,*target),best=s.local_best_distance.value_or(remaining);
            if(best-remaining>=.2){s.local_best_distance=remaining;s.local_progress_at=now;}
            if(now-s.local_progress_at.value_or(now)<12.){if(deferred){s.status="pending";return current;}
                observeMacro(id,s,current,goal,now);s.last_target=target;return target;}
            s.local_end_reason="timeout";s.local_retired=std::make_pair(*target,now+12.);}
        observeMacro(id,s,current,goal,now);}
    if(target&&s.last_target==target)s.last_target.reset();s.local_target.reset();s.local_intent.reset();s.local_start.reset();
    s.local_progress_at.reset();s.local_best_distance.reset();s.local_best_heading.reset();return {};
}
std::optional<Point> Navigator::retainedTarget(std::int64_t id,const Point&current,const Point&goal,const PathKey&key,double now,bool movement,std::optional<double> yaw)
{
    auto found=states.find(id);if(found==states.end())return {};auto&s=found->second;const auto planned=s.planned_goal.value_or(goal);
    if(!movement||s.request_path!=key||distance(planned,goal)>=grid.map.cell_size*2.){s.local_target.reset();s.retry_progress.reset();s.reached_corner.reset();return {};}
    if(s.local_target&&yaw){auto origin=s.local_start.value_or(current);const double heading=std::atan2((*s.local_target)[0]-origin[0],(*s.local_target)[2]-origin[2]);
        const double error=std::abs(angle(heading-*yaw));auto best=s.local_best_heading;
        if(!best||*best-error>=.02){s.local_best_heading=error;if(best)s.local_progress_at=now;}}
    return retainedLocal(id,s,current,planned,now);
}
void Navigator::rememberRuntimeTarget(std::int64_t id,const Point&current,const Point&goal,const Point&target,double now)
{auto i=states.find(id);if(i!=states.end()){rememberLocal(i->second,target,i->second.planned_goal.value_or(goal),current,now);i->second.last_target=target;}}
Point Navigator::localOrigin(NavigatorState&s,const Point&current,const Point&goal)const
{
    const auto local_intent=intent(s,goal);
    if(grid.height(grid.cellFor(current))&&s.local_completed&&s.local_completed->second==local_intent&&!grid.height(grid.cellFor(s.local_completed->first)))s.local_completed.reset();
    if(s.local_completed&&s.local_completed->second==local_intent&&distance(current,s.local_completed->first)<=grid.map.cell_size)return s.local_completed->first;
    if(s.local_target&&s.last_target.value_or(*s.local_target)==*s.local_target&&s.local_intent==local_intent&&distance(current,*s.local_target)<=arrival)return *s.local_target;
    return current;
}
std::optional<Point> Navigator::safeFallback(std::int64_t id,NavigatorState&s,const Point&current,const Point&goal,double now,const Path&avoid)
{
    const bool eroded=s.local_completed&&s.local_completed->second==intent(s,goal)&&!grid.height(grid.cellFor(s.local_completed->first))&&grid.height(grid.cellFor(current));
    const auto origin=localOrigin(s,current,goal);auto retained=retainedLocal(id,s,current,goal,now);if(retained)return retained;
    const std::set<Cell> empty;auto target=grid.safeLocal(origin,goal,now,avoid,id%2?1.:-1.,penalties(id,now),0.,eroded&&s.temporary_stalled?s.visited:empty);
    if(target&&s.temporary_stalled&&s.visited.count(grid.cellFor(*target)))return current;
    bool clear=true;if(target&&origin!=current){clear=!segmentPenalized(id,current,*target,now)&&grid.drySegment(current,*target,now);
        if(clear)clear=grid.connector(current,*target)==Proof::Clear;}
    if(!clear){s.local_completed.reset();s.local_target.reset();s.local_intent.reset();s.local_start.reset();return current;}
    if(target){if(s.local_retired&&now<s.local_retired->second&&distance(*target,s.local_retired->first)<=arrival)return {};
        rememberLocal(s,*target,goal,current,now);}return target;
}
Point Navigator::fallback(std::int64_t id,NavigatorState&s,const Point&current,const Point&goal,double now,const Path&avoid)
{
    if(s.controlled_shallow_target){const auto target=*s.controlled_shallow_target;
        if(distance(current,target)<=arrival||segmentPenalized(id,current,target,now)||grid.segmentHazard(current,target,3)||!grid.segmentClear(current,target))s.controlled_shallow_target.reset();}
    auto target=safeFallback(id,s,current,goal,now,avoid);if(target){s.last_target=target;s.status=distance(current,*target)<=arrival&&distance(current,goal)>arrival?"pending":"safe";
        s.terminal=distance(*target,goal)<=arrival;fallbackMode(id,"safe_local");return *target;}
    const auto result=segmentPenalized(id,current,goal,now)?current:goal;s.last_target=result;s.status="blocked";s.terminal=false;fallbackMode(id,"reactive");return result;
}
Point Navigator::pending(std::int64_t id,NavigatorState&s,const Point&current,const Point&goal,double now,const Path&avoid,bool last,bool immediate)
{
    // NativeSearch never exposes its unproved partial prefix. Only completed
    // native routes and the release's independently proved local leg may move.
    if(last&&s.last_target){const auto target=*s.last_target;const bool shallow=grid.segmentHazard(current,target,4);
        if(grid.segmentPenalty(current,target,now)<=0.&&!segmentPenalized(id,current,target,now)&&grid.segmentClear(current,target)&&
            (!shallow||s.controlled_shallow_target==target)&&distance(current,target)>arrival){s.status="pending";s.terminal=false;fallbackMode(id,"pending");return target;}}
    if(!s.pending_since)s.pending_since=now;
    if(immediate||now-*s.pending_since>=.6){auto target=safeFallback(id,s,current,goal,now,avoid);
        if(target){s.last_target=target;s.status="pending";s.terminal=false;fallbackMode(id,"safe_local");return *target;}}
    s.last_target=current;s.status="pending";s.terminal=false;fallbackMode(id,"pending");return current;
}
std::optional<Point> Navigator::ingress(std::int64_t id,NavigatorState&s,const Point&current,const Point&goal,double now,bool movement,const Path&avoid)
{
    if(grid.height(grid.cellFor(current))&&!s.ingress_escape){s.ingress_progress.reset();return {};}
    if(!s.ingress_progress||!movement||distance(current,goal)<=arrival||distance(current,s.ingress_progress->first)>=1.){
        s.ingress_progress=std::make_pair(current,now);s.ingress_escape.reset();return {};}
    if(s.ingress_escape){const auto target=s.ingress_escape->first;
        if(now<s.ingress_escape->second&&distance(current,target)>arrival&&grid.drySegment(current,target,now)&&!segmentPenalized(id,current,target,now))return target;
        s.ingress_escape.reset();}
    if(now-s.ingress_progress->second<12.)return {};s.ingress_progress->second=now;++s.ingress_replans;
    auto target=grid.safeLocal(current,goal,now,avoid,(id+s.ingress_replans)%2?1.:-1.,penalties(id,now),.42);
    if(target)s.ingress_escape=std::make_pair(*target,now+8.);return target;
}
std::optional<Point> Navigator::observeDirect(std::int64_t id,const Point&current,const Point&goal,const PathKey&key,double now,bool movement)
{
    if(!movement||distance(current,goal)<=arrival){auto state=states.find(id);if(state!=states.end()){state->second.retry_progress.reset();state->second.reached_corner.reset();}}
    auto found=direct_.find(id);if(found==direct_.end())found=direct_.emplace(id,DirectProgress{key,goal,current,now,0,{}}).first;auto&s=found->second;
    if(s.path!=key||distance(s.target,goal)>2.||!movement||distance(current,goal)<=arrival){s.path=key;s.target=goal;s.position=current;s.at=now;s.escape.reset();return {};}
    if(s.escape){const auto target=s.escape->first;if(now<s.escape->second&&distance(current,target)>arrival&&!segmentPenalized(id,current,target,now)&&grid.drySegment(current,target,now))return target;
        s.target=goal;s.position=current;s.at=now;s.escape.reset();return {};}
    if(distance(s.position,s.target)-distance(current,s.target)>=.2){s.target=goal;s.position=current;s.at=now;return {};}
    if(now-s.at<12.)return {};auto escape=grid.safeLocal(current,goal,now,{},id%2?1.:-1.,penalties(id,now),.42);
    s.target=goal;s.position=current;s.at=now;if(escape){s.escape=std::make_pair(*escape,now+4.);++s.replans;}return escape;
}
std::optional<Point> Navigator::driverProgressTarget(std::int64_t id,const Point&current,const Point&target,double now)const
{
    auto found=states.find(id);if(found==states.end())return {};const auto&s=found->second;
    if(s.status!="safe"||s.last_target!=target||distance(current,target)<=arrival)return {};
    if(s.local_target){if(s.local_target==target&&s.local_start&&s.local_intent&&s.local_intent==s.local_episode&&s.local_progress_at&&now-*s.local_progress_at<12.&&!s.temporary_stalled)return target;return {};}
    if(s.macro_kind=="path"&&s.path_key){auto p=paths.find(*s.path_key);if(p!=paths.end()&&!p->second.path->empty())return target;}return {};
}
bool Navigator::reportBlockedStep(std::int64_t id,const Point&current,const Point&target,double now)
{
    auto found=states.find(id);if(found==states.end()||distance(current,target)<=arrival)return false;auto&s=found->second;const auto edges=grid.edges(current,target);
    if(edges.empty()||now<s.blocked_escalated_until)return false;const auto key=edges.front();auto&t=s.blocked_tracker;
    if(t&&t->key==key&&now-t->last_at<=.5&&distance(current,t->origin)<=std::max(1.5,grid.map.cell_size*.5)){++t->count;t->last_at=now;}
    else t=BlockedTracker{key,1,now,now,current};if(t->count<4||now-t->first_at<1.)return false;
    failed_[id][key]={now+12.,240.};++s.generation;++s.blocked_replans;s.blocked_step_replan=true;s.blocked_escalated_until=now+2.;t.reset();
    s.path_key.reset();s.index=0;s.recovery_start=current;s.replan_active=true;s.macro_replan_active=false;macro_edges_.erase(id);s.macro_escape.reset();
    s.controlled_shallow_target.reset();cancelBot(id);return true;
}
bool Navigator::clearBlockedContact(std::int64_t id)
{auto found=states.find(id);if(found==states.end())return false;auto&s=found->second;bool changed=bool(s.blocked_tracker)||bool(s.hard_episode);s.blocked_tracker.reset();s.hard_episode.reset();return changed;}
bool Navigator::reportHardContact(std::int64_t id,const Point&current,const Point&target,double yaw,double now)
{
    auto found=states.find(id);if(found==states.end())return false;auto&s=found->second;const auto origin=grid.cellFor(current),destination=grid.cellFor(target);
    const bool use=!grid.edges(current,target).empty()&&std::sin(yaw)*(target[0]-current[0])+std::cos(yaw)*(target[2]-current[2])>0.;
    if(!s.hard_episode||s.hard_episode->origin_cell!=origin||s.hard_episode->target_cell!=destination||s.hard_episode->target!=target||s.hard_episode->navigation_target!=use){
        Point report=target;if(!use){const double d=std::max(1.,grid.map.cell_size)*2.;report={current[0]+std::sin(yaw)*d,current[1],current[2]+std::cos(yaw)*d};
            if(grid.edges(current,report).empty())return false;}
        s.hard_episode=HardEpisode{origin,destination,target,report,use};s.blocked_tracker.reset();}
    return reportBlockedStep(id,current,s.hard_episode->report_target,now);
}
bool Navigator::reportBlockedPlan(const Point&a,const Point&b)
{
    if(!grid.reviewNativeCorridor(a,b))return false;
    for(auto i=paths.begin();i!=paths.end();){bool affected=false;const auto&path=*i->second.path;
        for(std::size_t n=1;n<path.size();++n)if(grid.needsNativeReview(path[n-1],path[n])){affected=true;break;}
        if(affected)i=paths.erase(i);else ++i;}
    std::vector<CacheKey> keys;for(const auto&item:searches_)keys.push_back(item.first);cancelSearches(keys);return true;
}
bool Navigator::requestReplan(std::int64_t id,const Point&current,double now)
{
    cancelBot(id);for(auto i=paths.begin();i!=paths.end();)if(i->first.path.owner==id)i=paths.erase(i);else ++i;
    direct_.erase(id);fallback_modes_.erase(id);auto found=states.find(id);if(found==states.end())return false;auto&s=found->second;
    clearTemporary(s);s.local_completed.reset();s.reached_corner.reset();s.local_retired.reset();s.last_target.reset();s.local_target.reset();
    s.controlled_shallow_target.reset();s.macro_escape.reset();s.pending_since.reset();s.path_key.reset();s.index=0;s.last_position=current;
    s.progress_time=now;s.recovery=0;s.recovery_until=0.;s.recovery_start=current;s.replan_active=true;s.macro_replan_active=false;s.terminal=false;
    s.status="pending";++s.generation;resetMacro(s,current,{},now);return true;
}
std::pair<CacheKey,std::shared_ptr<const Path>> Navigator::path(const PathKey&identity,const Point&start,const Point&goal,double now)
{
    CacheKey key{identity,grid.cellFor(goal)};if(identity.owner)cancelBot(*identity.owner,key,identity.kind);
    auto cached=paths.find(key);if(cached!=paths.end()){const auto value=cached->second.path;const auto private_edges=penalties(identity.owner,now);
        const bool hull=cached->second.hull_revision!=grid.hull_revision&&grid.pathCrossesStaticHull(*value);
        const bool partial=!value->empty()&&distance(value->back(),goal)>grid.map.cell_size*3.;
        if(!value->empty()&&!(partial&&now-cached->second.at>=8.)&&!hull&&!grid.pathHasTimedPenalty(*value,now)&&!grid.pathHasEdgePenalty(*value,private_edges)){
            if(!partial)cached->second.at=now;cached->second.hull_revision=grid.hull_revision;return {key,value};}
        if(value->empty()&&now-cached->second.at<8.)return {key,value};paths.erase(cached);}
    auto search=searches_.find(key);std::shared_ptr<Search> receipt;
    if(search==searches_.end()){const auto private_edges=penalties(identity.owner,now);
        if(!grid.pathHasEdgePenalty({start,goal},private_edges)&&grid.drySegment(start,goal,now)){
            auto direct=std::make_shared<const Path>(Path{start,goal});paths[key]={direct,now,grid.hull_revision};return {key,direct};}
        std::set<Edge> hard;for(const auto&edge:private_edges)hard.insert(edge.first);
        std::optional<std::pair<Point,Point>> route;
        if(identity.kind=="route"||identity.kind=="route_join"||identity.base_kind=="route"||identity.base_kind=="route_join")route=std::make_pair(start,goal);
        receipt=jobs_->submit(start,goal,now,4096,clearance(identity),private_edges,hard,{},route);searches_[key]=receipt;
    }else receipt=search->second;
    advance(now);cached=paths.find(key);if(cached!=paths.end())return {key,cached->second.path};
    if(receipt->done){finish(key,receipt,now);cached=paths.find(key);if(cached!=paths.end())return {key,cached->second.path};}
    return {key,{}};
}
bool Navigator::plannedNext(std::int64_t id,const Point&current,const Path&p,std::size_t index,double now)
{
    if(index+1>=p.size())return false;const auto&target=p[index+1];const bool reached=distance(current,p[index])<=arrival;
    if((!reached&&!Grid::livePreservesClimb(current,p,index,index+1))||segmentPenalized(id,current,target,now)||grid.segmentPenalty(current,target,now)>0.||!grid.segmentClear(current,target))return false;
    return !grid.segmentHazard(current,target,4)||grid.segmentHazard(p[index],target,4);
}
bool Navigator::plannedCurrent(const Point&current,const Path&p,std::size_t index,double now,std::optional<Point>selected)
{
    if(index==0||index>=p.size())return false;const auto&target=p[index];
    if((selected!=target&&!Grid::livePreservesClimb(current,p,index-1,index))||grid.segmentPenalty(current,target,now)>0.||!grid.segmentClear(current,target))return false;
    return !grid.segmentHazard(current,target,4)||grid.segmentHazard(p[index-1],target,4);
}
std::size_t Navigator::entryIndex(std::int64_t id,const Point&current,const Path&p,double now)
{
    std::size_t best=0;double best_distance=distance(current,p[0]);for(std::size_t n=1;n<p.size();++n){double d=distance(current,p[n]);if(d<best_distance){best=n;best_distance=d;}}
    for(std::size_t n=0;n+1<p.size();++n){const auto&a=p[n];const auto&b=p[n+1];const double dx=b[0]-a[0],dz=b[2]-a[2],length=dx*dx+dz*dz;
        if(length<.01)continue;const double along=((current[0]-a[0])*dx+(current[2]-a[2])*dz)/length;if(along<=0.||along>=1.)continue;
        const double d=std::hypot(current[0]-a[0]-along*dx,current[2]-a[2]-along*dz);
        if(d>=best_distance||d>24.||(d>arrival&&distance(current,b)>24.)||grid.segmentHazard(current,b,4)||segmentPenalized(id,current,b,now))continue;
        const bool clear=n==0&&d<=arrival?(Grid::livePreservesClimb(current,p,0,1)&&grid.drySegment(current,b,now)):plannedCurrent(current,p,n+1,now);
        if(clear){best=n+1;best_distance=d;}}
    if(best==0&&p.size()>1&&distance(current,p[0])<=std::min(8.,grid.map.cell_size*2.)&&!segmentPenalized(id,current,p[1],now)&&
       !grid.segmentHazard(current,p[1],4)&&plannedCurrent(current,p,1,now))best=1;
    return best;
}
std::size_t Navigator::lookahead(std::int64_t id,const Point&current,const Path&p,std::size_t index,const PathKey&key,double now,std::optional<double>horizon)
{
    std::size_t result=index,limit=std::min(p.size(),index+(horizon?7:3));if(horizon)*horizon=std::max(grid.map.cell_size*2.,*horizon);
    const auto private_edges=penalties(id,now);
    for(std::size_t candidate=index+1;candidate<limit;++candidate){const auto&corner=p[index];const auto&next=p[candidate];const double d=distance(current,corner);
        if(arrival<d&&d<=grid.map.cell_size*2.&&(corner[0]-current[0])*(next[0]-corner[0])+(corner[2]-current[2])*(next[2]-corner[2])>0.){
            const double incoming=std::atan2(corner[0]-current[0],corner[2]-current[2]),outgoing=std::atan2(next[0]-corner[0],next[2]-corner[2]);
            if(std::abs(angle(outgoing-incoming))>.30)break;}
        if(horizon&&candidate>index+1&&distance(current,next)>*horizon)break;
        if((!clearance(key)||grid.preservesClearance(p,index,candidate))&&Grid::livePreservesClimb(current,p,index,candidate)&&
            !grid.pathHasEdgePenalty({current,next},private_edges)&&grid.drySegment(current,next,now))result=candidate;else break;}
    return result;
}
std::optional<std::size_t> Navigator::resumeCorner(std::int64_t id,NavigatorState&s,const Point&current,const Path&p,const CacheKey&key,std::size_t index,double now)
{
    if(!s.reached_corner||s.reached_corner->key!=key||s.reached_corner->request!=s.request_key||s.reached_corner->index!=index||
        s.reached_corner->point!=p[index]||index+1>=p.size()||distance(current,p[index])<=arrival)return index;
    const auto revision=grid.proof_revision;grid.proof_deferred=false;const bool clear=plannedNext(id,current,p,index,now);
    if(revision!=grid.proof_revision||grid.proof_deferred){s.status="pending";s.terminal=false;return {};}
    if(clear){s.reached_corner.reset();return index+1;}requestReplan(id,current,now);return {};
}
Point Navigator::nextTarget(std::int64_t id,const Point&current,Point goal,const PathKey&identity,double now,
    std::optional<Point>anchor,const Path&avoid,std::optional<double>horizon,bool movement)
{
    direct_.erase(id);tick(now);auto found=states.find(id);
    if(found==states.end()){NavigatorState initial;initial.last_position=current;initial.progress_time=now;
        initial.macro_at=now;initial.macro_position=current;found=states.emplace(id,std::move(initial)).first;}
    auto&s=found->second;
    if(s.blocked_step_replan&&active(failed_,id,now).empty()){
        s.blocked_step_replan=false;finishMacro(id,s);s.last_target.reset();resetMacro(s,current,{},now);}
    auto ingress_target=ingress(id,s,current,goal,now,movement,avoid);
    if(ingress_target){s.last_target=ingress_target;s.status="safe";s.terminal=false;fallbackMode(id,"safe_local");return *ingress_target;}
    if(s.request_path==identity&&s.planned_goal&&distance(*s.planned_goal,goal)<grid.map.cell_size*2.&&now-s.planned_at<2.)goal=*s.planned_goal;
    else{s.request_path=identity;s.planned_goal=goal;s.planned_at=now;}
    const CacheKey request{identity,grid.cellFor(goal)};const bool had_request=bool(s.request_key),changed=s.request_key!=request,transition=had_request&&changed;
    auto request_path=paths.find(request);const bool failed_retry=identity.owner&&request_path!=paths.end()&&request_path->second.path->empty()&&now-request_path->second.at>=8.;
    bool allow_last=true;
    if(changed){s.local_target.reset();s.local_retired.reset();s.local_completed.reset();s.reached_corner.reset();allow_last=advances(current,s.last_target,goal);
        if(!allow_last)s.last_target.reset();cancelBot(id);s.request_key=request;s.path_key.reset();s.index=0;s.last_position=current;s.progress_time=now;
        s.recovery=0;s.recovery_until=0.;s.recovery_start.reset();s.replan_active=false;s.macro_replan_active=false;macro_edges_.erase(id);
        s.macro_escape.reset();s.pending_since.reset();s.controlled_shallow_target.reset();resetMacro(s,current,s.last_target,now);}
    if(retryStalled(s,current,goal,now,movement))requestReplan(id,current,now);
    if(!movement){s.local_target.reset();s.reached_corner.reset();}
    else if(!changed){auto issued=s.last_target;auto cached=s.path_key?paths.find(*s.path_key):paths.end();
        if(issued&&cached!=paths.end()&&!cached->second.path->empty()){
            const auto revision=grid.proof_revision;grid.proof_deferred=false;grid.segmentClear(current,*issued);
            if(revision!=grid.proof_revision||grid.proof_deferred){s.status="pending";s.terminal=false;return current;}}
        auto retained=retainedLocal(id,s,current,goal,now);if(retained){s.status=*retained==current?"pending":"safe";s.terminal=false;return *retained;}}
    if(!changed&&movement)observeMacro(id,s,current,goal,now);
    else{if(s.macro_replan_active)finishMacro(id,s);resetMacro(s,current,s.last_target,now);}
    if(s.macro_escape){const auto target=s.macro_escape->first;
        if(now<s.macro_escape->second&&distance(current,target)>arrival&&!segmentPenalized(id,current,target,now)&&grid.drySegment(current,target,now)){
            s.controlled_shallow_target.reset();s.last_target=target;s.status="safe";s.terminal=false;fallbackMode(id,{});return target;}
        finishMacro(id,s);resetMacro(s,current,s.last_target,now);}
    if(!s.macro_replan_active&&distance(current,s.last_position)>=2.){s.last_position=current;s.progress_time=now;s.recovery=0;s.recovery_until=0.;
        cancelBot(id,{},"recovery");s.replan_active=false;s.recovery_start.reset();}
    Point start=identity.owner?current:anchor.value_or(current);if(anchor&&!identity.owner)start[1]=current[1];PathKey effective=identity;
    if(s.replan_active){effective=derived("recovery",id,std::to_string(s.generation),identity);start=s.recovery_start.value_or(current);}
    auto previous=s.path_key?paths.find(*s.path_key):paths.end();std::shared_ptr<const Path> previous_path=previous==paths.end()?nullptr:previous->second.path;
    auto result=path(effective,start,goal,now);auto key=result.first;auto selected_path=result.second;
    if(!selected_path||selected_path->empty()){
        if(grid.drySegment(current,goal,now)&&!segmentPenalized(id,current,goal,now)){
            s.controlled_shallow_target.reset();s.last_target=goal;s.status="safe";s.terminal=true;fallbackMode(id,"safe_direct");return goal;}
        if(!selected_path)return pending(id,s,current,goal,now,avoid,allow_last,transition||failed_retry);
        s.path_key=key;return fallback(id,s,current,goal,now,avoid);}
    const auto active_key=s.path_key;
    if(active_key&&*active_key!=key){auto active_path=paths.find(*active_key);if(active_path!=paths.end()){
        const auto active=active_path->second.path;
        if(!active->empty()&&!(distance(active->back(),goal)>grid.map.cell_size*3.&&distance(selected_path->back(),goal)<=grid.map.cell_size*3.)&&
            !(active_path->second.hull_revision!=grid.hull_revision&&grid.pathCrossesStaticHull(*active))&&!grid.pathHasTimedPenalty(*active,now)){
            key=*active_key;selected_path=active;active_path->second.at=now;}}}
    if(s.path_key!=key){s.path_key=key;s.index=entryIndex(id,current,*selected_path,now);clearTemporary(s);}
    std::size_t index=std::min(s.index,selected_path->size()-1);
    if(movement){auto resumed=resumeCorner(id,s,current,*selected_path,key,index,now);if(!resumed)return current;index=*resumed;}
    const bool following=active_key==key&&previous_path==selected_path&&s.last_target==(*selected_path)[index];bool passed=false;
    if(!following&&index+1<selected_path->size()){const auto&first=(*selected_path)[index];const auto&next=(*selected_path)[index+1];
        passed=(current[0]-first[0])*(next[0]-first[0])+(current[2]-first[2])*(next[2]-first[2])>0.&&distance(current,goal)<distance(first,goal);
        if(passed&&plannedNext(id,current,*selected_path,index,now)){++index;passed=false;}}
    std::optional<Point> selected;if(active_key==key&&previous_path==selected_path&&s.last_target==(*selected_path)[index])selected=s.controlled_shallow_target;
    const auto target=(*selected_path)[index];const bool shallow=grid.segmentHazard(current,target,4);
    if(passed||grid.segmentPenalty(current,target,now)>0.||segmentPenalized(id,current,target,now)||
        (shallow&&!plannedCurrent(current,*selected_path,index,now,selected))||!grid.segmentClear(current,target)){
        auto join=path(derived("join",id,cellIdentity(grid.cellFor(current)),identity),current,goal,now);key=join.first;
        if(!join.second)return pending(id,s,current,goal,now,avoid,allow_last,transition);
        if(join.second->empty()){s.path_key=key;return fallback(id,s,current,goal,now,avoid);}
        selected_path=join.second;s.path_key=key;s.index=0;index=0;}
    const double reach=std::min(10.,std::max(1.5,grid.map.cell_size*.55));
    while(index+1<selected_path->size()&&distance(current,(*selected_path)[index])<reach&&plannedNext(id,current,*selected_path,index,now))++index;
    const auto ahead=lookahead(id,current,*selected_path,index,effective,now,horizon);
    if(ahead==selected_path->size()-1&&distance(current,(*selected_path)[ahead])<reach&&distance((*selected_path)[ahead],goal)>reach){
        auto continuation=derived("continue",id,cellIdentity(grid.cellFor(current)),identity);auto continued=path(continuation,current,goal,now);
        if(continued.second&&!continued.second->empty()){
            selected_path=continued.second;s.path_key=continued.first;std::size_t next=0;
            if(selected_path->size()>1&&plannedNext(id,current,*selected_path,0,now))next=1;
            next=lookahead(id,current,*selected_path,next,continuation,now,horizon);s.index=next;const auto value=(*selected_path)[next];s.last_target=value;
            if(grid.segmentHazard(current,value,4))s.controlled_shallow_target=value;else s.controlled_shallow_target.reset();
            s.status="safe";s.terminal=distance(value,goal)<=arrival;fallbackMode(id,{});return value;}
        if(!continued.second)return pending(id,s,current,goal,now,avoid,allow_last,transition);
        return fallback(id,s,current,goal,now,avoid);}
    const auto value=(*selected_path)[ahead];
    if(distance(current,value)<=arrival&&distance(current,goal)>15.){s.index=ahead;
        if(movement&&ahead+1<selected_path->size())s.reached_corner=ReachedCorner{key,s.request_key,ahead,value};return fallback(id,s,current,goal,now,avoid);}
    s.index=ahead;s.last_target=value;if(grid.segmentHazard(current,value,4))s.controlled_shallow_target=value;else s.controlled_shallow_target.reset();
    s.status="safe";s.terminal=distance(value,goal)<=arrival;fallbackMode(id,{});return value;
}
bool Navigator::targetTerminal(std::int64_t id)const
{auto found=states.find(id);return found!=states.end()&&found->second.terminal;}
bool Navigator::shallowMatches(const Point&current,const Point&target,double yaw)const
{
    const double d=std::max(1.,grid.map.cell_size);const Point end{current[0]+std::sin(yaw)*d,current[1],current[2]+std::cos(yaw)*d};
    auto planned=grid.hazardCells(current,target,4),committed=grid.hazardCells(current,end,4);
    if(!planned||planned->empty()||!committed||grid.segmentHazard(current,target,3)||grid.segmentHazard(current,end,3))return false;
    std::set<Cell> allowed(planned->begin(),planned->end());for(const auto&cell:*committed)if(!allowed.count(cell))return false;return true;
}
bool Navigator::controlledShallowStep(std::int64_t id,const Point&current,double yaw,double maximum_error)const
{
    auto state=states.find(id);if(state==states.end()||!state->second.controlled_shallow_target)return false;const auto&target=*state->second.controlled_shallow_target;
    const double dx=target[0]-current[0],dz=target[2]-current[2];if(std::abs(dx)+std::abs(dz)<.1)return false;
    return std::abs(angle(yaw-std::atan2(dx,dz)))<=std::max(0.,maximum_error)&&shallowMatches(current,target,yaw);
}
bool Navigator::controlledShallowCommitted(std::int64_t id,const Point&current,double yaw)const
{
    auto state=states.find(id);if(state==states.end()||!state->second.controlled_shallow_target)return false;const auto&target=*state->second.controlled_shallow_target;
    const double dx=target[0]-current[0],dz=target[2]-current[2],length=std::sqrt(dx*dx+dz*dz);if(length<.1)return false;
    return (std::sin(yaw)*dx+std::cos(yaw)*dz)/length>.5&&shallowMatches(current,target,yaw);
}
}
