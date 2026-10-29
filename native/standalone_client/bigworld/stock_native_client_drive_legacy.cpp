#include "stock_native_client_drive_legacy.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace stock_native_client_drive {
namespace {
namespace world=stock_native_world;
double clamp(double v,double lo,double hi){return std::max(lo,std::min(hi,v));}
bool valid(const std::array<std::optional<double>,5>&samples,double yaw,double length,double width){for(const auto&v:samples)if(!v||!std::isfinite(*v))return false;return std::isfinite(yaw)&&std::isfinite(length)&&std::isfinite(width)&&length>0.&&width>0.;}
void commit(PlayerState&s,const LegacyPlane&plane){s.pitch=s.terrain_pitch=plane.pitch;s.roll=plane.roll;s.downhill=plane.downhill;s.slope_tangent=plane.slope;s.surface_up_cosine=plane.plane.normal[1];s.ground_plane=plane.plane;}
Point rounded(const Point&p){return {float(p[0]),float(p[1]),float(p[2])};}
}
std::optional<LegacyPlane>sampledPlayerPlane(const std::array<std::optional<double>,5>&samples,double yaw,double length,double width,double maximum){
    if(!valid(samples,yaw,length,width)||!std::isfinite(maximum)||maximum<0.)return {};const double front=*samples[0],rear=*samples[1],right=*samples[2],left=*samples[3],center=*samples[4];const double longitudinal=(front+rear)*.5,lateral=(right+left)*.5;
    const double residual=std::max({std::abs(longitudinal-lateral),std::abs(center-longitudinal),std::abs(center-lateral)});if(residual>maximum)return {};
    const double f=(front-rear)/length,r=(right-left)/width,sn=std::sin(yaw),cs=std::cos(yaw);LegacyPlane out;auto&p=out.plane;p.gradient_x=f*sn+r*cs;p.gradient_z=f*cs-r*sn;p.center_y=center;
    const double scale=std::max({1.,std::abs(p.gradient_x),std::abs(p.gradient_z)}),nx=p.gradient_x/scale,ny=1./scale,nz=p.gradient_z/scale,normal_length=std::sqrt(nx*nx+ny*ny+nz*nz);if(!std::isfinite(normal_length)||normal_length<=0.)return {};p.normal={-nx/normal_length,ny/normal_length,-nz/normal_length};if(p.normal[1]<=0.)return {};out.pitch=-std::atan2(front-rear,length);out.roll=std::atan2(r,std::hypot(1.,f));out.slope=std::hypot(f,r);if(!std::isfinite(out.slope))return {};
    const double downhill=std::hypot(-p.gradient_x,-p.gradient_z);out.downhill=downhill>.001?Vec{-p.gradient_x/downhill,-p.gradient_z/downhill}:Vec{};return out;
}
std::optional<LegacyPlane>sampledPlayerSupport(const std::array<std::optional<double>,5>&samples,double yaw,double length,double width){
    if(!valid(samples,yaw,length,width))return {};const std::array<Point,5>points{{{0.,length*.5,*samples[0]},{0.,-length*.5,*samples[1]},{width*.5,0.,*samples[2]},{-width*.5,0.,*samples[3]},{0.,0.,*samples[4]}}};std::vector<std::array<double,4>>faces;
    for(unsigned i=0;i<3;++i)for(unsigned j=i+1;j<4;++j)for(unsigned k=j+1;k<5;++k){const auto&a=points[i],&b=points[j],&c=points[k];const double dx1=b[0]-a[0],dz1=b[1]-a[1],dy1=b[2]-a[2],dx2=c[0]-a[0],dz2=c[1]-a[1],dy2=c[2]-a[2],determinant=dx1*dz2-dx2*dz1;if(std::abs(determinant)<=1.e-12)continue;
        const double u=(-a[0]*dz2+a[1]*dx2)/determinant,v=(-dx1*a[1]+dz1*a[0])/determinant;if(std::min({u,v,1.-u-v})<-1.e-9)continue;
        const double gx=(dy1*dz2-dy2*dz1)/determinant,gz=(dx1*dy2-dx2*dy1)/determinant,height=a[2]-gx*a[0]-gz*a[1];bool upper=true;for(const auto&p:points)if(p[2]>height+gx*p[0]+gz*p[1]+1.e-7){upper=false;break;}if(upper)faces.push_back({height,gx*gx+gz*gz,gx,gz});}
    if(faces.empty())return {};double lowest=faces[0][0];for(const auto&face:faces)lowest=std::min(lowest,face[0]);double gx=0.,gz=0.;unsigned count=0;for(const auto&face:faces)if(face[0]<=lowest+1.e-7){gx+=face[2];gz+=face[3];++count;}gx/=count;gz/=count;
    double height=-std::numeric_limits<double>::infinity();for(const auto&p:points)height=std::max(height,p[2]-gx*p[0]-gz*p[1]);const auto plane=sampledPlayerPlane({height+gz*length*.5,height-gz*length*.5,height+gx*width*.5,height-gx*width*.5,height},yaw,length,width,1.e-6);if(!plane)return {};auto out=*plane;for(const auto&p:points)out.contact_residual=std::max(out.contact_residual,height+gx*p[0]+gz*p[1]-p[2]);return out;
}
LegacySupport::LegacySupport(world::Pipeline&pipeline,const PlayerDescriptor&descriptor,double epsilon,double limit):pipeline_(pipeline),descriptor_(descriptor),plane_epsilon_(epsilon),pitch_limit_(limit){if(epsilon<0.||!std::isfinite(epsilon)||limit<=0.||!std::isfinite(limit))throw std::invalid_argument("Native player legacy support metadata is invalid");}
std::optional<double>LegacySupport::column(double x,double z,double hint,std::optional<double>maximum){const auto end=rounded({x,-1000.,z});auto start=rounded({x,hint+2.,z});for(unsigned layer=0;layer<4;++layer){world::Hit hit;const auto status=pipeline_.ground(start,end,hit);if(status!=world::QueryStatus::Found)return {};const double y=hit.point[1];if((!maximum||y<=*maximum)&&hit.normal[1]>.5)return y;const double next=y-.05;if(next<=end[1]+.01)return {};start=rounded({x,next,z});}return {};}
std::optional<double>LegacySupport::nearGround(double x,double z,double hint){const auto end=rounded({x,hint-30.,z});auto start=rounded({x,hint+8.,z});for(unsigned layer=0;layer<4;++layer){world::Hit hit;const auto status=pipeline_.ground(start,end,hit);if(status!=world::QueryStatus::Found)return {};const double y=hit.point[1];if(-14.<y-hint&&y-hint<6.&&hit.normal[1]>0.)return y;const double next=y-.05;if(!(end[1]<next&&next<start[1]))return {};start=rounded({x,next,z});}return {};}
LegacySamples LegacySupport::terrain(const PlayerState&s,const Point&position,double yaw,std::optional<double>maximum,std::optional<double>follow){const double length=std::max(1.5,descriptor_.motion.collision_shape[1]),width=std::max(.3,descriptor_.motion.collision_shape[0]),sn=std::sin(yaw),cs=std::cos(yaw);LegacySamples out;std::array<std::optional<double>,5>samples;
    for(unsigned i=0;i<3;++i){const double distance=i==0?length:i==1?0.:-length;const auto y=column(position[0]+sn*distance,position[2]+cs*distance,position[1],maximum);if(!y)continue;if(!out.highest||*y>*out.highest)out.highest=y;if(i==0)samples[0]=y;else if(i==1)samples[4]=out.center=y;else samples[1]=y;}
    samples[2]=column(position[0]+cs*width,position[2]-sn*width,position[1],maximum);samples[3]=column(position[0]-cs*width,position[2]+sn*width,position[1],maximum);out.plane=sampledPlayerSupport(samples,yaw,length*2.,width*2.);if(out.plane){out.plane->plane.center_x=position[0];out.plane->plane.center_z=position[2];}sample_=Sample{position[0],position[2],yaw,out.plane};
    if(out.plane){out.center=out.plane->plane.center_y;return out;}if(!follow||!s.grounded_once||(out.center&&position[1]-*out.center<=*follow))return out;
    std::optional<double>bridged;for(const auto&pair:std::array<std::array<unsigned,2>,2>{{{0,1},{2,3}}}){const auto&a=samples[pair[0]],&b=samples[pair[1]];if(!a||!b||*a-position[1]<-*follow||*b-position[1]<-*follow||*a-position[1]>.12||*b-position[1]>.12)continue;const double highest=std::max(*a,*b);bridged=bridged?std::max(*bridged,highest):highest;}
    if(!out.center){out.highest=out.center=bridged;return out;}if(!bridged||*bridged<=*out.center)return out;if(!out.highest||*bridged>*out.highest)out.highest=bridged;out.center=bridged;return out;
}
std::optional<LegacyPlane>LegacySupport::sampleRaw(const Point&position,double yaw){const double length=std::max(3.,std::abs(descriptor_.motion.hull_minimum[2])+std::abs(descriptor_.motion.hull_maximum[2])),width=std::max(2.,std::abs(descriptor_.motion.hull_minimum[0])+std::abs(descriptor_.motion.hull_maximum[0])),sn=std::sin(yaw),cs=std::cos(yaw);
    auto plane=sampledPlayerPlane({nearGround(position[0]+sn*length*.5,position[2]+cs*length*.5,position[1]),nearGround(position[0]-sn*length*.5,position[2]-cs*length*.5,position[1]),nearGround(position[0]+cs*width*.5,position[2]-sn*width*.5,position[1]),nearGround(position[0]-cs*width*.5,position[2]+sn*width*.5,position[1]),nearGround(position[0],position[2],position[1])},yaw,length,width,plane_epsilon_);if(!plane)return {};plane->plane.center_x=position[0];plane->plane.center_z=position[2];return plane;}
std::optional<step::SupportPlane>LegacySupport::samplePlane(const Point&position,double yaw){const auto plane=sampleRaw(position,yaw);return plane?std::optional<step::SupportPlane>{plane->plane}:std::nullopt;}
void LegacySupport::groundPose(PlayerState&s,const Point&position,double yaw){if(sample_&&sample_->x==position[0]&&sample_->z==position[2]&&sample_->yaw==yaw&&sample_->plane){commit(s,*sample_->plane);if(sample_->plane->contact_residual>plane_epsilon_){s.ground_plane.reset();s.downhill={};s.slope_tangent=0.;}return;}
    const auto plane=sampleRaw(position,yaw);if(!plane){s.downhill={};s.slope_tangent=0.;s.ground_plane.reset();s.surface_up_cosine.reset();return;}commit(s,*plane);
}
std::vector<LegacyImpact>LegacySupport::vertical(PlayerState&s,Point&position,double yaw,double dt,std::optional<Point>tick){std::vector<LegacyImpact>impacts;s.support_blocked=false;sample_.reset();const double gravity=descriptor_.motion.drive.t[0],gap=clamp(descriptor_.ground_follow_base+std::max(0.,s.speed*std::tan(clamp(s.last_drive_pitch,-pitch_limit_,pitch_limit_))*std::max(0.,dt))+gravity*std::max(0.,dt)*std::max(0.,dt),descriptor_.ground_follow_min,descriptor_.ground_follow_max);
    if(!s.airborne)s.vertical_speed=std::min(s.vertical_speed,-s.speed*std::tan(clamp(s.pitch,-pitch_limit_,pitch_limit_)));auto support=terrain(s,position,yaw,{},gap);auto ground=support.center?support.center:support.highest;
    const auto launch=[&](){const double v=s.speed*std::sin(-s.last_drive_pitch);return v>0.?v:0.;};
    if(ground){const double maximum=std::max(.6,std::abs(s.speed)*dt*2.5);double land=support.center.value_or(*ground);
        if(!s.grounded_once){position[1]=land;s.vertical_speed=0.;s.airborne=false;s.grounded_once=true;}
        else{if(support.center&&*support.center-position[1]>std::min(std::max(0.,maximum),.85)+.02){const double ceiling=position[1]+std::min(std::max(0.,maximum),.85)+.02;support=terrain(s,position,yaw,ceiling);ground=support.center?support.center:support.highest;
                if(!ground||*ground>ceiling){if(tick)position=*tick;s.vertical_speed=0.;s.airborne=false;s.support_blocked=true;return impacts;}land=support.center.value_or(*ground);}
            const bool reachable=*ground>=position[1]+s.vertical_speed*std::max(0.,dt)-gravity*std::max(0.,dt)*std::max(0.,dt)-.002;
            if(position[1]<*ground-.002||(position[1]<=*ground&&s.vertical_speed<=0.)||(!s.airborne&&reachable)){
                if(s.airborne&&s.vertical_speed<0.)impacts.push_back({{position[0],land,position[2]},std::abs(s.vertical_speed)});const double next=position[1]<*ground?position[1]+std::min(*ground-position[1],maximum):*ground;s.vertical_speed=dt>0.&&!s.airborne?(next<position[1]?(next-position[1])/dt:launch()):0.;position[1]=next;s.airborne=false;s.grounded_once=true;
            }else{if(!s.airborne&&std::abs(s.vertical_speed)<1.e-8)s.vertical_speed=launch();s.airborne=true;const unsigned steps=std::min(8,std::max(1,int(std::abs(s.vertical_speed*dt)/.5)+1));const double sub=dt/double(steps);double next=position[1];for(unsigned i=0;i<steps;++i){s.vertical_speed-=gravity*sub;next+=s.vertical_speed*sub;if(next<=land){next=land;impacts.push_back({{position[0],land,position[2]},std::abs(s.vertical_speed)});s.vertical_speed=0.;s.airborne=false;s.grounded_once=true;break;}}position[1]=next;}
        }
    }else if(s.grounded_once){if(!s.airborne&&std::abs(s.vertical_speed)<1.e-8)s.vertical_speed=launch();s.airborne=true;s.vertical_speed-=gravity*dt;position[1]+=s.vertical_speed*dt;}else{s.vertical_speed=0.;s.airborne=false;}return impacts;
}
}
