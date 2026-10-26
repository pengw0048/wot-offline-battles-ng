#pragma once

#include "compiled_space/binary_format.hpp"
#include "compiled_space/terrain2_types.hpp"
#include "compiled_space/string_table.hpp"
#include "cstdmf/lookup_table.hpp"
#include "cstdmf/bw_map.hpp"
#include "wot/stock_terrain_binary.hpp"
#include <memory>

namespace BW { namespace CompiledSpace {

// The scene's external arrays borrow aligned storage owned by this loader.
// TerrainSettings retains the same immutable source while streamed blocks live.
class StockTerrain2Data
{
public:
    std::shared_ptr<wot::StockTerrainBinary> source;
    Terrain2Types::Header header;
    BW::vector<Terrain2Types::BlockData> blocks;
    BW::vector<uint32> grid, references, contents;
    BW::vector<StaticLooseOctree::NodeBoundData> bounds;
    BW::vector<StaticLooseOctree::NodeCenter> centers;
    BW::vector<StaticLooseOctree::Node> nodes;
    BW::vector<StaticLooseOctree::NodeIndex> parents;
    BW::vector<DataSpan> spans;
    BW::vector<BW::string> resources;
    BW::map<BW::string, uint32> mappings;

    bool read(BinaryFormat::Stream* stream, const StringTable& strings)
    {
        clear();
        if (!stream || stream->tell() > stream->size()) return false;
        const size_t size = stream->size() - stream->tell();
        void* data = stream->readRaw(size);
        std::shared_ptr<wot::StockTerrainBinary> next(new wot::StockTerrainBinary());
        std::string error;
        if (!next->read(data, size, error))
        {
            ASSET_MSG("Terrain2SceneProvider: %s.\n", error.c_str());
            return false;
        }
        resources.clear();
        for (size_t i = 0; i < 3; ++i)
        {
            const uint32 key = next->header.resourceKeys[i];
            const char* resource = strings.entry(key);
            if (!resource) return false;
            resources.push_back(resource);
        }
        mappings.clear();
        for (const auto& mapping : next->mappings)
        {
            const char* name = strings.entry(mapping.resourceKey);
            if (!name) return false;
            BW::string key(name);
            // #1513 ac6a90 uses a case-insensitive terrain resource lookup.
            for (size_t i = 0; i < key.size(); ++i)
                if (key[i] >= 'A' && key[i] <= 'Z') key[i] += 'a' - 'A';
            mappings[key] = mapping.value;
        }
        source = next;
        static_assert(sizeof(header) == sizeof(next->header), "BWT2 header layout");
        std::memcpy(&header, &next->header, sizeof(header));
        copy(next->blocks, blocks);
        copy(next->grid, grid);
        copy(next->bounds, bounds);
        copy(next->centers, centers);
        copy(next->nodes, nodes);
        copy(next->references, references);
        copy(next->parents, parents);
        copy(next->spans, spans);
        copy(next->contents, contents);
        return true;
    }

    void publish(ExternalArray<Terrain2Types::BlockData>& outBlocks,
        ExternalArray<uint32>& outGrid, StaticLooseOctree& tree,
        ExternalArray<DataSpan>& outSpans, ExternalArray<uint32>& outContents)
    {
        bind(blocks, outBlocks);
        bind(grid, outGrid);
        StaticLooseOctree::StorageType& storage = tree.storage();
        storage.config_.maxDepth_ = source->config.maxDepth;
        storage.config_.worldCenter_.set(source->config.center[0],
            source->config.center[1], source->config.center[2]);
        storage.config_.worldSize_ = source->config.size;
        storage.config_.numDataEntries_ = source->config.numDataEntries;
        bind(bounds, storage.bounds_);
        bind(centers, storage.centers_);
        bind(nodes, storage.nodes_);
        bind(references, storage.dataReferences_);
        bind(parents, storage.parents_);
        bind(spans, outSpans);
        bind(contents, outContents);
    }

    void clear()
    {
        source.reset(); blocks.clear(); grid.clear(); bounds.clear();
        centers.clear(); nodes.clear(); references.clear(); parents.clear();
        spans.clear(); contents.clear(); resources.clear(); mappings.clear();
    }

private:
    template<class Source, class Destination>
    static void copy(const std::vector<Source>& input, BW::vector<Destination>& output)
    {
        static_assert(sizeof(Source) == sizeof(Destination), "BWT2 array layout");
        output.resize(input.size());
        if (!input.empty()) std::memcpy(&output[0], &input[0], input.size() * sizeof(Source));
    }
    template<class T> static void bind(BW::vector<T>& owner, ExternalArray<T>& view)
    {
        view.assignExternalStorage(owner.empty() ? NULL : &owner[0], owner.size(), owner.size());
    }
};
}}
