#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace wot
{
// #1513 009351d0 produces the seven-slot process cache; 00935d30 only reads.
// The last two slots are reset but not populated by that measurement path.
struct StockGraphicsDetection
{
    struct Profile
    {
        // GPU memory MiB, GPU benchmark score, RAM MiB, virtual memory MiB,
        // CPU frequency score. This ordering follows the original cache.
        std::array<std::uint32_t, 5> minimum;
        float pixelShader;
        float vertexShader;
        std::string name;
    };

    std::array<std::int64_t, 7> scores = {{0, 0, 0, 0, 0, 0, 0}};
    std::array<Profile, 5> profiles = {{
        {{{800, 80, 3000, 3000, 5000}}, 3.f, 3.f, "max"},
        {{{600, 60, 2000, 3000, 3000}}, 3.f, 3.f, "high"},
        {{{450, 40, 2000, 2000, 2000}}, 3.f, 3.f, "medium"},
        {{{250, 20, 1000, 1000, 1500}}, 2.f, 2.f, "low"},
        {{{250, 0, 1000, 1000, 1500}}, 2.f, 2.f, "min"}
    }}; // Defaults from 009365e0; exact authored auto-config fields override.

    std::int64_t score( int index ) const
    {
        return static_cast<unsigned>( index ) < scores.size() ? scores[index] : 0;
    }

    void store( int index, std::int64_t value )
    {
        if (static_cast<unsigned>( index ) < scores.size()) scores[index] = value;
    }

    static std::string lower( std::string value )
    {
        for (char & byte : value)
            if (byte >= 'A' && byte <= 'Z') byte += 'a' - 'A';
        return value;
    }

    int presetId( const std::string & name ) const
    {
        const std::string key = lower( name );
        for (unsigned index = 0; index < profiles.size(); ++index)
            if (profiles[index].name == key) return index;
        return 5;
    }

    // 009357b0 recomputes quality from cached scores without measuring or
    // changing them. 00934f20 adds shader capability limits to that quality.
    int classify( unsigned shaderVersion ) const
    {
        int result = 0;
        for (unsigned parameter = 0; parameter < 5; ++parameter)
        {
            int limit = 0;
            if (scores[parameter] < profiles[4].minimum[parameter] ||
                scores[parameter] < profiles[3].minimum[parameter]) limit = 4;
            else if (scores[parameter] < profiles[2].minimum[parameter]) limit = 3;
            else if (scores[parameter] < profiles[1].minimum[parameter]) limit = 2;
            else if (scores[parameter] < profiles[0].minimum[parameter]) limit = 1;
            result = std::max( result, limit );
        }
        for (int index = 4; index >= 0; --index)
        {
            const Profile & profile = profiles[index];
            const auto encode = []( float version ) -> unsigned
            {
                const unsigned major = static_cast<unsigned char>( version );
                const unsigned minor = static_cast<unsigned char>( (version - float( major )) * 10.f );
                return (major << 8) | minor;
            };
            if (shaderVersion < encode( profile.pixelShader ) ||
                shaderVersion < encode( profile.vertexShader ))
                return std::max( result, std::min( index + 1, 4 ) );
        }
        return result;
    }
};
} // namespace wot
