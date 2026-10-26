#pragma once
#include "stock_space_decals_data.hpp"
#include "stock_decal_admission.hpp"
#include <cstring>
#include <d3d9.h>
#include <vector>

BW_BEGIN_NAMESPACE
// Textures are borrowed only for this synchronous batch. The provider retains
// their leases, draw order and original material/instance data until it returns.
struct StockDeferredDecalPacket
{
    wot::StockSpaceDecalMaterialInput material = {};
    std::array<std::uint8_t, 124> record = {};
    std::array<IDirect3DBaseTexture9*, 4> textures = {};
    unsigned kind = 0;
};
inline wot::StockDecalAdmissionKey stockDeferredDecalAdmissionKey(const std::array<std::uint8_t, 124>& record)
{
    wot::StockDecalAdmissionKey key;
    std::memcpy(&key.materialGroup, record.data() + 112, 4);
    std::memcpy(&key.material, record.data(), 4);
    std::memcpy(&key.projection, record.data() + 11, 1);
    std::memcpy(&key.priority, record.data() + 4, 4);
    std::memcpy(&key.distanceSquared, record.data() + 12, 4);
    return key;
}
unsigned stockDeferredDecalVisibleLimit();
void clearStockDeferredDecalTextures();
bool drawStockDeferredDecalPackets(const std::vector<StockDeferredDecalPacket>& packets, unsigned phase);
BW_END_NAMESPACE
