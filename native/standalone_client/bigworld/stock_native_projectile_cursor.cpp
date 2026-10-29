#include "stock_native_projectile_cursor.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace stock_native_projectile {
namespace {
constexpr double epsilon=1e-9;
bool finite(const Point& p){return std::all_of(p.begin(),p.end(),[](double v){return std::isfinite(v);});}
double unit(double v){return std::max(0.,std::min(1.,v));}
double step(const Point& g,double maximum){
    maximum=std::max(.001,maximum);
    const double magnitude=std::sqrt(g[0]*g[0]+g[1]*g[1]+g[2]*g[2]);
    return magnitude<=1e-12?maximum:std::max(.001,std::min(maximum,std::sqrt(.4/magnitude)));
}
}
Point trajectory(const Point& o,const Point& v,const Point& g,double t){
    t=std::max(0.,t);const double half=.5*t*t;Point r{};
    for(unsigned i=0;i<3;++i)r[i]=o[i]+v[i]*t+g[i]*half;
    return r;
}
Point interpolate(const Point&a,const Point&b,double f){Point r{};f=unit(f);for(unsigned i=0;i<3;++i)r[i]=a[i]+(b[i]-a[i])*f;return r;}
double length(const Point&a,const Point&b){const double x=b[0]-a[0],y=b[1]-a[1],z=b[2]-a[2];return std::sqrt(x*x+y*y+z*z);}
Chord prepare_chord(const Cursor& s,double begin,double end){
    Chord result;result.start=trajectory(s.origin,s.velocity,s.gravity,begin-s.launch_time);
    const Point raw=trajectory(s.origin,s.velocity,s.gravity,end-s.launch_time);
    const double distance=length(result.start,raw),remaining=std::max(0.,s.max_distance-s.distance);
    double fraction=1.;
    if(distance>remaining){fraction=remaining/distance;result.distance_terminal=true;}
    else if(remaining<=0.){fraction=0.;result.distance_terminal=true;}
    result.end=interpolate(result.start,raw,fraction);result.start_time=begin;
    result.end_time=begin+(end-begin)*fraction;
    if(!finite(result.start)||!finite(result.end)||!std::isfinite(result.end_time))throw std::runtime_error("invalid projectile trajectory");
    return result;
}
Cursors::Cursors(std::size_t maximum,double now,double maximum_step):maximum_(maximum),now_(now),maximum_step_(std::min(.05,maximum_step)){
    if(!maximum||!std::isfinite(now)||now<0||!std::isfinite(maximum_step)||maximum_step<=0)throw std::invalid_argument("invalid projectile cursor limits");
}
bool Cursors::validate(Cursor& s,std::optional<double> admission,bool restoring)const{
    const double horizon=admission.value_or(now_);
    if(!std::isfinite(horizon)||horizon+epsilon<now_||!finite(s.origin)||!finite(s.velocity)||!finite(s.gravity)||
       !std::isfinite(s.launch_time)||s.launch_time<0||!std::isfinite(s.max_time)||s.max_time<=0||
       !std::isfinite(s.max_distance)||s.max_distance<=0)return false;
    if(restoring){
        if(!std::isfinite(s.cursor_time)||s.cursor_time<s.launch_time||s.cursor_time>horizon||
           s.cursor_time>s.launch_time+s.max_time||!std::isfinite(s.distance)||s.distance<0||s.distance>s.max_distance)return false;
        s.position=trajectory(s.origin,s.velocity,s.gravity,s.cursor_time-s.launch_time);
    }else{
        if(s.launch_time>horizon+epsilon)return false;
        s.cursor_time=s.launch_time;s.position=s.origin;s.distance=0;
    }
    s.maximum_step=step(s.gravity,maximum_step_);
    return finite(s.position)&&std::isfinite(s.maximum_step);
}
bool Cursors::launch(Cursor s,std::optional<double> horizon){
    if(!validate(s,horizon,false)||known_.count(s.key)||size()>=maximum_)return false;
    auto value=std::make_shared<Cursor>(s);known_.insert(s.key);
    if(advancing_){pending_launch_.insert(s.key);pending_.push_back({true,s.key,value});}else install(value);
    return true;
}
bool Cursors::restore(Cursor s,std::optional<double> horizon){
    if(advancing_||!validate(s,horizon,true)||known_.count(s.key)||size()>=maximum_)return false;
    known_.insert(s.key);install(std::make_shared<Cursor>(s));return true;
}
bool Cursors::replace(Cursor s,std::optional<Key> old,std::optional<double> horizon){
    if(advancing_||!validate(s,horizon,true))return false;
    const Key previous=old.value_or(s.key);
    if(!known_.count(previous)||(s.key!=previous&&known_.count(s.key)))return false;
    auto found=states_.find(previous);
    if(found==states_.end()&&size()>=maximum_)return false;
    auto value=std::make_shared<Cursor>(s);
    if(found!=states_.end()){
        if(s.key!=previous){
            auto position=std::find(order_.begin(),order_.end(),previous);
            if(position==order_.end()||std::find(round_robin_.begin(),round_robin_.end(),previous)==round_robin_.end())return false;
            *position=s.key;for(auto& k:round_robin_)if(k==previous)k=s.key;
            states_.erase(found);
        }
        states_[s.key]=value;
    }else install(value);
    known_.erase(previous);known_.insert(s.key);return true;
}
bool Cursors::rollback(const Key& k){if(advancing_||!known_.count(k))return false;erase(k);known_.erase(k);return true;}
bool Cursors::remove(const Key& k){
    if(!contains(k))return false;
    if(advancing_){if(pending_remove_.count(k))return false;pending_remove_.insert(k);pending_.push_back({false,k,{}});if(current_&&*current_==k)cancel_current_.insert(k);}
    else erase(k);
    return true;
}
bool Cursors::reset(std::optional<double> at){
    if(advancing_||(at&&(!std::isfinite(*at)||*at+epsilon<now_)))return false;
    if(at)now_=*at;
    states_.clear();order_.clear();round_robin_.clear();known_.clear();pending_launch_.clear();pending_remove_.clear();cancel_current_.clear();current_.reset();pending_.clear();metrics_={};return true;
}
std::optional<Cursor> Cursors::get(const Key& k)const{auto it=states_.find(k);return it==states_.end()?std::optional<Cursor>{}:*it->second;}
std::vector<Cursor> Cursors::snapshot()const{std::vector<Cursor> r;for(const auto&k:order_){auto it=states_.find(k);if(it!=states_.end())r.push_back(*it->second);}return r;}
std::size_t Cursors::sustainable(double interval)const{
    if(!std::isfinite(interval)||interval<=0)return 0;
    std::size_t total=0;for(const auto&row:states_)total+=std::size_t(std::ceil(std::max(0.,interval-epsilon)/row.second->maximum_step));return total;
}
double Cursors::debt(double at)const{double r=0;for(const auto&row:states_){const auto&s=*row.second;r=std::max(r,std::max(0.,std::min(at,s.launch_time+s.max_time)-s.cursor_time));}return r;}
void Cursors::install(State s){states_[s->key]=s;order_.push_back(s->key);round_robin_.push_back(s->key);}
void Cursors::erase(const Key& k){states_.erase(k);order_.erase(std::remove(order_.begin(),order_.end(),k),order_.end());round_robin_.erase(std::remove(round_robin_.begin(),round_robin_.end(),k),round_robin_.end());pending_launch_.erase(k);pending_remove_.erase(k);}
void Cursors::flush(){auto operations=std::move(pending_);pending_.clear();for(auto&op:operations){if(op.launch){pending_launch_.erase(op.key);install(op.state);}else erase(op.key);}pending_launch_.clear();pending_remove_.clear();cancel_current_.clear();}
void Cursors::finish(State s,Terminal terminal,CursorSink& sink){if(advancing_)++metrics_.terminals;erase(s->key);try{sink.terminal(*s,terminal);}catch(...) {}}
std::size_t Cursors::safely(State s,double at,CursorSink&sink,std::optional<std::size_t> budget){try{return one(s,at,sink,budget);}catch(...){if(states_.count(s->key))finish(s,{"callback_error",0},sink);return 0;}}
std::size_t Cursors::one(State s,double at,CursorSink& sink,std::optional<std::size_t> budget){
    if(s->distance>=s->max_distance){finish(s,{"max_distance",1},sink);return 0;}
    const double lifetime=s->launch_time+s->max_time,end=std::min(at,lifetime);
    if(end+epsilon<s->cursor_time)return 0;
    std::size_t used=0;double cursor=s->cursor_time;
    while(cursor+epsilon<end){
        if(budget&&used>=*budget)break;
        const double next=std::min(end,cursor+s->maximum_step);
        auto chord=prepare_chord(*s,cursor,next);
        std::optional<Terminal> result;
        ++metrics_.chords;current_=s->key;
        try{result=sink.chord(*s,chord);}catch(...){result=Terminal{"callback_error",0};}
        current_.reset();++used;
        double fraction=1.;
        if(result){if(!std::isfinite(result->fraction))*result={"callback_error",0};fraction=unit(result->fraction);result->fraction=fraction;}
        s->distance=std::min(s->max_distance,s->distance+length(chord.start,chord.end)*fraction);
        s->cursor_time=chord.start_time+(chord.end_time-chord.start_time)*fraction;
        s->position=interpolate(chord.start,chord.end,fraction);
        if(result){finish(s,*result,sink);return used;}
        if(cancel_current_.count(s->key))return used;
        if(chord.distance_terminal||s->distance>=s->max_distance){finish(s,{"max_distance",1},sink);return used;}
        cursor=next;
    }
    if(states_.count(s->key)&&!pending_remove_.count(s->key)&&at+epsilon>=lifetime&&s->cursor_time+epsilon>=lifetime){
        s->cursor_time=lifetime;s->position=trajectory(s->origin,s->velocity,s->gravity,s->max_time);finish(s,{"max_time",1},sink);
    }
    return used;
}
bool Cursors::advance(double at,CursorSink&sink,std::optional<std::size_t> budget){
    if(!std::isfinite(at)||at+epsilon<now_||advancing_)return false;
    metrics_={};metrics_.active=states_.size();metrics_.debt_before=debt(at);now_=std::max(now_,at);advancing_=true;
    if(!budget){const auto order=order_;for(const auto&k:order){auto it=states_.find(k);if(it!=states_.end())safely(it->second,at,sink,{});}}
    else{
        std::size_t remaining=*budget;
        while(remaining&&!round_robin_.empty()){
            const auto count=round_robin_.size();std::size_t used_round=0;
            for(std::size_t i=0;i<count&&remaining&&!round_robin_.empty();++i){
                const Key k=round_robin_.front();round_robin_.pop_front();auto it=states_.find(k);if(it==states_.end())continue;
                const auto used=safely(it->second,at,sink,1);
                if(states_.count(k)&&!pending_remove_.count(k))round_robin_.push_back(k);
                used_round+=used;remaining-=used;
            }
            if(!used_round)break;
        }
    }
    advancing_=false;flush();metrics_.debt_after=debt(at);return true;
}
}
