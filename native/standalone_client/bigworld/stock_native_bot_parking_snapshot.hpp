#pragma once
#include "stock_native_bot_parking.hpp"
struct _object;
namespace stock_native_bot_parking {
// Read immutable catalog records; all candidate geometry stays native.
bool readCatalog(_object* runtime,const offline_navigation::MapInput&,
    const std::array<std::array<int,2>,8>& directions,
    std::map<std::int64_t,Catalog>&);
}
