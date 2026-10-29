#pragma once
#include "offline_navigation.h"
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace stock_native_bot_navigation {
using Point=offline_navigation::Point;
using Cell=offline_navigation::Cell;
using Edge=std::pair<Cell,Cell>;
using Penalties=std::map<Edge,double>;
using TimedPenalties=std::map<Edge,std::pair<double,double>>;
using Path=std::vector<Point>;
// An unknown column is deferred, never published as a hard obstacle. All
// capabilities are synchronous native calls on the simulation owner thread.
enum class Proof {Unknown=-1,Blocked=0,Clear=1};
class World {
public:
    virtual ~World()=default;
    virtual std::optional<double> ground(double x,double z,double hint)=0;
    virtual Proof corridor(const Point&,const Point&,double half_width)=0;
    virtual Proof navigation(const Point&,const Point&,double cell_size,
        double grade_up,double grade_down,std::optional<double> slope)=0;
};
struct Hull {std::int64_t id=0;double x=0,z=0,yaw=0,half_length=0,half_width=0;};
struct Corridor {bool clear=false;std::optional<unsigned> hazards;};
class Grid {
    using ProofKey=std::tuple<std::array<int,3>,std::array<int,3>,double>;
    using GroundKey=std::array<int,3>;
    World& world_;
    std::map<GroundKey,std::optional<double>> ground_cache_;
    std::map<std::pair<Cell,Cell>,Corridor> corridor_cache_;
    std::deque<std::pair<Cell,Cell>> corridor_order_;
    std::map<ProofKey,bool> proof_cache_;
    std::map<std::pair<Cell,Cell>,bool> slope_cache_;
    std::set<Cell> review_seeds_;
    std::set<std::pair<Point,Point>> pending_;
    std::set<Point> live_joins_;
    std::map<Cell,std::pair<double,double>> local_corridors_;
    bool frame_open_=false;
    static std::array<int,3> pointKey(const Point&);
public:
    offline_navigation::MapInput map;
    double grade_up=0,grade_down=0;
    TimedPenalties failed_edges;
    Penalties static_hull_edges;
    std::set<Cell> review_cells;
    std::uint64_t hull_revision=0,proof_revision=0;
    bool proof_deferred=false;
    explicit Grid(offline_navigation::MapInput,World&,double grade_up,double grade_down);
    Cell cellFor(const Point&)const;
    Point pointFor(const Cell&,double height)const;
    std::optional<std::size_t> index(const Cell&,bool require_height=true)const;
    std::optional<double> height(const Cell&)const;
    std::optional<Cell> nearest(const Cell&,int radius)const;
    bool inside(double x,double z)const;
    std::optional<std::pair<double,double>> localCorridor(const Point&);
    bool hullPoseClear(const Point&,double yaw,double half_length,double half_width)const;
    bool hazardNear(const Point&,int radius)const;
    std::vector<Cell> cells(const Point&,const Point&,bool require_height=true)const;
    std::vector<Edge> edges(const Point&,const Point&)const;
    std::optional<double> edgeHeight(const Cell&,const Cell&)const;
    Corridor bakedCorridor(const Point&,const Point&);
    bool segmentHazard(const Point&,const Point&,unsigned mask);
    bool motionHazard(const Point&,const Point&,unsigned mask)const;
    bool pointHazard(const Point&,unsigned mask)const;
    std::optional<std::vector<Cell>> hazardCells(const Point&,const Point&,unsigned mask)const;
    int linkCount(const Cell&)const;
    double clearancePenalty(const Cell&)const;
    double penalty(const Cell&,const Path& avoid={},bool clearance=false)const;
    std::optional<double> clearanceExposure(const Path&)const;
    bool preservesClearance(const Path&,std::size_t,std::size_t,double maximum=.25)const;
    static bool preservesClimb(const Path&,std::size_t,std::size_t);
    static bool livePreservesClimb(const Point&,const Path&,std::size_t,std::size_t);
    bool setStaticHulls(const std::vector<Hull>&,double clearance);
    void pruneFailed(double now);
    double edgePenalty(const Edge&,double now);
    double segmentPenalty(const Point&,const Point&,double now);
    bool pathHasTimedPenalty(const Path&,double now);
    bool pathCrossesStaticHull(const Path&)const;
    bool pathHasEdgePenalty(const Path&,const Penalties&)const;
    bool reviewNativeCorridor(const Point&,const Point&);
    bool needsNativeReview(const Point&,const Point&)const;
    void invalidateNativeReview();
    void beginFrame();
    void endFrame();
    std::optional<double> ground(double x,double z,double hint);
    Proof nativeSegment(const Point&,const Point&,std::optional<double> slope={});
    bool supportedSlope(const Point&,const Point&);
    Proof liveEgress(const Point&,const Point&);
    Proof connector(const Point&,const Point&);
    bool segmentClear(const Point&,const Point&);
    bool drySegment(const Point&,const Point&,double now);
    std::optional<Point> safeLocal(const Point&,const Point&,double now,
        const Path& avoid={},double side=1.,const Penalties& edges={},
        double minimum_offset=0.,const std::set<Cell>& excluded={});
};
struct Search {
    std::int64_t id=0,hull_revision=0;
    double submitted_at=0,last_frame=0;
    bool done=false;
    unsigned status=0,steps=0;
    Point start{},goal{};
    Path result;
};
// Owns exactly the release pure A* context and its native-query frontier.
// No legacy simulation navigation Store, Python object, or Python callback.
class SearchQueue {
public:
    virtual ~SearchQueue()=default;
    virtual std::shared_ptr<Search> submit(const Point&,const Point&,double,
        unsigned,bool,const Penalties&,const std::set<Edge>&,const Path&,
        std::optional<std::pair<Point,Point>>)=0;
    virtual void cancel(const std::vector<std::shared_ptr<Search>>&)=0;
    virtual void advance(double,unsigned)=0;
    virtual void close()=0;
};
class Jobs final : public SearchQueue {
    Grid& grid_;
    std::int64_t context_=0,next_id_=0;
    std::map<std::int64_t,std::shared_ptr<Search>> jobs_;
    std::map<std::int64_t,std::deque<offline_navigation::Query>> queries_;
    std::deque<std::int64_t> order_;
    void poll(double now);
public:
    struct Stats {
        std::uint64_t submitted=0,completed=0,cancelled=0,queries=0,expanded=0;
        double worker_seconds=0,max_completion_age=0;
        std::array<std::uint64_t,6> worker{};
    } stats;
    explicit Jobs(Grid&);
    ~Jobs() override;
    Jobs(const Jobs&)=delete;
    Jobs& operator=(const Jobs&)=delete;
    std::shared_ptr<Search> submit(const Point&,const Point&,double now,
        unsigned max_expansions,bool clearance,const Penalties& private_edges={},
        const std::set<Edge>& hard_edges={},const Path& avoid={},
        std::optional<std::pair<Point,Point>> route={}) override;
    void cancel(const std::vector<std::shared_ptr<Search>>&) override;
    void advance(double now,unsigned budget) override;
    void close() override;
};
}
