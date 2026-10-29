#include "stock_native_client_drive.hpp"
#include <cmath>
#include <limits>
using namespace stock_native_client_drive;
namespace {
step::SuspensionDescriptor descriptor(const double* tuning){step::SuspensionDescriptor d;d.parameters={10000.,30000.,10000.,1./60.,10.};
    for(unsigned i=0;i<9;++i)d.tuning[i]=tuning[i];d.maximum_steps=int(d.tuning[8]);d.width=2.5;d.clearance=.3;d.rest_length=.5;d.contact_memory_distance=.5;d.footprint_half_width=.15;
    for(int side:{-1,1})for(int i=0;i<5;++i){step::SuspensionContact c;c.x=side;c.z=i-2.;c.side=side;c.spring=true;c.stiffness=10000.*d.tuning[0]/2.;
        c.damping=2000.;c.compression=.2;c.maximum=.4;c.force=100000.;c.rest_length=.5;c.footprint_front=c.footprint_rear=.5;d.springs.push_back(c);}
    for(int side:{-1,1})for(int i=0;i<4;++i){step::SuspensionContact c;c.x=side;c.z=i-1.5;c.side=side;c.track=true;c.penetration=.04;d.pseudo.push_back(c);}
    for(int i=0;i<4;++i){step::SuspensionContact c;c.y=.3;c.z=i-1.5;c.penetration=.1;d.pseudo.push_back(c);}
    for(double x:{-1.25,1.25})for(double y:{.3,2.})for(double z:{-2.5,2.5}){step::SuspensionContact c;c.x=x;c.y=y;c.z=z;c.rigid=true;c.penetration=.1;d.rigid.push_back(c);}
    d.turret_origin=Point{0.,1.,0.};for(double x:{-.5,.5})for(double y:{0.,1.})for(double z:{-.5,.5})d.turret_contacts.push_back({x,y,z});return d;}
struct PlaneGround:Ground{double gx=0.,gz=0.,height=0.;bool holes=false,missing=false;
    std::optional<double> initial(double x,double z,double)override{return missing?std::nullopt:std::optional<double>(height+gx*x+gz*z);}
    std::optional<double> column(double x,double z,double low,double high,std::optional<double>flat)override{
        if(missing||(holes&&std::abs(std::remainder(z,2.))<.08))return {};
        const double value=height+gx*x+gz*z,up=1./std::sqrt(1.+gx*gx+gz*gz);
        return value>=low-.01&&value<=high+.01&&up>.5&&(!flat||value<=*flat+.01||up<.995)?std::optional<double>(value):std::nullopt;}};
}
extern "C" int client_drive_fixture(const double*in,double*out){try{
    Owner owner(descriptor(in));unsigned n=9;Input input;auto&s=input.state;
    s.position={in[n],in[n+1],in[n+2]};n+=3;s.yaw=in[n++];s.pitch=s.terrain_pitch=in[n++];s.roll=in[n++];s.vertical_speed=in[n++];s.pitch_speed=in[n++];s.roll_speed=in[n++];s.support_vertical_speed=in[n++];
    s.speed=in[n++];s.turn_speed=in[n++];s.airborne=in[n++]!=0.;s.grounded_once=in[n++]!=0.;s.left_flying=in[n++]!=0.;s.right_flying=in[n++]!=0.;
    input.tick_position={in[n],in[n+1],in[n+2]};n+=3;const bool motion=in[n++]!=0.;Point mp{in[n],in[n+1],in[n+2]};n+=3;if(motion)input.motion_position=mp;
    input.elapsed=in[n++];input.turret_yaw=in[n++];input.height_delta=in[n++];input.speed_delta=in[n++];PlaneGround ground;
    ground.gx=in[n++];ground.gz=in[n++];ground.height=in[n++];ground.holes=in[n++]!=0.;ground.missing=in[n++]!=0.;
    const bool previous=in[n++]!=0.;const double gx=in[n++],gz=in[n++],height=in[n++];
    if(previous){step::SupportPlane p;p.center_y=height;p.gradient_x=gx;p.gradient_z=gz;const double len=std::sqrt(1.+gx*gx+gz*gz);p.normal={-gx/len,1./len,-gz/len};s.ground_plane=p;}
    if(in[n++]!=0.)s.support_gradient=std::array<double,2>{gx,gz};
    const auto result=owner.advance(input,ground);const auto&v=result.state;unsigned k=0;
    for(double q:v.position)out[k++]=q;for(double q:{v.yaw,v.pitch,v.roll,v.vertical_speed,v.pitch_speed,v.roll_speed,v.support_vertical_speed,v.speed,v.turn_speed})out[k++]=q;
    for(bool q:{v.airborne,v.grounded_once,v.left_flying,v.right_flying,result.outcome.blocked,result.outcome.pending_spawn})out[k++]=q;
    out[k++]=result.outcome.landing_impact.has_value();out[k++]=result.outcome.landing_impact.value_or(0.);out[k++]=v.ground_plane.has_value();
    for(double q:{v.ground_plane?v.ground_plane->gradient_x:0.,v.ground_plane?v.ground_plane->gradient_z:0.,v.ground_plane?v.ground_plane->center_y:0.})out[k++]=q;
    out[k++]=result.contacts.size();for(const auto&c:result.contacts){out[k++]=c.ground.has_value();out[k++]=c.ground.value_or(0.);}
    out[k++]=result.outcome.landing_witnesses.size();for(const auto&q:result.outcome.landing_witnesses)for(double p:q)out[k++]=p;
    return int(k);
}catch(...){return -1;}}
