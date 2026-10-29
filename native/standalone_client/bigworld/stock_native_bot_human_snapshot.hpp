#pragma once
#include "stock_native_bot_runtime.hpp"
#include <map>
struct _object;
namespace stock_native_bot {
struct HumanProjection {
    offline_simulation::control::ActorConfig config;
    bool last_effort=false,designated=false;
};
struct HumanDescriptor {
    ID id=0;
    std::uint64_t record_revision=0,descriptor_revision=0;
    bool target_available=false;
    std::string vehicle,class_tag;
    double armor=0.,mass=0.,contact_mass=0.;
    Point body_minimum{},body_maximum{};
    std::optional<Point> spawn_anchor;
    std::optional<double> spawn_drive_cap;
    double contact_drive_forward=0.,contact_drive_reverse=0.,contact_rotation_speed=0.,track_center=0.;
    bool pivot_available=false,rotation_around_center=false;
    std::array<double,4> shape{};
    contacts::Profile ram;
    double rolling_decel=0.,slide_hold=0.,gravity=0.,side_min_y=0.,side_full_y=0.,side_min=0.,side_full=0.;
    std::vector<std::string> crew;
    std::optional<double> vision_maximum,radio_maximum;
    // Index is ((crew mask * 2 + fire) * 3 + vision module state).
    std::vector<HumanProjection> projections;
    perception::Actor template_actor;
    std::vector<step::AimSample> aim_samples;
};
struct HumanIngress {Human human;step::WeaponTarget target;double radius=0.;};
// Installation may invoke the reviewed descriptor/effective_params producers.
// It freezes all crew/fire spotting states and retains no interpreter objects.
bool readHumanDescriptor(_object* runtime,_object* raw,_object* descriptor,HumanDescriptor&);
bool readHumanProjections(_object* raw,_object* effective,_object* descriptor,HumanDescriptor&);
// This ingress reads dictionaries/sequences only; it never invokes Python.
bool readHumanInput(_object* raw,const HumanDescriptor&,HumanIngress&);
}
