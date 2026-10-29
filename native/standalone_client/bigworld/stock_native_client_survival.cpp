#include "stock_native_client_survival.hpp"
#include <algorithm>
namespace stock_native_client_survival {
Effect Owner::drowning(double dt,bool eligible,const std::function<std::optional<int>()>&level_sample,const std::function<double()>&clock){
    Effect effect;if(dt<=0.||!eligible)return effect;
    state_.drown_check+=dt;if(state_.drown_check<.3)return effect;
    const double elapsed=state_.drown_check;state_.drown_check=0.;
    const auto level=level_sample();if(!level)return effect;
    if(*level==2){if(state_.drown_level!=2)state_.drown_started=clock();state_.drown_time+=elapsed;}
    else{state_.drown_time=0.;state_.drown_started.reset();}
    effect.sampled=true;effect.flag=*level==2;
    if(*level!=state_.drown_level){state_.drown_level=*level;effect.event=*level;}
    return effect;
}
Effect Owner::overturn(double dt,bool eligible,double up,const std::function<double()>&clock){
    Effect effect;if(dt<=0.||!eligible)return effect;
    up=std::max(-1.,std::min(1.,up));
    const int level=up<=config_.onboard_cosine?2:up<=config_.warning_cosine?1:0;
    if(level==0){state_.overturn_check=state_.overturn_time=0.;state_.overturn_started.reset();effect.sampled=true;
        if(state_.overturn_level!=0){state_.overturn_level=0;effect.event=0;}return effect;}
    state_.overturn_check+=dt;
    if(state_.overturn_check+.000001<std::max(0.,config_.ignore_delay))return effect;
    if(level!=state_.overturn_level){state_.overturn_level=level;state_.overturn_time=0.;state_.overturn_started=level==2?std::optional<double>(clock()):std::nullopt;effect.event=level;}
    effect.sampled=true;effect.flag=level==2;
    if(level!=2){state_.overturn_time=0.;state_.overturn_started.reset();return effect;}
    state_.overturn_time=std::min(30.,state_.overturn_time+dt);return effect;
}
}
