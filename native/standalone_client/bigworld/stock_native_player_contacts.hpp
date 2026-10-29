#pragma once
#include "stock_native_world_pipeline.hpp"
#include <deque>
#include <map>
#include <optional>

namespace stock_native_player_contacts {
namespace world=stock_native_world;
using ID=std::int64_t;
using Point=world::Point;
using Token=std::set<world::MaterialKey>;
struct Descriptor {
    Point body_minimum{},body_maximum{};
    double mass=0.,drive_forward=0.,drive_reverse=0.,rotation_speed=0.;
    bool pivot_available=false,rotation_around_center=true;
    double track_center=0.;
    bool traverse_valid=true;
};
struct Receipt {
    ID player=0,sequence=0;
    Token token;
    Point start{},end{};
    double yaw=0.,end_yaw=0.,speed=0.,dt=0.,pitch=0.,roll=0.;
    bool operator<(const Receipt&)const;
};
struct Request {Receipt receipt;std::optional<Descriptor> descriptor;};
struct Verdict {
    std::uint64_t serial=0;
    ID player=0,sequence=0;
    bool accepted=false;
    Token token;
};
enum class TreeStatus {Tree,Pending,Other,Hard};
class TreeAuthority {
public:
    virtual ~TreeAuthority()=default;
    virtual TreeStatus classify(const world::Key&)=0;
    virtual world::ContactResult commit(const Token&,const Point& start,
        const Point& end,double end_yaw,double speed,double now)=0;
};
class World {
public:
    virtual ~World()=default;
    virtual TreeStatus tree(const world::Key&)=0;
    virtual world::ContactResult trees(const Token&,const Receipt&,double now)=0;
    virtual world::ContactResult catalog(const Descriptor&,const Receipt&,double now,bool commit)=0;
};
// The worker verifies its exact catalog geometry. Already-presented human
// trees take the separate publication-only authority seam.
class PipelineWorld final:public World {
    world::Pipeline pipeline_;
    TreeAuthority& trees_;
public:
    PipelineWorld(world::Registry& registry,world::Scene& scene,TreeAuthority& trees):
        pipeline_(registry,scene,128),trees_(trees){}
    TreeStatus tree(const world::Key& key)override{return trees_.classify(key);}
    world::ContactResult trees(const Token& token,const Receipt& receipt,double now)override;
    world::ContactResult catalog(const Descriptor&,const Receipt&,double now,bool commit)override;
};
struct Statistics {std::uint64_t proposals=0,commits=0,deferred=0,terminals=0,retries=0,failures=0;};
class Owner {
    struct Record {Verdict verdict;double next_retry=0.;};
    std::map<Receipt,Record> results_;
    std::map<std::uint64_t,Receipt> serials_;
    std::deque<Receipt> order_;
    std::uint64_t next_serial_=1;
    Statistics statistics_;
    std::optional<bool> resolve(const Request&,double,World&);
public:
    std::vector<Verdict> update(const std::vector<Request>&,double now,World&);
    // Call only after the transport accepts the exact returned verdict.
    bool acknowledge(std::uint64_t serial,double now);
    const Statistics& statistics()const{return statistics_;}
    std::size_t cached()const{return results_.size();}
};
}
