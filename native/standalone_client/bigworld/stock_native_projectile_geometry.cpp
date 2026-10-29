#include "pch.hpp"
#include "stock_native_projectile_geometry.hpp"
#include "stock_native_projectile_cone.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace stock_native_projectile_damage {
namespace {
namespace geometry=stock_native_bot_geometry;
BW::Vector3 native(const Point&p){return BW::Vector3(float(p[0]),float(p[1]),float(p[2]));}
Point point(const BW::Vector3&p){return {p.x,p.y,p.z};}
BW::Vector3 toLocal(const geometry::Frames& f,unsigned i,const Point&p){return f.component_inverse[i].applyPoint((i==0?f.chassis_inverse:f.body_inverse).applyPoint(native(p)));}
BW::Vector3 toWorld(const geometry::Frames& f,unsigned i,const Point&p){BW::Matrix component;if(!component.invert(f.component_inverse[i]))throw std::runtime_error("Projectile component frame is singular");return (i==0?f.chassis:f.body).applyPoint(component.applyPoint(native(p)));}
}
GeometryWorld::GeometryWorld(const geometry::Vehicle& vehicle,const wot::offline_bot_step::InternalLayout& layout,const geometry::Pose& pose,
 std::function<bool(const Point&,const Point&,const Point&)> visible,std::array<std::optional<unsigned>,2> stickers,std::optional<bool> detached):vehicle_(vehicle),layout_(layout),pose_(pose),visible_(std::move(visible)),stickers_(stickers){if(detached)vehicle_.detached=*detached;}
std::vector<BlastComponent> GeometryWorld::components(const Point& burst,double){std::vector<BlastComponent> out;if(!vehicle_.ready)return out;const auto frames=geometry::frames(vehicle_,pose_);
 for(unsigned i=0;i<4;++i){const auto& c=vehicle_.components[i];if((i>=2&&vehicle_.detached)||!c.tree||!c.bounds_available)continue;bool structural=false;
  for(const auto& material:c.materials)if(material.second.vehicle_damage_factor&&*material.second.vehicle_damage_factor>0.){structural=true;break;}if(!structural)continue;
  out.push_back({i,point(toLocal(frames,i,burst)),c.minimum,c.maximum});}return out;}
Point GeometryWorld::toWorld(unsigned component,const Point& local){if(component>=4)throw std::invalid_argument("Projectile component index invalid");return point(::stock_native_projectile_damage::toWorld(geometry::frames(vehicle_,pose_),component,local));}
std::vector<ImpactLayer> GeometryWorld::vehicleRay(const Point& start,const Point& end){std::vector<ImpactLayer> result;for(const auto& h:geometry::intersections(vehicle_,pose_,start,end)){ImpactLayer layer(h.distance,h.angle_cosine,h.component,h.material,h.material?h.material->use_lining:true);layer.local_point=h.local_point;layer.world_normal=h.world_normal;result.push_back(std::move(layer));}return result;}
bool GeometryWorld::visible(const Point& start,const Point& contact,const Point& burst){return visible_&&visible_(start,contact,burst);}
std::vector<CriticalLayer> GeometryWorld::internal(const Target&,const Point& start,const Point& end,bool explosion,const std::vector<ImpactLayer>& covered){
 const auto frames=geometry::frames(vehicle_,pose_);std::array<wot::offline_bot_step::InternalSegment,4> segments{};for(unsigned i=0;i<4;++i){if(i>=2&&vehicle_.detached)continue;segments[i]={point(toLocal(frames,i,start)),point(toLocal(frames,i,end)),true};}
 std::set<std::string> excluded;for(const auto& hit:covered)if(hit.material&&!hit.material->device.empty())excluded.insert(hit.material->device);
 double length=0.;for(int i=0;i<3;++i){const auto d=end[i]-start[i];length+=d*d;}length=std::sqrt(length);
 std::vector<wot::offline_bot_step::ImpactHit> hits;if(explosion)hits=internalCone(layout_,segments,length,excluded);else{hits=wot::offline_bot_step::internal_hits(layout_,segments,length);hits.erase(std::remove_if(hits.begin(),hits.end(),[&](const auto& hit){return excluded.count(hit.name)!=0;}),hits.end());}
 std::vector<CriticalLayer> result;for(const auto& hit:hits){CriticalLayer row;row.distance=hit.distance;row.name=hit.name;result.push_back(std::move(row));}return result;
}
std::optional<std::uint64_t> GeometryWorld::sticker(const Target&,const Point& start,const Point& end,const std::vector<ImpactLayer>& hits,const std::optional<ArmorContact>& contact,int result){
 const auto id=stickers_[result==2?1:0];if(!id)return {};const ImpactLayer* selected=nullptr;for(const auto& hit:hits){if(contact&&(hit.component!=hits[contact->layer].component||std::abs(hit.distance-contact->distance)>1.e-5))continue;if(!selected||hit.distance<selected->distance)selected=&hit;}
 if(!selected||selected->component==0||selected->component>=4)return {};const auto& component=vehicle_.components[selected->component];if(!component.tree||!component.bounds_available)return {};const auto frames=geometry::frames(vehicle_,pose_);
 const auto value=encodeSticker(selected->component,*id,component.minimum,component.maximum,point(toLocal(frames,selected->component,start)),point(toLocal(frames,selected->component,end)));if(!value)return {};
 if(BW::stockBspSegment(*component.tree,native(value->decoded_start),native(value->decoded_end)).empty())return {};return value->encoded;
}
}
