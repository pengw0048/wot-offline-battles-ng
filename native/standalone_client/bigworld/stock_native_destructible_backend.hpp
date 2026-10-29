#pragma once
#include "stock_native_destructible_runtime.hpp"
#include "stock_native_bot_world_adapter.hpp"
#include "stock_native_bot_foliage_snapshot.hpp"

struct _object;
namespace stock_native_destructible {
struct PresentationCounters {
    std::uint64_t python_boundary_calls=0,emit_calls=0,lifetime_stop_calls=0,light_sound_calls=0,failed_calls=0;
};
// The only Python references retained here are original resource presentation
// lists/callables. No simulation, identity or collision policy is script-owned.
class NativeBackend final:public Backend,public stock_native_world::Effects,public stock_native_bot_foliage::TreePoses {
    struct TreeResource {
        Parameters parameters;
        _object * fracture=nullptr,*touchdown=nullptr;
    };
    BW::ClientSpacePtr space_;
    stock_native_world::Registry & registry_;
    std::map<stock_native_world::Key,BW::StockDestructibleBinding> bindings_;
    std::map<stock_native_world::Key,std::uint64_t> incarnations_;
    std::uint64_t next_incarnation_=0;
    std::map<stock_native_world::Key,BW::StockDestructibleBinding> presentation_bindings_;
    std::map<stock_native_world::Key,BW::Matrix> initial_;
    std::map<stock_native_world::Key,TreeResource> trees_;
    _object *emit_=nullptr,*stop_=nullptr,*remove_sound_=nullptr;
    Runtime runtime_;
    PresentationCounters presentation_counters_;
    bool retired_=false;
    std::function<bool(const stock_native_world::TurretSweep &)> turret_;
    bool read(const Destruction &,BW::StockDestructibleSnapshot &,
        BW::StockDestructibleBinding &,int module=-1)const;
    bool callPresentation(_object * function,_object * arguments,PresentationPhase);
public:
    NativeBackend(BW::ClientSpacePtr,stock_native_world::Registry &,
        const std::function<bool(const stock_native_world::TurretSweep &)> &);
    ~NativeBackend();
    NativeBackend(const NativeBackend &)=delete;
    NativeBackend &operator=(const NativeBackend &)=delete;
    // Install at the lifecycle/resource boundary, after exact identity proof.
    bool installPresenter();
    void bind(const stock_native_world::Key &,const BW::StockDestructibleBinding &);
    void installTree(const stock_native_world::Key &,const Parameters &,
        _object * fracture_variants,_object * touchdown_variants);
    void clear();
    // In-flight presentation may synchronously retire its owner. Keep all
    // referenced records intact until the pinned owner leaves its CAPI call.
    void retire(){retired_=true;}
    Runtime & runtime(){return runtime_;}
    const PresentationCounters& presentation_counters()const{return presentation_counters_;}
    bool current()const override;
    SnapshotStatus snapshot(const Destruction &,Parameters &)override;
    bool fallPitch(const Destruction &,double,double &,std::uint32_t &)override;
    bool position(const Body &,double)override;
    bool geometry(const Destruction &,const ChildEffect &)override;
    bool present(const Destruction &,int,PresentationPhase)override;
    bool destroy(const Destruction &)override;
    bool turretSweep(const stock_native_world::TurretSweep &)override;
    std::uint64_t generation(const Destruction&)const override;
    bool replay(const Destruction&,const Body*)override;
    bool waiting(std::uint32_t chunk)const override{return runtime_.waiting(chunk);}
    bool loaded(std::uint32_t chunk)const override;
    bool pose(stock_native_bot_foliage::Key,const stock_native_bot_foliage::Point&,
        const stock_native_bot_foliage::Point&,stock_native_bot_foliage::Point&,
        std::array<stock_native_bot_foliage::Point,3>&)override;
};
}
