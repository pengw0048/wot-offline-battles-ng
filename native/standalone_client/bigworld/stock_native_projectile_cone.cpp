#include "stock_native_projectile_cone.hpp"
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
namespace stock_native_projectile_damage {
namespace {
using namespace wot::offline_bot_step;
using P=Point;using Primitive=InternalPrimitive;
const double tangent=std::sqrt(1.-.7071067811865476*.7071067811865476)/.7071067811865476;
P add(const P&a,const P&b){return {a[0]+b[0],a[1]+b[1],a[2]+b[2]};} P sub(const P&a,const P&b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
P mul(const P&a,double x){return {a[0]*x,a[1]*x,a[2]*x};}double dot(const P&a,const P&b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
P cross(const P&a,const P&b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
double length(const P&a){return std::sqrt(dot(a,a));}bool zero(const P&a){return dot(a,a)<=1.e-16;}
P yaw(const P&p,double deg){const double a=deg*std::acos(-1.)/180.,c=std::cos(a),s=std::sin(a);return {p[0]*c-p[2]*s,p[1],p[0]*s+p[2]*c};}
P center(const Primitive& p){if(p.shape==InternalShape::box&&!p.has_center)return mul(add(p.minimum,p.maximum),.5);return p.center;}
P half(const Primitive& p){return p.has_half_extents?p.half_extents:mul(sub(p.maximum,p.minimum),.5);}
P support(const Primitive& p,const P& direction){const P c=center(p);const double len=length(direction);const P unit=len<=1.e-8?P{1.,0.,0.}:mul(direction,1./len);
 if(p.shape==InternalShape::sphere)return add(c,mul(unit,std::max(0.,p.radius)));
 if(p.shape==InternalShape::capsule){P a{};a[p.axis]=1.;a=yaw(a,p.yaw_degrees);return add(c,add(mul(a,dot(direction,a)>=0.?p.half_length:-p.half_length),mul(unit,std::max(0.,p.radius))));}
 const P local=yaw(direction,-p.yaw_degrees);P q{};if(p.shape==InternalShape::ellipsoid){double denominator=0.;for(int i=0;i<3;++i){const double r=std::max(.0001,p.radii[i]);denominator+=r*r*local[i]*local[i];}denominator=std::sqrt(denominator);if(denominator<=1.e-8)return c;for(int i=0;i<3;++i){const double r=std::max(.0001,p.radii[i]);q[i]=r*r*local[i]/denominator;}}
 else {const auto h=half(p);for(int i=0;i<3;++i)q[i]=(local[i]>=0.?1.:-1.)*std::max(0.,h[i]);}return add(c,yaw(q,p.yaw_degrees));}
P coneSupport(const P& apex,const P& axis,double depth,const P& d){const double axial=dot(d,axis);const P radial=sub(d,mul(axis,axial));const double r=length(radial);if(depth<=0.||axial+tangent*r<=0.)return apex;const P base=add(apex,mul(axis,depth));return r<=1.e-8?base:add(base,mul(radial,depth*tangent/r));}
std::pair<bool,P> line(std::vector<P>& s){const auto a=s.back(),b=s[s.size()-2],ab=sub(b,a),ao=mul(a,-1.);if(dot(ab,ao)<=0.){s={a};return {false,ao};}const P d=cross(cross(ab,ao),ab);if(!zero(d))return {false,d};const double den=dot(ab,ab),projection=den>1.e-12?dot(ao,ab)/den:0.;if(projection<=1.+1.e-7)return {true,{}};s={b};return {false,mul(b,-1.)};}
std::pair<bool,P> triangle(std::vector<P>& s){const P a=s.back(),b=s[s.size()-2],c=s[s.size()-3],ab=sub(b,a),ac=sub(c,a),ao=mul(a,-1.),abc=cross(ab,ac);
 if(dot(cross(abc,ac),ao)>0.){s=dot(ac,ao)>0.?std::vector<P>{c,a}:std::vector<P>{b,a};return line(s);}if(dot(cross(ab,abc),ao)>0.){s={b,a};return line(s);}if(dot(abc,ao)>0.)return {false,abc};s={b,c,a};return {false,mul(abc,-1.)};}
std::pair<bool,P> tetra(std::vector<P>& s){const P a=s.back(),b=s[s.size()-2],c=s[s.size()-3],d=s[s.size()-4],ao=mul(a,-1.),ab=sub(b,a),ac=sub(c,a),ad=sub(d,a);
 std::vector<std::vector<P>> faces{{c,b,a},{d,c,a},{b,d,a}};P normals[]={cross(ab,ac),cross(ac,ad),cross(ad,ab)},opposite[]={ad,ab,ac};for(int i=0;i<3;++i){auto n=normals[i];if(dot(n,opposite[i])>0.)n=mul(n,-1.);if(dot(n,ao)>0.){s=faces[i];return triangle(s);}}return {true,{}};}
bool intersects(const Primitive&p,const P& apex,const P& axis,double depth){P direction=sub(center(p),add(apex,mul(axis,depth*.5)));if(zero(direction))direction={1.,0.,0.};auto sample=[&](const P& d){return sub(support(p,d),coneSupport(apex,axis,depth,mul(d,-1.)));};P first=sample(direction);std::vector<P> simplex{first};direction=mul(first,-1.);if(zero(direction))return true;
 for(int i=0;i<48;++i){const P q=sample(direction);if(dot(q,direction)<-1.e-7)return false;for(const auto& existing:simplex)if(length(sub(q,existing))<=1.e-8)return false;simplex.push_back(q);auto result=simplex.size()==2?line(simplex):simplex.size()==3?triangle(simplex):tetra(simplex);if(result.first||zero(result.second))return true;direction=result.second;}return false;}
P triangleClosest(const P&p,const P&a,const P&b,const P&c){const auto ab=sub(b,a),ac=sub(c,a),ap=sub(p,a);const double d1=dot(ab,ap),d2=dot(ac,ap);if(d1<=0.&&d2<=0.)return a;const auto bp=sub(p,b);const double d3=dot(ab,bp),d4=dot(ac,bp);if(d3>=0.&&d4<=d3)return b;const double vc=d1*d4-d3*d2;if(vc<=0.&&d1>=0.&&d3<=0.)return add(a,mul(ab,d1/(d1-d3)));const auto cp=sub(p,c);const double d5=dot(ab,cp),d6=dot(ac,cp);if(d6>=0.&&d5<=d6)return c;const double vb=d5*d2-d1*d6;if(vb<=0.&&d2>=0.&&d6<=0.)return add(a,mul(ac,d2/(d2-d6)));const double va=d3*d6-d5*d4;if(va<=0.&&d4-d3>=0.&&d5-d6>=0.)return add(b,mul(sub(c,b),(d4-d3)/((d4-d3)+(d5-d6))));const double den=va+vb+vc;if(den==0.)return a;return add(add(a,mul(ab,vb/den)),mul(ac,vc/den));}
std::pair<double,P> nearest(const Primitive&p,const P& point){const auto c=center(p);if(p.shape==InternalShape::mesh){if(p.mesh.closed&&!internal_intervals(p,point,point).empty())return {0.,point};double best=std::numeric_limits<double>::infinity();P nearest{};for(const auto& f:p.mesh.triangles){const auto q=triangleClosest(point,p.mesh.vertices[f[0]],p.mesh.vertices[f[1]],p.mesh.vertices[f[2]]);const double d=length(sub(q,point));if(d<best){best=d;nearest=q;}}return {best,nearest};}
 if(p.shape==InternalShape::sphere){const auto delta=sub(point,c);const double d=length(delta),r=std::max(0.,p.radius);if(d<=r||d<=1.e-6)return {0.,point};return {d-r,add(c,mul(delta,r/d))};}
 const auto local=yaw(sub(point,c),-p.yaw_degrees);P q{};
 if(p.shape==InternalShape::capsule){P a{};a[p.axis]=std::max(-p.half_length,std::min(p.half_length,local[p.axis]));const P d=sub(local,a);const double n=length(d),r=std::max(.0001,p.radius);if(n<=r)return {0.,point};q=add(a,mul(d,r/n));}
 else if(p.shape==InternalShape::ellipsoid){P r;double normal=0.;for(int i=0;i<3;++i){r[i]=std::max(.0001,p.radii[i]);normal+=std::pow(local[i]/r[i],2.);}if(normal<=1.)return {0.,point};auto equation=[&](double x){double result=-1.;for(int i=0;i<3;++i)result+=std::pow(r[i]*std::abs(local[i])/(x+r[i]*r[i]),2.);return result;};double high=*std::max_element(r.begin(),r.end())*std::max(length(local),*std::max_element(r.begin(),r.end())),low=0.;while(equation(high)>0.&&high<=1.e6)high*=2.;for(int i=0;i<36;++i){const double m=(low+high)*.5;if(equation(m)>0.)low=m;else high=m;}const double lambda=(low+high)*.5;for(int i=0;i<3;++i)q[i]=r[i]*r[i]*local[i]/(lambda+r[i]*r[i]);}
 else {if(std::abs(p.yaw_degrees)<=1.e-4){for(int i=0;i<3;++i)q[i]=std::max(p.minimum[i],std::min(p.maximum[i],point[i]));return {length(sub(point,q)),q};}const auto h=half(p);for(int i=0;i<3;++i)q[i]=std::max(-h[i],std::min(h[i],local[i]));}
 const P result=add(c,yaw(q,p.yaw_degrees));return {length(sub(point,result)),result};}
std::vector<P> clipPlane(const std::vector<P>& polygon,const P& axis,double limit,bool above){std::vector<P> out;if(polygon.empty())return out;P previous=polygon.back();double prev=dot(previous,axis)-limit;for(const auto& current:polygon){const double now=dot(current,axis)-limit;const bool before=above?prev>=0.:prev<=0.,after=above?now>=0.:now<=0.;if(before!=after)out.push_back(add(previous,mul(sub(current,previous),prev/(prev-now))));if(after)out.push_back(current);previous=current;prev=now;}return out;}
bool triangleCone(P a,P b,P c,const P& apex,const P& axis,double depth){a=sub(a,apex);b=sub(b,apex);c=sub(c,apex);auto polygon=clipPlane({a,b,c},axis,0.,true);polygon=clipPlane(polygon,axis,depth,false);if(polygon.empty())return false;auto quad=[&](const P&p,const P&q){return dot(p,q)-(1.+tangent*tangent)*dot(p,axis)*dot(q,axis);};
 for(std::size_t i=0;i<polygon.size();++i){const auto& p=polygon[i];const double f=quad(p,p);if(f<=1.e-10)return true;const P edge=sub(polygon[(i+1)%polygon.size()],p);const double aa=quad(edge,edge),bb=quad(p,edge);if(aa>0.){const double t=std::max(0.,std::min(1.,-bb/aa));if(f+2.*t*bb+t*t*aa<=1.e-10)return true;}}
 const auto e1=sub(b,a),e2=sub(c,a);const double aa=quad(e1,e1),bb=quad(e1,e2),cc=quad(e2,e2),det=aa*cc-bb*bb;if(aa>0.&&det>1.e-18){const double l1=quad(a,e1),l2=quad(a,e2),u=(bb*l2-cc*l1)/det,v=(bb*l1-aa*l2)/det;if(u>=-1.e-8&&v>=-1.e-8&&u+v<=1.+1.e-8){const auto q=add(add(a,mul(e1,u)),mul(e2,v));const double axial=dot(q,axis);if(-1.e-8<=axial&&axial<=depth+1.e-8&&quad(q,q)<=1.e-10)return true;}}return false;}
bool meshCone(const Primitive&p,const P& apex,const P& axis,double depth){for(const auto& face:p.mesh.triangles)if(triangleCone(p.mesh.vertices[face[0]],p.mesh.vertices[face[1]],p.mesh.vertices[face[2]],apex,axis,depth))return true;return false;}
std::optional<double> entry(const Primitive&p,const P& apex,const P& axis,double depth,double distance,const P& closest){if(distance<=1.e-7)return 0.;auto hit=[&](double d){return p.shape==InternalShape::mesh?meshCone(p,apex,axis,d):intersects(p,apex,axis,d);};if(!hit(depth))return {};
 const auto delta=sub(closest,apex);const double axial=dot(delta,axis),radial=length(sub(delta,mul(axis,axial)));if(p.shape!=InternalShape::mesh&&axial>=-1.e-7&&axial<=depth+1.e-7&&radial<=std::max(0.,axial)*tangent+1.e-7)return std::max(0.,axial);
 double low=0.,high=depth;for(int i=0;i<14;++i){const double middle=(low+high)*.5;if(hit(middle))high=middle;else low=middle;}return high;}
}
std::vector<wot::offline_bot_step::ImpactHit> internalCone(const InternalLayout& layout,const std::array<InternalSegment,4>& segments,double depth,const std::set<std::string>& covered){
 if(!layout.valid||depth<=.0001)return {};struct Candidate {double axial;std::string parent,zone,entity;std::size_t index;};std::vector<Candidate> candidates;
 std::set<std::string> excluded;for(auto name:covered){if(name.size()>=6&&name.substr(name.size()-6)=="Health")name.resize(name.size()-6);if(!name.empty())excluded.insert(name);}
 for(const auto& target:layout.targets){const int parent=target.parent=="chassis"?0:target.parent=="hull"?1:target.parent=="turret"?2:target.parent=="gun"?3:-1;if(parent<0||!segments[parent].available||target.entity.empty()||excluded.count(target.entity))continue;
  const auto& segment=segments[parent];auto axis=sub(segment.end,segment.start);const double len=length(axis);if(len<=.0001)continue;axis=mul(axis,1./len);
  for(std::size_t i=0;i<target.primitives.size();++i){const auto& p=target.primitives[i];const auto near=nearest(p,segment.start);if(near.first>depth*std::sqrt(2.)+.0001)continue;const auto at=entry(p,segment.start,axis,depth,near.first,near.second);if(at)candidates.push_back({*at,target.parent,target.zone_id,target.entity,i});}
 }
 std::stable_sort(candidates.begin(),candidates.end(),[](const Candidate&a,const Candidate&b){return std::tie(a.axial,a.parent,a.zone,a.index)<std::tie(b.axial,b.parent,b.zone,b.index);});std::set<std::string> seen;std::vector<ImpactHit> out;for(const auto& c:candidates)if(seen.insert(c.entity).second)out.push_back({c.axial,c.entity+"Health"});
 std::sort(out.begin(),out.end(),[](const ImpactHit&a,const ImpactHit&b){return std::tie(a.distance,a.name)<std::tie(b.distance,b.name);});return out;
}
}
