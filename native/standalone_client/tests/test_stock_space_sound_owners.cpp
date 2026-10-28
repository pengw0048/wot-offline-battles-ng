#include <cassert>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <iostream>
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define ERROR_MSG(...) std::fprintf(stderr,__VA_ARGS__)
namespace BW
{
struct ClientSpace {bool dead=false;bool cleared()const{return dead;}};
using ClientSpacePtr=std::shared_ptr<ClientSpace>;
}
#ifndef WOT_SOUND_PRODUCTION_FILE
#define WOT_SOUND_PRODUCTION_FILE "../bigworld/stock_space_sounds.cpp"
#endif
// The production owner is consumed within this translation unit. GCC alone
// warns about its private anonymous-namespace implementation types. Suppress only
// that fixture include diagnostic; all other warnings remain errors.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsubobject-linkage"
#endif
#include "stock_space_sounds_host.hpp"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

int main()
{
    using namespace BW;
    auto space=std::make_shared<ClientSpace>();
    auto owner=acquireStockSpaceSounds(space.get());
    auto def=std::make_shared<Definition>();def->name="static_tree";def->events={"first","unused"};
    std::vector<StockSpaceSoundHandle> handles;
    for(unsigned i=0;i<201;++i)handles.push_back(owner->add(def,Vector3(float(i),2,3)));
    assert(handles.front()==0&&handles.back()==200&&owner->groups.size()==1);
    auto group=owner->groups.begin()->second;auto voice=group->voice;
    assert(group->positions.size()==200&&voice->event=="first"&&voice->starts==0);
    owner->start();assert(voice->starts==1&&voice->source->positions.size()==200);
    owner->remove(handles[200]);assert(group->positions.size()==200);
    owner->remove(handles[0]);assert(voice->source->positions.size()==199);
    assert(voice->source->positions[0].x==1);
    clearStockCurrentSpaceSounds(space);assert(owner->groups.empty()&&!voice->playing);
    // Delayed unload is harmless after preclear and replacement registration.
    auto replacement=owner->add(def,Vector3(9,8,7));assert(replacement==201);
    for(auto handle:handles)owner->remove(handle);
    assert(owner->groups.size()==1&&owner->groups.begin()->second->positions.size()==1);
    // SDK failure publishes no successful playback; positions remain retryable.
    updateAccepted=false;auto current=owner->groups.begin()->second;
    owner->start();assert(current->voice->starts==0);
    updateAccepted=true;owner->start();assert(current->voice->starts==1);
    // Clear detaches every old group before the first backend callback, so
    // callback-created replacements survive and stale start snapshots skip them.
    auto other=std::make_shared<Definition>();other->name="another";other->events={"other"};
    owner->add(other,Vector3());bool entered=false;
    onAudioStop=[&](){if(entered)return;entered=true;assert(owner->groups.empty());owner->add(def,Vector3());};
    owner->clear();onAudioStop={};assert(entered&&owner->groups.size()==1);
    current=owner->groups.begin()->second;
    onAudioUpdate=[&](){onAudioUpdate={};owner->clear();owner->add(other,Vector3());};
    owner->start();assert(current->voice->starts==0&&owner->groups.size()==1);
    current=owner->groups.begin()->second;
    onAudioPlay=[&](){onAudioPlay={};owner->clear();};
    owner->start();assert(!current->voice->playing&&owner->groups.empty());
    onAudioCreate=[&](){onAudioCreate={};owner->clear();};
    owner->add(def,Vector3());assert(owner->groups.empty()&&!audioVoices.back()->playing);
    // WGDE lookup success is distinct from a live registered sound. Real
    // mapping identity and typed instance index select only that object.
    auto identities=std::make_shared<wot::StockSpaceSoundIdentities>();
    identities->chunks={{12,0,3}};identities->items={{0,0},{1,99},{2,0}};
    identities->objects={5,0x80000007u,19};
    int mappingA=0,mappingB=0;
    owner->mapping(&mappingA).data=identities;
    const auto model=owner->add(def,Vector3(1,0,0));
    const auto tree=owner->add(def,Vector3(2,0,0));
    registerStockSpaceObjectSound(owner,&mappingA,false,5,model);
    registerStockSpaceObjectSound(owner,&mappingA,true,7,tree);
    audioReady=false;
    clearStockCurrentSpaceSounds(space);
    assert(removeStockSpaceObjectSound(owner,12,0)&&owner->groups.begin()->second->positions.size()==2);
    audioReady=true;
    assert(removeStockSpaceObjectSound(owner,12,0));
    assert(owner->groups.begin()->second->positions.size()==1);
    assert(removeStockSpaceObjectSound(owner,12,0)); // Existing object, no sound now.
    assert(removeStockSpaceObjectSound(owner,12,2)); // Object never had a sound.
    assert(!removeStockSpaceObjectSound(owner,12,3)&&!removeStockSpaceObjectSound(owner,99,0));
    assert(removeStockSpaceObjectSound(owner,12,1)&&owner->groups.empty());
    const auto older=owner->add(def,Vector3());
    registerStockSpaceObjectSound(owner,&mappingA,false,5,older);
    const auto newer=owner->add(def,Vector3());
    registerStockSpaceObjectSound(owner,&mappingB,false,5,newer);
    removeStockSpaceSoundMapping(owner,&mappingA);
    assert(owner->groups.begin()->second->positions.size()==1);
    owner->remove(older);assert(owner->groups.begin()->second->positions.front().handle==newer);
    removeStockSpaceSoundMapping(owner,&mappingB);assert(owner->groups.empty());
    // Explicit space retirement removes its identity before callbacks; another
    // space's live groups and later cleanup of the retired handle stay separate.
    auto secondSpace=std::make_shared<ClientSpace>();auto second=acquireStockSpaceSounds(secondSpace.get());
    second->add(def,Vector3());owner->add(def,Vector3());
    retireStockSpaceSounds(space.get());assert(owner->retired&&!owner->space&&owner->groups.empty());
    assert(owner->add(def,Vector3())==INVALID_STOCK_SPACE_SOUND&&second->groups.size()==1);
    owner->remove(replacement);finiStockSpaceSounds();assert(second->retired&&second->groups.empty());
    assert(!fail("fixture error boundary"));
    std::cout<<"Production static sound groups: 200 cap / first event / deferred play / failure / stale handle / reentry / space retirement PASS\n";
}
