#pragma once
#include "stock_native_bot_parking.hpp"
#include "stock_native_bot_seed.hpp"
namespace stock_native_bot_parking {
// Read immutable catalog records; all candidate geometry stays native.
bool readCatalog(const stock_native_bot::RoundSeed& seed,const offline_navigation::MapInput&,
    const std::array<std::array<int,2>,8>& directions,
    std::map<std::int64_t,Catalog>&);
}
