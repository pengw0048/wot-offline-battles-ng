#pragma once

#include "wot/stock_battle_presentation.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace wot
{
namespace stock_battle_marker_canvas
{
using stock_battle_presentation::Camera;
using stock_battle_presentation::GfxMatrix3;
using stock_battle_presentation::GfxMatrix4;
using stock_battle_presentation::Matrix44;
using stock_battle_presentation::Vector4f;

// scaleProperties/alphaProperties: (min, max, default, speed); #1513 013e4c98.
struct DistanceProperties
{
    float minimum, maximum, initial, speed;
};

inline DistanceProperties defaultProperties()
{
    DistanceProperties result = {50.f, 100.f, 100.f, -0.3f};
    return result;
}

// 013e5372..013e5437: clamp(initial - speed * z / far * zoom * 100, min, max).
inline float distanceValue(const DistanceProperties& properties, float viewZ, float farPlane,
    float zoomFactor)
{
    const float distance = viewZ * (1.f / farPlane);
    const float value = properties.initial - properties.speed * distance * zoomFactor * 100.f;
    if (properties.minimum > value)
        return properties.minimum;
    return properties.maximum < value ? properties.maximum : value;
}

// 013e54e6..013e5694: Scaleform projection of one marker. The marker origin
// lands on the vehicle's clip position and keeps its view depth for sorting.
inline GfxMatrix4 markerProjection(const Camera& camera, float clipX, float clipY, float viewZ)
{
    const float screenHeight = camera.halfScreenHeight + camera.halfScreenHeight;
    const float screenWidth = camera.halfScreenWidth + camera.halfScreenWidth;
    const float depth = camera.farPlane - camera.nearPlane;
    GfxMatrix4 projection = {};
    float scaleX = 0.05f / screenWidth;
    float scaleY = -0.05f / screenHeight;
    projection.m[0][0] = scaleX + scaleX;
    projection.m[1][1] = scaleY + scaleY;
    projection.m[2][2] = 1.f / depth;
    projection.m[2][3] = (-1.f / depth) * camera.nearPlane;
    projection.m[3][3] = 1.f;
    // x87 at the default 53-bit precision, stored as float.
    GfxMatrix4 translation = stock_battle_presentation::gfxIdentity4();
    translation.m[0][3] = static_cast<float>(
        static_cast<double>(camera.halfScreenWidth) * clipX * 20.0);
    translation.m[1][3] = static_cast<float>(
        static_cast<double>(camera.halfScreenHeight) * clipY * -20.0);
    translation.m[2][3] = viewZ;
    return stock_battle_presentation::multiply(projection, translation);
}

// 013e56f2..013e57b2: the marker's own 3D matrix, diag(default/100, default/100, 1).
inline GfxMatrix3 markerMatrix3D(const DistanceProperties& scale)
{
    const float value = scale.initial * 0.01f;
    GfxMatrix3 result = {{{value, 0.f, 0.f, 0.f}, {0.f, value, 0.f, 0.f}, {0.f, 0.f, 1.f, 0.f}}};
    return result;
}

// Backend provides Clip (a GFx::Value), Provider (a MatrixProviderPtr) and:
//   bool isObject(clip); bool attachMovie(root, symbol, name, depth, out)
//   void getMember(clip, name, out); void removeFromParent(clip)
//   void swapChildren(first, second); bool matrix(provider, Matrix44&)
//   void setMatrix3D(clip, GfxMatrix3); void setDisplay3D(clip, GfxMatrix4, visible)
//   void setVisible(clip, bool); void setScale(clip, double); void setAlpha(clip, double)
template <class Backend>
class Canvas
{
public:
    typedef typename Backend::Clip Clip;
    typedef typename Backend::Provider Provider;

    // 013e4250: the 0x80-byte Marker of the WG canvas.
    struct Marker
    {
        Provider provider;   // +0x08
        Clip inner;          // +0x10 "marker"
        Clip shadow;         // +0x30 "bgShadow"
        Clip clip;           // +0x50 the attached symbol
        float viewZ;         // +0x70
        unsigned depth;      // +0x74
        bool inFront;        // +0x78
        bool active;         // +0x79
    };

    enum AddResult { ADDED, NO_ROOT, ATTACH_FAILED };

    explicit Canvas(Backend& backend) :
        backend_(backend), nextDepth_(0),
        scale_(defaultProperties()), alpha_(defaultProperties())
    {
    }

    DistanceProperties& scaleProperties() { return scale_; }
    const DistanceProperties& scaleProperties() const { return scale_; }
    DistanceProperties& alphaProperties() { return alpha_; }
    const DistanceProperties& alphaProperties() const { return alpha_; }

    // 013e4820. IDs start at 0; a deleted ID or depth is reused last-in first-out.
    AddResult addMarker(Clip& root, const Provider& provider, const std::string& symbol,
        bool active, unsigned& id)
    {
        if (!backend_.isObject(root))
            return NO_ROOT;
        const unsigned markerID = freeIDs_.empty() ?
            static_cast<unsigned>(markers_.size()) : freeIDs_.back();
        const unsigned depth = freeDepths_.empty() ? nextDepth_ : freeDepths_.back();
        std::shared_ptr<Marker> marker(new Marker());
        const std::string name = stock_battle_presentation::instanceName(symbol, markerID);
        if (!backend_.attachMovie(root, symbol.c_str(), name.c_str(), static_cast<int>(depth),
                marker->clip))
            return ATTACH_FAILED;
        if (!freeIDs_.empty())
            freeIDs_.pop_back();
        if (freeDepths_.empty())
            ++nextDepth_;
        else
            freeDepths_.pop_back();
        marker->provider = provider;
        marker->viewZ = 0.f;
        marker->depth = depth;
        marker->inFront = false;
        marker->active = active;
        backend_.getMember(marker->clip, "marker", marker->inner);
        backend_.getMember(marker->clip, "bgShadow", marker->shadow);
        markers_[markerID] = marker;
        id = markerID;
        return ADDED;
    }

    // 013e43d0: recycle the depth, remove the AS3 child, then recycle the ID.
    bool delMarker(unsigned id)
    {
        typename Markers::iterator found = markers_.find(id);
        if (found == markers_.end())
            return false;
        std::shared_ptr<Marker> marker = found->second;
        freeDepths_.push_back(marker->depth);
        backend_.removeFromParent(marker->clip);
        markers_.erase(found);
        freeIDs_.push_back(id);
        return true;
    }

    Marker* find(unsigned id)
    {
        typename Markers::iterator found = markers_.find(id);
        return found == markers_.end() ? NULL : found->second.get();
    }

    // 013e3f20.
    bool setActive(unsigned id, bool active)
    {
        Marker* marker = find(id);
        if (marker == NULL)
            return false;
        marker->active = active;
        return true;
    }

    // 013e4050: unknown IDs are ignored; an active marker keeps its provider
    // when asked to clear it.
    void setMatrix(unsigned id, const Provider& provider)
    {
        Marker* marker = find(id);
        if (marker == NULL || (marker->active && !provider))
            return;
        if (!(marker->provider == provider))
            marker->provider = provider;
    }

    // 013e5160 followed by sortMarkers 013e4f80.
    void tick(const Camera& camera)
    {
        for (typename Markers::iterator it = markers_.begin(); it != markers_.end(); ++it)
        {
            Marker& marker = *it->second;
            if (!marker.active)
            {
                backend_.setVisible(marker.clip, false);
                continue;
            }
            Matrix44 world;
            if (!marker.provider || !backend_.matrix(marker.provider, world))
                continue;
            const float x = world.m[3][0], y = world.m[3][1], z = world.m[3][2];
            marker.viewZ = stock_battle_presentation::transformPoint(camera.view, x, y, z).z;
            const float scale = distanceValue(scale_, marker.viewZ, camera.farPlane,
                camera.zoomFactor);
            const float alpha = distanceValue(alpha_, marker.viewZ, camera.farPlane,
                camera.zoomFactor);
            Vector4f clip = stock_battle_presentation::transformPoint(
                camera.viewProjection, x, y, z);
            marker.inFront = clip.w > 0.f;
            if (marker.inFront)
            {
                const float inverse = 1.f / clip.w;
                clip.x *= inverse;
                clip.y *= inverse;
            }
            const GfxMatrix4 projection = markerProjection(camera, clip.x, clip.y, marker.viewZ);
            backend_.setMatrix3D(marker.clip, markerMatrix3D(scale_));
            backend_.setScale(marker.inner, scale);
            backend_.setAlpha(marker.shadow, alpha);
            backend_.setDisplay3D(marker.clip, projection, marker.inFront);
        }
        sortMarkers();
    }

    void clear()
    {
        markers_.clear();
    }

    std::size_t size() const { return markers_.size(); }

private:
    typedef std::map<unsigned, std::shared_ptr<Marker> > Markers;

    // 013e4f80: order visible markers by view depth (nearest first), then
    // bubble the stored depths so the nearest marker has the highest one,
    // mirroring each depth swap with AS3 swapChildren (013e3080).
    void sortMarkers()
    {
        if (markers_.empty())
            return;
        std::vector<Marker*> visible;
        for (typename Markers::iterator it = markers_.begin(); it != markers_.end(); ++it)
        {
            Marker* marker = it->second.get();
            if (marker && marker->inFront && marker->active)
                visible.push_back(marker);
        }
        std::sort(visible.begin(), visible.end(), nearer);
        const std::size_t count = visible.size();
        for (std::size_t pass = 1; pass < count; ++pass)
        {
            for (std::size_t index = 0; index + pass < count; ++index)
            {
                Marker& first = *visible[index];
                Marker& second = *visible[index + 1];
                if (static_cast<int>(first.depth) < static_cast<int>(second.depth))
                {
                    std::swap(first.depth, second.depth);
                    backend_.swapChildren(first.clip, second.clip);
                }
            }
        }
    }

    // 013e4230.
    static bool nearer(const Marker* first, const Marker* second)
    {
        return second->viewZ > first->viewZ;
    }

    Backend& backend_;
    Markers markers_;
    std::vector<unsigned> freeIDs_;
    std::vector<unsigned> freeDepths_;
    unsigned nextDepth_;
    DistanceProperties scale_;
    DistanceProperties alpha_;
};
}
}
