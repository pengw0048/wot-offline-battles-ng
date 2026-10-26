#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <type_traits>
#include <vector>

namespace wot
{
struct StockSpaceSoundData
{
    struct Definition
    {
        std::uint32_t wwiseName, eventName;
        float maxDistance;
        std::array<float,3> position;
    };
    std::vector<Definition> definitions;
    // 6ccfc0 reads one stride/count/payload array and then returns. The pinned
    // 52 sections append an empty second array, but neither its header nor its
    // payload is consumed. Preserve those bytes without inventing validation.
    std::vector<unsigned char> unusedTail;
    bool read(const void* input, std::size_t length, std::string& error)
    {
        const auto* bytes = static_cast<const unsigned char*>(input);
        if (!bytes || length < 8) { error="Invalid original WTau2 header";return false; }
        std::uint32_t stride, count;
        std::memcpy(&stride,bytes,4);std::memcpy(&count,bytes+4,4);
        if (stride != 24 || count > (length-8)/24)
        { error="Invalid original WTau2 sound array";return false; }
        StockSpaceSoundData next;
        next.definitions.resize(count);
        const std::size_t end = 8+std::size_t(count)*24;
        if (count) std::memcpy(next.definitions.data(),bytes+8,std::size_t(count)*24);
        next.unusedTail.assign(bytes+end,bytes+length);
        *this=std::move(next);error.clear();return true;
    }
};
static_assert(sizeof(StockSpaceSoundData::Definition)==24,"Original WTau2 description ABI");
// The sound-removal API uses the real WGDE1 chunk/item identity chain. This
// reader does not apply destruction, replace geometry or invent collision IDs.
struct StockSpaceSoundIdentities
{
    struct Chunk { std::uint32_t id, first, count; };
    struct Item { std::uint32_t object, unused; };
    std::vector<Chunk> chunks;
    std::vector<Item> items;
    std::vector<std::uint32_t> objects;
    std::vector<unsigned char> unusedTail;
    bool read(const void* input,std::size_t length,std::string& error)
    {
        const auto* bytes=static_cast<const unsigned char*>(input);
        std::size_t offset=0;
        auto array=[&](auto& values)->bool
        {
            using Value=typename std::decay<decltype(values)>::type::value_type;
            if (!bytes || offset>length || length-offset<8) return false;
            std::uint32_t stride,count;
            std::memcpy(&stride,bytes+offset,4);std::memcpy(&count,bytes+offset+4,4);offset+=8;
            if (stride!=sizeof(Value) || count>(length-offset)/sizeof(Value)) return false;
            values.resize(count);
            if (count) std::memcpy(values.data(),bytes+offset,std::size_t(count)*sizeof(Value));
            offset+=std::size_t(count)*sizeof(Value);return true;
        };
        StockSpaceSoundIdentities next;
        if (!array(next.chunks)||!array(next.items)||!array(next.objects))
        { error="Invalid original WGDE1 sound identity arrays";return false; }
        for (const auto& chunk:next.chunks)
            if (chunk.first>next.items.size() || chunk.count>next.items.size()-chunk.first)
            { error="WGDE sound identity chunk is outside its item array";return false; }
        for (const auto& item:next.items)
            if (item.object>=next.objects.size())
            { error="WGDE sound identity item is outside its object array";return false; }
        next.unusedTail.assign(bytes+offset,bytes+length);
        *this=std::move(next);error.clear();return true;
    }
    bool lookup(std::uint32_t chunkID,std::uint32_t itemIndex,std::uint32_t& object) const
    {
        for (const auto& chunk:chunks)
        {
            if (chunk.id!=chunkID) continue;
            // Original 6ba8f0 treats the converted uint32 item as signed.
            if (itemIndex>0x7fffffffu || itemIndex>=chunk.count) return false;
            const auto& item=items[std::size_t(chunk.first)+itemIndex];
            const auto value=objects[item.object];
            if (value==0xffffffffu) return false;
            object=value;return true;
        }
        return false;
    }
};
static_assert(sizeof(StockSpaceSoundIdentities::Chunk)==12,"Original WGDE1 chunk ABI");
static_assert(sizeof(StockSpaceSoundIdentities::Item)==8,"Original WGDE1 item ABI");

// 6c24f0 selects an authored BSMO fragile hardpoint, independently of the
// definition's hardPoint string, then multiplies local * instance.world.
// Return the exact two operands; the production owner calls D3DX directly.
template<class Binary>
bool stockSpaceModelSoundMatrices(const Binary& binary,std::size_t instance,
    typename Binary::Matrix& local,typename Binary::Matrix& world,std::string& error)
{
    if (instance>=binary.instances.models.size() || instance>=binary.instances.transforms.size())
    { error="Compiled sound instance is outside BSMI";return false; }
    const auto model=binary.instances.models[instance];
    if (model>=binary.models.info.size())
    { error="Compiled sound model is outside BSMO";return false; }
    local.fill(0.f);local[0]=local[5]=local[10]=local[15]=1.f;
    const auto& info=binary.models.info[model];
    if (info.type==2)
    {
        if (info.index>=binary.models.fragile.size())
        { error="Compiled sound fragile model is outside BSMO";return false; }
        const auto hardpoint=binary.models.fragile[info.index].words[6];
        if (hardpoint!=0xffffffffu)
        {
            if (hardpoint>=binary.models.hardpoints.size())
            { error="Compiled model sound hardpoint is outside BSMO";return false; }
            local=binary.models.hardpoints[hardpoint];
        }
    }
    world=binary.instances.transforms[instance];error.clear();return true;
}

}
