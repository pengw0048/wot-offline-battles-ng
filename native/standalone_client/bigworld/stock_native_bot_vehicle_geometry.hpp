#pragma once
#include "stock_native_bot_step_state.hpp"
#include "stock_native_bot_armor_law.hpp"
#include "stock_bsp_collision_model.hpp"
#include "offline_simulation_types.h"
#include "model/model.hpp"
#include "math/matrix.hpp"
#include "pyscript/script_math.hpp"
#include <map>
#include <optional>
#include <string>

namespace stock_native_bot_geometry {
using Key=offline_simulation::ActorKey;
using Point=wot::offline_bot_step::Point;
using Material=stock_native_bot_armor::Material;
struct Component {
    BW::ModelPtr model;
    const BW::BSPTree * tree=nullptr;
    Point minimum{},maximum{};
    bool bounds_available=false;
    std::map<std::uint32_t,Material> materials;
    std::string name;
};
struct Vehicle {
    std::array<Component,4> components;
    Point hull_offset{},turret_offset{},gun_offset{};
    std::optional<double> static_turret_yaw,static_gun_pitch;
    bool has_siege=false,authority_geometry=false,ready=false,detached=false;
    BW::MatrixProviderPtr native_body,native_ground,native_turret,native_gun;
    std::uint64_t identity=0;
};
struct Pose {
    Point position{};
    double yaw=0.,pitch=0.,roll=0.,turret_yaw=0.,gun_pitch=0.;
    int siege=0;
    bool native_components=true,native_hydraulics=true;
};
struct Frames {
    BW::Matrix body,chassis;
    BW::Matrix body_inverse,chassis_inverse;
    std::array<BW::Matrix,4> component_inverse;
};
struct Hit {
    double distance=0.,angle_cosine=0.;
    unsigned component=0;
    std::uint32_t material_kind=0;
    Point local_point{},world_point{},world_normal{};
    const Material * material=nullptr;
};
// This lifecycle reader pins the native Model/MatrixProvider owners. It keeps
// no callable or borrowed Python reference and never runs inside a slice.
bool readVehicle(PyObject * descriptor,PyObject * entity,bool authority_geometry,
    std::uint64_t identity,Vehicle &);
Frames frames(const Vehicle&,const Pose&);
std::vector<Hit> intersections(const Vehicle&,const Pose&,const Point&,const Point&);
std::optional<std::pair<Point,Point>> worldBounds(const Vehicle&,const Pose&,int component=-1);
class Vehicles {
    std::map<Key,Vehicle> actors_;
public:
    void install(Key,Vehicle);
    void remove(Key key){actors_.erase(key);}
    void detached(Key,bool);
    const Vehicle * find(Key)const;
    std::vector<Hit> intersections(Key,const Pose&,const Point&,const Point&)const;
    void clear(){actors_.clear();}
};
}
