#include "stock_native_bot_clock.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stock_native_bot {
double Clock::bounded(double maximum,const std::vector<BurstDeadline>& bursts)
{
    double result=std::max(0.,maximum);
    for(const auto& burst:bursts)if(burst.active) {
        const double due=std::max(0.,burst.remaining);
        if(due<=1.e-9)return std::min(result,1.e-9);
        result=std::min(result,due);
    }
    return result;
}
double Clock::available(const std::vector<BurstDeadline>& bursts)const
{
    const double value=std::max(0.,bank);
    if(value<=1.e-12)return 0.;
    if(value+1.e-9<period) {
        bool armed=false;
        for(const auto& burst:bursts)
            if(burst.active&&std::max(0.,burst.remaining)<=value+1.e-9){armed=true;break;}
        if(!armed)return 0.;
    }
    return bounded(std::min(value,.2),bursts);
}
bool Clock::begin(double elapsed,double now,const std::vector<BurstDeadline>& bursts)
{
    if(open_)throw std::logic_error("native Bot callback already open");
    if(!std::isfinite(elapsed)||!std::isfinite(now)||!std::isfinite(period)||period<=0.)
        throw std::invalid_argument("invalid native Bot callback clock");
    callback_steps=0;callback_max_step=0.;
    const double input=std::max(0.,elapsed);
    bank+=input;refresh_bank+=input;callback_now_=now;
    if(available(bursts)<=0.)return false;
    const double periods=std::floor((refresh_bank+1.e-9)/period);
    refresh_=periods>0.;
    if(refresh_)refresh_bank=std::max(0.,refresh_bank-periods*period);
    open_=true;return true;
}
std::optional<Slice> Clock::next(const std::vector<BurstDeadline>& bursts)
{
    if(!open_)throw std::logic_error("native Bot callback is not open");
    const double elapsed=available(bursts);
    if(elapsed<=0.)return std::nullopt;
    bank=std::max(0.,bank-elapsed);
    Slice result;result.elapsed=elapsed;result.now=callback_now_-bank;
    result.evaluation_time=callback_now_;result.refresh=refresh_;
    result.publish=refresh_||bank+1.e-9<period;
    result.start_us=sample_us;
    const double duration=std::max(1.,std::round(elapsed*1000000.));
    if(duration>double(std::numeric_limits<std::int64_t>::max()-sample_us))
        throw std::overflow_error("native Bot sample clock exhausted");
    result.end_us=sample_us+static_cast<std::int64_t>(duration);
    refresh_=false;++callback_steps;callback_max_step=std::max(callback_max_step,elapsed);
    if(next_publication<=0.)next_publication=result.now;
    while(next_publication<=result.now+1.e-9)next_publication+=period;
    return result;
}
void Clock::complete(const Slice& slice)
{
    if(!open_||slice.start_us!=sample_us||slice.end_us<=slice.start_us)
        throw std::logic_error("native Bot slice completion order changed");
    sample_us=slice.end_us;
}
void Clock::finish(){open_=false;refresh_=false;}
}
