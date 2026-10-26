#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace wot
{
// #1513 965770/964c50/965050. Three rows are the reflected PointLight;
// instanced vertices retain the original fourth padding row and 64-byte stride.
struct StockLocalLightMath
{
    using Vector = std::array<float, 3>;
    using Four = std::array<float, 4>;
    struct Bounds { Vector minimum, maximum; };
    struct Packet { Four position, colour, attenuation, orientation; };
    enum Group { Instanced, Volume, Fallback };

    static float distanceSquared(const Vector& a, const Vector& b)
    {
        const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
        return y * y + x * x + z * z;
    }
    static Packet omni(const Vector& position, const Four& colour, float intensity,
        float inner, float outer, const Vector& camera)
    {
        Packet result{};
        result.position = {{position[0], position[1], position[2], distanceSquared(position, camera)}};
        for (size_t component = 0; component < 4; ++component) result.colour[component] = colour[component] * intensity;
        result.attenuation = {{inner, outer, 0.f, 0.f}};
        return result;
    }
    static Packet spot(const Vector& position, const Four& colour, float intensity,
        float inner, float outer, float cosine, float tangent, const Four& orientation)
    {
        Packet result{};
        result.position = {{position[0], position[1], position[2], 0.f}};
        for (size_t component = 0; component < 4; ++component) result.colour[component] = colour[component] * intensity;
        result.attenuation = {{inner, outer, cosine, tangent * outer}};
        result.orientation = orientation;
        return result;
    }
    static Group volumeGroup(bool instancing, bool requestsShadow, bool shadowsEnabled, bool forceVolume)
    {
        return instancing && (!requestsShadow || !shadowsEnabled) && !forceVolume ? Instanced : Volume;
    }
    static Group omniGroup(const Vector& position, float outer, const Vector& camera,
        float nearDistance, bool instancing, bool requestsShadow, bool shadowsEnabled, bool forceVolume)
    {
        const float distance = outer * 1.05f + nearDistance;
        if (distanceSquared(position, camera) <= distance * distance) return Fallback;
        return volumeGroup(instancing, requestsShadow, shadowsEnabled, forceVolume);
    }
    static Group spotGroup(const Vector& position, const Vector& direction, float outer,
        float cosine, float sine, const Bounds& bounds, const Vector& camera, float nearDistance,
        bool instancing, bool requestsShadow, bool shadowsEnabled, bool forceVolume)
    {
        const float radius = outer * .05f + nearDistance;
        bool within = true;
        for (size_t axis = 0; axis < 3; ++axis)
            within = within && bounds.minimum[axis] - radius <= camera[axis] && camera[axis] < bounds.maximum[axis] + radius;
        if (within)
        {
            const Vector relative = {{camera[0] - position[0], camera[1] - position[1], camera[2] - position[2]}};
            const float shift = radius / sine;
            const Vector shifted = {{relative[0] + direction[0] * shift, relative[1] + direction[1] * shift,
                relative[2] + direction[2] * shift}};
            const float projection = direction[0] * shifted[0] + direction[1] * shifted[1] + direction[2] * shifted[2];
            const float shiftedSquared = shifted[1] * shifted[1] + shifted[0] * shifted[0] + shifted[2] * shifted[2];
            if (projection > 0.f && projection * projection >= shiftedSquared * (cosine * cosine))
            {
                const float squared = relative[1] * relative[1] + relative[0] * relative[0] + relative[2] * relative[2];
                const float behind = -(direction[0] * relative[0] + direction[1] * relative[1] + direction[2] * relative[2]);
                if (!(behind > 0.f && behind * behind >= (sine * sine) * squared)) return Fallback;
                if (squared <= radius * radius) return Fallback;
            }
        }
        return volumeGroup(instancing, requestsShadow, shadowsEnabled, forceVolume);
    }
};
static_assert(sizeof(StockLocalLightMath::Packet) == 64, "Original local-light vertex stride");
}
