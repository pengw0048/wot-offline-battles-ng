#pragma once
#include "offline_visibility.h"
#include <optional>
struct _object;
namespace stock_native_bot_foliage {
using Key=std::pair<std::uint32_t,std::uint32_t>;
using Point=native_visibility::Vec3;
struct TreeProfile {Point center{},half_sizes{};std::optional<std::size_t> standing;};
struct StablePose {std::array<double,12> signature{};unsigned reads=0;};
struct TreeState {
    native_visibility::FoliageSnapshot foliage;
    std::map<Key,TreeProfile> profiles;
    std::set<Key> activated,refreshing,seen_bodies;
    std::map<Key,std::size_t> instances;
    std::map<Key,std::vector<native_visibility::Cell>> cells;
    std::map<std::size_t,std::vector<native_visibility::Cell>> standing_cells;
    std::map<Key,StablePose> stable;
    double next_refresh=0.;
};
// These readers are installation/event-ingress marshalling only. They never
// invoke the Python visibility or foliage helper methods.
bool readFoliageSnapshot(_object * battle,native_visibility::FoliageSnapshot&);
bool readFoliageUpdate(_object * battle,offline_visibility::Update&);
bool readFoliageTreeState(_object * battle,TreeState&);
class TreePoses {
public:
    virtual ~TreePoses()=default;
    virtual bool waiting(std::uint32_t chunk)const=0;
    virtual bool loaded(std::uint32_t chunk)const=0;
    virtual bool pose(Key,const Point& local_center,const Point& half_sizes,
        Point& world_center,std::array<Point,3>& half_axes)=0;
};
class Trees {
    TreeState state_;
    bool update(Key,const Point&,const std::array<Point,3>&,
        std::set<std::size_t>&,std::set<native_visibility::Cell>&);
public:
    explicit Trees(TreeState state):state_(std::move(state)){}
    bool activate(Key);
    // Call at the original frame stage before visibility. The source is the
    // native destructible binding/body owner, never a per-tree Python call.
    bool refresh(double now,const std::set<Key>& active_bodies,TreePoses&,
        offline_visibility::Update&,bool force=false,bool force_no_animation=false);
    const TreeState& state()const{return state_;}
};
}
