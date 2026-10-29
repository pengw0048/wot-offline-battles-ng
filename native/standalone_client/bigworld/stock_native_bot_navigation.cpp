#include "stock_native_bot_navigation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace stock_native_bot_navigation {
namespace {
constexpr double pi=3.14159265358979323846;
const double max_grade=std::tan(25.*pi/180.);
const std::array<Cell,8> neighbours{{{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}}};
double distance(const Point&a,const Point&b){const double dx=a[0]-b[0],dz=a[2]-b[2];return std::sqrt(dx*dx+dz*dz);}
Edge edge(Cell a,Cell b){if(b<a)std::swap(a,b);return {a,b};}
std::vector<Cell> line(Cell a,Cell b)
{
    std::vector<Cell> result{a};const int dx=std::abs(b[0]-a[0]),dz=std::abs(b[1]-a[1]);
    const int sx=a[0]<b[0]?1:-1,sz=a[1]<b[1]?1:-1;int error=dx-dz;
    while(a!=b){const int twice=error*2;if(twice>-dz){error-=dz;a[0]+=sx;}
        if(twice<dx){error+=dx;a[1]+=sz;}result.push_back(a);}return result;
}
bool covers(double cx,double cz,double extent,const Hull&h,double length,double width)
{
    const double dx=h.x-cx,dz=h.z-cz,s=std::sin(h.yaw),c=std::cos(h.yaw);
    if(std::abs(dx)>extent+std::abs(s)*length+std::abs(c)*width)return false;
    if(std::abs(dz)>extent+std::abs(c)*length+std::abs(s)*width)return false;
    const double radius=extent*(std::abs(s)+std::abs(c));
    return std::abs(dx*s+dz*c)<=length+radius&&std::abs(dx*c-dz*s)<=width+radius;
}
double rounded(double value,int places){const double factor=std::pow(10.,places);return std::round(value*factor)/factor;}
}
Grid::Grid(offline_navigation::MapInput value,World& world,double up,double down):
    world_(world),map(std::move(value)),grade_up(up),grade_down(down)
{
    if(map.width<=0||map.height<=0||map.heights.size()!=std::size_t(map.width)*map.height||
       map.links.size()!=map.heights.size()||(!map.hazards.empty()&&map.hazards.size()!=map.heights.size()))
        throw std::invalid_argument("invalid baked navigation graph");
    if(!std::isfinite(map.cell_size)||!std::isfinite(up)||!std::isfinite(down)||up<=0||down<=0)
        throw std::invalid_argument("invalid navigation scale");
    map.cell_size=std::max(1.,map.cell_size);map.max_grade=std::max(.05,map.max_grade);
    if(map.hazards.empty())map.hazards.resize(map.heights.size());
}
Cell Grid::cellFor(const Point&p)const
{return {int(std::floor((p[0]-map.origin_x)/map.cell_size+.5)),int(std::floor((p[2]-map.origin_z)/map.cell_size+.5))};}
Point Grid::pointFor(const Cell&c,double h)const{return {map.origin_x+c[0]*map.cell_size,h,map.origin_z+c[1]*map.cell_size};}
std::optional<std::size_t> Grid::index(const Cell&c,bool require_height)const
{
    if(c[0]<0||c[1]<0||c[0]>=map.width||c[1]>=map.height)return {};
    const auto n=std::size_t(c[1])*map.width+c[0];
    if(require_height&&std::isnan(map.heights[n]))return {};return n;
}
std::optional<double> Grid::height(const Cell&c)const{auto n=index(c);return n?std::optional<double>(map.heights[*n]):std::nullopt;}
std::optional<Cell> Grid::nearest(const Cell&c,int maximum)const
{
    if(index(c))return c;std::optional<Cell> best;int best_distance=0;
    for(int radius=1;radius<=std::max(0,maximum);++radius){
        for(int z=c[1]-radius;z<=c[1]+radius;++z)for(int x=c[0]-radius;x<=c[0]+radius;++x){
            if(std::max(std::abs(x-c[0]),std::abs(z-c[1]))!=radius||!index({x,z}))continue;
            const int d=(x-c[0])*(x-c[0])+(z-c[1])*(z-c[1]);
            if(!best||d<best_distance){best=Cell{x,z};best_distance=d;}}
        if(best)return best;}return {};
}
bool Grid::inside(double x,double z)const
{return !map.has_bounds||(map.bounds[0]<=x&&x<=map.bounds[2]&&map.bounds[1]<=z&&z<=map.bounds[3]);}
std::optional<std::pair<double,double>> Grid::localCorridor(const Point&p)
{
    const auto cell=cellFor(p);if(!index(cell))return {};auto found=local_corridors_.find(cell);
    if(found!=local_corridors_.end())return found->second;
    const std::array<Cell,4> axes{{{1,0},{1,1},{0,1},{-1,1}}};std::array<double,4> spans{};
    for(unsigned i=0;i<axes.size();++i){int count=1;for(int sign:{1,-1})for(int reach=1;reach<=5;++reach){
        if(!index({cell[0]+axes[i][0]*reach*sign,cell[1]+axes[i][1]*reach*sign}))break;++count;}
        spans[i]=count*std::hypot(axes[i][0],axes[i][1])*map.cell_size;}
    const auto longest=std::max_element(spans.begin(),spans.end())-spans.begin();
    return local_corridors_[cell]={std::atan2(double(axes[longest][0]),double(axes[longest][1])),spans[(longest+2)%4]};
}
bool Grid::hullPoseClear(const Point&p,double yaw,double half_length,double half_width)const
{
    half_length=std::max(.5,half_length);half_width=std::max(.3,half_width);
    const double s=std::sin(yaw),c=std::cos(yaw);
    for(double along:{-half_length,0.,half_length})for(double across:{-half_width,0.,half_width})
        if(!index(cellFor({p[0]+s*along+c*across,0.,p[2]+c*along-s*across})))return false;
    return true;
}
bool Grid::hazardNear(const Point&p,int radius)const
{
    const auto c=cellFor(p);radius=std::max(0,radius);
    for(int z=c[1]-radius;z<=c[1]+radius;++z)for(int x=c[0]-radius;x<=c[0]+radius;++x){
        auto i=index({x,z},false);if(i&&(map.hazards[*i]&3))return true;}
    return false;
}
std::vector<Cell> Grid::cells(const Point&a,const Point&b,bool require_height)const
{
    std::optional<Cell> start=require_height?nearest(cellFor(a),2):std::optional<Cell>(cellFor(a));
    const auto end=cellFor(b);if(!start||!index(*start,require_height)||!index(end,require_height))return {};
    return line(*start,end);
}
std::vector<Edge> Grid::edges(const Point&a,const Point&b)const
{
    const auto list=line(cellFor(a),cellFor(b));std::vector<Edge> result;
    for(std::size_t i=1;i<list.size();++i)result.push_back(edge(list[i-1],list[i]));return result;
}
std::optional<double> Grid::edgeHeight(const Cell&a,const Cell&b)const
{
    auto i=index(a),j=index(b);if(!i||!j)return {};
    for(unsigned n=0;n<neighbours.size();++n)if(b[0]-a[0]==neighbours[n][0]&&b[1]-a[1]==neighbours[n][1])
        return (map.links[*i]&(1u<<n))?std::optional<double>(map.heights[*j]):std::nullopt;
    return {};
}
Corridor Grid::bakedCorridor(const Point&a,const Point&b)
{
    const auto key=std::make_pair(cellFor(a),cellFor(b));auto found=corridor_cache_.find(key);
    if(found!=corridor_cache_.end())return found->second;
    const auto list=cells(a,b);Corridor out;out.clear=!list.empty();if(out.clear)out.hazards=0;
    for(std::size_t n=1;n<list.size();++n){auto i=index(list[n]);if(!i){out.clear=false;out.hazards.reset();break;}
        *out.hazards|=map.hazards[*i];if(out.clear&&!edgeHeight(list[n-1],list[n]))out.clear=false;}
    if(corridor_cache_.size()>=2048){corridor_cache_.erase(corridor_order_.front());corridor_order_.pop_front();}
    corridor_order_.push_back(key);corridor_cache_[key]=out;return out;
}
bool Grid::segmentHazard(const Point&a,const Point&b,unsigned mask)
{const auto corridor=bakedCorridor(a,b);return !corridor.hazards||(*corridor.hazards&mask);}
bool Grid::motionHazard(const Point&a,const Point&b,unsigned mask)const
{
    const auto list=cells(a,b,false);if(list.empty())return true;
    for(std::size_t n=1;n<list.size();++n){auto i=index(list[n],false);if(!i||(map.hazards[*i]&mask))return true;}return false;
}
bool Grid::pointHazard(const Point&p,unsigned mask)const
{auto i=index(cellFor(p),false);return i&&(map.hazards[*i]&mask);}
std::optional<std::vector<Cell>> Grid::hazardCells(const Point&a,const Point&b,unsigned mask)const
{
    if(!index(cellFor(a)))return {};const auto list=cells(a,b);if(list.empty())return {};
    std::vector<Cell> result;for(std::size_t n=1;n<list.size();++n){auto i=index(list[n]);if(!i)return {};
        if(map.hazards[*i]&mask)result.push_back(list[n]);}return result;
}
int Grid::linkCount(const Cell&c)const
{auto i=index(c);if(!i)return 0;unsigned mask=map.links[*i]&255,count=0;for(;mask;mask&=mask-1)++count;return int(count);}
double Grid::clearancePenalty(const Cell&c)const{return std::max(0,8-linkCount(c))*map.cell_size*.2;}
double Grid::penalty(const Cell&c,const Path& avoid,bool clearance)const
{
    double result=clearance?clearancePenalty(c):0.;auto i=index(c);if(i&&(map.hazards[*i]&4))result+=map.cell_size*4.;
    const auto p=pointFor(c,0.);for(const auto&point:avoid){const double d=distance(p,point);
        if(d<map.cell_size*1.5)result+=(map.cell_size*1.5-d)*3.;}return result;
}
std::optional<double> Grid::clearanceExposure(const Path& path)const
{
    if(path.empty())return 0.;std::vector<Cell> all;
    if(path.size()==1){auto c=nearest(cellFor(path[0]),2);if(!c)return {};all.push_back(*c);}
    else for(std::size_t n=1;n<path.size();++n){auto segment=cells(path[n-1],path[n]);if(segment.empty())return {};
        if(!all.empty()&&all.back()==segment.front())segment.erase(segment.begin());all.insert(all.end(),segment.begin(),segment.end());}
    if(all.empty())return {};double missing=0.;for(const auto&c:all)missing+=8-linkCount(c);return missing/all.size();
}
bool Grid::preservesClearance(const Path&p,std::size_t first,std::size_t last,double maximum)const
{
    if(last<first+2)return true;const auto original=clearanceExposure(Path(p.begin()+first,p.begin()+last+1));
    const auto shortcut=clearanceExposure({p[first],p[last]});return original&&shortcut&&*shortcut<=*original+maximum;
}
bool Grid::preservesClimb(const Path&p,std::size_t first,std::size_t last)
{
    if(last<first+2)return true;for(std::size_t i=first+1;i<last;++i){const auto&a=p[i-1];const auto&b=p[i];const auto&c=p[i+1];
        const double run=distance(b,c);if(run<=.1||std::abs((c[1]-b[1])/run)<=std::tan(pi/8.))continue;
        if(std::abs(b[0]-a[0])+std::abs(b[2]-a[2])<=.1)continue;
        double turn=std::atan2(c[0]-b[0],c[2]-b[2])-std::atan2(b[0]-a[0],b[2]-a[2]);
        while(turn>pi)turn-=2*pi;while(turn<-pi)turn+=2*pi;if(std::abs(turn)>pi/4.)return false;}return true;
}
bool Grid::livePreservesClimb(const Point&current,const Path&p,std::size_t first,std::size_t last)
{if(last<first)return true;Path live{current};live.insert(live.end(),p.begin()+first,p.begin()+last+1);return preservesClimb(live,0,live.size()-1);}
bool Grid::setStaticHulls(const std::vector<Hull>& hulls,double clearance)
{
    Penalties updated;const double padding=rounded(std::max(0.,clearance),3);
    for(auto h:hulls){h.x=rounded(h.x,2);h.z=rounded(h.z,2);h.yaw=rounded(h.yaw,3);
        const double length=std::max(.5,rounded(h.half_length,2))+padding,width=std::max(.3,rounded(h.half_width,2))+padding;
        const double radius=std::hypot(length,width);const auto first=cellFor({h.x-radius,0.,h.z-radius}),last=cellFor({h.x+radius,0.,h.z+radius});
        for(int z=first[1];z<=last[1];++z)for(int x=first[0];x<=last[0];++x){const auto p=pointFor({x,z},0.);
            if(!covers(p[0],p[2],map.cell_size*.5,h,length,width))continue;
            for(const auto&n:neighbours)updated[edge({x,z},{x+n[0],z+n[1]})]=240.;}}
    if(updated==static_hull_edges)return false;static_hull_edges=std::move(updated);++hull_revision;return true;
}
void Grid::pruneFailed(double now)
{
    for(auto i=failed_edges.begin();i!=failed_edges.end();)if(now>=i->second.first)i=failed_edges.erase(i);else ++i;
    while(failed_edges.size()>128){auto oldest=std::min_element(failed_edges.begin(),failed_edges.end(),
        [](const auto&a,const auto&b){return a.second.first<b.second.first;});failed_edges.erase(oldest);}
}
double Grid::edgePenalty(const Edge&key,double now)
{
    double value=0.;auto timed=failed_edges.find(key);if(timed!=failed_edges.end()){
        if(now>=timed->second.first)failed_edges.erase(timed);else value=timed->second.second;}
    auto hull=static_hull_edges.find(key);return hull==static_hull_edges.end()?value:std::max(value,hull->second);
}
double Grid::segmentPenalty(const Point&a,const Point&b,double now)
{double value=0.;for(const auto&key:edges(a,b))value=std::max(value,edgePenalty(key,now));return value;}
bool Grid::pathHasTimedPenalty(const Path&p,double now)
{
    for(std::size_t n=1;n<p.size();++n)for(const auto&key:edges(p[n-1],p[n])){auto i=failed_edges.find(key);
        if(i!=failed_edges.end()){if(now>=i->second.first)failed_edges.erase(i);else if(i->second.second>0.)return true;}}
    return false;
}
bool Grid::pathCrossesStaticHull(const Path&p)const{return pathHasEdgePenalty(p,static_hull_edges);}
bool Grid::pathHasEdgePenalty(const Path&p,const Penalties&penalties)const
{for(std::size_t n=1;n<p.size();++n)for(const auto&key:edges(p[n-1],p[n]))if(penalties.count(key))return true;return false;}
bool Grid::reviewNativeCorridor(const Point&a,const Point&b)
{
    const double d=distance(a,b);if(d<=0.)return false;const double fraction=std::min(1.,map.cell_size*8./d);
    Point end{};for(unsigned n=0;n<3;++n)end[n]=a[n]+(b[n]-a[n])*fraction;const auto before=review_cells.size();
    for(const auto&c:cells(a,end,false))if(review_seeds_.insert(c).second)
        for(int dx=-8;dx<=8;++dx)for(int dz=-8;dz<=8;++dz){Cell review{c[0]+dx,c[1]+dz};if(index(review,false))review_cells.insert(review);}
    return before!=review_cells.size();
}
bool Grid::needsNativeReview(const Point&a,const Point&b)const
{if(review_cells.empty())return false;for(const auto&c:cells(a,b,false))if(review_cells.count(c))return true;return false;}
void Grid::invalidateNativeReview(){++proof_revision;proof_cache_.clear();}
void Grid::beginFrame(){frame_open_=true;pending_.clear();proof_deferred=false;}
void Grid::endFrame(){frame_open_=false;pending_.clear();}
std::array<int,3> Grid::pointKey(const Point&p)
{return {int(std::floor(p[0]*.5+.5)),int(std::floor(p[2]*.5+.5)),int(std::floor(p[1]/8.+.5))};}
std::optional<double> Grid::ground(double x,double z,double hint)
{(void)hint;if(!inside(x,z))return {};return height(cellFor({x,hint,z}));}
Proof Grid::nativeSegment(const Point&a,const Point&b,std::optional<double> slope)
{
    if(distance(a,b)>map.cell_size*12.)return Proof::Blocked;
    const double grade=slope?std::min(*slope,max_grade):std::min(grade_up,grade_down);
    const ProofKey key{pointKey(a),pointKey(b),slope?grade:-1.};auto found=proof_cache_.find(key);
    if(found!=proof_cache_.end())return found->second?Proof::Clear:Proof::Blocked;
    if(frame_open_&&pending_.count({a,b})){proof_deferred=true;return Proof::Unknown;}
    const auto revision=proof_revision;Proof result=Proof::Unknown;
    try{result=world_.navigation(a,b,map.cell_size,grade_up,grade_down,slope);}catch(...){result=Proof::Unknown;}
    if(revision!=proof_revision)result=Proof::Unknown;
    if(result==Proof::Unknown){if(frame_open_)pending_.insert({a,b});proof_deferred=true;return result;}
    if(proof_cache_.size()>=4096)proof_cache_.erase(proof_cache_.begin());proof_cache_[key]=result==Proof::Clear;return result;
}
bool Grid::supportedSlope(const Point&a,const Point&b)
{
    if(distance(a,b)>map.cell_size*12.||(height(cellFor(b))&&pointHazard(b,2)))return false;
    const auto key=std::make_pair(cellFor(a),cellFor(b));auto cached=slope_cache_.find(key);if(cached!=slope_cache_.end())return cached->second;
    const auto list=cells(a,b,false);bool clear=!list.empty();for(const auto&c:list){auto n=index(c);if(!n||map.hazards[*n]){clear=false;break;}}
    for(std::size_t n=1;clear&&n<list.size();++n){const auto&first=list[n-1];const auto&last=list[n];if(edgeHeight(first,last))continue;
        if(first[0]==last[0]||first[1]==last[1]){clear=false;break;}
        const std::array<Cell,2> corners{{{last[0],first[1]},{first[0],last[1]}}};
        for(const auto&c:corners){auto i=index(c);if(!i||map.hazards[*i]||!edgeHeight(first,c)||!edgeHeight(c,last)||!edgeHeight(last,c)||!edgeHeight(c,first)){clear=false;break;}}
        if(!clear)break;const std::array<double,4> h{*height(first),*height(corners[0]),*height(corners[1]),*height(last)};
        for(const auto&ids:std::array<std::array<int,3>,4>{{{0,1,2},{1,0,3},{2,0,3},{3,1,2}}})
            if((std::pow(h[ids[1]]-h[ids[0]],2)+std::pow(h[ids[2]]-h[ids[0]],2))/(map.cell_size*map.cell_size)>max_grade*max_grade){clear=false;break;}}
    if(slope_cache_.size()>=2048)slope_cache_.clear();slope_cache_[key]=clear;return clear;
}
Proof Grid::liveEgress(const Point&a,const Point&b)
{
    if(motionHazard(a,b,5)||(height(cellFor(b))&&pointHazard(b,2)))return Proof::Blocked;
    const double d=distance(a,b);if(d>24.)return Proof::Blocked;const double lx=(b[2]-a[2])/std::max(d,.001),lz=(a[0]-b[0])/std::max(d,.001);
    for(double side:{-2.15,2.15})if(motionHazard({a[0]+lx*side,a[1],a[2]+lz*side},{b[0]+lx*side,b[1],b[2]+lz*side},5))return Proof::Blocked;
    try{auto y=world_.ground(a[0],a[2],a[1]);if(!y||!std::isfinite(*y))return Proof::Blocked;
        Point previous{a[0],*y,a[2]},first=previous;const int steps=std::max(1,int(std::ceil(d/(map.cell_size*.42))));
        for(int n=1;n<=steps;++n){const double f=double(n)/steps,x=a[0]+(b[0]-a[0])*f,z=a[2]+(b[2]-a[2])*f;
            y=world_.ground(x,z,previous[1]);if(!y||!std::isfinite(*y))return Proof::Blocked;
            if(std::abs(*y-previous[1])>std::hypot(x-previous[0],z-previous[2])*max_grade)return Proof::Blocked;
            for(double side:{-2.15,2.15}){auto shoulder=world_.ground(x+lx*side,z+lz*side,*y);
                if(!shoulder||!std::isfinite(*shoulder)||std::abs(*shoulder-*y)>std::abs(side)*max_grade)return Proof::Blocked;}
            previous={x,*y,z};}
        const auto revision=proof_revision;const auto result=world_.corridor(first,previous,2.15);
        return revision==proof_revision?result:Proof::Unknown;
    }catch(...){return Proof::Blocked;}
}
Proof Grid::connector(const Point&a,const Point&b)
{const auto revision=proof_revision;try{auto result=world_.corridor(a,b,2.15);return revision==proof_revision?result:Proof::Unknown;}catch(...){return Proof::Blocked;}}
bool Grid::segmentClear(const Point&a,const Point&b)
{
    if(!inside(b[0],b[2]))return false;if(distance(a,b)<.25)return true;
    if(!height(cellFor(a)))return liveEgress(a,b)==Proof::Clear;
    if(!bakedCorridor(a,b).clear){if(supportedSlope(a,b))return nativeSegment(a,b,max_grade)==Proof::Clear;
        if(!height(cellFor(b))&&distance(a,b)<=24.&&live_joins_.count(b))return liveEgress(a,b)==Proof::Clear;return false;}
    return !needsNativeReview(a,b)||nativeSegment(a,b)==Proof::Clear;
}
bool Grid::drySegment(const Point&a,const Point&b,double now)
{
    if(!bakedCorridor(a,b).hazards&&distance(a,b)<=24.&&(!height(cellFor(a))||live_joins_.count(b)))
        return segmentPenalty(a,b,now)<=0.&&inside(b[0],b[2])&&liveEgress(a,b)==Proof::Clear;
    return segmentPenalty(a,b,now)<=0.&&!segmentHazard(a,b,4)&&segmentClear(a,b);
}
std::optional<Point> Grid::safeLocal(const Point&current,const Point&goal,double now,const Path&avoid,
    double side,const Penalties&private_edges,double minimum_offset,const std::set<Cell>&excluded)
{
    const double dx=goal[0]-current[0],dz=goal[2]-current[2];if(std::abs(dx)+std::abs(dz)<.1)return {};
    const double desired=std::atan2(dx,dz);minimum_offset=std::max(0.,minimum_offset);
    if(!height(cellFor(current))){const auto c=cellFor(current);std::vector<std::pair<double,Point>> candidates;
        for(int oz=-2;oz<=2;++oz)for(int ox=-2;ox<=2;++ox){Cell key{c[0]+ox,c[1]+oz};if(excluded.count(key))continue;
            auto y=height(key);if(y){auto p=pointFor(key,*y);if(distance(current,p)>1.5)candidates.push_back({distance(current,p),p});}}
        std::sort(candidates.begin(),candidates.end());if(!candidates.empty()){const auto&p=candidates.front().second;
            double delta=std::atan2(p[0]-current[0],p[2]-current[2])-desired;delta=std::fmod(delta+3*pi,2*pi)-pi;
            if(std::abs(delta)>=minimum_offset&&!pathHasEdgePenalty({current,p},private_edges)&&drySegment(current,p,now))return p;}}
    side=side>=0.?1.:-1.;const std::array<double,9> offsets{0.,side*.45,-side*.45,side*.85,-side*.85,side*1.30,-side*1.30,side*1.75,-side*1.75};
    std::optional<Point> best;std::pair<double,double> best_score;int native_candidates=0;
    for(double d:{map.cell_size*.78,map.cell_size*.52})for(double offset:offsets){if(std::abs(offset)<minimum_offset)continue;
        const double yaw=desired+offset,x=current[0]+std::sin(yaw)*d,z=current[2]+std::cos(yaw)*d;
        if(excluded.count(cellFor({x,current[1],z})))continue;auto y=ground(x,z,current[1]);
        if(!y){if(native_candidates<3){++native_candidates;Point p{x,current[1],z};
                if(!pathHasEdgePenalty({current,p},private_edges)&&inside(x,z)&&liveEgress(current,p)==Proof::Clear){
                    if(live_joins_.size()>=64)live_joins_.erase(live_joins_.begin());live_joins_.insert(p);return p;}}continue;}
        Point p{x,*y,z};if(segmentPenalty(current,p,now)>0.||segmentHazard(current,p,4)||!segmentClear(current,p)||pathHasEdgePenalty({current,p},private_edges))continue;
        const std::pair<double,double> score{distance(p,goal)+std::abs(offset)*3.5+penalty(cellFor(p),avoid,false)*2.,std::abs(offset)};
        if(!best||score<best_score){best=p;best_score=score;}}
    return best;
}
Jobs::Jobs(Grid&grid):grid_(grid),context_(offline_navigation::open(grid.map))
{if(!context_)throw std::runtime_error("native navigation map could not be opened");}
Jobs::~Jobs(){close();}
std::shared_ptr<Search> Jobs::submit(const Point&a,const Point&b,double now,unsigned expansions,bool clearance,
    const Penalties&private_edges,const std::set<Edge>&hard_edges,const Path&avoid,std::optional<std::pair<Point,Point>> route)
{
    if(!context_)throw std::runtime_error("native navigation context is closed");
    auto job=std::make_shared<Search>();job->id=++next_id_;job->hull_revision=grid_.hull_revision;job->submitted_at=now;job->start=a;job->goal=b;
    offline_navigation::SearchInput input;input.job_id=job->id;input.hull_revision=job->hull_revision;input.start=a;input.goal=b;
    input.max_expansions=expansions;input.prefer_clearance=clearance;input.avoid_points=avoid;
    for(const auto&entry:private_edges)input.edge_penalties.push_back({entry.first.first,entry.first.second,entry.second});
    input.hard_edges.assign(hard_edges.begin(),hard_edges.end());input.native_review_cells.assign(grid_.review_cells.begin(),grid_.review_cells.end());
    Penalties world=grid_.static_hull_edges;for(const auto&entry:grid_.failed_edges)if(now<entry.second.first)world[entry.first]=std::max(world[entry.first],entry.second.second);
    for(const auto&entry:world)input.world_penalties.push_back({entry.first.first,entry.first.second,entry.second});
    if(route){input.has_route_corridor=true;input.route_start=route->first;input.route_end=route->second;}
    if(!offline_navigation::submit(context_,std::move(input)))throw std::runtime_error("native navigation job was not accepted");
    jobs_[job->id]=job;++stats.submitted;return job;
}
void Jobs::cancel(const std::vector<std::shared_ptr<Search>>&jobs)
{
    std::vector<std::int64_t> identifiers;for(const auto&job:jobs)if(job&&jobs_.erase(job->id)){
        identifiers.push_back(job->id);queries_.erase(job->id);job->done=true;job->result.clear();job->status=1;}
    if(context_&&!identifiers.empty()){offline_navigation::cancel(context_,identifiers);stats.cancelled+=identifiers.size();}
}
void Jobs::poll(double now)
{
    auto output=offline_navigation::poll(context_);stats.worker=output.stats;
    for(auto&row:output.completed){auto it=jobs_.find(row.job_id);queries_.erase(row.job_id);if(it==jobs_.end())continue;
        auto job=it->second;jobs_.erase(it);job->done=true;job->status=row.status;job->result=row.status==0?std::move(row.path):Path{};
        job->hull_revision=row.hull_revision;job->last_frame=now;job->steps=row.expanded;++stats.completed;
        stats.expanded+=row.expanded;stats.worker_seconds+=row.worker_seconds;stats.max_completion_age=std::max(stats.max_completion_age,std::max(0.,now-job->submitted_at));}
    for(auto&query:output.queries){if(!jobs_.count(query.job_id))continue;auto it=queries_.find(query.job_id);
        if(it==queries_.end()){order_.push_back(query.job_id);it=queries_.emplace(query.job_id,std::deque<offline_navigation::Query>{}).first;}it->second.push_back(std::move(query));}
}
void Jobs::advance(double now,unsigned budget)
{
    if(!context_)return;poll(now);std::vector<std::pair<std::int64_t,bool>> answers;std::vector<offline_navigation::Query> deferred;
    while(budget&&!order_.empty()){const auto id=order_.front();order_.pop_front();auto found=queries_.find(id);
        if(found==queries_.end()||found->second.empty()||!jobs_.count(id)){queries_.erase(id);continue;}
        auto query=found->second.front();found->second.pop_front();Proof proof=Proof::Blocked;
        try{proof=grid_.nativeSegment(query.start,query.end);}catch(...){proof=Proof::Blocked;}
        if(proof==Proof::Unknown)deferred.push_back(query);else if(jobs_.count(id))answers.emplace_back(query.query_id,proof==Proof::Clear);
        --budget;if(!found->second.empty())order_.push_back(id);else queries_.erase(found);}
    for(auto&query:deferred){if(!jobs_.count(query.job_id))continue;auto found=queries_.find(query.job_id);
        if(found==queries_.end()){order_.push_back(query.job_id);found=queries_.emplace(query.job_id,std::deque<offline_navigation::Query>{}).first;}
        found->second.push_back(std::move(query));}
    if(!answers.empty()){offline_navigation::answer(context_,answers);stats.queries+=answers.size();}poll(now);
}
void Jobs::close()
{
    if(!context_)return;for(auto&entry:jobs_){entry.second->done=true;entry.second->result.clear();entry.second->status=1;}
    jobs_.clear();queries_.clear();order_.clear();offline_navigation::close(context_);context_=0;
}
}
