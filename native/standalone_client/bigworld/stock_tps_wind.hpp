#pragma once
#include "stock_tps_random.hpp"

namespace wot
{
class StockTPSWind
{
public:
    float remaining = 0.f;
    StockTPSVector3 advance( const StockTPSVector3 & weather, float elapsed, StockTPSRandom & random )
    {
        // f98c90/fa8100 first publishes the current weather. fa4020 then
        // samples a gust only on expiry, consuming all three random values.
        StockTPSVector3 wind = weather;
        remaining -= elapsed;
        if (remaining < 0.f)
        {
            remaining = 1.2f;
            wind.x = random.between(weather.x * .5f,weather.x * 1.5f) + .02f;
            wind.y = random.between(weather.y * .5f,weather.y * 1.5f);
            wind.z = random.between(weather.z * .5f,weather.z * 1.5f) + .07f;
        }
        return wind;
    }
};
}
