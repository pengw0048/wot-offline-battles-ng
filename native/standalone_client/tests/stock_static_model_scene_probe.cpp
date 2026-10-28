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
    Moo::DrawContext draw;SceneDrawContext context{draw,Matrix()};
    auto* drawer=space.scene().getObjectOperation<DrawOperation>()->handler.get();
    auto* ticker=space.scene().getObjectOperation<TickOperation>()->handler.get();
    drawer->doDraw(context,objects.data(),objects.size());assert(draw.items.size()==6);
    assert(draw.items[0].material->base->properties==6&&!draw.items[0].material->base->boolean);
    assert(draw.items[0].material->base->floating==1.5f&&draw.items[0].material->base->integer==-1);
    assert(draw.items[0].material->base->texture=="col.dds");
    assert(draw.items[5].packet->paletteSize_==4);
    // Initial current and previous poses are produced by the same actual frame.
    assert(draw.items[5].packet->palette()[0].w==10&&draw.items[5].packet->palette()[6].w==10);
    draw.clear();ticker->doTick(objects.data(),objects.size(),.5f);ticker->doUpdateAnimations(objects.data(),objects.size(),.5f);
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

}
