#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace wot
{
// 009c18a3: elapsed milliseconds from the original Julia/RandTex GPU passes.
// Capability/measurement failures have their own original score1/score2 exits.
inline int stockGpuPerformanceScore( double julia, double randomTexture )
{
    const auto range = []( double value, double minimum, double maximum )
    {
        // MINSD chooses its second operand for an unordered input.
        if (std::isnan( value ) || value > maximum) return maximum;
        return value < minimum ? minimum : value;
    };
    double j = (range( julia, 3., 170. ) - 3.) * (1. / 167.);
    j = range( j, 0., 1. );
    const double jRemaining = 1. - j;
    double r = (range( randomTexture, 14., 300. ) - 14.) * (1. / 286.);
    r = range( r, 0., 1. );
    const double rRemaining = 1. - r;
    double jScore = jRemaining * jRemaining;
    double rScore = rRemaining * rRemaining;
    jScore *= jScore;
    rScore *= rScore;
    jScore *= jRemaining;
    rScore *= 30.;
    jScore *= 70.;
    return std::max( 3, std::min( 100, int( jScore + rScore ) ) );
}

// 00935570..009355fb. Original vendor enumeration: Intel1, AMD2, other3.
// Preserve its single-processor result1 and AMD weighting, even though the
// older public detector instead multiplies the measured frequency by all CPUs.
inline std::int64_t stockCpuFrequencyScore( std::uint64_t elapsedTicks,
    unsigned processors, unsigned vendor )
{
    const std::uint64_t twice = elapsedTicks * 2;
    std::int64_t signedTwice;
    std::memcpy( &signedTwice, &twice, sizeof( signedTwice ) );
    // 013d80b0 is signed _alldiv after the doubled tick delta wraps.
    const std::int64_t frequency = signedTwice / 1000000;
    if (processors < 2) return 1;
    if (vendor == 2) return static_cast<std::int64_t>( double( frequency ) * 0.8 );
    const std::uint64_t product = static_cast<std::uint64_t>( frequency ) * processors;
    std::int64_t result;
    std::memcpy( &result, &product, sizeof( result ) );
    return result;
}
} // namespace wot
