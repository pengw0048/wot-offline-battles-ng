#pragma once
#include "cstdmf/bw_namespace.hpp"
#include "cstdmf/smartpointer.hpp"
#include "stock_ground_decals_data.hpp"
#include <string>
BW_BEGIN_NAMESPACE
class ClientSpace;
class Vector2;
class Vector3;
class WorldTriangle;
// The result flag is the original provider-dispatch boolean, independent of
// whether Terrain2 appended contacts. A false result is a native no-decal.
bool queryStockGroundDecalProjection(const SmartPointer<ClientSpace>& space,
    const Vector3& start,const Vector3& end,const Vector2& size,const WorldTriangle& nearest,
    wot::GroundDecalMath::Projection& projection,bool& admitted,std::string& error);
extern int StockGroundDecals_token;
BW_END_NAMESPACE
