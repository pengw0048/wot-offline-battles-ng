#include "../bigworld/stock_tps_model.hpp"
#include <cstring>
#include <iostream>

std::uint32_t bits( float value )
{ std::uint32_t result;std::memcpy( &result, &value, 4 );return result; }
int main()
{
    wot::StockTPSEmitterResource source;
    for (unsigned index : { 2u, 3u, 4u, 5u, 6u })
    {
        auto & curve = source.curves[index];
        curve.times = { 0.f, .5f, 1.f };
        curve.values = { .2f, -1.f, .4f };
        if (index == 6)
        {
            curve.components = 4;
            curve.values = { .2f, .4f, .6f, .8f, 1.f, 2.f, 3.f, 4.f, -.1f, -.2f, -.3f, -.4f };
        }
    }
    float elapsed, ground;
    unsigned paused, hasSource, stopped;
    std::int64_t identity;
    wot::StockTPSModelParticle model;
    auto & p = model.movement;
    while (std::cin >> elapsed >> ground >> paused >> hasSource >> p.flags >>
        p.ownerIdentity >> identity >> p.children >> model.rotationFlags >> stopped >>
        p.velocity.x >> p.velocity.y >> p.velocity.z >> p.position.x >> p.position.y >> p.position.z >>
        p.speedMultiplier >> p.windMultiplier >> p.delay >> p.lifetime >> p.age >> p.gravity >>
        p.inheritedVelocityDuration >> p.inheritedVelocity.x >> p.inheritedVelocity.y >> p.inheritedVelocity.z >>
        p.additionalVelocity.x >> p.additionalVelocity.y >> p.additionalVelocity.z >>
        model.angles.x >> model.angles.y >> model.angles.z >>
        model.angularVelocity.x >> model.angularVelocity.y >> model.angularVelocity.z >>
        model.colour[0] >> model.colour[1] >> model.colour[2] >> model.colour[3])
    {
        p.source = hasSource ? &source : nullptr;model.stopped = stopped != 0;
        model.tick( elapsed, ground, identity, paused != 0 );
        std::cout << p.flags << ' ' << model.rotationFlags << ' ' << unsigned(model.stopped) << ' ';
        for (float value : { p.velocity.x, p.velocity.y, p.velocity.z, p.position.x, p.position.y, p.position.z,
            p.speedMultiplier, p.windMultiplier, p.delay, p.age, p.inheritedVelocityDuration,
            model.angles.x, model.angles.y, model.angles.z,
            model.colour[0], model.colour[1], model.colour[2], model.colour[3] })
            std::cout << bits( value ) << ' ';
        std::cout << '\n';
    }
}
