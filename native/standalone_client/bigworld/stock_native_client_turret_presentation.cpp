#include "stock_native_client_turret_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace stock_native_client_turret_presentation {
namespace {
using Matrix=std::array<double,9>;
constexpr double pi=3.14159265358979323846;
Point add(const Point&a,const Point&b){return {a[0]+b[0],a[1]+b[1],a[2]+b[2]};}
Point sub(const Point&a,const Point&b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
Point scale(const Point&a,double s){return {a[0]*s,a[1]*s,a[2]*s};}
double dot(const Point&a,const Point&b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
Point cross(const Point&a,const Point&b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
Point multiply(const Matrix&m,const Point&p){return {m[0]*p[0]+m[1]*p[1]+m[2]*p[2],m[3]*p[0]+m[4]*p[1]+m[5]*p[2],m[6]*p[0]+m[7]*p[1]+m[8]*p[2]};}
Matrix matrix(const Point&a){
    const double sy=std::sin(a[0]),cy=std::cos(a[0]),sp=std::sin(a[1]),cp=std::cos(a[1]),sr=std::sin(a[2]),cr=std::cos(a[2]);
    // The source constructs columns with Ry * Rx * Rz. Preserve its operation
    // order, including the zero terms, rather than simplifying products.
    Matrix out{};for(unsigned column=0;column<3;++column){Point p{};p[column]=1.;const Point z{cr*p[0]-sr*p[1],sr*p[0]+cr*p[1],p[2]};const Point x{z[0],cp*z[1]-sp*z[2],sp*z[1]+cp*z[2]};const Point y{cy*x[0]+sy*x[2],x[1],-sy*x[0]+cy*x[2]};for(unsigned row=0;row<3;++row)out[row*3+column]=y[row];}return out;
}
Point angles(const Matrix&m){const double pitch=std::asin(std::max(-1.,std::min(1.,-m[5])));return std::abs(std::cos(pitch))>1.e-8?Point{std::atan2(m[2],m[8]),pitch,std::atan2(m[3],m[4])}:Point{std::atan2(-m[6],m[0]),pitch,0.};}
Matrix rotate(const Matrix&m,const Point&omega,double dt){const double speed=std::sqrt(dot(omega,omega));if(speed*dt<=1.e-12)return m;const Point axis=scale(omega,1./speed);const double angle=speed*dt,c=std::cos(angle),s=std::sin(angle);Matrix out;for(unsigned column=0;column<3;++column){const Point v{m[column],m[3+column],m[6+column]};const Point result=add(add(scale(v,c),scale(cross(axis,v),s)),scale(axis,dot(axis,v)*(1.-c)));for(unsigned row=0;row<3;++row)out[row*3+column]=result[row];}return out;}
std::array<double,4> quaternion(const Matrix&m){const double trace=m[0]+m[4]+m[8];if(trace>0.){const double s=2.*std::sqrt(trace+1.);return {(m[7]-m[5])/s,(m[2]-m[6])/s,(m[3]-m[1])/s,s*.25};}unsigned i=0;for(unsigned j=1;j<3;++j)if(m[4*j]>m[4*i])i=j;const unsigned j=(i+1)%3,k=(i+2)%3;const double s=2.*std::sqrt(std::max(0.,1.+m[4*i]-m[4*j]-m[4*k]));std::array<double,4> q{};q[i]=s*.25;q[j]=(m[3*i+j]+m[3*j+i])/s;q[k]=(m[3*i+k]+m[3*k+i])/s;q[3]=(m[3*k+j]-m[3*j+k])/s;return q;}
Matrix interpolate(const Matrix&first,const Matrix&second,double alpha){auto a=quaternion(first),b=quaternion(second);double cosine=0.;for(unsigned i=0;i<4;++i)cosine+=a[i]*b[i];if(cosine<0.){for(auto&v:b)v=-v;cosine=-cosine;}const double angle=std::acos(std::min(1.,cosine));std::array<double,4> q;double square=0.;for(unsigned i=0;i<4;++i){q[i]=angle<=1.e-6?a[i]+(b[i]-a[i])*alpha:(a[i]*std::sin((1.-alpha)*angle)+b[i]*std::sin(alpha*angle))/std::sin(angle);square+=q[i]*q[i];}const double size=std::sqrt(square);for(auto&v:q)v/=size;const auto x=q[0],y=q[1],z=q[2],w=q[3];return {1.-2.*(y*y+z*z),2.*(x*y-z*w),2.*(x*z+y*w),2.*(x*y+z*w),1.-2.*(x*x+z*z),2.*(y*z-x*w),2.*(x*z-y*w),2.*(y*z+x*w),1.-2.*(x*x+y*y)};}
}
Pose poseAt(const Row&row,double elapsed,const Tuning&tuning){
    if(!std::isfinite(elapsed))throw std::invalid_argument("Native turret render clock is invalid");elapsed=std::max(0.,elapsed);Pose out;const auto&flight=row.flight;
    if(flight.body){out.body=std::make_shared<const Frame>(*flight.body);const auto&body=*out.body;const double dt=body.sleeping?0.:std::min(tuning.flight_step,elapsed);const auto initial=matrix(body.attitude),rotation=rotate(initial,body.angular_velocity,dt);const auto com=add(body.position,multiply(initial,body.centre));out.position=sub(add(com,scale(body.velocity,dt)),multiply(rotation,body.centre));if(!body.grounded)out.position=add(out.position,{0.,-.5*tuning.gravity*dt*dt,0.});out.attitude=angles(rotation);return out;}
    if(elapsed>=flight.duration){out.position=flight.rest;if(flight.rest_attitude)out.attitude=*flight.rest_attitude;else {out.attitude=add(row.attitude,scale(row.spin,std::max(0.,flight.duration)));out.attitude[1]=std::round(out.attitude[1]/pi)*pi;out.attitude[2]=std::round(out.attitude[2]/pi)*pi;}return out;}
    if(flight.segments.empty())throw std::invalid_argument("Native turret render segments are unavailable");const auto*segment=&flight.segments.front();for(const auto&candidate:flight.segments)if(elapsed>=candidate.start)segment=&candidate;const double dt=elapsed-segment->start;out.position={segment->origin[0]+segment->velocity[0]*dt,segment->origin[1]+segment->velocity[1]*dt-.5*tuning.gravity*dt*dt,segment->origin[2]+segment->velocity[2]*dt};out.attitude=add(row.attitude,scale(row.spin,elapsed));return out;
}
bool Buffer::push(const Row&row,double now,double age){
    if(!row.flight.body)return false;if(!std::isfinite(now)||!std::isfinite(age))throw std::invalid_argument("Native turret sample clock is invalid");const double source=double(row.motion_sequence?row.motion_time_ms:row.created_time_ms)/1000.;if(!samples_.empty()&&source<samples_.back().time)return false;const bool waking=!samples_.empty()&&samples_.back().frame->sleeping;double interval=samples_.empty()?0.:std::max(source-samples_.back().time,now-*arrival_);
    if(waking){const double hold=std::max(samples_.back().time,source-std::max(tuning_.minimum_delay,delay_));auto frame=samples_.back().frame;samples_={{hold,std::move(frame)}};cursor_=std::max(*cursor_,hold);last_render_=now;interval=tuning_.flight_step;}
    delay_=std::max({tuning_.minimum_delay,delay_-interval*tuning_.delay_decay,std::max(0.,age)+interval,samples_.empty()?tuning_.initial_delay:0.});if(!samples_.empty()&&source==samples_.back().time)samples_.pop_back();samples_.push_back({source,std::make_shared<const Frame>(*row.flight.body)});while(samples_.size()>64)samples_.pop_front();arrival_=now;anchor_=source+std::max(0.,age);if(!cursor_)cursor_=source;return true;
}
std::optional<Pose> Buffer::pose(double now){
    if(samples_.empty())return {};if(!std::isfinite(now))throw std::invalid_argument("Native turret render clock is invalid");const double newest=samples_.back().time;const bool first=!last_render_;const double dt=std::max(0.,now-last_render_.value_or(now));last_render_=now;const double target=anchor_+std::max(0.,now-*arrival_)-delay_;if(first)cursor_=std::max(samples_.front().time,std::min(newest,target));const double error=std::max(0.,target-*cursor_),maximum=*cursor_+dt*(1.+std::min(1.,error*error));cursor_=std::max(samples_.front().time,std::min({newest,maximum,std::max(*cursor_,target)}));while(samples_.size()>2&&samples_[1].time<=*cursor_)samples_.pop_front();const auto*left=&samples_.front();const auto*right=&samples_.back();for(std::size_t i=1;i<samples_.size();++i){if(samples_[i].time>=*cursor_){right=&samples_[i];break;}left=&samples_[i];}const double span=right->time-left->time,alpha=span>0.?std::max(0.,std::min(1.,(*cursor_-left->time)/span)):1.;const auto&a=*left->frame;const auto&b=*right->frame;const auto ra=matrix(a.attitude),rb=matrix(b.attitude),rotation=interpolate(ra,rb,alpha);const auto ca=add(a.position,multiply(ra,a.centre)),cb=add(b.position,multiply(rb,b.centre)),com=add(ca,scale(sub(cb,ca),alpha));return Pose{sub(com,multiply(rotation,b.centre)),angles(rotation),alpha>=1.?right->frame:left->frame};
}
}
