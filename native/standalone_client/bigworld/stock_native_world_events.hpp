#pragma once
#include "stock_native_destructible_runtime.hpp"
#include <set>
struct _object;
namespace stock_native_world {
using TrustedTreeStatus=TreeIdentity;
class WorldEvents {
    Registry& registry_;
    stock_native_destructible::Runtime& native_;
    Scene& scene_;
    std::uint32_t space_;
    _object *battle_=nullptr;
    std::map<Key,Event> trusted_;
    std::set<Key> trusted_published_,activated_trees_;
    std::map<MaterialKey,Destruction> replay_pending_;
    std::uint64_t packed_=0,epoch_;
    std::uint64_t stream_revision_=~std::uint64_t(0);
    // Native consequences of one accepted destruction: speculation settles, a tree activates its fallen foliage
    // and a canonical tree is published.
    void record(const Destruction&,bool canonical);
public:
    WorldEvents(Registry&,stock_native_destructible::Runtime&,Scene&,std::uint32_t space);
    template<class Backend>WorldEvents(Registry&r,Backend&b,Scene&s,std::uint32_t space):WorldEvents(r,b.runtime(),s,space){}
    ~WorldEvents();
    WorldEvents(const WorldEvents&)=delete;WorldEvents&operator=(const WorldEvents&)=delete;
    // Retires the empty stock AreaDestructibles animator clock and binds the battle identity pack() is fenced by.
    // Existing stock animator bodies are rejected: install before any legacy authority advances.
    bool install(_object * battle);
    // Ordered (sequence,event-dict) rows. Transport admission is the caller's
    // external boundary; no callback or LAN method runs inside this owner.
    _object * pack(_object * battle);
    bool ack(std::uint64_t sequence);
    bool ingress(_object * canonical_event,double now);
    // Projectile CAS transport owns publication. This applies only the native
    // consequences of an accepted destruction and is safe to retry.
    bool mirrorOnly(const Destruction&);
    bool mappingChanged(double now);
    TrustedTreeStatus trustedTreeStatus(const Key&);
    ContactResult commitTrustedTrees(const std::set<MaterialKey>& token,
        const Point& start,const Point& end,double end_yaw,double speed,double now);
    std::set<Key> takeActivatedTrees(){auto out=std::move(activated_trees_);activated_trees_.clear();return out;}
};
}
