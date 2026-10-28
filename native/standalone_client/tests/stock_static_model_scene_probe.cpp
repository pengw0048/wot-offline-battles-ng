#include <fstream>
int main(int argc,char** argv)
{
    using namespace BW;
    using namespace BW::CompiledSpace;
    assert(argc==2);
    BinaryFormat file;
    std::ifstream input(argv[1],std::ios::binary);
    for(unsigned i=0;i<3;++i){uint32 size;input.read(reinterpret_cast<char*>(&size),4);file.sections.emplace_back(size);input.read(reinterpret_cast<char*>(file.sections.back().data()),size);}
    assert(input);
    StringTable strings;
    strings.values={{1,"compiled.vertices"},{2,"indices"},{3,"bsp.primitives"},
        {100,"name"},{101,"source.animation"},{102,"cache.anca"},{200,"original.fx"},
        {201,"boolean"},{202,"floating"},{203,"integer"},{204,"matrix"},{205,"vector"},{206,"texture"},{207,"col.dds"}};
    BW::CompiledSpace::CompiledSpace space;
    StockStaticModelSceneLoader::registerHandlers(space.scene());
    StockStaticModelSceneLoader loader(2);
    assert(loader.loadMustSucceed());
    assert(loader.loadFromSpace(&space,file,{},Matrix(),strings));assert(file.closes==3);
    assert(space.dynamic.nodes.empty());assert(loader.bind());assert(loader.bind());assert(space.dynamic.nodes.size()==2);
    std::vector<SceneObject> objects;for(auto& pair:space.dynamic.nodes)objects.push_back(pair.second);
    BW::string modelName="stale";assert(StockStaticModelSceneLoader::modelFilename(objects[0],modelName)&&modelName.empty());
    assert(!StockStaticModelSceneLoader::modelFilename(SceneObject(),modelName));
    Moo::DrawContext draw;SceneDrawContext context{draw,Matrix()};
    auto* drawer=space.scene().getObjectOperation<DrawOperation>()->handler.get();
    auto* ticker=space.scene().getObjectOperation<TickOperation>();
    drawer->doDraw(context,objects.data(),objects.size());assert(draw.items.size()==6);
    assert(draw.items[0].material->base->properties==6&&!draw.items[0].material->base->boolean);
    assert(draw.items[0].material->base->floating==1.5f&&draw.items[0].material->base->integer==-1);
    assert(draw.items[0].material->base->texture=="col.dds");
    assert(draw.items[5].packet->paletteSize_==4);
    // Initial current and previous poses are produced by the same actual frame.
    assert(draw.items[5].packet->palette()[0].w==10&&draw.items[5].packet->palette()[6].w==10);
    draw.clear();Moo::sampledFrames.clear();
    ticker->tick(objects.data(),objects.size(),.5f);ticker->updateAnimations(objects.data(),objects.size(),.5f);
    assert(Moo::sampledFrames==std::vector<float>{5.f});
    drawer->doDraw(context,objects.data(),objects.size());assert(draw.items.size()==6);
    auto* rigid=draw.items[4].packet;assert(rigid->stockAnimatedWorld_);
    assert(rigid->matrix()[0][3][0]==15&&rigid->matrix()[1][3][0]==10);
    auto* skin=draw.items[5].packet;
    assert(skin->stockPreviousPaletteEntries_==2&&skin->paletteSize_==4);
    assert(skin->palette()[0].w==15&&skin->palette()[3].w==16);
    assert(skin->palette()[6].w==10&&skin->palette()[9].w==11);
    draw.clear();draw.pass=Moo::RENDERING_PASS_REFLECTION;
    drawer->doDraw(context,objects.data(),objects.size());assert(draw.items.size()==3);
    draw.clear();draw.pass=Moo::RENDERING_PASS_SHADOWS;
    drawer->doDraw(context,objects.data(),objects.size());assert(draw.items.size()==6);
    CollisionState hit;auto* collision=space.scene().getObjectOperation<CollisionOperation>()->handler.get();
    assert(collision->doCollide(objects[0],Vector3(),Vector3(),SweepParams(),hit));
    assert(hit.triangles==1&&hit.flags==0x4a00);
    hit={};assert(collision->doCollide(objects[0],WorldTriangle(),Vector3(),SweepParams(),hit));assert(hit.triangles==1);
    // Clear queued resource users before retiring the mapping, as a real frame flush does.
    draw.clear();loader.unload();loader.unload();assert(space.dynamic.nodes.empty());
    assert(Moo::liveCaches.empty());assert(Moo::destruction.size()==2&&Moo::destruction[0]=="animation"&&Moo::destruction[1]=="cache");
    // Shader variants can omit authored uniforms. Native setters then fail
    // locally while the real model geometry and collision remain available.
    Moo::missingProperties=true;StockStaticModelSceneLoader optimized(2);
    assert(optimized.loadFromSpace(&space,file,{},Matrix(),strings));assert(optimized.bind());
    objects.clear();for(auto& pair:space.dynamic.nodes)objects.push_back(pair.second);
    draw.pass=Moo::RENDERING_PASS_COLOR;
    drawer->doDraw(context,objects.data(),objects.size());assert(draw.items.size()==6);
    assert(draw.items[0].material->base->properties==0);
    hit={};assert(collision->doCollide(objects[0],Vector3(),Vector3(),SweepParams(),hit));assert(hit.triangles==1);
    draw.clear();optimized.unload();Moo::missingProperties=false;
    // Masked mappings publish no scene objects but still own and release their actual resources.
    StockStaticModelSceneLoader hidden(1);
    assert(hidden.loadFromSpace(&space,file,{},Matrix(),strings));assert(hidden.bind());assert(space.dynamic.nodes.empty());hidden.unload();
    // A failed required resource cannot publish a partially populated model scene.
    Moo::missingGeometry=true;StockStaticModelSceneLoader failed(2);
    assert(!failed.loadFromSpace(&space,file,{},Matrix(),strings));assert(!failed.bind());failed.unload();assert(space.dynamic.nodes.empty());Moo::missingGeometry=false;
    // addObject notifies synchronously before returning the newly inserted handle.
    StockStaticModelSceneLoader retiring(2);
    assert(retiring.loadFromSpace(&space,file,{},Matrix(),strings));
    space.dynamic.onAdd=[&]{retiring.unload();};
    assert(!retiring.bind());assert(space.dynamic.nodes.empty());assert(Moo::liveCaches.empty());retiring.unload();
    // Missing original animation is a NULL terminal result. The real authored
    // default node transforms still compose, without inventing a moving channel.
    strings.values[102]="missing.anca";StockStaticModelSceneLoader missing(2);
    assert(missing.loadFromSpace(&space,file,{},Matrix(),strings));assert(missing.bind());
    objects.clear();for(auto& pair:space.dynamic.nodes)objects.push_back(pair.second);
    draw.pass=Moo::RENDERING_PASS_COLOR;
    drawer->doDraw(context,objects.data(),objects.size());assert(draw.items.size()==6);
    assert(draw.items[5].packet->palette()[0].w==10&&draw.items[5].packet->palette()[3].w==11);
    draw.clear();missing.unload();assert(space.dynamic.nodes.empty()&&Moo::liveCaches.empty());
    // The readonly ground-decal seam uses actual remapped BSP triangles and
    // model transforms, and excludes the original collision/material groups.
    size_t materialArray=0;
    for(unsigned i=0;i<2;++i){uint32 stride,count;std::memcpy(&stride,file.sections[1].data()+materialArray,4);std::memcpy(&count,file.sections[1].data()+materialArray+4,4);materialArray+=8+size_t(stride)*count;}
    const size_t materialFlags=materialArray+12;uint32 savedMaterial;
    std::memcpy(&savedMaterial,file.sections[1].data()+materialFlags,4);
    for(uint32 flags : {0x100u,0x150u,0x4700u,0x6400u,0x6500u})
    {
        std::memcpy(file.sections[1].data()+materialFlags,&flags,4);
        StockStaticModelSceneLoader query(2);assert(query.loadFromSpace(&space,file,{},Matrix(),strings));assert(query.bind());
        objects.clear();for(auto& pair:space.dynamic.nodes)objects.push_back(pair.second);
        BW::vector<WorldTriangle> triangles;
        assert(!StockStaticModelSceneLoader::collectStockDecalTriangles(SceneObject(),{12,0,0},{2,0,0},{0,2,0},{0,0,2},triangles));
        assert(StockStaticModelSceneLoader::collectStockDecalTriangles(objects[0],{12,0,0},{2,0,0},{0,2,0},{0,0,2},triangles));
        const bool admitted=flags==0x100u||flags==0x6500u;assert(triangles.size()==unsigned(admitted));
        if(admitted){assert(triangles[0].flags()==flags&&triangles[0].v0().x==10&&triangles[0].v1().x==10);}
        assert(lastDecalBox[0].x==2&&lastDecalBox[1].x==2);
        auto old=objects[0];triangles.clear();bool retiredObserved=false;
        space.dynamic.onRemove=[&]{assert(StockStaticModelSceneLoader::collectStockDecalTriangles(old,{},{},{},{},triangles)&&triangles.empty());retiredObserved=true;};
        query.unload();assert(retiredObserved);
    }
    std::memcpy(file.sections[1].data()+materialFlags,&savedMaterial,4);
    // Authored decoration is hidden only by the primary draw context. Its
    // authored collision choices and independently constructed passes remain.
    uint32 matrixCount;std::memcpy(&matrixCount,file.sections[0].data()+4,4);
    const size_t flagsOffset=8+size_t(matrixCount)*64+8;
    uint32 initialFlags;std::memcpy(&initialFlags,file.sections[0].data()+flagsOffset,4);
    const uint32 decorationFlags=initialFlags|0x80;
    std::memcpy(file.sections[0].data()+flagsOffset,&decorationFlags,4);
    decor=1;StockStaticModelSceneLoader decoration(2);
    assert(decoration.loadFromSpace(&space,file,{},Matrix(),strings));assert(decoration.bind());
    objects.clear();for(auto& pair:space.dynamic.nodes)objects.push_back(pair.second);
    context.lowDetailed=true;
    drawer->doDraw(context,objects.data(),objects.size());assert(draw.items.size()==3);
    hit={};assert(!collision->doCollide(objects[0],Vector3(),Vector3(),SweepParams(),hit));
    hit={};assert(collision->doCollide(objects[1],Vector3(),Vector3(),SweepParams(),hit));assert(hit.triangles==1);
    draw.clear();context.lowDetailed=false;
    drawer->doDraw(context,objects.data(),objects.size());assert(draw.items.size()==6);
    draw.clear();draw.pass=Moo::RENDERING_PASS_REFLECTION;
    drawer->doDraw(context,objects.data(),objects.size());assert(draw.items.size()==3);
    draw.clear();draw.pass=Moo::RENDERING_PASS_SHADOWS;
    drawer->doDraw(context,objects.data(),objects.size());assert(draw.items.size()==6);
    draw.clear();decoration.unload();decor=0;draw.pass=Moo::RENDERING_PASS_COLOR;
    std::memcpy(file.sections[0].data()+flagsOffset,&initialFlags,4);
    // An fx=-1 slot retains NULL instead of manufacturing a default material.
    const uint32 absent=0xffffffffu;std::memcpy(file.sections[2].data()+8,&absent,4);
    StockStaticModelSceneLoader noEffect(2);assert(noEffect.loadFromSpace(&space,file,{},Matrix(),strings));assert(noEffect.bind());
    objects.clear();for(auto& pair:space.dynamic.nodes)objects.push_back(pair.second);
    drawer->doDraw(context,objects.data(),objects.size());assert(draw.items.empty());
    hit={};assert(collision->doCollide(objects[0],Vector3(),Vector3(),SweepParams(),hit));assert(hit.triangles==1);
    noEffect.unload();assert(space.dynamic.nodes.empty()&&Moo::liveCaches.empty());

    // Full production endpoints mutate the real renderer/collider membership.
    std::string error;
    auto readFixture=[&]{BinaryFormat next;for(unsigned n=0;n<3;++n){uint32 size;input.read(reinterpret_cast<char*>(&size),4);next.sections.emplace_back(size);input.read(reinterpret_cast<char*>(next.sections.back().data()),size);}assert(input);return next;};
    for(unsigned scenario=0;scenario<3;++scenario)
    {
        BinaryFormat source=readFixture();StockStaticModelSceneLoader owner(2);
        assert(owner.loadFromSpace(&space,source,{},Matrix(),strings));assert(owner.bind());
        auto endpoint=registeredDestructibles.lock();assert(endpoint);
        StockDestructibleSnapshot value;const unsigned item=scenario==2?1:0;
        assert(endpoint->snapshot(item,value)&&value.category==2&&value.effectScale==1.25f&&value.filename.empty());
        assert(value.world[3][0]==10&&value.hardpointWorld[3][0]==10&&value.height==2);
        assert(space.dynamic.nodes.size()==(scenario==2?1u:2u));
        assert(endpoint->destroy(item,false,true,false,error));
        assert(space.dynamic.nodes.size()==(scenario==0?1u:2u));
        objects.clear();for(auto& pair:space.dynamic.nodes)objects.push_back(pair.second);
        draw.clear();drawer->doDraw(context,objects.data(),objects.size());assert(draw.items.size()==3*space.dynamic.nodes.size());draw.clear();
        bool ordinaryReplacement=false;
        for(const auto& object:objects){hit={};assert(collision->doCollide(object,Vector3(),Vector3(),SweepParams(),hit));ordinaryReplacement|=hit.flags==0x100;}
        assert(ordinaryReplacement==(scenario!=0));
        assert(endpoint->restore(error));assert(space.dynamic.nodes.size()==(scenario==2?1u:2u));
        Matrix moved;moved[3][0]=33;assert(endpoint->transform(item,moved,.2f,error));
        assert(endpoint->snapshot(item,value)&&value.world[3][0]==33);
        // Retirement is observable before synchronous removal callbacks run.
        bool retiringObserved=false;space.dynamic.onRemove=[&]{assert(!endpoint->snapshot(item,value));retiringObserved=true;};
        owner.unload();assert(retiringObserved&&space.dynamic.nodes.empty()&&registeredDestructibles.expired());
        assert(!endpoint->snapshot(item,value));endpoint.reset();assert(Moo::liveCaches.empty());
    }

    // The original child-animation callback occurs before static child removal.
    BinaryFormat debris=readFixture();StockStaticModelSceneLoader debrisOwner(2);
    assert(debrisOwner.loadFromSpace(&space,debris,{},Matrix(),strings)&&debrisOwner.bind());
    auto endpoint=registeredDestructibles.lock();assert(endpoint&&space.dynamic.nodes.size()==2);
    bool childNotified=false;
    onDestruction=[&](uint32 child){
        assert(child==1&&space.dynamic.nodes.size()==2);StockDestructibleSnapshot snapshot;
        assert(endpoint->snapshot(child,snapshot));
        unsigned physical=0;for(const auto& node:space.dynamic.nodes){CollisionState childHit;
            if(collision->doCollide(node.second,Vector3(),Vector3(),SweepParams(),childHit))++physical;}
        assert(physical==1); // Parent hidden; child still participates in native queries.
        Matrix pose;pose[3][0]=44;assert(endpoint->transform(child,pose,0,error));childNotified=true;
    };
    assert(endpoint->destroy(0,true,false,true,error)&&childNotified&&space.dynamic.nodes.empty());
    assert(endpoint->restore(error)&&space.dynamic.nodes.size()==2);
    StockDestructibleSnapshot retained;assert(endpoint->snapshot(1,retained)&&retained.world[3][0]==44);
    onDestruction=[&](uint32 child){assert(child==1);debrisOwner.unload();};
    assert(!endpoint->destroy(0,true,false,true,error));assert(space.dynamic.nodes.empty()&&registeredDestructibles.expired());
    endpoint.reset();assert(Moo::liveCaches.empty());

    // Exercise both actual SDK dispatch overloads. DynamicSceneProvider uses
    // the per-object overload; the typed overload can supply a whole batch.
    strings.values[102]="cache.anca";
    std::vector<BinaryFormat> populations;
    const unsigned counts[]={1,7,64,1763};
    for(unsigned count:counts)
    {
        populations.push_back(readFixture());
        auto& source=populations.back();StockStaticModelSceneLoader owner(2);
        Moo::rc().frame=count==1?~uint32(0):0; // Initial frame and uint32 wrap are valid.
        assert(owner.loadFromSpace(&space,source,{},Matrix(),strings)&&owner.bind());
        assert(space.dynamic.nodes.size()==count);
        objects.clear();for(const auto& entry:space.dynamic.nodes)objects.push_back(entry.second);
        auto admission=registeredDestructibles.lock();assert(admission);
        Moo::sampledFrames.clear();
        ticker->tick(objects.data(),objects.size(),.25f);
        ticker->updateAnimations(objects.data(),objects.size(),.25f);
        assert(Moo::sampledFrames==std::vector<float>{2.5f});
        // Repeated untyped, typed and direct handler calls share mapping state.
        ticker->tick(objects.data(),objects.size(),.25f);
        ticker->updateAnimations(objects.data(),objects.size(),.25f);
        ticker->tick(objects[0].type(),objects.data(),objects.size(),.25f);
        ticker->updateAnimations(objects[0].type(),objects.data(),objects.size(),.25f);
        ticker->handler->doTick(objects.data(),objects.size(),.25f);
        ticker->handler->doUpdateAnimations(objects.data(),objects.size(),.25f);
        assert(Moo::sampledFrames==std::vector<float>{2.5f});
        drawer->doDraw(context,objects.data(),objects.size());
        auto* animated=draw.items.back().packet;
        assert(animated->palette()[0].w==12.5f&&animated->palette()[6].w==10.f);draw.clear();
        // Explicit same-frame geometry refresh remains a zero-time recompute.
        Matrix moved;moved[3][0]=33;moved[3][2]=100;
        assert(admission->transform(count-1,moved,0,error));
        assert((Moo::sampledFrames==std::vector<float>{2.5f,2.5f}));
        drawer->doDraw(context,objects.data(),objects.size());
        assert(draw.items.back().packet->palette()[0].w==35.5f);draw.clear();
        Moo::rc().nextFrame();ticker->tick(objects.data(),objects.size(),.5f);
        ticker->updateAnimations(objects.data(),objects.size(),.5f);
        assert(Moo::sampledFrames.size()==3&&Moo::sampledFrames.back()==7.5f);
        // A paused frame still owns one palette advance/evaluation. A failed
        // device frame can tick without animation, then recover independently.
        Moo::rc().nextFrame();ticker->tick(objects.data(),objects.size(),0);
        ticker->updateAnimations(objects.data(),objects.size(),0);
        assert(Moo::sampledFrames.size()==4&&Moo::sampledFrames.back()==7.5f);
        Moo::rc().nextFrame();ticker->tick(objects.data(),objects.size(),.25f);
        assert(Moo::sampledFrames.size()==4);
        Moo::rc().nextFrame();ticker->tick(objects.data(),objects.size(),.25f);
        ticker->updateAnimations(objects.data(),objects.size(),.25f);
        assert(Moo::sampledFrames.size()==5&&Moo::sampledFrames.back()==10.f);
        // Admit before a channel can synchronously reenter the same owner.
        Moo::rc().nextFrame();Moo::channelCalls=0;
        Moo::onAnimation=[&]{ticker->tick(objects.data(),objects.size(),.125f);
            ticker->updateAnimations(objects.data(),objects.size(),.125f);};
        ticker->tick(objects.data(),objects.size(),.125f);
        ticker->updateAnimations(objects.data(),objects.size(),.125f);
        assert(Moo::sampledFrames.size()==6&&Moo::sampledFrames.back()==11.25f&&Moo::channelCalls==2);
        // Retiring during a channel stops that evaluation and keeps resources
        // alive through its callback. No stale scene array is used afterward.
        const SceneObject selected=objects.back();SceneObject selectedCopy=selected;
        Moo::rc().nextFrame();Moo::channelCalls=0;
        Moo::onAnimation=[&]{owner.unload();assert(space.dynamic.nodes.empty());};
        ticker->tick(&selectedCopy,1,.125f);ticker->updateAnimations(&selectedCopy,1,.125f);
        assert(Moo::sampledFrames.size()==7&&Moo::sampledFrames.back()==12.5f&&Moo::channelCalls==1);
        admission.reset();assert(Moo::liveCaches.empty());
        // A newly bound mapping can run in the very same engine frame.
        assert(owner.loadFromSpace(&space,source,{},Matrix(),strings)&&owner.bind());
        objects.clear();for(const auto& entry:space.dynamic.nodes)objects.push_back(entry.second);
        Moo::sampledFrames.clear();ticker->tick(objects.data(),objects.size(),.125f);
        ticker->updateAnimations(objects.data(),objects.size(),.125f);
        assert(Moo::sampledFrames==std::vector<float>{1.25f});owner.unload();assert(Moo::liveCaches.empty());
    }
    // Each mapping owns its frame tokens, including a late bind in a frame
    // where another mapping has already advanced.
    StockStaticModelSceneLoader small(2),large(2);Moo::rc().nextFrame();
    assert(small.loadFromSpace(&space,populations[0],{},Matrix(),strings)&&small.bind());
    objects.clear();for(const auto& entry:space.dynamic.nodes)objects.push_back(entry.second);
    Moo::sampledFrames.clear();ticker->tick(objects.data(),objects.size(),.25f);
    ticker->updateAnimations(objects.data(),objects.size(),.25f);
    assert(Moo::sampledFrames==std::vector<float>{2.5f});
    assert(large.loadFromSpace(&space,populations[2],{},Matrix(),strings)&&large.bind());
    assert(space.dynamic.nodes.size()==65);Moo::sampledFrames.clear();
    objects.clear();for(const auto& entry:space.dynamic.nodes)objects.push_back(entry.second);
    ticker->tick(objects.data(),objects.size(),.25f);ticker->updateAnimations(objects.data(),objects.size(),.25f);
    assert(Moo::sampledFrames==std::vector<float>{2.5f});
    Moo::rc().nextFrame();Moo::sampledFrames.clear();
    ticker->tick(objects[0].type(),objects.data(),objects.size(),.25f);
    ticker->updateAnimations(objects[0].type(),objects.data(),objects.size(),.25f);
    assert((Moo::sampledFrames==std::vector<float>{5.f,5.f}));
    small.unload();large.unload();assert(space.dynamic.nodes.empty()&&Moo::liveCaches.empty());
    std::puts("Static mapping frame ownership: 1, 7, 64, 1763 instances; batch, reentry, refresh, multi-mapping and rebind passed");
}
