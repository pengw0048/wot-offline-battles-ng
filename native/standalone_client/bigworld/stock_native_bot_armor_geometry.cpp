#include "stock_native_bot_armor_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace stock_native_bot_armor {
namespace {
std::vector<XZ> vertices(const Body& b)
{
    const double s=std::sin(b.yaw),c=std::cos(b.yaw);std::vector<XZ> result;
    for(const auto sign:std::vector<XZ>{{{-1.,-1.}},{{1.,-1.}},{{1.,1.}},{{-1.,1.}}})
        result.push_back({{b.x+sign[0]*b.shape[0]*c+sign[1]*b.shape[1]*s,b.z-sign[0]*b.shape[0]*s+sign[1]*b.shape[1]*c}});
    return result;
}
struct Probe {
    Geometry& geometry;
    std::array<Body,2> bodies;
    std::array<Body,2> physical;
    std::array<XZ,2> normals;
    std::array<std::set<std::string>,2> tracks;
    std::optional<double> operator()(unsigned index,const Point& hit,const XZ& normal)
    {
        const double normal_length=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]);
        if(!std::isfinite(normal_length)||normal_length<=1.e-6)return {};
        const XZ inward{{normal[0]/normal_length,normal[1]/normal_length}};
        const auto& b=physical[index];const double reach=std::sqrt(b.shape[0]*b.shape[0]+b.shape[1]*b.shape[1])+1.;
        double depth=(b.x-hit[0])*inward[0]+(b.z-hit[2])*inward[1];
        if(!std::isfinite(depth)||depth< -1.e-6)return {};
        depth=std::max(0.,depth);
        const Point start=geometry.native_point({{hit[0]-inward[0]*reach,hit[1],hit[2]-inward[1]*reach}});
        const Point end=geometry.native_point({{hit[0]+inward[0]*depth,hit[1],hit[2]+inward[1]*depth}});
        auto layers=geometry.segment(bodies[index],start,end);if(layers.empty())return {};
        std::stable_sort(layers.begin(),layers.end(),[](const Layer&a,const Layer&c){return a.distance<c.distance;});
        const auto& first=layers.front();
        const bool incoming=first.distance>=reach-.15&&first.distance<=reach+depth+1.e-6&&
            normals[index][0]*inward[0]+normals[index][1]*inward[1]>1.-1.e-6;
        if(incoming&&first.material&&(first.material->device=="leftTrackHealth"||first.material->device=="rightTrackHealth"))tracks[index].insert(first.material->device);
        for(const auto& layer:layers) {
            const auto m=layer.material;if(!m||!m->armor||!m->vehicle_damage_factor)continue;
            if(std::isfinite(*m->armor)&&*m->armor>0.&&std::isfinite(*m->vehicle_damage_factor)&&*m->vehicle_damage_factor>0.)return *m->armor;
        }
        return {};
    }
    Plates result(double a,double b)const {
        Plates p;p.armor={{a,b}};
        for(unsigned i=0;i<2;++i)p.tracks[i]={tracks[i].begin(),tracks[i].end()};
        return p;
    }
};
}
std::vector<XZ> overlap_polygon(const Body&a,const Body&b)
{
    auto polygon=vertices(a);const auto clip=vertices(b);
    for(unsigned i=0;i<4;++i) {
        const auto start=clip[i],end=clip[(i+1)%4];const double ex=end[0]-start[0],ez=end[1]-start[1];
        auto inside=[&](const XZ&p){return ex*(p[1]-start[1])-ez*(p[0]-start[0])>=-1.e-7;};
        auto intersect=[&](const XZ&first,const XZ&second){const double x=second[0]-first[0],z=second[1]-first[1],d=x*ez-z*ex;if(std::abs(d)<=1.e-12)return second;const double r=((start[0]-first[0])*ez-(start[1]-first[1])*ex)/d;return XZ{{first[0]+r*x,first[1]+r*z}};};
        if(polygon.empty())return {};
        std::vector<XZ> output;auto previous=polygon.back();bool previous_inside=inside(previous);
        for(const auto& current:polygon) {
            const bool current_inside=inside(current);
            if(current_inside!=previous_inside)output.push_back(intersect(previous,current));
            if(current_inside)output.push_back(current);
            previous=current;previous_inside=current_inside;
        }
        polygon=std::move(output);
    }
    return polygon;
}
std::vector<double> sample_heights(double low,double high,double hit)
{
    if(!std::isfinite(hit))return {};
    if(!std::isfinite(low)||!std::isfinite(high)||high<=low)return {hit};
    const double observed=std::max(low,std::min(high,hit));std::vector<double> raw;
    for(double fraction:{.5,1./3.,2./3.,.2,.8})raw.push_back(low+(high-low)*fraction);
    std::stable_sort(raw.begin(),raw.end(),[&](double a,double b){return std::abs(a-observed)<std::abs(b-observed);});
    raw.insert(raw.begin(),observed);std::vector<double> result;
    for(double h:raw)if(std::none_of(result.begin(),result.end(),[&](double old){return std::abs(h-old)<=1.e-4;}))result.push_back(h);
    return result;
}
std::array<double,2> hull_interval(const Hull&h,double y,double pitch,double roll)
{
    const double sp=std::sin(pitch),cp=std::cos(pitch),sr=std::sin(roll),cr=std::cos(roll);
    std::array<double,2> range{{std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()}};
    for(unsigned bits=0;bits<8;++bits) {
        const double x=((bits&1)?h.maximum[0]:h.minimum[0])+h.mount[0];
        const double ly=((bits&2)?h.maximum[1]:h.minimum[1])+h.mount[1];
        const double z=((bits&4)?h.maximum[2]:h.minimum[2])+h.mount[2];
        const double height=y+cp*(sr*x+cr*ly)-sp*z;range[0]=std::min(range[0],height);range[1]=std::max(range[1],height);
    }
    return range;
}
std::optional<Plates> contact_pair(const Body&a,const Body&b,const XZ&raw,Geometry& geometry)
{
    if(a.id==b.id||!geometry.ready(a)||!geometry.ready(b))return {};
    const double length=std::sqrt(raw[0]*raw[0]+raw[1]*raw[1]);if(!std::isfinite(length)||length<=1.e-6)return {};
    const XZ normal{{raw[0]/length,raw[1]/length}};
    const double low=std::max(a.y+a.shape[2],b.y+b.shape[2]),high=std::min(a.y+a.shape[3],b.y+b.shape[3]);if(high<=low)return {};
    const auto polygon=overlap_polygon(a,b);if(polygon.empty())return {};
    XZ center{};for(const auto&p:polygon){center[0]+=p[0];center[1]+=p[1];}for(double&v:center)v/=double(polygon.size());
    const auto physical_a=geometry.physical_body(a),physical_b=geometry.physical_body(b);
    auto heights=sample_heights(low,high,(low+high)*.5);const auto ah=geometry.hull(a),bh=geometry.hull(b);
    if(ah&&bh) {
        const auto av=hull_interval(*ah,physical_a.y,physical_a.pitch,physical_a.roll),bv=hull_interval(*bh,physical_b.y,physical_b.pitch,physical_b.roll);
        const double lo=std::max(low,std::max(av[0],bv[0])),hi=std::min(high,std::min(av[1],bv[1]));
        if(hi>lo){const auto structural=sample_heights(lo,hi,(lo+hi)*.5);heights.insert(heights.begin()+1,structural.begin(),structural.end());}
    }
    const XZ tangent{{-normal[1],normal[0]}};
    auto projection=[&](const XZ&p){return p[0]*tangent[0]+p[1]*tangent[1];};
    auto smaller=[&](const XZ&x,const XZ&z){return projection(x)<projection(z);};
    const auto first=*std::min_element(polygon.begin(),polygon.end(),smaller),last=*std::max_element(polygon.begin(),polygon.end(),smaller);
    std::vector<XZ> points{center};
    for(const auto&side:{first,last}) {
        XZ p{{(center[0]+side[0])*.5,(center[1]+side[1])*.5}};
        if(std::none_of(points.begin(),points.end(),[&](const XZ&q){return std::pow(p[0]-q[0],2)+std::pow(p[1]-q[1],2)<=1.e-6;}))points.push_back(p);
    }
    Probe probe{geometry,{{a,b}},{{physical_a,physical_b}},{{normal,{{-normal[0],-normal[1]}}}}, {}};
    struct Unresolved{Point point;std::array<std::optional<double>,2> plates;};std::vector<Unresolved> unresolved;
    for(const auto&p:points)for(double y:heights) {
        const Point point=geometry.native_point({{p[0],y,p[1]}});const auto pa=probe(0,point,probe.normals[0]),pb=probe(1,point,probe.normals[1]);
        if(pa&&pb)return probe.result(*pa,*pb);unresolved.push_back({point,{{pa,pb}}});
    }
    for(auto candidate:unresolved) {
        for(unsigned i=0;i<2;++i)if(!candidate.plates[i]) {
            const auto&body=probe.physical[i];const double dx=body.x-candidate.point[0],dz=body.z-candidate.point[2],size=std::hypot(dx,dz);
            if(!std::isfinite(size)||size<=1.e-6)continue;
            const XZ n{{dx/size,dz/size}};const double alignment=n[0]*probe.normals[i][0]+n[1]*probe.normals[i][1];
            if(alignment<=0.||alignment>=1.-1.e-9)continue;
            candidate.plates[i]=probe(i,candidate.point,n);
        }
        if(candidate.plates[0]&&candidate.plates[1])return probe.result(*candidate.plates[0],*candidate.plates[1]);
    }
    return {};
}
}
