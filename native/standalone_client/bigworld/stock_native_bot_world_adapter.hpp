#pragma once

#include "stock_native_world_pipeline.hpp"
#include "stock_destructibles.hpp"
#include "space/client_space.hpp"

namespace stock_native_world {
// The resource loader installs identities only after the existing #1513
// catalog/layout proof. Subsequent queries retain this exact native endpoint.
struct FrozenIdentity {
    Instance instance;
    BW::StockDestructibleBinding binding;
    BW::Matrix reference_world;
    int category=-1;
    LocatorSignature locator_signature{};
    double quantization=0.;
    bool canonical_ambiguous=false,native_pending=false;
    std::vector<Box> local_boxes;
};
class Effects {
public:
    virtual ~Effects()=default;
    virtual void bind(const Key&,const BW::StockDestructibleBinding&)=0;
    virtual bool destroy(const Destruction&)=0;
    virtual bool turretSweep(const TurretSweep&)=0;
};
class NativeScene final : public Scene {
    BW::ClientSpacePtr space_;
    Registry& registry_;
    Effects& effects_;
    std::map<Key,FrozenIdentity> identities_;
    std::map<std::uint32_t,std::vector<Key>> by_chunk_;
    std::uint64_t stream_revision_=0;
    bool retired_=false;
    bool locator(const Key&,const FrozenIdentity&,const BW::StockDestructibleSnapshot&,
        BW::Matrix&,BW::Matrix&)const;
public:
    NativeScene(BW::ClientSpacePtr,Registry&,Effects&);
    bool current()const;
    void retire(){retired_=true;}
    // false means the resource proof no longer identifies the live mapping.
    bool install(const Key&,FrozenIdentity);
    void remove(const Key&);
    QueryStatus segment(const Point&,const Point&,std::uint16_t,const Filter&,Hit&)override;
    QueryStatus material(const Point&,const Point&,const Point&,MaterialHit&)override;
    QueryStatus materialFiltered(const Point&,const Point&,const Point&,const Filter&,MaterialHit&)override;
    QueryStatus water(const Point&,const Point&,double&)override;
    Admission refresh(const Key&,Registry&)override;
    bool unresolved(const Key&,std::vector<Box>&)override;
    bool destroy(const Destruction&)override;
    bool treeChunks(const std::vector<Box>&,std::vector<std::uint32_t>&)override;
    bool warmTreeChunks(const std::vector<std::uint32_t>&)override;
    TreeIdentity trustedTree(const Key&)override;
    std::uint64_t streamRevision()const override;
    bool turretSweep(const TurretSweep&)override;
};
}
