#include "stock_native_bot_clock.hpp"
#include <iomanip>
#include <iostream>
using namespace stock_native_bot;
int main()
{
    Clock clock;double elapsed=0.,now=0.;int action=0;
    BurstDeadline burst;
    std::cout<<std::setprecision(17);
    while(std::cin>>elapsed>>now>>action) {
        if(action==1)burst={true,.035};
        if(action==2)burst={false,0.};
        std::cout<<"[";
        if(clock.begin(elapsed,now,{burst})) {
            bool first=true;
            while(const auto slice=clock.next({burst})) {
                if(!first)std::cout<<",";first=false;
                std::cout<<"["<<slice->elapsed<<","<<slice->now<<","<<slice->refresh<<","<<slice->publish<<","<<slice->start_us<<","<<slice->end_us<<"]";
                if(burst.active) {
                    burst.remaining-=slice->elapsed;
                    if(burst.remaining<=1.e-9)burst.active=false;
                }
                clock.complete(*slice);
            }
            clock.finish();
        }
        std::cout<<"],"<<clock.bank<<","<<clock.refresh_bank<<","<<clock.sample_us<<","<<clock.callback_steps<<","<<clock.callback_max_step<<","<<clock.next_publication<<"\n";
    }
}
