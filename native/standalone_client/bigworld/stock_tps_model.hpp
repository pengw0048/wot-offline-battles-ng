#pragma once
#include "stock_tps_particle.hpp"
#include <array>

namespace wot
{
// fa31c0: visual model controllers advance their base once per source substep.
// Their angular motion and colour are separate from the Fx fixed-step owner.
class StockTPSModelParticle
{
public:
    StockTPSParticle movement;
    StockTPSVector3 angles, angularVelocity;
    std::array<float, 4> colour{ 1.f, 1.f, 1.f, 1.f };
    std::uint32_t rotationFlags = 0;
    bool stopped = false;
    void warmup( float elapsed, std::int64_t identity )
    { advanceBase( elapsed, identity, false ); }

    void tick( float elapsed, float groundHeight, std::int64_t identity, bool paused = false )
    {
        std::array<float, 4> sampled{};
        if ((movement.flags & 0x800u) && hasCurve( 6 ))
        {
            if (sample( 6, sampled )) colour = sampled;
            else movement.flags &= ~0x800u;
        }
        if (movement.flags & 0x4000u)
        {
            if (hasCurve( 4 ))
            {
                if (sample( 4, sampled )) movement.windMultiplier = sampled[0];
                else { movement.flags &= ~0x4000u;movement.windMultiplier = 0.f; }
            }
        }
        if (!stopped)
        {
            if ((rotationFlags & 8u) && movement.position.y <= groundHeight)
            {
                rotationFlags &= ~7u;
                movement.speedMultiplier = 0.f;
                movement.gravity = 0.f;
                stopped = true;
                movement.position.y = groundHeight + .25f;
                advanceBase( elapsed, identity, paused );
                return;
            }
            if (movement.flags & 0x10000u)
            {
                if (!hasCurve( 3 )) movement.flags &= ~0x10000u;
                else movement.speedMultiplier = sample( 3, sampled ) ? sampled[0] : 1.f;
            }
            advanceBase( elapsed, identity, paused );
            if (rotationFlags & 7u)
            {
                const float multiplier = sample( 5, sampled ) ? sampled[0] : 1.f;
                if (rotationFlags & 1u) angles.x = angularVelocity.x * elapsed * multiplier + angles.x;
                if (rotationFlags & 2u) angles.y = angularVelocity.y * elapsed * multiplier + angles.y;
                if (rotationFlags & 4u) angles.z = angularVelocity.z * elapsed * multiplier + angles.z;
            }
        }
        else
        {
            movement.flags &= ~0x4000u;
            movement.windMultiplier = 0.f;
            advanceBase( elapsed, identity, paused );
        }
    }
private:
    bool hasCurve( unsigned index ) const
    { return movement.source && movement.source->curves[index].times.size() > 1; }
    bool sample( unsigned index, std::array<float, 4> & result ) const
    {
        return hasCurve( index ) && movement.source->curves[index].sample(
            movement.age / movement.lifetime, result );
    }
    void advanceBase( float elapsed, std::int64_t identity, bool paused )
    {
        // fa3770, called directly by fa31c0 without the Fx wind/substep pass.
        if (paused) return;
        if (movement.ownerIdentity >= 0 && movement.ownerIdentity != identity) movement.flags |= 1u;
        if (movement.flags & 1u) { movement.flags |= 0x200u;return; }
        if (movement.delay > 0.f) { movement.delay -= elapsed;return; }
        float step = elapsed;
        if (movement.delay < 0.f) { step += movement.delay;movement.delay = 0.f; }
        StockTPSVector3 delta{ movement.velocity.x * step * movement.speedMultiplier,
            movement.velocity.y * step * movement.speedMultiplier,
            movement.velocity.z * step * movement.speedMultiplier };
        if (movement.inheritedVelocityDuration > 0.f)
        {
            const float fraction = (movement.inheritedVelocityDuration - movement.age) / movement.inheritedVelocityDuration;
            if (fraction > 0.f)
            {
                delta.x = delta.x + movement.inheritedVelocity.x * fraction * step;
                delta.y = delta.y + movement.inheritedVelocity.y * fraction * step;
                delta.z = delta.z + movement.inheritedVelocity.z * fraction * step;
            }
            else movement.inheritedVelocityDuration = 0.f;
        }
        if (movement.flags & 0x200000u)
        {
            delta.x = movement.additionalVelocity.x * movement.windMultiplier * step + delta.x;
            delta.y = movement.additionalVelocity.y * movement.windMultiplier * step + delta.y;
            delta.z = movement.additionalVelocity.z * movement.windMultiplier * step + delta.z;
        }
        if (!(movement.flags & 0x80u))
        {
            float gravity = movement.gravity;
            std::array<float, 4> sampled{};
            if (!(movement.flags & 0x2000u) || sample( 2, sampled ))
            {
                if (movement.flags & 0x2000u) gravity = sampled[0];
                movement.velocity.y -= gravity * 9.8f * elapsed;
            }
        }
        movement.position.x = movement.position.x + delta.x;
        movement.position.y = delta.y + movement.position.y;
        movement.position.z = delta.z + movement.position.z;
        movement.age += elapsed;
        if (movement.lifetime < movement.age)
            movement.flags |= movement.children || (movement.flags & 0x40u) ? 0x20u : 1u;
    }
};
}
