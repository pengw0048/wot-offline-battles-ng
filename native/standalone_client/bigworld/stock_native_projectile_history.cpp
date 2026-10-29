#include "stock_native_projectile_history.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace stock_native_projectile {
namespace {constexpr double pi=3.14159265358979323846;constexpr double epsilon=1e-9;
double travel(const Pose&a,const Pose&b){return std::abs(angle_delta(a.yaw,b.yaw))+std::abs(angle_delta(a.pitch,b.pitch))+std::abs(angle_delta(a.roll,b.roll))+std::abs(angle_delta(a.turret_yaw,b.turret_yaw))+std::abs(angle_delta(a.gun_pitch,b.gun_pitch));}
int cell(double v){if(!std::isfinite(v)||v/64.<std::numeric_limits<int>::min()||v/64.>std::numeric_limits<int>::max())throw std::invalid_argument("invalid projectile cell");return int(std::floor(v/64.));}
}
double angle_delta(double from,double to){double value=std::fmod(to-from+pi,2*pi);if(value<0)value+=2*pi;return value-pi;}
void History::sample(double at,Poses poses){
    if(!std::isfinite(at))throw std::invalid_argument("invalid projectile sample time");
    if(!samples_.empty()&&std::abs(samples_.back().at-at)<=epsilon)samples_.back()={at,std::move(poses)};
    else samples_.push_back({at,std::move(poses)});
    const double floor=at-21.;while(samples_.size()>2&&samples_[1].at<floor)samples_.pop_front();
}
std::optional<Pose> History::at(const std::string& key,double wanted)const{
    if(samples_.empty()||wanted<samples_.front().at-epsilon)return {};
    if(wanted<=samples_.front().at+epsilon){auto p=samples_.front().poses.find(key);return p==samples_.front().poses.end()?std::optional<Pose>{}:p->second;}
    std::size_t low=1,high=samples_.size();while(low<high){auto mid=(low+high)/2;if(wanted>samples_[mid].at+epsilon)low=mid+1;else high=mid;}
    if(low>=samples_.size())return {};
    const auto&left=samples_[low-1];const auto&right=samples_[low];auto a=left.poses.find(key),b=right.poses.find(key);
    if(a==left.poses.end()||b==right.poses.end())return {};
    if(wanted>=right.at-epsilon||right.at<=left.at+epsilon)return b->second;
    const double f=std::max(0.,std::min(1.,(wanted-left.at)/(right.at-left.at)));Pose p=a->second;p.position=interpolate(p.position,b->second.position,f);
    p.yaw+=angle_delta(p.yaw,b->second.yaw)*f;p.pitch+=angle_delta(p.pitch,b->second.pitch)*f;p.roll+=angle_delta(p.roll,b->second.roll)*f;
    p.turret_yaw+=angle_delta(p.turret_yaw,b->second.turret_yaw)*f;p.gun_pitch+=angle_delta(p.gun_pitch,b->second.gun_pitch)*f;
    return p;
}
std::optional<std::vector<double>> History::sweep(const std::string&key,double start,double end)const{
    const double duration=end-start;if(duration<=1e-12)return std::vector<double>{0,1};
    std::vector<double> boundaries{start};for(const auto&s:samples_)if(s.at>start+epsilon&&s.at<end-epsilon)boundaries.push_back(s.at);boundaries.push_back(end);
    struct Interval{double start,end,travel;};std::vector<Interval> intervals;double total=0;std::set<double> siege;
    for(std::size_t i=0;i+1<boundaries.size();++i){auto a=at(key,boundaries[i]),b=at(key,boundaries[i+1]);if(!a||!b)return {};
        const double t=travel(*a,*b);intervals.push_back({boundaries[i],boundaries[i+1],t});total+=t;
        if(i+2<boundaries.size()&&a->siege!=b->siege)siege.insert(boundaries[i+1]);
    }
    const double divisions=std::max(1.,16.-double(siege.size()));double angle_step=pi/180.;if(total>angle_step*divisions)angle_step=total/divisions;
    auto absolute=siege;if(total>1e-12){double threshold=angle_step,travelled=0;for(const auto&i:intervals){if(i.travel<=1e-12)continue;
        while(threshold<total-1e-12&&threshold<=travelled+i.travel+1e-12){const double ratio=std::max(0.,std::min(1.,(threshold-travelled)/i.travel));absolute.insert(i.start+(i.end-i.start)*ratio);threshold+=angle_step;}travelled+=i.travel;
    }}
    std::vector<double> result{0};for(double t:absolute){const double f=(t-start)/duration;if(f>epsilon&&f<1.-epsilon)result.push_back(f);}result.push_back(1);
    if(result.size()>17){result.resize(16);result.push_back(1);}return result;
}
bool expanded_bounds(const Point&p,const Point&a,const Point&b,double radius){for(unsigned i=0;i<3;++i)if(p[i]<std::min(a[i],b[i])-radius||p[i]>std::max(a[i],b[i])+radius)return false;return true;}
double point_segment_distance_sq(const Point&p,const Point&a,const Point&b){Point d{},offset{};double total=0,dot=0;for(unsigned i=0;i<3;++i){d[i]=b[i]-a[i];offset[i]=p[i]-a[i];total+=d[i]*d[i];dot+=offset[i]*d[i];}const double f=total>1e-12?std::max(0.,std::min(1.,dot/total)):0.;double result=0;for(unsigned i=0;i<3;++i){const double v=offset[i]-d[i]*f;result+=v*v;}return result;}
void SpatialIndex::clear(){valid_=false;bins_.clear();fallback_.clear();records_.clear();floor_=ceiling_=0;}
bool SpatialIndex::build(const History&history,const std::vector<std::string>&records,const std::vector<Cursor>&cursors,double now,std::size_t maximum,double lag){
    clear();records_=records;if(cursors.empty())return false;
    floor_=cursors.front().cursor_time;for(const auto&s:cursors)floor_=std::min(floor_,s.cursor_time);floor_-=lag;ceiling_=now;
    const auto&samples=history.samples();
    if(samples.empty()||!std::isfinite(floor_)||!std::isfinite(now)||floor_>now+epsilon||floor_<samples.front().at-epsilon||now>samples.back().at+epsilon)return false;
    std::size_t estimate=0;for(const auto&s:cursors)estimate+=std::size_t(std::ceil(std::max(0.,std::max(0.,now-s.cursor_time)/.05-1e-12)));estimate=std::min(estimate,maximum);
    std::vector<const PoseSample*> middle;double previous=-std::numeric_limits<double>::infinity();for(const auto&s:samples){if(!std::isfinite(s.at)||s.at<previous-epsilon)return false;previous=s.at;if(s.at>floor_+epsilon&&s.at<now-epsilon)middle.push_back(&s);}
    if(!estimate||records.size()*(middle.size()+2)>=estimate*records.size())return false;
    std::size_t entries=0;
    try{for(const auto&key:records){auto a=history.at(key,floor_),b=history.at(key,now);if(!a||!b){fallback_.insert(key);continue;}
        double lx=std::min(a->position[0],b->position[0]),lz=std::min(a->position[2],b->position[2]),hx=std::max(a->position[0],b->position[0]),hz=std::max(a->position[2],b->position[2]);
        bool incomplete=false;for(const auto*s:middle){auto it=s->poses.find(key);if(it==s->poses.end()){incomplete=true;break;}lx=std::min(lx,it->second.position[0]);lz=std::min(lz,it->second.position[2]);hx=std::max(hx,it->second.position[0]);hz=std::max(hz,it->second.position[2]);}
        if(incomplete){fallback_.insert(key);continue;}
        const int minx=cell(lx-15.-1e-6),minz=cell(lz-15.-1e-6),maxx=cell(hx+15.+1e-6),maxz=cell(hz+15.+1e-6);
        const std::int64_t count=(std::int64_t(maxx)-minx+1)*(std::int64_t(maxz)-minz+1);
        if(count<=0||count>256){fallback_.insert(key);continue;}entries+=std::size_t(count);if(entries>8192){bins_.clear();return false;}
        for(int x=minx;x<=maxx;++x)for(int z=minz;z<=maxz;++z)bins_[{x,z}].insert(key);
    }}catch(...){bins_.clear();return false;}
    valid_=true;return true;
}
std::vector<std::string> SpatialIndex::candidates(const Point&a,const Point&b,double start,double end)const{
    if(!valid_||!std::isfinite(start)||!std::isfinite(end)||start>end+epsilon||start<floor_-epsilon||end>ceiling_+epsilon)return records_;
    auto keys=fallback_;
    try{const int minx=cell(std::min(a[0],b[0])),minz=cell(std::min(a[2],b[2])),maxx=cell(std::max(a[0],b[0])),maxz=cell(std::max(a[2],b[2]));
        const std::int64_t count=(std::int64_t(maxx)-minx+1)*(std::int64_t(maxz)-minz+1);if(count<=0||count>256)return records_;
        for(int x=minx;x<=maxx;++x)for(int z=minz;z<=maxz;++z){auto it=bins_.find({x,z});if(it!=bins_.end())keys.insert(it->second.begin(),it->second.end());}
    }catch(...){return records_;}
    std::vector<std::string> result;for(const auto&key:records_)if(keys.count(key))result.push_back(key);return result;
}
}
