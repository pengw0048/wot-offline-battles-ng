#include "stock_native_bot_internal_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
namespace wot::offline_bot_step {
namespace {
using Interval=std::pair<double,double>;
Point sub(const Point&a,const Point&b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
double dot(const Point&a,const Point&b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
Point cross(const Point&a,const Point&b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
std::optional<Interval> clip(Interval v,double lo,double hi){v.first=std::max(v.first,lo);v.second=std::min(v.second,hi);return v.first<=v.second?std::optional<Interval>(v):std::nullopt;}
std::optional<Interval> box(const Point&a,const Point&b,const Point&lo,const Point&hi) {
 double first=0.,last=1.;for(int i=0;i<3;++i){const double d=b[i]-a[i];if(std::abs(d)<=1.e-7){if(a[i]<lo[i]||a[i]>hi[i])return {};continue;}
  auto x=(lo[i]-a[i])/d,y=(hi[i]-a[i])/d;if(x>y)std::swap(x,y);first=std::max(first,x);last=std::min(last,y);if(first>last)return {};}
 return Interval{first,last};
}
std::optional<Interval> sphere(const Point&a,const Point&b,const Point&center,double radius){
 const auto d=sub(b,a),o=sub(a,center);const double aa=dot(d,d);if(aa<=1.e-7)return {};
 const double bb=2.*dot(o,d),cc=dot(o,o)-radius*radius,disc=bb*bb-4.*aa*cc;if(disc<0.)return {};
 const double root=std::sqrt(disc);return clip({(-bb-root)/(2.*aa),(-bb+root)/(2.*aa)},0.,1.);
}
Point rotate(const Point&p,const Point&center,double degrees){const double a=degrees*std::acos(-1.)/180.;if(std::abs(a)<=1.e-7)return p;
 const double dx=p[0]-center[0],dz=p[2]-center[2],c=std::cos(a),s=std::sin(a);
 return {center[0]+dx*c-dz*s,p[1],center[2]+dx*s+dz*c};}
bool line_box(const Point& start,const Point& delta,const Point& low,const Point& high,double first,double last){
 for(int i=0;i<3;++i){if(std::abs(delta[i])<1.e-15){if(start[i]<low[i]-1.e-8||start[i]>high[i]+1.e-8)return false;continue;}
  double a=(low[i]-start[i])/delta[i],b=(high[i]-start[i])/delta[i];if(a>b)std::swap(a,b);
  first=std::max(first,a);last=std::min(last,b);if(first>last+1.e-8)return false;}return true;
}
using Group=std::pair<double,int>;
std::vector<Group> mesh_hits(const InternalMesh& mesh,const Point& start,const Point& delta,double first,double last){
 if(mesh.nodes.empty())return {};std::vector<int> stack{0};std::vector<std::pair<double,int>> hits;
 while(!stack.empty()){const int index=stack.back();stack.pop_back();const auto& node=mesh.nodes.at(index);
  if(!line_box(start,delta,node.minimum,node.maximum,first,last))continue;
  if(node.left>=0){stack.push_back(node.left);stack.push_back(node.right);continue;}
  for(int face_index:node.triangles){const auto& face=mesh.triangles.at(face_index);const auto& a=mesh.vertices.at(face[0]);
   const auto e1=sub(mesh.vertices.at(face[1]),a),e2=sub(mesh.vertices.at(face[2]),a),p=cross(delta,e2);
   const double det=dot(e1,p);if(std::abs(det)<=1.e-14)continue;const auto t=sub(start,a);const double u=dot(t,p)/det;
   if(u< -1.e-8||u>1.+1.e-8)continue;const auto q=cross(t,e1);const double v=dot(delta,q)/det;
   if(v< -1.e-8||u+v>1.+1.e-8)continue;const double at=dot(e2,q)/det;
   if(at<first-1.e-8||at>last+1.e-8)continue;int sign=det>0.?1:-1;if(!mesh.orientations.empty())sign*=mesh.orientations.at(face_index);
   hits.push_back({at,sign});}}
 std::stable_sort(hits.begin(),hits.end());std::vector<Group> groups;
 for(const auto& hit:hits){const int mask=hit.second<0?1:2;if(!groups.empty() && std::abs(hit.first-groups.back().first)<=1.e-9)groups.back().second|=mask;else groups.push_back({hit.first,mask});}
 return groups;
}
std::vector<Interval> mesh_intervals(const InternalPrimitive& primitive,const Point& start,const Point& end){
 const auto& mesh=primitive.mesh;const auto d=sub(end,start);if(!line_box(start,d,primitive.minimum,primitive.maximum,0.,1.))return {};
 if(dot(d,d)<1.e-24){if(!mesh.closed)return {};const Point ray{1.,.3713906763541037,.52999894000318};
  const auto hits=mesh_hits(mesh,start,ray,-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::infinity());int n=0;
  for(const auto& hit:hits){if(std::abs(hit.first)<=1.e-8)return {{0.,1.}};if(hit.first>0. && hit.second!=3)++n;}return n%2?std::vector<Interval>{{0.,1.}}:std::vector<Interval>{};}
 const auto inf=std::numeric_limits<double>::infinity();const auto hits=mesh_hits(mesh,start,d,mesh.closed?-inf:0.,mesh.closed?inf:1.);
 std::vector<Interval> out;std::optional<double> entry;
 for(const auto& hit:hits){if(!mesh.closed){const auto t=std::max(0.,std::min(1.,hit.first));out.push_back({t,t});continue;}
  if(hit.second==3){if(hit.first>=0.&&hit.first<=1.)out.push_back({hit.first,hit.first});continue;}
  if(!entry)entry=hit.first;else {auto range=clip({*entry,hit.first},0.,1.);if(range)out.push_back(*range);entry.reset();}}
 std::sort(out.begin(),out.end());return out;
}
}
std::vector<std::pair<double,double>> internal_intervals(const InternalPrimitive& p,const Point& start,const Point& end){
 if(p.shape==InternalShape::mesh)return mesh_intervals(p,start,end);
 if(p.shape==InternalShape::box && std::abs(p.yaw_degrees)<=1.e-4) {
  const auto value=box(start,end,p.minimum,p.maximum);return value?std::vector<Interval>{*value}:std::vector<Interval>{};
 }
 const auto a=rotate(start,p.center,-p.yaw_degrees),b=rotate(end,p.center,-p.yaw_degrees);
 std::optional<Interval> result;
 if(p.shape==InternalShape::box){Point lo=p.minimum,hi=p.maximum;if(std::abs(p.yaw_degrees)>1.e-4)
   for(int i=0;i<3;++i){lo[i]=p.center[i]-p.half_extents[i];hi[i]=p.center[i]+p.half_extents[i];}
  result=box(a,b,lo,hi);
 } else if(p.shape==InternalShape::sphere)result=sphere(start,end,p.center,p.radius);
 else if(p.shape==InternalShape::ellipsoid){Point aa,bb;for(int i=0;i<3;++i){const double r=std::max(.0001,p.radii[i]);aa[i]=(a[i]-p.center[i])/r;bb[i]=(b[i]-p.center[i])/r;}
  result=sphere(aa,bb,Point{},1.);
 } else {const auto aa=sub(a,p.center),bb=sub(b,p.center),d=sub(bb,aa);const int axis=p.axis>=0&&p.axis<3?p.axis:1;
  const double radius=std::max(.0001,p.radius),half=std::max(0.,p.half_length);double qa=0.,qb=0.,qc=-radius*radius;
  for(int i=0;i<3;++i)if(i!=axis){qa+=d[i]*d[i];qb+=2.*aa[i]*d[i];qc+=aa[i]*aa[i];}
  std::vector<Interval> ranges;std::optional<Interval> cylinder;
  if(qa>1.e-7){const double disc=qb*qb-4.*qa*qc;if(disc>=0.){const double root=std::sqrt(disc);cylinder=clip({(-qb-root)/(2.*qa),(-qb+root)/(2.*qa)},0.,1.);}}
  else if(qc<=0.)cylinder=Interval{0.,1.};
  if(cylinder){if(std::abs(d[axis])<=1.e-7){if(aa[axis]>=-half&&aa[axis]<=half)ranges.push_back(*cylinder);}
   else {const double t1=(-half-aa[axis])/d[axis],t2=(half-aa[axis])/d[axis];auto v=clip(*cylinder,std::min(t1,t2),std::max(t1,t2));if(v)ranges.push_back(*v);}}
  for(double sign:{-1.,1.}){Point center{};center[axis]=sign*half;auto v=sphere(aa,bb,center,radius);if(v)ranges.push_back(*v);}
  if(!ranges.empty()){double lo=1.,hi=0.;for(const auto& v:ranges){lo=std::min(lo,v.first);hi=std::max(hi,v.second);}result=Interval{lo,hi};}
 }
 return result?std::vector<Interval>{*result}:std::vector<Interval>{};
}
std::vector<ImpactHit> internal_hits(const InternalLayout& layout,const std::array<InternalSegment,4>& segments,double world_length){
 if(!layout.valid || world_length<=.0001)return {};std::map<std::string,double> nearest;
 for(const auto& target:layout.targets){int parent=target.parent=="chassis"?0:target.parent=="hull"?1:target.parent=="turret"?2:target.parent=="gun"?3:-1;
  if(parent<0||!segments[parent].available||target.entity.empty())continue;const auto& s=segments[parent];
  for(const auto& primitive:target.primitives){const auto hits=internal_intervals(primitive,s.start,s.end);for(const auto& hit:hits){const auto name=target.entity+"Health";const double distance=hit.first*world_length;
   const auto it=nearest.find(name);if(it==nearest.end()||distance<it->second)nearest[name]=distance;}}}
 std::vector<ImpactHit> out;for(const auto& row:nearest)out.push_back({row.second,row.first});
 std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.distance!=b.distance?a.distance<b.distance:a.name<b.name;});return out;
}
}
