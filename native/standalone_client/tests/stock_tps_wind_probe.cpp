#include "../bigworld/stock_tps_wind.hpp"
#include <cstring>
#include <iostream>

std::uint32_t bits(float value)
{ std::uint32_t result;std::memcpy(&result,&value,4);return result; }
int main()
{
    wot::StockTPSRandom random([](){return 0;});
    for (auto & value : random.values) if (!(std::cin >> value)) return 2;
    float elapsed;
    wot::StockTPSVector3 weather;
    wot::StockTPSWind owner;
    while (std::cin >> elapsed >> weather.x >> weather.y >> weather.z >> owner.remaining >> random.cursor)
    {
        const auto value = owner.advance(weather,elapsed,random);
        std::cout << bits(value.x) << ' ' << bits(value.y) << ' ' << bits(value.z) << ' '
            << bits(owner.remaining) << ' ' << random.cursor << '\n';
    }
}
