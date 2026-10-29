#if !defined(WOT_CLIENT_RAM_GEOMETRY_HOST_BUILD)
#include "pch.hpp"
#include "stock_native_bot_vehicle_geometry.hpp"
#endif
#include "stock_native_client_drive_ram.hpp"
#include "stock_native_client_query.hpp"
#include <cmath>
namespace stock_native_client_drive {
namespace {
namespace cs=stock_native_client_state;namespace geo=stock_native_bot_geometry;
class NativeSurface final:public RamSurface {
    std::shared_ptr<cs::Actor> actor_;
    std::shared_ptr<const void> geometry_;
    const geo::Vehicle*vehicle_;
    geo::Pose pose_;
    std::uint64_t descriptor_;
    std::int64_t engine_;
    bool current()const{return actor_&&!actor_->tombstone&&!actor_->missing&&actor_->ready&&actor_->engine_id==engine_&&actor_->descriptor_revision==descriptor_&&actor_->geometry==geometry_;}
public:
    NativeSurface(std::shared_ptr<cs::Actor> actor,const RamPose&p):actor_(std::move(actor)),geometry_(actor_->geometry),vehicle_(stock_native_client_query::geometry(*actor_)),descriptor_(actor_->descriptor_revision),engine_(actor_->engine_id){
        pose_.position={p[0],p[1],p[2]};pose_.yaw=p[3];pose_.pitch=p[4];pose_.roll=p[5];
        // The original proof freezes the supplied body matrix, while stock
        // component providers still supply current turret/gun transforms.
        pose_.native_hydraulics=false;pose_.native_components=true;
    }
    bool available()const{return vehicle_&&vehicle_->ready;}
    std::optional<std::array<double,2>>hullSpan()const override{
        if(!current()||!available()||!vehicle_->components[1].bounds_available)return {};
        const auto&hull=vehicle_->components[1];const auto&offset=vehicle_->hull_offset;
        BW::Matrix matrix;matrix.setRotate(float(pose_.yaw),float(pose_.pitch),float(pose_.roll));
        const double pitch=matrix.pitch(),roll=matrix.roll();
        const double sp=std::sin(pitch),cp=std::cos(pitch),sr=std::sin(roll),cr=std::cos(roll);
        std::optional<std::array<double,2>>out;
        for(unsigned ix=0;ix<2;++ix)for(unsigned iy=0;iy<2;++iy)for(unsigned iz=0;iz<2;++iz){const double x=(ix?hull.maximum[0]:hull.minimum[0])+offset[0],y=(iy?hull.maximum[1]:hull.minimum[1])+offset[1],z=(iz?hull.maximum[2]:hull.minimum[2])+offset[2],height=double(float(pose_.position[1]))+cp*(sr*x+cr*y)-sp*z;if(!out)out=std::array<double,2>{height,height};else{(*out)[0]=std::min((*out)[0],height);(*out)[1]=std::max((*out)[1],height);}}
        return out;
    }
    std::vector<RamLayer>intersections(const cs::Point&a,const cs::Point&b)const override{
        std::vector<RamLayer>out;if(!current()||!available())return out;
        for(const auto&hit:geo::intersections(*vehicle_,pose_,a,b)){RamLayer row;row.distance=hit.distance;if(hit.material){row.armor=hit.material->armor;row.vehicle_damage_factor=hit.material->vehicle_damage_factor;row.device=hit.material->device;}out.push_back(std::move(row));}return out;
    }
};
class NativeGeometry final:public RamGeometry {
public:
 std::shared_ptr<const RamSurface>capture(const std::shared_ptr<cs::Actor>&actor,const RamPose&pose,const std::array<double,4>&)const override{if(!actor||!actor->geometry)return {};auto out=std::make_shared<NativeSurface>(actor,pose);return out->available()?out:nullptr;}
};
}
std::shared_ptr<const RamGeometry>nativeRamGeometry(){return std::make_shared<NativeGeometry>();}
}
