#pragma once
#include "stock_native_projectile_owner.hpp"
#include "stock_native_client_state.hpp"
#include "stock_native_client_drive_python.hpp"
#include "stock_native_client_query.hpp"
#include <memory>
struct _object;
namespace stock_native_client_projectile {
struct Frozen;
class World final:public stock_native_projectile::OwnerWorld {
    struct State;
    std::unique_ptr<State> state_;
public:
    World(std::shared_ptr<stock_native_client_state::State>,stock_native_client_drive::SceneAccess,
        std::shared_ptr<stock_native_client_query::ProjectileTurrets>);
    ~World();
    bool current()const;
    bool install(_object*battle);
    bool ingress(_object*battle,double now);
    void clock(stock_native_projectile::Owner*,double now);
    std::vector<stock_native_projectile::Record> records()override;
    bool liveGeometry(const stock_native_projectile::Record&,const stock_native_projectile::Pose&)const override;
    stock_native_projectile::CollisionQuery collide(const stock_native_projectile::Record&,
        const stock_native_projectile::Point&,const stock_native_projectile::Point&,
        const std::optional<stock_native_projectile::Pose>&)override;
    std::optional<double> turretBlock(const stock_native_projectile::Chord&)override;
    stock_native_projectile::SceneResult scenery(const stock_native_projectile::Cursor&,
        stock_native_projectile::FlightMeta&,const stock_native_projectile::Point&,
        const stock_native_projectile::Point&,const stock_native_projectile::Point&)override;
    stock_native_projectile_damage::Resolution resolve(stock_native_projectile::Flight&,
        const stock_native_projectile::Cursor&,const stock_native_projectile::TerminalData&)override;
    std::vector<stock_native_world::Destruction> takeDestructions()override;
    std::string shooterKey(const stock_native_projectile::Launch&)const override;
    bool pipelined(const stock_native_projectile::Launch&)const override{return true;}
    bool historyRecord(const stock_native_projectile::Record&r)const override{return r.started&&!r.tombstone;}
    std::optional<double> localLaunchTime(const stock_native_projectile::Launch&,double)const override;
    double wireValue(double)const override;
    void frozen(const Frozen&);
    stock_native_projectile_damage::Random& random();
};
}
