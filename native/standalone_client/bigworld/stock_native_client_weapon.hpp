#pragma once
#include <array>
#include <optional>
#include <vector>

namespace stock_native_client_weapon {
struct Config {
    double base_dispersion=0.,after_shot=0.,after_shot_in_burst=0.,aim_time=0.;
    double reload=0.,clip_reload=0.,burst_interval=0.;
    double bloom_move=1.,bloom_rotation=1.,bloom_turret=1.;
    int clip_size=0,burst_count=0;
};
struct Gun {
    Config config;
    std::vector<int> ammunition;
    int shot=0,clip=0,burst_remaining=0,burst_total=0;
    std::optional<int> pending;
    double reload_time=0.,reload_duration=0.,dispersion=0.;
    bool load_started=false;
    bool select(int,bool instant=false);
    bool request(int);
    bool partialReload();
    bool ready(bool live=true)const;
    bool begin(int);
    bool commit(bool final,double factor=1.);
    bool cancel(double factor=1.);
    void tick(double dt,bool live,double movement,double rotation,double turret,
        const std::array<double,3>& descriptor_factors,double dispersion_factor=1.,double aim_factor=1.);
    bool rescale(double factor);
};
using Point=std::array<double,3>;
Point scatter(const Point&,double radius,double azimuth);
}
