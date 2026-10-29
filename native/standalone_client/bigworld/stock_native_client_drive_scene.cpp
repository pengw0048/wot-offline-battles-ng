#include "stock_native_client_drive.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace stock_native_client_drive {
namespace world=stock_native_world;
std::optional<double> SceneGround::column(double x,double z,double low,double high,std::optional<double> flat) {
    layers_.clear();if(high<low)std::swap(high,low);double start=high;
    for(unsigned layer=0;layer<3;++layer){world::Hit hit;const auto status=pipeline_.ground({x,start,z},{x,low,z},hit);
        if(status==world::QueryStatus::Failed)throw std::runtime_error("Native player suspension ground query failed");
        if(status==world::QueryStatus::Miss)return {};
        const double height=hit.point[1],length=std::sqrt(hit.normal[0]*hit.normal[0]+hit.normal[1]*hit.normal[1]+hit.normal[2]*hit.normal[2]);
        if(!std::isfinite(height)||!std::isfinite(length)||length<=.000001)throw std::runtime_error("Native player suspension ground hit is malformed");
        const double up=hit.normal[1]/length;
        const auto verdict=height<low-.01||height>high+.01?GroundVerdict::OutsideBand:up<=.5?GroundVerdict::Wall:
            flat&&height>*flat+.01&&up>=.995?GroundVerdict::AboveFlat:GroundVerdict::Support;
        layers_.push_back({height,up,verdict});if(verdict==GroundVerdict::Support)return height;
        const double next=height-.02;if(next<=low+.01||next>=start)break;start=next;
    }return {};
}
std::optional<double> SceneGround::initial(double x,double z,double hint) {
    // Exact local _ground_y near-hull pass, followed by its allow_wide pass.
    double start=hint+8.;const double end=hint-30.;
    for(unsigned layer=0;layer<4;++layer){world::Hit hit;const auto status=pipeline_.ground({x,start,z},{x,end,z},hit);
        if(status!=world::QueryStatus::Found)break;const double value=hit.point[1];
        if(-14.<value-hint&&value-hint<6.&&hit.normal[1]>0.)return value;
        const double next=value-.05;if(!(end<next&&next<start))break;start=next;}
    start=std::max(1000.,hint+50.);std::optional<double> height;
    for(unsigned layer=0;layer<4;++layer){world::Hit hit;auto status=pipeline_.ground({x,start,z},{x,-1000.,z},hit);
        if(status==world::QueryStatus::Failed)throw std::runtime_error("Native player spawn ground query failed");
        if(status==world::QueryStatus::Miss)return {};height=hit.point[1];
        world::Hit below;status=pipeline_.ground({x,*height-.4,z},{x,-1000.,z},below);
        if(status==world::QueryStatus::Failed)throw std::runtime_error("Native player spawn lower-ground query failed");
        if(status==world::QueryStatus::Miss||*height-below.point[1]<2.5)return height;start=*height-.4;
    }return height;
}
}
