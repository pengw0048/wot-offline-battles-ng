#include "pch.hpp"
#include "stock_control_point_radius.hpp"
#include "moo/complex_effect_material.hpp"
#include "moo/device_callback.hpp"
#include "moo/draw_context.hpp"
#include "moo/effect_property.hpp"
#include "moo/effect_visual_context.hpp"
#include "moo/index_buffer.hpp"
#include "moo/primitive.hpp"
#include "moo/render_context.hpp"
#include "moo/vertex_buffer.hpp"
#include "moo/vertex_declaration.hpp"
#include "moo/vertex_format.hpp"
#include "moo/vertices.hpp"
#include "moo/visual.hpp"
#include "moo/visual_manager.hpp"
#include "romp/water.hpp"
#include "scene/scene_draw_context.hpp"
#include "space/client_space.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

BW_BEGIN_NAMESPACE
// #1513 WGTerrainSelectedArea (738f60), used by the WTCP radius. Its source is
// the authored Visual mesh. Projection changes only its position bytes, keeps
// the authored UVs and removes triangles steeper than the original threshold.
struct StockControlPointRadius : Moo::DeviceCallback
{
    ClientSpace* space = NULL;
    Moo::VisualPtr visual;
    Moo::VerticesPtr vertices;
    Moo::PrimitivePtr primitive;
    Moo::ComplexEffectMaterialPtr material;
    Moo::VertexBuffer vertexBuffer;
    Moo::IndexBuffer indexBuffer;
    std::vector<unsigned char> source;
    std::vector<uint32> indices;
    std::vector<unsigned char> projectedSource;
    std::vector<uint32> projectedIndices;
    Matrix world, inverse;
    BoundingBox bounds;
    Vector4 colour;
    uint32 stride = 0, vertexCount = 0, indexCount = 0;
    float height = 0.f;
    bool active = true, prepared = false, projected = false, invertible = false;

    virtual bool recreateForD3DExDevice() const { return true; }
    virtual void deleteManagedObjects() { releaseBuffers(); }
    virtual void deleteUnmanagedObjects() { releaseBuffers(); }
    virtual void createManagedObjects() {}
    virtual void createUnmanagedObjects() {}
    void releaseBuffers()
    { projected = false; vertexBuffer.release(); indexBuffer.release(); }

    bool load(const char* resource, ClientSpace* owner, const Vector3& position,
        float radius, float overTerrainHeight, const Vector4& radiusColour)
    {
        space = owner; height = overTerrainHeight; colour = radiusColour;
        visual = Moo::VisualManager::instance()->get(resource);
        if (!visual) return false;
        Moo::Visual::ReadGuard guard(visual.getObject());
        const auto& sets = visual->renderSets();
        if (sets.empty() || sets.front().geometry_.empty() ||
            sets.front().geometry_.front().primitiveGroups_.empty()) return false;
        const auto& geometry = sets.front().geometry_.front();
        vertices = geometry.vertices_; primitive = geometry.primitives_;
        const auto original = geometry.primitiveGroups_.front().material_;
        if (!vertices || !primitive || !original) return false;
        material = new Moo::ComplexEffectMaterial(*original);
        auto& properties = material->baseMaterial()->properties();
        for (auto it = properties.begin(); it != properties.end(); ++it)
            if (it->second) it->second = it->second->clone();
        bounds = visual->boundingBox();
        if (bounds.insideOut()) return false;
        const Vector3 extent = bounds.maxBounds() - bounds.minBounds();
        if (extent.x == 0.f || extent.z == 0.f) return false;
        // 73b9d0/73b8b0 use the real mesh extent, uniform Y=1 and an XZ-only
        // translation. The WTCP record rotation does not rotate its ground ring.
        world.setScale(radius * 2.f / extent.x, 1.f, radius * 2.f / extent.z);
        world.translation(Vector3(position.x, 0.f, position.z));
        invertible = inverse.invert(world);
        bounds.transformBy(world);
        Vector3 low = bounds.minBounds(), high = bounds.maxBounds();
        low.y += position.y; high.y += position.y; bounds.setBounds(low, high);
        stride = vertices->vertexStride(); vertexCount = vertices->nVertices();
        uint32 positionStream = 0;
        size_t positionOffset = 0;
        Moo::VertexElement::StorageType::Value positionType;
        if (!vertices->pDecl() || !vertices->pDecl()->format().findElement(
                Moo::VertexElement::SemanticType::POSITION, 0, &positionStream,
                &positionOffset, &positionType) || positionStream != 0 ||
                positionOffset != 0 || positionType != Moo::VertexElement::StorageType::FLOAT3)
            return false;
        if (stride < sizeof(Vector3) || vertexCount == 0 ||
            size_t(vertexCount) > (std::numeric_limits<uint32>::max)() / stride) return false;
        const auto& originalIndices = primitive->indices();
        indices.reserve(originalIndices.size());
        for (uint32 i = 0; i != originalIndices.size(); ++i)
        {
            const uint32 index = originalIndices[i];
            if (index >= vertexCount) return false;
            indices.push_back(index);
        }
        return !indices.empty() && indices.size() % 3 == 0;
    }

    bool prepare()
    {
        if (!active || !space || space->cleared() || !invertible || !Moo::rc().device()) return false;
        if (prepared) return true;
        if (source.empty())
        {
            // Keep the exact complete source stream: UV/normal/other channels
            // are never reconstructed from positions or guessed declarations.
            Moo::VertexBuffer original = vertices->vertexBuffer();
            void* data = NULL;
            const uint32 size = vertexCount * stride;
            if (FAILED(original.lock(vertices->vertexBufferOffset(0), size, &data, D3DLOCK_READONLY)))
                return false;
            source.assign(static_cast<unsigned char*>(data), static_cast<unsigned char*>(data) + size);
            original.unlock();
        }
        auto output = source;
        std::vector<uint32> filtered;
        float minimum = (std::numeric_limits<float>::max)();
        float maximum = -(std::numeric_limits<float>::max)();
        for (uint32 i = 0; i != vertexCount; ++i)
        {
            Vector3 authored, point, projectedPoint, local;
            std::memcpy(&authored, output.data() + i * stride, sizeof(authored));
            D3DXVec3TransformCoord(&point, &authored, &world);
            float ground = space->findTerrainHeight(Vector3(point.x, 0.f, point.z));
            if (ground == (std::numeric_limits<float>::min)()) return false;
            // 739790 calls f645e0 (first positive real-water distance), then
            // subtracts the original 10000 constant before taking MAX(height).
            const Vector3 start(point.x, -1000.f, point.z), end(point.x, 1000.f, point.z);
            float water = -1.f;
            for (const Water* surface : Waters::instance())
            {
                water = surface->stockGroundRayDistance(start, end);
                if (water > 0.f) break;
            }
            const float waterHeight = water - 10000.f;
            if (waterHeight > ground) ground = waterHeight;
            if (!std::isfinite(ground) || ground < -10000.f) return false;
            projectedPoint = Vector3(point.x, ground + height, point.z);
            D3DXVec3TransformCoord(&local, &projectedPoint, &inverse);
            std::memcpy(output.data() + i * stride, &local, sizeof(local));
            minimum = (std::min)(minimum, local.y); maximum = (std::max)(maximum, local.y);
        }
        for (size_t i = 0; i != indices.size(); i += 3)
        {
            Vector3 a, b, c, normal;
            std::memcpy(&a, output.data() + indices[i] * stride, sizeof(a));
            std::memcpy(&b, output.data() + indices[i + 1] * stride, sizeof(b));
            std::memcpy(&c, output.data() + indices[i + 2] * stride, sizeof(c));
            const Vector3 ab = a - b, ac = a - c;
            D3DXVec3Cross(&normal, &ab, &ac);
            D3DXVec3Normalize(&normal, &normal);
            if (fabsf(normal.y) >= .5f)
                filtered.insert(filtered.end(), indices.begin() + i, indices.begin() + i + 3);
        }
        if (!active || space->cleared()) return false;
        auto low = bounds.minBounds(), high = bounds.maxBounds();
        low.y = (std::min)(low.y, minimum); high.y = (std::max)(high.y, maximum);
        bounds.setBounds(low, high);
        projectedSource.swap(output); projectedIndices.swap(filtered);
        prepared = true;
        return true;
    }

    bool project()
    {
        if (!prepare()) return false;
        releaseBuffers();
        if (FAILED(vertexBuffer.create(uint32(projectedSource.size()), D3DUSAGE_WRITEONLY, 0,
                D3DPOOL_MANAGED, "Other Buffers/Vertex Buffer/ControlPointRadius"))) return false;
        void* destination = NULL;
        if (FAILED(vertexBuffer.lock(0, uint32(projectedSource.size()), &destination, 0))) return false;
        std::memcpy(destination, projectedSource.data(), projectedSource.size()); vertexBuffer.unlock();
        indexCount = uint32(projectedIndices.size());
        if (indexCount)
        {
            if (FAILED(indexBuffer.create(indexCount, primitive->indicesFormat(), D3DUSAGE_WRITEONLY,
                    D3DPOOL_MANAGED, "index buffer/unknown"))) return false;
            auto destinationIndices = indexBuffer.lock();
            if (!destinationIndices.valid()) return false;
            for (uint32 i = 0; i != indexCount; ++i) destinationIndices[i] = projectedIndices[i];
            indexBuffer.unlock();
        }
        projected = true; return true;
    }

    void draw()
    {
        if (!active || !prepared || !space || space->cleared() ||
            (!projected && !project()) || !indexCount) return;
        auto effect = material->pass(Moo::RENDERING_PASS_COLOR);
        Moo::rc().push(); Moo::rc().world(world);
        Moo::rc().effectVisualContext().worldMatrix(&world);
        if (effect && effect->begin())
        {
            for (uint32 pass = 0; pass != effect->numPasses(); ++pass)
            {
                if (!effect->beginPass(pass)) break;
                ID3DXEffect* binding = effect->pEffect()->pEffect();
                if (binding && SUCCEEDED(binding->SetVector("g_color", &colour)) &&
                    SUCCEEDED(binding->CommitChanges()) && SUCCEEDED(vertices->setVertices(false)) &&
                    SUCCEEDED(vertexBuffer.set(0, 0, stride)) && SUCCEEDED(indexBuffer.set()))
                    Moo::rc().drawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, vertexCount, 0, indexCount / 3);
                effect->endPass();
            }
            effect->end();
        }
        Moo::rc().effectVisualContext().worldMatrix(NULL); Moo::rc().pop();
    }
};
namespace
{
class RadiusDraw : public Moo::DrawContext::UserDrawItem
{
public:
    explicit RadiusDraw(const std::shared_ptr<StockControlPointRadius>& owner) : owner_(owner) {}
    virtual void draw() { owner_->draw(); }
    virtual void fini() { delete this; }
private:
    std::shared_ptr<StockControlPointRadius> owner_;
};
}
std::shared_ptr<StockControlPointRadius> loadStockControlPointRadius(const char* resource,
    ClientSpace* space, const Vector3& position, float radius, float height, const Vector4& colour)
{
    const auto owner = std::make_shared<StockControlPointRadius>();
    return owner->load(resource, space, position, radius, height, colour) ? owner :
        std::shared_ptr<StockControlPointRadius>();
}
void drawStockControlPointRadius(const std::shared_ptr<StockControlPointRadius>& owner,
    const SceneDrawContext& context)
{
    if (owner && owner->active && owner->prepared &&
        context.drawContext().renderingPassType() == Moo::RENDERING_PASS_COLOR)
        context.drawContext().drawUserItem(new RadiusDraw(owner), Moo::DrawContext::TRANSPARENT_CHANNEL_MASK);
}
void retireStockControlPointRadius(const std::shared_ptr<StockControlPointRadius>& owner)
{ if (owner) owner->active = false; }
bool stockControlPointRadiusBounds(const std::shared_ptr<StockControlPointRadius>& owner, BoundingBox& bounds)
{ if (!owner) return false; bounds = owner->bounds; return true; }
bool prepareStockControlPointRadiusBounds(const std::shared_ptr<StockControlPointRadius>& owner,
    BoundingBox& bounds)
{ if (!owner || !owner->prepare()) return false; bounds = owner->bounds; return true; }
BW_END_NAMESPACE
