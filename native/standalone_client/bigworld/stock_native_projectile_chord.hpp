#pragma once
#include "stock_native_projectile_history.hpp"
#include "stock_native_projectile_damage.hpp"
namespace stock_native_projectile {
namespace damage=stock_native_projectile_damage;
struct Record {
    std::string key;
    std::int64_t id=0,engine_id=0;
    bool human=false,local=false,ready=false,tombstone=false,started=false,alive=false;
    bool detached=false,pitch_hull_aiming=false;
    Pose current;
};
struct Collision {
    double distance=0,cosine=0;
    unsigned component=0;
    std::uint32_t material_kind=0;
    const damage::Material* material=nullptr;
    Point local_point{},world_point{},world_normal{};
};
struct CollisionQuery {
    bool valid=true;
    double length=0;
    Point start{},end{};
    std::vector<Collision> collisions;
};
struct FlightMeta {
    damage::FrozenShot shot;
    std::string shooter_key;
    Point range_origin{};
    double piercing_loss=0;
    std::map<std::string,double> presentation_offsets;
    std::set<std::string> clamped_keys;
    bool offsets_frozen=false;
    std::string failure_boundary;
};
struct TerminalData {
    Point impact{};
    std::optional<std::string> target;
    CollisionQuery query;
    Chord trajectory_query;
    std::optional<Pose> collision_pose;
    double piercing_loss=0;
    std::optional<double> penetration_factor;
    std::string stop_reason;
};
struct SceneResult {
    double world_distance=999999.;
    bool stopped_by_destructible=false;
    std::string stop_reason;
};
class ChordWorld {
public:
    virtual ~ChordWorld()=default;
    virtual bool liveGeometry(const Record&r,const Pose&)const{return r.pitch_hull_aiming||r.detached;}
    // Query endpoints/length are the native float Vector3 values used by BSP.
    virtual CollisionQuery collide(const Record&,const Point&,const Point&,const std::optional<Pose>&)=0;
    virtual std::optional<double> turretBlock(const Chord&)=0;
    virtual SceneResult scenery(const Cursor&,FlightMeta&,const Point&,const Point&,const Point& direction)=0;
};
struct ChordMetrics {std::size_t scans=0,candidates=0;};
class ChordResolver {
    const History& history_;
    ChordWorld& world_;
    std::map<std::string,const Record*> records_;
    SpatialIndex index_;
    ChordMetrics metrics_{};
public:
    ChordResolver(const History&,ChordWorld&);
    void begin(const std::vector<Record>&,const std::vector<Cursor>&,double now,
        std::size_t maximum_chords,double presentation_lag=0);
    std::optional<Terminal> trace(const Cursor&,FlightMeta&,const Chord&,std::optional<TerminalData>&);
    const ChordMetrics& metrics()const{return metrics_;}
};
}
