#pragma once
#include <functional>
#include <optional>
namespace stock_native_client_survival {
struct Config {
    double ignore_delay=.1;
    double warning_cosine=.3420201433256688;
    double onboard_cosine=.17364817766693041;
};
struct State {
    double drown_check=0.,drown_time=0.;int drown_level=0;
    std::optional<double> drown_started;
    double overturn_check=0.,overturn_time=0.;int overturn_level=0;
    std::optional<double> overturn_started;
};
struct Effect {
    // False means the original law made no flag write at this boundary.
    bool sampled=false,flag=false;
    std::optional<int> event;
};
class Owner {
    Config config_;
    State state_;
public:
    Owner(Config config,State state={}):config_(config),state_(state){}
    const State& state()const{return state_;}
    Effect drowning(double dt,bool eligible,
        const std::function<std::optional<int>()>&native_level,
        const std::function<double()>&server_clock);
    Effect overturn(double dt,bool eligible,double up_cosine,
        const std::function<double()>&server_clock);
};
}
