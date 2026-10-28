#pragma once
#include "cstdmf/bw_namespace.hpp"
#include <memory>
BW_BEGIN_NAMESPACE
class ClientSpace;
class SceneDrawContext;
class BoundingBox;
class Vector3;
class Vector4;
struct StockControlPointRadius;
std::shared_ptr<StockControlPointRadius> loadStockControlPointRadius(const char* resource,
    ClientSpace* space, const Vector3& position, float radius, float height, const Vector4& colour);
void drawStockControlPointRadius(const std::shared_ptr<StockControlPointRadius>& owner,
    const SceneDrawContext& context);
void retireStockControlPointRadius(const std::shared_ptr<StockControlPointRadius>& owner);
bool stockControlPointRadiusBounds(const std::shared_ptr<StockControlPointRadius>& owner,
    BoundingBox& bounds);
// Prepare actual terrain bounds before visibility selection; GPU upload stays
// in the draw phase. A temporarily unavailable surface can be retried by tick.
bool prepareStockControlPointRadiusBounds(const std::shared_ptr<StockControlPointRadius>& owner,
    BoundingBox& bounds);
BW_END_NAMESPACE
