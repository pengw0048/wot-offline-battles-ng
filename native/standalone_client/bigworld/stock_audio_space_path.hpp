#pragma once
#include <string>

namespace wot
{
// #1513 01246a10's slash split feeds its second component to
// 01246c32..01246c61. No basename guessing or slash normalization is applied.
inline std::string stockAudioArenaResource( const std::string & geometryPath )
{
    const auto first = geometryPath.find( '/' );
    if (first == std::string::npos || first + 1 == geometryPath.size())
        return std::string();
    const auto last = geometryPath.find( '/', first + 1 );
    return "scripts/arena_defs/" + geometryPath.substr( first + 1,
        last == std::string::npos ? std::string::npos : last - first - 1 ) + ".xml";
}
}
