#pragma once

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace wot
{
// #1513 6d1a10/6d4230/9924a0. Wire records are independent of the public
// engine's aligned Matrix/AABB and survive closing the mapped space section.
struct StockStaticModelBinary
{
    using U32 = std::uint32_t;
    using Matrix = std::array<float, 16>;
    using Vector = std::array<float, 4>;
    struct Range { U32 first, last; };
    struct Bounds { float minimum[3], maximum[3]; };
    struct InstanceFlags { U32 flags, activity; };
    struct InstanceAnimation { U32 instance; std::int32_t animation; float time, rate; };
    struct Triple { U32 values[3]; };
    struct FiveFloats { float values[5]; };
    struct Collider { Bounds bounds; U32 resource, materialFirst, materialLast; };
    struct BspMaterial { U32 source, flags; };
    struct ModelInfo { U32 type, index; };
    struct Render { U32 nodeFirst, nodeLast, material, group, vertices, primitives, flags; };
    struct Animation
    {
        U32 name;
        float frameRate;
        std::int32_t firstFrame, lastFrame;
        U32 cognate, resource, nodeFirst, nodeLast, cache;
    };
    struct Node { U32 parentOffset; Matrix transform; };
    struct Falling { U32 words[11]; };
    struct Fragile { U32 words[9]; };
    struct VertexSize { U32 resource, bytes; };
    struct Material { std::int32_t effect, first, last; };
    struct Property { U32 name, kind, value; };

    struct Instances
    {
        std::vector<Matrix> transforms;
        std::vector<InstanceFlags> flags;
        std::vector<U32> masks, models;
        std::vector<std::int32_t> poseOffsets;
        std::vector<InstanceAnimation> animations;
        std::vector<U32> poseNodes;
        std::vector<Triple> triples;
        std::vector<U32> auxiliary;
        std::vector<FiveFloats> parameters;
    } instances;
    struct Models
    {
        std::vector<Range> lods;
        std::vector<Collider> colliders;
        std::vector<BspMaterial> bspMaterials;
        std::vector<Bounds> bounds;
        std::vector<ModelInfo> info;
        std::vector<U32> sounds;
        std::vector<float> lodDistancesSquared;
        std::vector<Range> lodRenders;
        std::vector<Render> renders;
        std::vector<U32> renderNodes;
        std::vector<Animation> animations;
        std::vector<U32> animationNodes;
        std::vector<Node> nodes;
        std::vector<Matrix> hardpoints;
        std::vector<Falling> falling;
        std::vector<Fragile> fragile;
        std::vector<VertexSize> vertexSizes;
    } models;
    struct Materials
    {
        std::vector<Material> records;
        std::vector<U32> effects;
        std::vector<Property> properties;
        std::vector<Matrix> matrices;
        std::vector<Vector> vectors;
    } materials;

    bool active(std::size_t index, U32 mappingMask) const
    {
        // 6d1a10: only bit zero of the second word is initial activity.
        return index < instances.flags.size() &&
            (instances.flags[index].activity & 1u) &&
            (instances.masks[index] & mappingMask);
    }

    // a36b60 converts authored squared extent to the native projected-area
    // threshold. FLT_MAX and negative extents have explicit original sentinels.
    static float lodThreshold(const Bounds& box, float squaredExtent)
    {
        if (squaredExtent < 0.f) return squaredExtent;
        if (squaredExtent == FLT_MAX) return 0.f;
        float size = 0.f;
        if (box.minimum[0] <= box.maximum[0] && box.minimum[1] <= box.maximum[1] &&
            box.minimum[2] <= box.maximum[2])
            for (unsigned axis = 0; axis < 3; ++axis)
                size = std::max(size, box.maximum[axis] - box.minimum[axis]);
        const float radiusSquared = size * .5f * size * .5f;
        const float z = (box.maximum[2] + box.minimum[2]) * .5f + std::sqrt(squaredExtent);
        const float zSquared = z * z;
        const float x = box.minimum[0] + box.maximum[0];
        const float y = box.minimum[1] + box.maximum[1];
        const float distance = (y * .25f * y + x * .25f * x + zSquared) - radiusSquared;
        const float denominator = std::max(zSquared - radiusSquared, radiusSquared * .05f);
        return std::sqrt(std::max(radiusSquared * .1f, distance) * (1.f / denominator)) *
            radiusSquared * 1.7671458f * (1.f / denominator);
    }

    static std::array<float, 3> point(const Matrix& matrix, const std::array<float, 3>& p)
    {
        return {{p[0] * matrix[0] + p[1] * matrix[4] + p[2] * matrix[8] + matrix[12],
            p[0] * matrix[1] + p[1] * matrix[5] + p[2] * matrix[9] + matrix[13],
            p[0] * matrix[2] + p[1] * matrix[6] + p[2] * matrix[10] + matrix[14]}};
    }

    // a36840/963560: camera coefficients are projection.m00 and m11 from
    // the actual view, not viewport dimensions or assumed FOV/aspect constants.
    static float lodMetric(const Bounds& box, const Matrix& world, const Matrix& view,
        float lodScale, float projectionX, float projectionY)
    {
        if (lodScale <= 0.f) return 1.f;
        const std::array<float, 3> center = {{(box.maximum[0] + box.minimum[0]) * .5f,
            (box.maximum[1] + box.minimum[1]) * .5f, (box.maximum[2] + box.minimum[2]) * .5f}};
        const auto p = point(view, point(world, center));
        float size = 0.f;
        if (box.minimum[0] <= box.maximum[0] && box.minimum[1] <= box.maximum[1] &&
            box.minimum[2] <= box.maximum[2])
            for (unsigned axis = 0; axis < 3; ++axis)
                size = std::max(size, box.maximum[axis] - box.minimum[axis]);
        float scale = 0.f;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const unsigned row = axis * 4;
            scale = std::max(scale, std::sqrt(world[row] * world[row] +
                world[row + 1] * world[row + 1] + world[row + 2] * world[row + 2]));
        }
        const float radius = size * .5f * scale;
        if (std::fabs(radius) < .0004f) return 0.f;
        const float xySquared = p[1] * p[1] + p[0] * p[0], zSquared = p[2] * p[2];
        const float radiusSquared = radius * radius;
        const float distance = (xySquared + zSquared) - radiusSquared;
        const float attenuation = std::max(1.f - xySquared / std::max(zSquared, .01f), .5f);
        const float denominator = std::max(radiusSquared * .05f, zSquared - radiusSquared);
        const float projectionArea = projectionX * .7853982f * projectionY;
        const float scaleSquared = lodScale * lodScale;
        return std::sqrt(std::max(radiusSquared * .1f, distance) * (1.f / denominator)) *
            radiusSquared * projectionArea * (1.f / denominator) * scaleSquared * attenuation;
    }

    bool read(const void* bsmi, std::size_t bsmiBytes,
        const void* bsmo, std::size_t bsmoBytes,
        const void* bsma, std::size_t bsmaBytes, std::string& error)
    {
        StockStaticModelBinary next;
        Cursor i(bsmi, bsmiBytes), o(bsmo, bsmoBytes), a(bsma, bsmaBytes);
        Instances& in = next.instances;
        Models& mo = next.models;
        Materials& ma = next.materials;
        if (!i.array(in.transforms) || !i.array(in.flags) || !i.array(in.masks) ||
            !i.array(in.models) || !i.array(in.poseOffsets) || !i.array(in.animations) ||
            !i.array(in.poseNodes) || !i.array(in.triples) || !i.array(in.auxiliary) ||
            !i.array(in.parameters) || !i.finished() ||
            !o.array(mo.lods) || !o.array(mo.colliders) || !o.array(mo.bspMaterials) ||
            !o.array(mo.bounds) || !o.array(mo.info) || !o.array(mo.sounds) ||
            !o.array(mo.lodDistancesSquared) || !o.array(mo.lodRenders) ||
            !o.array(mo.renders) || !o.array(mo.renderNodes) || !o.array(mo.animations) ||
            !o.array(mo.animationNodes) || !o.array(mo.nodes) || !o.array(mo.hardpoints) ||
            !o.array(mo.falling) || !o.array(mo.fragile) || !o.array(mo.vertexSizes) ||
            !o.finished() || !a.array(ma.records) || !a.array(ma.effects) ||
            !a.array(ma.properties) || !a.array(ma.matrices) || !a.array(ma.vectors) ||
            !a.finished())
        {
            *this = StockStaticModelBinary();
            error = "Invalid BSMI2/BSMO1/BSMA1 record layout";
            return false;
        }
        if (!next.validate(error))
        {
            *this = StockStaticModelBinary();
            return false;
        }
        *this = std::move(next);
        error.clear();
        return true;
    }

private:
    class Cursor
    {
        const unsigned char* bytes_;
        std::size_t length_, offset_ = 0;
        bool raw(void* out, std::size_t length)
        {
            if (!bytes_ || length > length_ - offset_) return false;
            if (length) std::memcpy(out, bytes_ + offset_, length);
            offset_ += length;
            return true;
        }
    public:
        Cursor(const void* bytes, std::size_t length) :
            bytes_(static_cast<const unsigned char*>(bytes)), length_(length) {}
        template<class T> bool array(std::vector<T>& out)
        {
            U32 stride = 0, count = 0;
            if (!raw(&stride, 4) || stride != sizeof(T) || !raw(&count, 4) ||
                count > (length_ - offset_) / sizeof(T)) return false;
            out.resize(count);
            return raw(count ? &out[0] : nullptr, std::size_t(count) * sizeof(T));
        }
        bool finished() const { return offset_ == length_; }
    };
    static bool range(U32 first, U32 last, std::size_t count)
    {
        return (first == 0xffffffffu && last == 0xffffffffu) ||
            (first <= last && last < count);
    }
    static bool finiteMatrix(const Matrix& m)
    {
        for (float v : m) if (!std::isfinite(v)) return false;
        return true;
    }
    bool validate(std::string& error) const
    {
        const auto fail = [&](const char* message) { error = message; return false; };
        const std::size_t count = instances.transforms.size(), modelCount = models.lods.size();
        if (instances.flags.size() != count || instances.masks.size() != count ||
            instances.models.size() != count || instances.poseOffsets.size() != count ||
            instances.triples.size() != count || instances.parameters.size() != count)
            return fail("Static model instance arrays have different lengths");
        if (models.colliders.size() != modelCount || models.bounds.size() != modelCount ||
            models.info.size() != modelCount || models.sounds.size() != modelCount ||
            models.lodDistancesSquared.size() != models.lodRenders.size())
            return fail("Static model definition arrays have different lengths");
        for (std::size_t n = 0; n < count; ++n)
        {
            if (instances.models[n] >= modelCount || instances.poseOffsets[n] < -1 ||
                !finiteMatrix(instances.transforms[n])) return fail("Invalid static model instance");
        }
        for (const auto& r : models.lods)
            if (!range(r.first, r.last, models.lodRenders.size())) return fail("Invalid model LOD span");
        for (const auto& r : models.lodRenders)
            if (!range(r.first, r.last, models.renders.size())) return fail("Invalid LOD render span");
        for (float d : models.lodDistancesSquared)
            if (!std::isfinite(d)) return fail("Invalid squared model LOD distance");
        for (const auto& r : models.renders)
        {
            if (!range(r.nodeFirst, r.nodeLast, models.renderNodes.size()) ||
                r.material >= materials.records.size() || (r.flags & ~1u))
                return fail("Invalid static render record");
        }
        for (U32 n : instances.poseNodes)
            if (n >= models.nodes.size()) return fail("Invalid instance pose node");
        for (const auto& c : models.colliders)
            if (!range(c.materialFirst, c.materialLast, models.bspMaterials.size()))
                return fail("Invalid BSP material span");
        for (const auto& m : models.bspMaterials)
            if (m.source > 0xffffu || m.flags > 0xffffu) return fail("Invalid BSP triangle flags");
        for (const auto& a : models.animations)
            if (!range(a.nodeFirst, a.nodeLast, models.animationNodes.size()) ||
                !std::isfinite(a.frameRate) || a.frameRate <= 0.f)
                return fail("Invalid static model animation");
        for (const auto& a : instances.animations)
            if (a.instance >= count || a.animation < -1 ||
                (a.animation >= 0 && std::size_t(a.animation) >= models.animations.size()) ||
                !std::isfinite(a.time) || !std::isfinite(a.rate) || a.rate < 0.f)
                return fail("Invalid instance animation producer");
        for (std::int32_t offset : instances.poseOffsets)
            if (offset >= 0 && std::size_t(offset) >= instances.poseNodes.size())
                return fail("Invalid shared instance pose offset");
        for (const auto& a : instances.animations)
        {
            if (a.animation < 0) continue;
            const std::int32_t begin = instances.poseOffsets[a.instance];
            if (begin < 0) return fail("Animation producer has no shared pose");
            std::size_t end = std::size_t(begin) + 1;
            for (; end < instances.poseNodes.size(); ++end)
            {
                const U32 parent = models.nodes[instances.poseNodes[end]].parentOffset;
                if (parent == 0xffffffffu || parent > end - std::size_t(begin)) break;
                if (!parent) return fail("Invalid shared pose parent offset");
            }
            const auto& animation = models.animations[a.animation];
            if (animation.nodeFirst == 0xffffffffu) continue;
            for (U32 channel = animation.nodeFirst; channel <= animation.nodeLast; ++channel)
            {
                const U32 target = models.animationNodes[channel];
                if (target != 0xffffffffu && target >= end - std::size_t(begin))
                    return fail("Animation channel targets outside its shared pose");
            }
        }
        for (std::size_t instance = 0; instance < count; ++instance)
        {
            const std::int32_t begin = instances.poseOffsets[instance];
            if (begin < 0) continue;
            std::size_t end = std::size_t(begin) + 1;
            for (; end < instances.poseNodes.size(); ++end)
            {
                const U32 parent = models.nodes[instances.poseNodes[end]].parentOffset;
                if (parent == 0xffffffffu || parent > end - std::size_t(begin)) break;
                if (!parent) return fail("Invalid render pose parent offset");
            }
            const auto& lods = models.lods[instances.models[instance]];
            if (lods.first == 0xffffffffu) continue;
            for (U32 lod = lods.first; lod <= lods.last; ++lod)
            {
                const auto& renders = models.lodRenders[lod];
                if (renders.first == 0xffffffffu) continue;
                for (U32 draw = renders.first; draw <= renders.last; ++draw)
                {
                    const auto& render = models.renders[draw];
                    if (render.nodeFirst == 0xffffffffu) continue;
                    for (U32 node = render.nodeFirst; node <= render.nodeLast; ++node)
                        if (models.renderNodes[node] >= end - std::size_t(begin))
                            return fail("Render node targets outside its shared pose");
                }
            }
        }
        for (const auto& m : materials.records)
            if (m.effect < -1 || (m.effect >= 0 && std::size_t(m.effect) >= materials.effects.size()) ||
                !range(U32(m.first), U32(m.last), materials.properties.size()))
                return fail("Invalid compiled material record");
        for (const auto& p : materials.properties)
            if (p.kind > 6 || (p.kind == 4 && p.value >= materials.matrices.size()) ||
                (p.kind == 5 && p.value >= materials.vectors.size()))
                return fail("Invalid compiled material property");
        for (const auto& n : models.nodes)
            if (!finiteMatrix(n.transform)) return fail("Invalid static node matrix");
        return true;
    }
};
static_assert(sizeof(StockStaticModelBinary::Collider) == 36, "Original BSMO collider size");
static_assert(sizeof(StockStaticModelBinary::Node) == 68, "Original BSMO node size");
static_assert(sizeof(StockStaticModelBinary::Animation) == 36, "Original BSMO animation size");
}
