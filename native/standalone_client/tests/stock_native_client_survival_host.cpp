#include "stock_native_client_survival.hpp"
namespace survival=stock_native_client_survival;
extern "C" int player_survival_step(const double*config,const double*input,int kind,double dt,int eligible,int available,int level,double up,double clock,double*output){
    survival::State state;state.drown_check=input[0];state.drown_time=input[1];state.drown_level=int(input[2]);if(input[3])state.drown_started=input[4];state.overturn_check=input[5];state.overturn_time=input[6];state.overturn_level=int(input[7]);if(input[8])state.overturn_started=input[9];
    survival::Owner owner({config[0],config[1],config[2]},state);int samples=0,clocks=0;
    auto now=[&](){++clocks;return clock;};
    const auto effect=kind==0?owner.drowning(dt,eligible!=0,[&](){++samples;return available?std::optional<int>(level):std::nullopt;},now):owner.overturn(dt,eligible!=0,up,now);
    const auto&s=owner.state();const double values[]={s.drown_check,s.drown_time,double(s.drown_level),s.drown_started?1.:0.,s.drown_started.value_or(0.),s.overturn_check,s.overturn_time,double(s.overturn_level),s.overturn_started?1.:0.,s.overturn_started.value_or(0.),effect.sampled?1.:0.,effect.flag?1.:0.,effect.event?1.:0.,double(effect.event.value_or(0)),double(samples),double(clocks)};
    for(unsigned i=0;i<16;++i)output[i]=values[i];return 1;
}
