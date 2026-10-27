#pragma once

#include "cstdmf/bw_string.hpp"
#include "cstdmf/smartpointer.hpp"
#include "duplo/pymodel.hpp"
#include "math/vector2.hpp"
#include <memory>

BW_BEGIN_NAMESPACE

// Both the Python registration producer and WGStickerModel use this owner.
// Registration returns -1 before initialisation or after finalisation.
int registerStockDamageSticker(const BW::string& name,
    const BW::string& texture, const BW::string& bumpTexture,
    const Vector2& size, float variation, bool randomYaw);

// A link resolves its actual current part rather than keeping a borrowed
// SuperModel pointer alive across compound reset or model replacement.
class StockVehicleStickerGeometry
{
public:
    virtual ~StockVehicleStickerGeometry() {}
    virtual bool acquire(PyModelPtr& model, Matrix& world) = 0;
};

PyObject* newStockVehicleStickerGeometry(
    const std::shared_ptr<StockVehicleStickerGeometry>& geometry);
// The link held by a getPartGeometryLink result, or null for any other object.
std::shared_ptr<StockVehicleStickerGeometry> stockVehicleStickerGeometry(PyObject* link);

bool initialiseStockVehicleStickers();
void finiStockVehicleStickers();
bool drawStockVehicleStickers();

extern int StockVehicleStickers_token;

BW_END_NAMESPACE
