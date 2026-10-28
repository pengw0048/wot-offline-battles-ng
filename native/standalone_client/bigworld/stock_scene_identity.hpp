#pragma once

#include "cstdmf/bw_namespace.hpp"
#include "cstdmf/bw_string.hpp"
#include "cstdmf/stdmf.hpp"

BW_BEGIN_NAMESPACE
class CollisionObstacle;

struct StockObstacleIdentity {
    uint32 chunkID = 0;
    uint32 itemIndex = 0;
    BW::string filename;
    float scale = 0.f;
};

// The loaded static-item owner supplies original WGDE/BSMI/SpTr identity.
// False means the obstacle has no source identity. PyErr, when present,
// reports a lookup failure. No obstacle pointer is used as an item index.
bool readStockObstacleIdentity(const CollisionObstacle& obstacle,
    StockObstacleIdentity& output);

// The original static-item key alone, for scene queries: a chunk model's
// authored identity or a compiled instance's WGDE1 row (006ba9b0). False
// means the obstacle has no source identity.
bool readStockObstacleItem(const CollisionObstacle& obstacle,
    uint32& chunkID, uint32& itemIndex);

// Exact StaticModelFeature filename; compiled instances intentionally return empty.
bool readStockObstacleFilename(const CollisionObstacle& obstacle, BW::string& output);

BW_END_NAMESPACE
