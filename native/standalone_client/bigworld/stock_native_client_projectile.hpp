#pragma once
#include "stock_native_projectile_owner.hpp"
#include "stock_native_projectile_damage_python.hpp"
#include "stock_native_client_weapon.hpp"
#include <map>
#include <algorithm>
#include <functional>
#include <optional>
namespace stock_native_client_projectile {
namespace projectile=stock_native_projectile;
namespace damage=stock_native_projectile_damage;
using Point=projectile::Point;
// Exact binary-rational, ties-to-even six-decimal protocol rounding. This is
// distinct from Python 2.7 round(), which is retained for gameplay milliseconds.
double wireRound(double);
struct Barrel {Point hull{},turret{},gun{};double length=0.;};
Point barrelPoint(const Barrel&,const Point&position,const Point&attitude,double turret_yaw,double gun_pitch);
using Normalizer=std::function<Point(Point)>;
struct Trigger {
    std::int64_t round=0,player=0,input_sequence=0,server_ms=0;
    int team=0,shell=0;
    std::string vehicle;
    Point origin{},direction{},position{};
    damage::LaunchDescriptor mounted;
    std::array<std::optional<unsigned>,2> stickers;
    double now=0.,reload_factor=1.,dispersion=0.;
    bool perfect=false,deadeye=false;
};
struct Frozen {
    projectile::Launch flight;
    damage::LaunchDescriptor source;
    std::array<std::optional<unsigned>,2> stickers;
    std::int64_t round=0,player=0,input_sequence=0;
    int team=0,shells_before=0;
    double reload_factor=1.,local_time=0.;
};
void normalize(Frozen&);
struct Burst {
    int count=0,committed=0,shell=0;
    std::int64_t group=0;
    double start=0.,interval=0.;
};
struct Metrics {std::uint64_t triggers=0,launched=0,send_failures=0,commit_failures=0,rejected=0;};
class Lifecycle {
    projectile::Owner& owner_;
    std::map<std::string,Frozen> launches_;
    std::optional<Frozen> pending_;
    std::optional<stock_native_client_weapon::Gun> gun_before_;
    std::optional<Burst> burst_;
    std::int64_t next_sequence_=0;
    Metrics metrics_;
public:
    explicit Lifecycle(projectile::Owner&owner):owner_(owner){}
    const Frozen* begin(const Trigger&,stock_native_client_weapon::Gun&,damage::Random&,const Normalizer& normalizer={});
    bool complete(bool sent,stock_native_client_weapon::Gun&);
    bool canonical(const Frozen&,double now,bool admit=true);
    bool cancelBurst(stock_native_client_weapon::Gun&,double reload_factor);
    bool burstDue(double now)const;
    void reset(bool preserve_sequence=false);
    void prune();
    void authorityChanged();
    void sequence(std::int64_t value){next_sequence_=std::max(next_sequence_,value);}
    const std::map<std::string,Frozen>& launches()const{return launches_;}
    const std::optional<Frozen>& pending()const{return pending_;}
    const std::optional<Burst>& burst()const{return burst_;}
    const Metrics& metrics()const{return metrics_;}
};
}
