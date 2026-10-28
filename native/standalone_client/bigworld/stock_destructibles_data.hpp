#pragma once
// Original #1513 compiled-model metadata and geometry transition laws.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace wot
{
struct StockDestructibleGeometryChanges
{
    std::vector<std::uint32_t> added,removed,updated,fallingCandidates;
};

template<class Binary>
int stockCompiledDestructibleCategory(const Binary& source,std::uint32_t instance)
{
    if(instance>=source.instances.models.size())return -1;
    const auto model=source.instances.models[instance];
    if(model>=source.models.info.size())return -1;
    const auto& info=source.models.info[model];
    if(info.type==1)return 1;
    if(info.type==2 && info.index<source.models.fragile.size())return 2|(source.models.fragile[info.index].words[8]&1u);
    return -1;
}

template<class Binary>
std::uint32_t stockCompiledDestructibleEffectKey(const Binary& source,std::uint32_t instance,const std::string& effect)
{
    if(instance>=source.instances.models.size())return 0;
    const auto model=source.instances.models[instance];
    if(model>=source.models.info.size())return 0;
    const auto& info=source.models.info[model];
    if(info.type==1 && info.index<source.models.falling.size())
    {
        const auto& value=source.models.falling[info.index];
        if(effect=="fractureEffect")return value.words[1];
        if(effect=="lifetimeEffect")return value.words[0];
        if(effect=="touchdownEffect")return value.words[2];
    }
    if(info.type==2 && info.index<source.models.fragile.size())
    {
        const auto& value=source.models.fragile[info.index];
        if(effect=="decayEffect")return value.words[2];
        if(effect=="effect" || effect=="ramEffect")return value.words[1];
        if(effect=="hitEffect")return value.words[3]?value.words[3]:((value.words[8]&1u)?0:value.words[1]);
        if(effect=="lifetimeEffect")return value.words[0];
    }
    return 0;
}

template<class Binary>
float stockCompiledDestructibleEffectScale(const Binary& source,std::uint32_t instance)
{
    if(instance>=source.instances.models.size())return 1.f;
    const auto model=source.instances.models[instance];
    if(model>=source.models.info.size())return 1.f;
    const auto& info=source.models.info[model];
    const std::uint32_t* word=nullptr;
    if(info.type==1 && info.index<source.models.falling.size())word=&source.models.falling[info.index].words[3];
    if(info.type==2 && info.index<source.models.fragile.size())word=&source.models.fragile[info.index].words[5];
    float result=1.f;if(word)std::memcpy(&result,word,4);return result;
}

// Execute the flag/model selection portion of 006c1300. The scene owner then
// applies its actual add/remove/update operations and qualified Python events.
// Validate every authored span first, before mutating the live geometry state.
template<class Binary>
bool destroyStockCompiledDestructible(Binary& source,std::uint32_t instance,bool animate,
    StockDestructibleGeometryChanges& changes,std::string& error,
    const std::function<bool(std::uint32_t)>& beforeFalling={})
{
    changes={};
    auto fail=[&](const char* text){error=text;return false;};
    const auto count=source.instances.models.size();
    if(instance>=count || source.instances.flags.size()!=count || source.instances.triples.size()!=count)
        return fail("Compiled destructible instance is outside BSMI");
    const auto model=source.instances.models[instance];
    if(model>=source.models.info.size())return fail("Compiled destructible model is outside BSMO");
    const auto info=source.models.info[model];
    if(info.type!=2 || info.index==0xffffffffu){error.clear();return true;}
    if(info.index>=source.models.fragile.size())return fail("Compiled fragile metadata is outside BSMO");
    const auto links=source.instances.triples[instance];
    const auto replacement=source.models.fragile[info.index].words[7];
    if(replacement!=0xffffffffu && replacement>=source.models.info.size())
        return fail("Compiled destroyed model is outside BSMO");
    auto live=[&](std::uint32_t index){return (source.instances.flags[index].activity&255u)!=0;};
    auto activity=[&](std::uint32_t index,bool active){auto& value=source.instances.flags[index].activity;value=(value&~255u)|(active?1u:0u);};
    const auto merged=links.values[2];
    if(merged!=0xffffffffu && merged>=count)return fail("Compiled merged instance is outside BSMI");
    const bool expand=merged!=0xffffffffu && live(merged);
    if(expand)
    {
        const auto group=source.instances.triples[merged];
        if(group.values[0]<=group.values[1] && group.values[1]>=count)
            return fail("Compiled merged module span is outside BSMI");
    }
    if(links.values[0]!=0xffffffffu && links.values[0]<=links.values[1])
    {
        if(links.values[1]>=source.instances.auxiliary.size())return fail("Compiled debris span is outside BSMI");
        for(auto index=links.values[0];index<=links.values[1];++index)
            if(source.instances.auxiliary[index]>=count)return fail("Compiled debris instance is outside BSMI");
    }
    if(expand)
    {
        const auto group=source.instances.triples[merged];
        for(auto index=group.values[0];index<=group.values[1];++index)
        {activity(index,true);if(index!=instance)changes.added.push_back(index);}
        activity(merged,false);changes.removed.push_back(merged);
    }
    if(replacement!=0xffffffffu)
    {
        source.instances.models[instance]=replacement;
        if(expand)changes.added.push_back(instance);
        else if(live(instance))changes.updated.push_back(instance);
    }
    else
    {
        if(!expand && live(instance))changes.removed.push_back(instance);
        activity(instance,false);
    }
    if(links.values[0]!=0xffffffffu && links.values[0]<=links.values[1])
    {
        for(auto index=links.values[0];index<=links.values[1];++index)
        {
            const auto debris=source.instances.auxiliary[index];
            if(animate && live(debris))
            {
                changes.fallingCandidates.push_back(debris);
                // 6c1691..6c17ff publishes animation while the child is still
                // physically active, before 6c1806 removes its static instance.
                if(beforeFalling && !beforeFalling(debris))return fail("Compiled child animation retired its mapping");
            }
            auto added=std::find(changes.added.begin(),changes.added.end(),debris);
            if(added!=changes.added.end())
            {*added=changes.added.back();changes.added.pop_back();}
            else if(live(debris))changes.removed.push_back(debris);
            activity(debris,false);
        }
    }
    error.clear();return true;
}
}
