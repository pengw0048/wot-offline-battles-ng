#include "stock_native_projectile_chord.hpp"
#include <algorithm>
#include <cmath>
namespace stock_native_projectile {
namespace {
Point adjust(const Point& p,const Point& midpoint,const Point& endpoint){Point out{};for(unsigned i=0;i<3;++i)out[i]=p[i]+midpoint[i]-endpoint[i];return out;}
bool broad(const Point&position,const Point&a,const Point&b){return expanded_bounds(position,a,b,15.)&&point_segment_distance_sq(position,a,b)<=225.;}
}
ChordResolver::ChordResolver(const History& history,ChordWorld&world):history_(history),world_(world){}
void ChordResolver::begin(const std::vector<Record>&records,const std::vector<Cursor>&cursors,double now,std::size_t maximum,double lag){
    records_.clear();metrics_={};std::vector<std::string> keys;
    for(const auto&r:records){records_[r.key]=&r;keys.push_back(r.key);}
    index_.build(history_,keys,cursors,now,maximum,lag);
}
std::optional<Terminal> ChordResolver::trace(const Cursor&state,FlightMeta&meta,const Chord&chord,std::optional<TerminalData>&terminal){
    const double chord_length=length(chord.start,chord.end);if(chord_length<=1e-6)return {};
    const auto&history=history_.samples();
    if(!meta.presentation_offsets.empty()&&!meta.offsets_frozen&&!history.empty()){
        const double retained=std::max(0.,chord.start_time-history.front().at);
        for(auto&offset:meta.presentation_offsets){const double requested=std::max(0.,offset.second);offset.second=std::min(requested,retained);if(requested>retained)meta.clamped_keys.insert(offset.first);}
        meta.offsets_frozen=true;
    }
    Point direction{};for(unsigned i=0;i<3;++i)direction[i]=(chord.end[i]-chord.start[i])/chord_length;
    double nearest_fraction=1.;std::optional<TerminalData> nearest;
    auto failure=[&](const char* boundary){meta.failure_boundary=boundary;return std::optional<Terminal>(Terminal{"callback_error",0});};
    for(const auto&key:index_.candidates(chord.start,chord.end,chord.start_time,chord.end_time)){
        const auto row=records_.find(key);if(row==records_.end())continue;const auto&r=*row->second;++metrics_.scans;
        if(key==meta.shooter_key||r.tombstone||!r.ready||r.local)continue;
        const auto offset=meta.presentation_offsets.find(key);const double lag=offset==meta.presentation_offsets.end()?0.:offset->second;
        const double begin=chord.start_time-lag,end=chord.end_time-lag;
        if(history.empty()||begin<history.front().at-1e-9||end>history.back().at+1e-9){if(r.started)return failure("historic_pose_unavailable");continue;}
        const auto a=history_.at(key,begin),b=history_.at(key,end),mid=history_.at(key,(begin+end)*.5);
        if(!a||!b||!mid){if(r.started)return failure("historic_pose_unavailable");continue;}
        auto adjusted_start=adjust(chord.start,mid->position,a->position),adjusted_end=adjust(chord.end,mid->position,b->position);
        if(!broad(mid->position,adjusted_start,adjusted_end)||!r.started)continue;
        auto accept=[&](CollisionQuery query,double first,double last,const std::optional<Pose>&pose){
            if(query.collisions.empty()||query.length<=1e-6)return;
            const auto hit=std::min_element(query.collisions.begin(),query.collisions.end(),[](const Collision&a,const Collision&b){return a.distance<b.distance;});
            const double local=std::max(0.,std::min(1.,hit->distance/query.length)),fraction=first+(last-first)*local;
            if(fraction>=nearest_fraction)return;
            nearest_fraction=fraction;TerminalData data;data.target=key;data.query=std::move(query);data.collision_pose=pose;
            data.trajectory_query.start=interpolate(chord.start,chord.end,first);data.trajectory_query.end=interpolate(chord.start,chord.end,last);
            data.trajectory_query.start_time=chord.start_time+(chord.end_time-chord.start_time)*first;data.trajectory_query.end_time=chord.start_time+(chord.end_time-chord.start_time)*last;nearest=std::move(data);
        };
        if(world_.liveGeometry(r,*mid)){
            adjusted_start=adjust(chord.start,r.current.position,a->position);adjusted_end=adjust(chord.end,r.current.position,b->position);++metrics_.candidates;
            auto query=world_.collide(r,adjusted_start,adjusted_end,{});if(!query.valid)return failure("live_component_matrix_unavailable");accept(std::move(query),0,1,{});continue;
        }
        const auto fractions=history_.sweep(key,begin,end);if(!fractions)return failure("historic_pose_unavailable");
        struct Candidate{double start,end;Point a,b;Pose pose;};std::vector<Candidate> candidates;
        for(std::size_t i=0;i+1<fractions->size();++i){
            const double first=(*fractions)[i],last=(*fractions)[i+1],at_start=begin+(end-begin)*first,at_end=begin+(end-begin)*last;
            const auto x=history_.at(key,at_start),y=history_.at(key,at_end),m=history_.at(key,(at_start+at_end)*.5);
            if(!x||!y||!m)return failure("historic_pose_unavailable");
            const auto qa=adjust(interpolate(chord.start,chord.end,first),m->position,x->position),qb=adjust(interpolate(chord.start,chord.end,last),m->position,y->position);
            if(broad(m->position,qa,qb))candidates.push_back({first,last,qa,qb,*m});
        }
        if(candidates.empty())continue;
        ++metrics_.candidates;
        for(const auto&candidate:candidates){auto query=world_.collide(r,candidate.a,candidate.b,candidate.pose);if(!query.valid)return failure("historic_component_matrix_unavailable");accept(std::move(query),candidate.start,candidate.end,candidate.pose);}
    }
    const double cap=chord_length*nearest_fraction;const auto turret=world_.turretBlock(chord);
    double scene_fraction=nearest_fraction;if(turret&&*turret<=cap)scene_fraction=std::max(0.,*turret/chord_length);
    auto scene=world_.scenery(state,meta,chord.start,interpolate(chord.start,chord.end,scene_fraction),direction);
    const bool turret_blocks=turret&&*turret<=cap&&*turret<=scene.world_distance;
    if(turret_blocks){scene.world_distance=*turret;scene.stop_reason="detached_turret";}
    // Fixed release _SHOT_OCCLUSION_EPSILON is one millimetre.
    const bool world_blocks=turret_blocks||(scene.world_distance<99999.&&(!nearest||scene.stopped_by_destructible||cap>scene.world_distance+.001));
    if(world_blocks){
        const double fraction=std::max(0.,std::min(1.,scene.world_distance/chord_length));TerminalData data;data.impact=interpolate(chord.start,chord.end,fraction);data.piercing_loss=meta.piercing_loss;data.penetration_factor=meta.shot.penetration_factor;data.stop_reason=scene.stop_reason;terminal=std::move(data);return Terminal{"impact",fraction};
    }
    if(nearest){nearest->impact=interpolate(chord.start,chord.end,nearest_fraction);nearest->piercing_loss=meta.piercing_loss;nearest->penetration_factor=meta.shot.penetration_factor;terminal=std::move(nearest);return Terminal{"impact",nearest_fraction};}
    return {};
}
}
