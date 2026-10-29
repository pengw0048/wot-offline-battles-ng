#pragma once
#include "stock_native_bot_navigation.hpp"

namespace stock_native_bot_navigation {
// identity is the lossless native strategic-order identity. Kind and owner
// retain the release's shared-route versus private-follower distinction.
struct PathKey {
    std::string kind,identity,base_kind;
    std::optional<std::int64_t> owner;
    bool operator<(const PathKey&b)const{return std::tie(kind,identity,base_kind,owner)<std::tie(b.kind,b.identity,b.base_kind,b.owner);}
    bool operator==(const PathKey&b)const{return kind==b.kind&&identity==b.identity&&base_kind==b.base_kind&&owner==b.owner;}
    bool operator!=(const PathKey&b)const{return !(*this==b);}
};
struct CacheKey {
    PathKey path;Cell goal{};
    bool operator<(const CacheKey&b)const{return std::tie(path,goal)<std::tie(b.path,b.goal);}
    bool operator==(const CacheKey&b)const{return path==b.path&&goal==b.goal;}
    bool operator!=(const CacheKey&b)const{return !(*this==b);}
};
struct LocalIntent {
    std::optional<CacheKey> request;std::uint64_t generation=0;Point goal{};
    bool operator==(const LocalIntent&b)const{return request==b.request&&generation==b.generation&&goal==b.goal;}
    bool operator!=(const LocalIntent&b)const{return !(*this==b);}
};
struct PathRecord {std::shared_ptr<const Path> path;double at=0;std::uint64_t hull_revision=0;};
struct RetryProgress {std::optional<CacheKey> request;double at=0;std::optional<double> replan_at;std::set<Cell> visited;int failures=0,replans=0;};
struct ReachedCorner {CacheKey key;std::optional<CacheKey> request;std::size_t index=0;Point point{};};
struct BlockedTracker {Edge key;int count=0;double first_at=0,last_at=0;Point origin{};};
struct HardEpisode {Cell origin_cell{},target_cell{};Point target{},report_target{};bool navigation_target=false;};
struct NavigatorState {
    Point last_position{},macro_position{};
    double progress_time=0,recovery_until=0,planned_at=0,macro_at=0;
    int recovery=0,macro_replans=0,blocked_replans=0,ingress_replans=0;
    std::size_t index=0,macro_index=0;
    std::uint64_t generation=0;
    std::optional<CacheKey> path_key,request_key,macro_path_key;
    std::optional<PathKey> request_path;
    std::optional<Point> planned_goal,recovery_start,last_target,controlled_shallow_target;
    bool replan_active=false,macro_replan_active=false,terminal=false,blocked_step_replan=false;
    std::string status="pending",macro_kind="path",local_end_reason;
    double blocked_escalated_until=0;
    std::optional<BlockedTracker> blocked_tracker;
    std::optional<HardEpisode> hard_episode;
    std::optional<Point> local_target,local_start,macro_target;
    std::optional<LocalIntent> local_intent,local_episode;
    std::optional<std::pair<Point,LocalIntent>> local_completed;
    std::optional<std::pair<Point,double>> local_retired,macro_escape,ingress_escape;
    std::optional<std::pair<Point,double>> ingress_progress;
    std::optional<double> local_progress_at,local_best_distance,local_best_heading,pending_since;
    std::optional<RetryProgress> retry_progress;
    std::optional<ReachedCorner> reached_corner;
    std::set<Cell> visited;
    std::deque<Cell> visited_order;
    bool temporary_stalled=false;
};
struct DirectProgress {
    PathKey path;Point target{},position{};double at=0;int replans=0;
    std::optional<std::pair<Point,double>> escape;
};
class Navigator {
    std::unique_ptr<SearchQueue> jobs_;
    std::map<CacheKey,std::shared_ptr<Search>> searches_;
    std::map<std::int64_t,TimedPenalties> failed_,macro_edges_;
    std::map<std::int64_t,DirectProgress> direct_;
    std::map<std::int64_t,std::string> fallback_modes_;
    double credit_=0;
    std::optional<double> automatic_at_,housekeeping_at_;
    std::uint64_t frame_=0,processed_=std::uint64_t(-1);
    unsigned frame_budget_=96;
    bool frame_open_=false;
    static PathKey derived(const std::string&,std::int64_t,const std::string&,const PathKey&);
    static bool clearance(const PathKey&);
    static void clearTemporary(NavigatorState&);
    static LocalIntent intent(const NavigatorState&,const Point&);
    void cancelSearches(const std::vector<CacheKey>&);
    void cancelBot(std::int64_t,const std::optional<CacheKey>& keep={},const std::string& kind={});
    void advance(double now);
    void finish(const CacheKey&,const std::shared_ptr<Search>&,double now);
    void fallbackMode(std::int64_t,const std::string&);
    Penalties active(std::map<std::int64_t,TimedPenalties>&,std::int64_t,double);
    Penalties penalties(std::optional<std::int64_t>,double);
    void resetMacro(NavigatorState&,const Point&,const std::optional<Point>&,double,const std::string& kind="path");
    bool startMacro(std::int64_t,NavigatorState&,const Point&,const Point&,double);
    void finishMacro(std::int64_t,NavigatorState&);
    bool observeMacro(std::int64_t,NavigatorState&,const Point&,const Point&,double);
    void rememberLocal(NavigatorState&,const Point&,const Point&,const Point&,double);
    bool localPassed(const NavigatorState&,const Point&,const Point&)const;
    void recordRefusal(NavigatorState&,const Point&,double);
    bool retryStalled(NavigatorState&,const Point&,const Point&,double,bool);
    std::optional<Point> retainedLocal(std::int64_t,NavigatorState&,const Point&,const Point&,double);
    Point localOrigin(NavigatorState&,const Point&,const Point&)const;
    std::optional<Point> safeFallback(std::int64_t,NavigatorState&,const Point&,const Point&,double,const Path&);
    Point fallback(std::int64_t,NavigatorState&,const Point&,const Point&,double,const Path&);
    Point pending(std::int64_t,NavigatorState&,const Point&,const Point&,double,const Path&,bool last,bool immediate);
    std::optional<Point> ingress(std::int64_t,NavigatorState&,const Point&,const Point&,double,bool,const Path&);
    std::pair<CacheKey,std::shared_ptr<const Path>> path(const PathKey&,const Point&,const Point&,double);
    bool plannedNext(std::int64_t,const Point&,const Path&,std::size_t,double);
    bool plannedCurrent(const Point&,const Path&,std::size_t,double,std::optional<Point> selected={});
    std::size_t entryIndex(std::int64_t,const Point&,const Path&,double);
    std::size_t lookahead(std::int64_t,const Point&,const Path&,std::size_t,const PathKey&,double,std::optional<double>);
    std::optional<std::size_t> resumeCorner(std::int64_t,NavigatorState&,const Point&,const Path&,const CacheKey&,std::size_t,double);
    bool shallowMatches(const Point&,const Point&,double)const;
public:
    Grid& grid;
    std::map<CacheKey,PathRecord> paths;
    std::map<std::int64_t,NavigatorState> states;
    explicit Navigator(Grid&,std::unique_ptr<SearchQueue> jobs={});
    void beginFrame(double elapsed);
    void endFrame();
    void tick(double now);
    void close();
    void invalidateNativePlanning();
    bool requestReplan(std::int64_t,const Point&,double now);
    bool reportBlockedStep(std::int64_t,const Point&,const Point&,double now);
    bool reportBlockedPlan(const Point&,const Point&);
    bool reportHardContact(std::int64_t,const Point&,const Point&,double realised_yaw,double now);
    bool clearBlockedContact(std::int64_t);
    bool segmentPenalized(std::int64_t,const Point&,const Point&,double now);
    std::optional<Point> observeDirect(std::int64_t,const Point&,const Point&,const PathKey&,double,bool movement=true);
    std::optional<Point> retainedTarget(std::int64_t,const Point&,const Point&,const PathKey&,double,bool movement=true,std::optional<double> yaw={});
    void rememberRuntimeTarget(std::int64_t,const Point&,const Point&,const Point&,double);
    std::optional<Point> driverProgressTarget(std::int64_t,const Point&,const Point&,double)const;
    Point nextTarget(std::int64_t,const Point&,Point goal,const PathKey&,double,
        std::optional<Point> anchor={},const Path& avoid={},std::optional<double> lookahead={},bool movement=true);
    bool targetTerminal(std::int64_t)const;
    bool controlledShallowStep(std::int64_t,const Point&,double yaw,double maximum_error=.45)const;
    bool controlledShallowCommitted(std::int64_t,const Point&,double yaw)const;
};
}
