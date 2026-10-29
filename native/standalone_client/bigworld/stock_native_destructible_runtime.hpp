#pragma once

#include "stock_native_world_pipeline.hpp"

#include <array>
#include <map>

namespace stock_native_destructible {
using stock_native_world::Destruction;
using stock_native_world::Kind;
using stock_native_world::MaterialKey;
using Frame = std::array<float,16>;
enum class SnapshotStatus { Missing, Ready, Invalid };
enum class PresentationPhase {
    StopLifetime, Fracture, Decay, Ram, Hit, Touchdown,
    TouchdownBreak, RemoveLightSound
};
struct Parameters {
    // Exact tree descriptor values or wg_getFallingParams matrix fields.
    double scale=0.,height=0.,mass=0.,spring_angle=0.,stiffness=0.;
    double spring_resist=0.,air_resist=0.,bury_depth=0.;
    // Frozen initial chunk-local matrix; never reread the moving matrix.
    Frame initial{};
};
struct Body {
    Destruction order;
    Frame initial{};
    double pitch=0.,pitch_speed=0.,inertia=0.,mass=0.,height=0.;
    double spring_angle=0.,stiffness=0.,spring_resist=0.,air_resist=0.;
    double pitch_constraint=0.,simulation_time=0.,bury_depth=0.,fall_yaw=0.;
    bool touchdown_pending=false,touchdown_effects=false;
};
struct MoveResult {bool active=true,touchdown=false;};
// Exact #1513 AreaDestructibles._DestructiblesAnimator.__moveBody law.
// Integration is double, as in CPython; native matrices remain float32.
MoveResult moveBody(Body &,double dt);
double encodedYaw(double yaw);
double encodedTreePitch(double pitch);
Body makeBody(const Destruction &,const Parameters &,double yaw,double pitch,
    int discrete_speed,bool touchdown_effects);
using ChildEffect=std::function<bool(const Destruction &)>;
class Backend {
public:
    virtual ~Backend()=default;
    virtual bool current()const=0;
    virtual SnapshotStatus snapshot(const Destruction &,Parameters &)=0;
    virtual bool fallPitch(const Destruction &,double yaw,double & pitch,
        std::uint32_t & collision_flags)=0;
    // Native Matrix.setRotateX/preMultiply/postMultiply, translation bury,
    // then wg_setDestructibleMatrix's local-to-chunk transform and broadphase.
    virtual bool position(const Body &,double wind)=0;
    // Execute the real compiled geometry operation with animate=true. Each
    // BSMA child event is routed to this native handler before child removal.
    virtual bool geometry(const Destruction &,const ChildEffect &)=0;
    // Only this low-frequency resource presentation seam may enter original
    // #1513 TerrainEffects. It must not call mod policy, authority or sensors.
    virtual bool present(const Destruction &,int module_index,
        PresentationPhase)=0;
    virtual std::uint64_t generation(const Destruction&)const{return 0;}
    // Rebind only a newly streamed native incarnation. No resource effect or
    // animation restart is permitted; restore geometry or the retained pose.
    virtual bool replay(const Destruction&,const Body*){return false;}
};
struct Counters {
    std::uint64_t accepted=0,queued=0,geometry=0,transforms=0,replays=0;
    std::uint64_t present_calls=0,mod_callbacks=0,body_steps=0;
};
class Runtime {
    struct Record {Destruction order;bool applied=false,delayed=false;double due=0.;std::optional<std::uint64_t> data;std::optional<Body> last_pose;std::uint64_t generation=0;};
    Backend & backend_;
    std::map<MaterialKey,Record> accepted_;
    std::vector<Body> bodies_;
    Counters counters_;
    std::uint64_t epoch_=1;
    bool applying_=false,updating_=false;
    bool apply(Record &,double now);
    bool geometry(const Destruction &);
    bool present(const Destruction &,PresentationPhase);
public:
    explicit Runtime(Backend & backend):backend_(backend){}
    bool destroy(const Destruction &);
    bool moduleChildEffect(const Destruction &);
    // Called by the native main-thread frame owner. No Python callback clock
    // or per-body callback exists; every body retains ordered substeps.
    void update(double elapsed,double now);
    void mappingChanged(double now);
    void clear();
    const std::vector<Body> & bodies()const{return bodies_;}
    const Counters & counters()const{return counters_;}
    std::optional<std::uint64_t> acceptedData(const Destruction&)const;
    bool applied(const Destruction&)const;
    std::vector<Destruction> acceptedOrders()const {
        std::vector<Destruction> out;for(const auto&row:accepted_)out.push_back(row.second.order);return out;
    }
    bool waiting(std::uint32_t chunk)const {
        for(const auto& row:accepted_)if(row.second.order.key.first==chunk&&!row.second.applied)return true;
        return false;
    }
};
}
