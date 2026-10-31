#pragma once
#include "stock_native_client_drive.hpp"
namespace stock_native_client_drive {
// Local accepted visible-scene snapshots. Only changed cells and the current
// native stream revision invalidate the last support envelope; no map scan or
// Python identity/geometry producer runs on this boundary.
class LocalGeometry {
    struct Stamp {
        std::array<int,4> region{};
        std::uint64_t stream=0;
        bool operator==(const Stamp&o)const{return region==o.region&&stream==o.stream;}
    };
    Stamp stamp_;
    std::set<stock_native_world::Key> members_;
    bool installed_=false;
    std::uint64_t refreshes_=0,reuses_=0;
public:
    // Admits the baked catalog members of the support region through the native scene; isolation is the
    // registry's own (scene locator proof and catalog exclusions).
    bool sync(stock_native_world::Registry&,
        stock_native_world::Scene&,const std::array<Point,2>& region);
    std::uint64_t refreshes()const{return refreshes_;}
    std::uint64_t reuses()const{return reuses_;}
};
}
