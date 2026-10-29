#pragma once
#include "stock_native_bot_environment.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>
struct _object;
namespace wot::offline_bot_step {
enum class InternalShape {box,sphere,ellipsoid,capsule,mesh};
struct InternalNode {Point minimum{},maximum{};int left=-1,right=-1;std::vector<int> triangles;};
struct InternalMesh {std::vector<Point> vertices;std::vector<std::array<int,3>> triangles;std::vector<int> orientations;
    std::vector<InternalNode> nodes;bool closed=false;};
struct InternalPrimitive {InternalShape shape=InternalShape::box;Point minimum{},maximum{},center{},half_extents{},radii{};
    double yaw_degrees=0,radius=0,half_length=0;bool has_center=false,has_half_extents=false;int axis=1;std::string id;InternalMesh mesh;};
struct InternalTarget {std::string parent,entity,zone_id;std::vector<InternalPrimitive> primitives;};
struct InternalLayout {bool valid=false;std::vector<InternalTarget> targets;};
struct InternalSegment {Point start{},end{};bool available=false;};
std::vector<std::pair<double,double>> internal_intervals(const InternalPrimitive&,const Point&,const Point&);
std::vector<ImpactHit> internal_hits(const InternalLayout&,const std::array<InternalSegment,4>&,double world_length);
// Installation only; snapshots all objects and retains no Python references.
bool load_internal_layout(_object* descriptor,InternalLayout&);
}
