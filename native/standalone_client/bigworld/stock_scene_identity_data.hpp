#pragma once
// Portable #1513 WGDE1 static-item identity of compiled scene instances.

#include "stock_space_sounds_data.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wot {

struct StockSceneItemIdentity
{
    bool known = false;
    std::uint32_t chunkID = 0;
    std::uint32_t itemIndex = 0;
};

// DestructiblesSceneProvider bind 006bab30 builds the reverse map later read
// by 006ba9b0. For every chunk and every row i < count, each reference j of
// items[first + i] = {begin, end} resolves through 006baa10 to
// objects[begin + j]: -1 is no object, bit 31 selects SpTr, otherwise BSMI.
// The key stores (chunk id, i): i is the raw row, so an empty row (end ==
// begin - 1) adds no key but still consumes its index. A repeated key keeps
// the last row. A reference outside objects, which the original reads
// unchecked, adds no key here. This returns the BSMI keys; SpTr keys belong
// to the tree scene owner.
inline std::vector<StockSceneItemIdentity> stockSceneItemIdentities(
    const StockSpaceSoundIdentities & wgde, std::size_t bsmiCount )
{
    std::vector<StockSceneItemIdentity> bsmi( bsmiCount );
    for (const auto & chunk : wgde.chunks)
    {
        for (std::uint32_t row = 0; row < chunk.count; ++row)
        {
            const auto & item = wgde.items[std::size_t( chunk.first ) + row];
            const std::uint64_t references = std::uint32_t( item.unused - item.object + 1u );
            const std::uint64_t available = wgde.objects.size() - item.object;
            for (std::uint64_t j = 0; j < references && j < available; ++j)
            {
                const std::uint32_t object = wgde.objects[std::size_t( item.object + j )];
                if (object == 0xffffffffu || (object & 0x80000000u) || object >= bsmi.size())
                    continue;
                bsmi[object].known = true;
                bsmi[object].chunkID = chunk.id;
                bsmi[object].itemIndex = row;
            }
        }
    }
    return bsmi;
}

} // namespace wot
