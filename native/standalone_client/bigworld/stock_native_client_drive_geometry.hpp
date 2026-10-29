#pragma once
#include "stock_native_client_drive.hpp"
struct _object;
namespace stock_native_client_drive {
// The legacy set also holds three-field structure keys; those are not trees.
std::optional<stock_native_world::Key> visibleTreeKey(_object*);
// Local accepted visible-scene snapshots. Only changed cells and the current
// native stream revision invalidate the last support envelope; no map scan or
// Python identity/geometry producer runs on this boundary.
class LocalGeometry {
    struct Stamp {
        std::array<int,4> region{};
        std::int64_t global=0,unpublished=0;
        std::uint64_t stream=0;
        std::vector<std::int64_t> cells;
        bool operator==(const Stamp&o)const{return region==o.region&&global==o.global&&unpublished==o.unpublished&&stream==o.stream&&cells==o.cells;}
    };
    Stamp stamp_;
    std::set<stock_native_world::Key> members_;
    std::set<std::uint32_t> mirrored_chunks_;
    std::set<stock_native_world::Key> mirrored_slots_;
    bool installed_=false,isolation_seeded_=false;
    std::uint64_t refreshes_=0,reuses_=0;
public:
    void seedIsolation(_object* sensor);
    bool sync(_object* sensor,stock_native_world::Registry&,
        stock_native_world::Scene&,const std::array<Point,2>& region);
    std::uint64_t refreshes()const{return refreshes_;}
    std::uint64_t reuses()const{return reuses_;}
};
}
