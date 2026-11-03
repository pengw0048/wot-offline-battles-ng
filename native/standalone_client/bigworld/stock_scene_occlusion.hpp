#pragma once

#include "math/boundbox.hpp"
#include "math/matrix.hpp"
#include <d3d9.h>
#include <cstddef>
#include <memory>

BW_BEGIN_NAMESPACE
class Scene;
class IntersectionSet;

struct StockSceneOcclusionView
{
    Scene* scene = NULL; // Selects the primary scene's shared 4000-query ring.
    Matrix viewProjection;
    Vector3 camera;
    float nearPlane = 0.f, farPlane = 0.f;
    uint32 frame = 0;
    D3DVIEWPORT9 viewport{};
    IDirect3DSurface9* depth = NULL; // Identity only; the mapping retains its frame depth.
};

// One compiled mapping owns its identities, revisions and query receipts.
// Neither a recycled SceneObject address nor an old mapping can inherit history.
class StockSceneOcclusionMapping
{
public:
    explicit StockSceneOcclusionMapping(std::size_t instances);
    ~StockSceneOcclusionMapping();
    void beginFrame(const StockSceneOcclusionView& view);
    bool visible(uint32 instance, const BoundingBox& bounds,
        bool queryAllowed = true, bool* admitted = NULL);
    void issue();
    void invalidate(uint32 instance);
    void retire();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Main colour scene only: filter after the frustum query; issue after opaque
// geometry has populated the same viewport/depth surface, before later passes.
void filterStockSceneOcclusion(Scene& scene, IntersectionSet& objects,
    const Matrix& viewProjection);
void issueStockSceneOcclusion(Scene& scene);
BW_END_NAMESPACE
