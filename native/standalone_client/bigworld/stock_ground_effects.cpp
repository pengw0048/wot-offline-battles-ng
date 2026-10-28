#include "pch.hpp"
#include "stock_ground_effects.hpp"
#include "stock_ground_effects_data.hpp"
#include "stock_tps_scene.hpp"
#include "stock_audio_source.hpp"
#include "stock_graphics_settings_consumers.hpp"
#include "stock_dynamic_decals.hpp"
#include "pyscript/py_data_section.hpp"
#include "pyscript/script_math.hpp"
#include "space/client_space.hpp"
#include "space/deprecated_space_helpers.hpp"
#include "physics2/collision_callback.hpp"
#include "physics2/collision_obstacle.hpp"
#include "physics2/material_kinds.hpp"
#include "physics2/worldtri.hpp"
#include "terrain/base_terrain_block.hpp"
#include "moo/renderer.hpp"
#include "romp/water.hpp"
#include "cstdmf/stock_worker_runtime.hpp"
#include <array>
#include <cstdlib>
#include <deque>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

BW_BEGIN_NAMESPACE
int StockGroundEffects_token=1;
float Water::stockGroundRayDistance(const Vector3& start,const Vector3& end) const
{
    // a302e0's false-alpha branch. The original readiness and visibility writers
    // are a2ba50/a2d970 and a2ff10 respectively.
    if (wot::stockSimulationWorker()) return collideStockWorkerWater(this,start,end);
    if (!initialised_||!visible_) return -1.f;
    return stockWaterGeometryDistance(start,end);
}
float Water::stockQueryRayDistance(const Vector3& start,const Vector3& end) const
{
    if (!stockQueryDataReady_) return -1.f;
    return stockWaterGeometryDistance(start,end);
}
float Water::stockWaterGeometryDistance(const Vector3& start,const Vector3& end) const
{
    if (start==end) return -1.f;
    Vector3 normal;
    const Vector3 up(0.f,1.f,0.f);
    D3DXVec3TransformNormal(&normal,&up,&transform_);
    const Vector3& position=this->position();
    const float plane=(start.x*normal.x+normal.y*start.y+normal.z*start.z)-
        (normal.y*position.y+normal.x*position.x+normal.z*position.z);
    const Vector3 projection(start.x-normal.x*plane,start.y-normal.y*plane,start.z-normal.z*plane);
    Vector3 direction=end-start,towards=projection-start;
    D3DXVec3Normalize(&direction,&direction);
    D3DXVec3Normalize(&towards,&towards);
    const float cosine=towards.x*direction.x+towards.y*direction.y+towards.z*direction.z;
    if (!(cosine>0.f)) return -1.f;
    const Vector3 difference=projection-start;
    const float distance=sqrtf(difference.x*difference.x+difference.y*difference.y+difference.z*difference.z)*(1.f/cosine);
    const Vector3 hit(direction.x*distance+start.x,direction.y*distance+start.y,direction.z*distance+start.z);
    Vector4 local;
    D3DXVec3Transform(&local,&hit,&invTransform_);
    const Vector3& minimum=bbActual_.minBounds();
    const Vector3& maximum=bbActual_.maxBounds();
    if (minimum.x<=local.x+.001f&&minimum.z<=local.z+.001f&&
        local.x-.001f<=maximum.x&&local.z-.001f<=maximum.z) return distance;
    return -1.f;
}
namespace
{
using GroundMath=wot::StockGroundEffectMath;
Vector3 vector3(const GroundMath::Vector& value) { return Vector3(value[0],value[1],value[2]); }
GroundMath::Vector vector3(const Vector3& value) { return {{value.x,value.y,value.z}}; }
const char* groundNames[]={"ground","stone","wood","metal","snow","sand","water"};
const char* effectNames[]={"effect_explosion","effect_explosion_add","effect_explosion_snp","effect_explosion_fwd","effect_smoke"};
struct Sound
{
    BW::string main,keyoff,category;
    StockAudioSource source;
    explicit Sound(const char* name):category(name) {}
    void load(DataSectionPtr section)
    {
        if (!section) return;
        DataSectionPtr primary=section->openSection("main");
        main=(primary?primary:section)->asString();
        DataSectionPtr release=section->openSection("keyoff");
        if (release) keyoff=release->asString();
    }
    void position(const Vector3& value) { if (source) setStockAudioSourceOffset(source,value); }
    void play(const Vector3& value)
    {
        if (!source)
        {
            std::ostringstream name;name<<category<<'/'<<static_cast<const void*>(this);
            source=createStockAudioSource(BW::string(name.str().c_str()));
        }
        else stopStockAudioSource(source);
        position(value);
        if (!main.empty() && !playStockAudioSource(source,main))
            ERROR_MSG("Ground effect: cannot play sound %s\n",main.c_str());
    }
    void finish() { if (source&&!keyoff.empty()&&!playStockAudioSource(source,keyoff))
        ERROR_MSG("Ground effect: cannot play keyoff %s\n",keyoff.c_str()); }
    void stop() { if (source) stopStockAudioSource(source); }
};
struct Decal
{
    Vector2 size=Vector2(1.f,1.f);
    BW::string group,diffuse,normal,specular;
    float variation=.2f;
    bool randomYaw=true;
    bool load(DataSectionPtr parent,std::string& error)
    {
        DataSectionPtr section=parent->openSection("decal");
        if (!section) { error="Ground effect decal section is missing";return false; }
        size=section->readVector2("size",size);
        group=section->readString("groupName");
        randomYaw=section->readBool("randomYaw",randomYaw);
        variation=section->readFloat("variation",variation);
        diffuse=section->readString("diffuseName");normal=section->readString("bumpName");
        specular=section->readString("smTexName");
        return true;
    }
    bool play(const Vector3& point,std::string& error)
    {
        if (Renderer::instance().pipelineType()==IRendererPipeline::TYPE_FORWARD_SHADING) return true;
        float yaw=0.f;
        if (randomYaw) yaw=float(std::rand())*9.5828123154938221e-05f;
        const float maximum=variation+1.f,minimum=1.f-variation;
        const float scale=float(std::rand())*(maximum-minimum)*3.0518509447574615e-05f+minimum;
        const Vector2 extent(scale*size.x,size.y*scale);
        const float top=point.y+1.f,bottom=point.y-1.f;
        const float centre=(top+bottom)*.5f;
        Vector3 projectionNormal(0.f,bottom-top,0.f);
        D3DXVec3Normalize(&projectionNormal,&projectionNormal);
        const float radius=sqrtf(extent.y*extent.y+extent.x*extent.x)*.707f;
        projectionNormal.x=projectionNormal.x*radius;projectionNormal.y=projectionNormal.y*radius;projectionNormal.z=projectionNormal.z*radius;
        const Vector3 start(point.x-projectionNormal.x,centre-projectionNormal.y,point.z-projectionNormal.z);
        const Vector3 end(projectionNormal.x+point.x,projectionNormal.y+centre,projectionNormal.z+point.z);
        return addStockDynamicDecal(stockDynamicDecalGroupLifetime(group),start,end,extent,yaw,
            diffuse,normal,specular,true,error);
    }
};
struct Settings
{
    std::array<std::array<BW::string,5>,7> effects;
    Decal decal;
    float smokeVariation=.5f,addVariation=.5f,stepOffset=.5f;
    int optimisation=2,sniperOptimisation=10;
    bool load(DataSectionPtr section,bool artillery,std::string& error)
    {
        if (!section) { error="Ground effect section is missing";return false; }
        for (size_t ground=0;ground<effects.size();++ground)
        {
            DataSectionPtr source=section->openSection(groundNames[ground]);
            if (!source) { error=std::string("Ground effect surface section is missing: ")+groundNames[ground];return false; }
            for (size_t effect=0;effect<5;++effect) effects[ground][effect]=source->readString(effectNames[effect]);
        }
        addVariation=section->readFloat("add_variation",addVariation);
        if (artillery)
        {
            smokeVariation=section->readFloat("smoke_variation",smokeVariation);
            optimisation=8;
            DataSectionPtr limits=section->openSection("optimization");
            if (limits)
            {
                optimisation=limits->readInt("default",optimisation);
                sniperOptimisation=limits->readInt("sniper",sniperOptimisation);
            }
        }
        else
        {
            stepOffset=section->readFloat("step_offset",stepOffset);
            optimisation=section->readInt("optimization",optimisation);
        }
        return decal.load(section,error);
    }
};
struct Artillery
{
    Settings settings;
    Sound closeSound{"Artillery"},farSound{"Artillery"},tracer{"Artillery"};
    bool load(DataSectionPtr section,std::string& error)
    {
        if (!settings.load(section,true,error)) return false;
        closeSound.load(section->openSection("wwexplosion_close"));farSound.load(section->openSection("wwexplosion_far"));
        tracer.load(section->openSection("wwtracer"));return true;
    }
    void stop() { closeSound.stop();farSound.stop();tracer.stop(); }
};
struct Hit : CollisionCallback
{
    uint32 material=0;
    bool found=false;
    virtual int operator()(const CollisionObstacle&,const WorldTriangle& triangle,float)
    {
        if (triangle.collisionFlags()&TRIANGLE_PROJECTILENOCOLLIDE) return COLLIDE_ALL;
        found=true;material=triangle.materialKind();return COLLIDE_BEFORE;
    }
};
// Implemented below on Water's real local box and readiness state. Ground's
// original query passes false for the optional alpha/rigid-cell test.
float waterDistance(const Vector3& start,const Vector3& end)
{
    for (const Water* water:Waters::instance())
    {
        const float distance=water->stockGroundRayDistance(start,end);
        if (distance>0.f) return distance;
    }
    return -1.f;
}
Vector3 groundPoint(ClientSpace& space,const Vector3& point,size_t& kind)
{
    const Vector3 start(point.x,1000.f,point.z),end(point.x,-1000.f,point.z);
    const float water=waterDistance(start,end);
    Hit hit;
    const float distance=space.collide(start,end,hit);
    Vector3 result=point;
    BW::string material;
    if (hit.found && distance>=0.f)
    {
        result=Vector3(point.x,1000.f-distance,point.z);
        material=MaterialKinds::instance().userString(hit.material,"effect_material");
    }
    if (water>0.f && (hit.found?result.y:-100.f)<1000.f-water)
    {
        result.y=1000.f-water;material="water";
        Waters::instance().addMovement(result+Vector3(0.f,5.f,0.f),result-Vector3(0.f,5.f,0.f),10.f);
    }
    if (material.empty()) material="ground";
    for (kind=0;kind<7;++kind) if (material==groundNames[kind]) return result;
    // 8b0d80 is map::operator[]: an unknown nonempty material inserts an
    // empty five-effect definition, then 8b0cd0 copies those empty filenames.
    return result;
}
struct Strike
{
    Settings settings;
    Sound closeSound{"Airstrike"},farSound{"Airstrike"};
    std::vector<GroundMath::Impact> impacts;
    size_t cursor=0;
    float elapsed=0.f;
    int counter=0;
    ClientSpacePtr owningSpace;
    bool load(DataSectionPtr section,std::string& error)
    {
        if (!settings.load(section,false,error)) return false;
        closeSound.load(section->openSection("wwexplosion_close"));farSound.load(section->openSection("wwexplosion_far"));return true;
    }
    bool launch(const Vector3& origin,const Vector3& velocity,float width,float length,int across,int along,std::string& error)
    {
        owningSpace=DeprecatedSpaceHelpers::cameraSpace();
        if (!owningSpace||owningSpace->cleared()) { error="Airstrike requires a live camera space";return false; }
        Vector3 direction;
        D3DXVec3Normalize(&direction,&velocity);
        impacts=GroundMath::airstrike(vector3(origin),vector3(direction),velocity.length(),width,length,across,along,
            settings.addVariation,settings.stepOffset,[](){return std::rand();});
        for (auto& impact:impacts)
        {
            Vector3 point=vector3(impact.position);
            if (!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z)||!std::isfinite(impact.delay))
            { error="Airstrike geometry exceeds the native floating-point range";return false; }
            const float height=owningSpace->findTerrainHeight(point);
            if (height!=Terrain::BaseTerrainBlock::NO_TERRAIN) impact.position[1]=height;
        }
        if (!impacts.empty()) { closeSound.play(origin);farSound.play(origin); }
        return true;
    }
    bool alive() const { return cursor<impacts.size(); }
    void stop() { closeSound.stop();farSound.stop();impacts.clear();cursor=0; }
    bool tick(float delta,std::string& error)
    {
        const ClientSpacePtr space=DeprecatedSpaceHelpers::cameraSpace();
        if (!space||space!=owningSpace||space->cleared()) { stop();return true; }
        while (alive() && !(elapsed<impacts[cursor].delay))
        {
            const auto impact=impacts[cursor++];
            ++counter;
            if (counter>=settings.optimisation || impact.kind==2)
            {
                counter=0;
                size_t ground=0;
                const Vector3 point=groundPoint(*space,vector3(impact.position),ground);
                const bool forward=Renderer::instance().pipelineType()==IRendererPipeline::TYPE_FORWARD_SHADING;
                size_t effect=3;
                if (!forward)
                {
                    if (impact.kind!=2 && !settings.decal.play(point,error)) { stop();return false; }
                    effect=stockBinocularsEnabled()?2:impact.kind==2?4:impact.kind==1?1:0;
                }
                const BW::string resource=ground<settings.effects.size()?settings.effects[ground][effect]:BW::string();
                if (!resource.empty()&&!playStockTPSOneShot(resource,point,error)) { stop();return false; }
                closeSound.position(point);farSound.position(point);
            }
            if (!alive()) { closeSound.finish();farSound.finish(); }
        }
        elapsed=elapsed+delta;return true;
    }
};
struct Manager
{
    std::vector<std::unique_ptr<Artillery>> artillery;
    std::vector<DataSectionPtr> airstrikes;
    std::vector<std::unique_ptr<Strike>> active;
    void stopAll() { for (auto& strike:active) strike->stop();active.clear();for (auto& source:artillery) source->stop(); }
    ~Manager() { stopAll(); }
};
std::unique_ptr<Manager> manager;
Manager* currentManager()
{
    if (!manager) PyErr_SetString(PyExc_RuntimeError,"Ground effect manager is not initialised");
    return manager.get();
}
DataSectionPtr sectionArgument(PyObject* object)
{
    PyDataSectionPtr section;
    if (Script::setData(object,section,"() argument 1")) return NULL;
    if (!section) { PyErr_SetString(PyExc_TypeError,"Ground effect argument 1 must be a DataSection");return NULL; }
    return section->pSection();
}
}
class PyGroundEffectManager : public PyObjectPlus
{
    Py_Header(PyGroundEffectManager,PyObjectPlus)
public:
    PyGroundEffectManager():PyObjectPlus(&s_type_) {}
    PY_METHOD_DECLARE(py_loadArtillery)
    PY_METHOD_DECLARE(py_loadAirstrike)
    PY_METHOD_DECLARE(py_playAirstrike)
    PY_METHOD_DECLARE(py_stopAll)
    PY_FACTORY_DECLARE()
};
PY_TYPEOBJECT(PyGroundEffectManager)
PY_BEGIN_METHODS(PyGroundEffectManager)
    PY_METHOD(loadArtillery)
    PY_METHOD(loadAirstrike)
    PY_METHOD(playAirstrike)
    PY_METHOD(stopAll)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(PyGroundEffectManager)
PY_END_ATTRIBUTES()
PY_FACTORY_NAMED(PyGroundEffectManager,"PyGroundEffectManager",BigWorld)
PyObject* PyGroundEffectManager::pyNew(PyObject* args)
{
    if (!PyArg_ParseTuple(args,":PyGroundEffectManager")) return NULL;
    return new PyGroundEffectManager;
}
PyObject* PyGroundEffectManager::py_loadArtillery(PyObject* args)
{
    PyObject* object;
    if (!PyArg_ParseTuple(args,"O:loadArtillery",&object)) return NULL;
    DataSectionPtr section=sectionArgument(object);
    if (!section) return NULL;
    Manager* owner=currentManager();if (!owner) return NULL;
    auto value=std::make_unique<Artillery>();std::string error;
    if (!value->load(section,error)) { PyErr_SetString(PyExc_ValueError,error.c_str());return NULL; }
    const size_t index=owner->artillery.size();owner->artillery.push_back(std::move(value));
    return PyInt_FromSize_t(index);
}
PyObject* PyGroundEffectManager::py_loadAirstrike(PyObject* args)
{
    PyObject* object;
    if (!PyArg_ParseTuple(args,"O:loadAirstrike",&object)) return NULL;
    DataSectionPtr section=sectionArgument(object);
    if (!section) return NULL;
    Manager* owner=currentManager();if (!owner) return NULL;
    const size_t index=owner->airstrikes.size();owner->airstrikes.push_back(section);
    return PyInt_FromSize_t(index);
}
PyObject* PyGroundEffectManager::py_playAirstrike(PyObject* args)
{
    PyObject *objects[7];
    if (!PyArg_ParseTuple(args,"OOOOOOO:playAirstrike",&objects[0],&objects[1],&objects[2],&objects[3],&objects[4],&objects[5],&objects[6])) return NULL;
    int index,across,along;Vector3 position,velocity;float width,length;
    if (Script::setData(objects[0],index,"() argument 1")||Script::setData(objects[1],position,"() argument 2")||
        Script::setData(objects[2],velocity,"() argument 3")||Script::setData(objects[3],width,"() argument 4")||
        Script::setData(objects[4],length,"() argument 5")||Script::setData(objects[5],across,"() argument 6")||
        Script::setData(objects[6],along,"() argument 7")) return NULL;
    Manager* owner=currentManager();if (!owner) return NULL;
    if (index<0||static_cast<size_t>(index)>=owner->airstrikes.size())
    { PyErr_SetString(PyExc_IndexError,"Airstrike definition index is out of range");return NULL; }
    auto strike=std::make_unique<Strike>();std::string error;
    try
    {
        if (!strike->load(owner->airstrikes[index],error)||!strike->launch(position,velocity,width,length,across,along,error))
        { PyErr_SetString(PyExc_RuntimeError,error.c_str());return NULL; }
    }
    catch(const std::exception& exception) { PyErr_SetString(PyExc_ValueError,exception.what());return NULL; }
    owner->active.push_back(std::move(strike));Py_RETURN_NONE;
}
PyObject* PyGroundEffectManager::py_stopAll(PyObject* args)
{
    if (!PyArg_ParseTuple(args,":stopAll")) return NULL;
    if (manager) manager->stopAll();Py_RETURN_NONE;
}
bool initialiseStockGroundEffects() { if (!manager) manager.reset(new Manager);return true; }
void tickStockGroundEffects(float elapsed)
{
    if (!manager) return;
    if (!std::isfinite(elapsed)||elapsed<0.f) { ERROR_MSG("Ground effect: invalid frame delta\n");return; }
    auto& active=manager->active;
    for (auto it=active.begin();it!=active.end();)
    {
        if (!(*it)->alive()) { it=active.erase(it);continue; }
        std::string error;
        if (!(*it)->tick(elapsed,error)) ERROR_MSG("Ground effect: %s\n",error.c_str());
        ++it;
    }
}
void finiStockGroundEffects() { manager.reset(); }
BW_END_NAMESPACE
