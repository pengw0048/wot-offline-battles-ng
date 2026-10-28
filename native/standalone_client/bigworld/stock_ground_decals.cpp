#include "pch.hpp"
#include "stock_ground_decals.hpp"
#include "stock_dynamic_decals.hpp"
#include "stock_static_model_scene.hpp"
#include "compiled_space/terrain2_scene_provider.hpp"
#include "math/convex_hull.hpp"
#include "math/planeeq.hpp"
#include "math/vector2.hpp"
#include "moo/dynamic_decal.hpp"
#include "moo/renderer.hpp"
#include "physics2/worldtri.hpp"
#include "pyscript/script.hpp"
#include "scene/intersect_scene_view.hpp"
#include "scene/intersection_set.hpp"
#include "scene/scene.hpp"
#include "scene/scene_intersect_context.hpp"
#include "space/client_space.hpp"
#include <cmath>

BW_BEGIN_NAMESPACE
int StockGroundDecals_token=1;
namespace
{
using MathPoint=wot::GroundDecalMath::Point;
MathPoint point(const Vector3& value) {return {{value.x,value.y,value.z}};}
}

bool queryStockGroundDecalProjection(const SmartPointer<ClientSpace>& space,
    const Vector3& start,const Vector3& end,const Vector2& size,const WorldTriangle& nearest,
    wot::GroundDecalMath::Projection& projection,bool& admitted,std::string& error)
{
    admitted=false;
    if(!space || space->cleared()) {error="The ground decal camera space is unavailable";return false;}
    // 931d99 prepares the collider with the original ray bounds, independently
    // of the later contact box. Expanding this broadphase changes its objects.
    BoundingBox bounds;bounds.addBounds(start);bounds.addBounds(end);
    const Vector3& low=bounds.minBounds();const Vector3& high=bounds.maxBounds();
    PlaneEq planes[]={PlaneEq(Vector3(-1,0,0),-low.x),PlaneEq(Vector3(1,0,0),high.x),
        PlaneEq(Vector3(0,-1,0),-low.y),PlaneEq(Vector3(0,1,0),high.y),
        PlaneEq(Vector3(0,0,-1),-low.z),PlaneEq(Vector3(0,0,1),high.z)};
    ConvexHull hull(6,planes);SceneIntersectContext context(space->scene());IntersectionSet objects;
    context.includeStaticObjects(true);context.includeDynamicObjects(false);
    context.stockDecalCollisionOnly(true);
    auto* view=space->scene().getView<IntersectSceneView>();
    if(!view) {error="The ground decal scene intersection owner is unavailable";return false;}
    view->intersect(context,hull,objects);
    // 93200b sums all three nearest-triangle vertices before multiplying 1/3.
    const Vector3 center=(nearest.v0()+nearest.v1()+nearest.v2())*.3333333432674408f;
    const float radius=(size.y>size.x?size.y:size.x)+.5f;
    const Vector3 first(radius,0,0),second(0,radius,0),third(0,0,radius);
    std::vector<wot::GroundDecalMath::SurfaceContact> contacts;
    bool providerResult=false;
    // f5e720 dispatches ascending SceneObject type buckets. Preserve that
    // order: contact accumulation is floating point and must not be regrouped.
    for(IntersectionSet::SceneObjectType type=0;type<objects.maxType();++type)
        for(const auto& object:objects.objectsOfType(type))
        {
            BW::vector<WorldTriangle> triangles;
            const bool isStatic=CompiledSpace::StockStaticModelSceneLoader::collectStockDecalTriangles(
                object,center,first,second,third,triangles);
            const bool known=isStatic || CompiledSpace::Terrain2SceneProvider::collectStockDecalTriangles(
                object,center,first,second,third,triangles);
            if(!known)continue;
            for(const auto& triangle:triangles)
            {
                const wot::GroundDecalMath::Triangle source={{point(triangle.v0()),point(triangle.v1()),point(triangle.v2())}};
                const auto generated=wot::GroundDecalMath::contacts(source,point(center),radius);
                if(isStatic && !generated.empty())providerResult=true;
                for(const auto& value:generated)contacts.push_back({value.point,value.normal,triangle.flags()});
            }
            if(space->cleared()) {error="The ground decal space retired during geometry collection";return false;}
        }
    if(!providerResult) {error.clear();return true;}
    const Vector3 ray=end-start;D3DXVECTOR3 direction(ray.x,ray.y,ray.z);
    D3DXVec3Normalize(&direction,&direction);
    if(!wot::GroundDecalMath::project(contacts,point(start),{{direction.x,direction.y,direction.z}},projection,[](const MathPoint& value) {
        D3DXVECTOR3 input(value[0],value[1],value[2]),output;D3DXVec3Normalize(&output,&input);
        return MathPoint{{output.x,output.y,output.z}};
    }))
    {error.clear();return true;}
    if(!std::isfinite(projection.axis[0]) || !std::isfinite(projection.axis[1]) || !std::isfinite(projection.axis[2]) ||
        !std::isfinite(projection.distance) || !std::isfinite(projection.depth))
    {error="The original ground decal contacts produced a nonfinite projection";return false;}
    admitted=true;error.clear();return true;
}

PyObject* wg_addDecal(const BW::string& group,const Vector3& start,const Vector3& end,const Vector2& size,
    float yaw,int diffuse,int normal,int specular)
{
    // 93383a tests length==0 before checking the active pipeline.
    if(std::sqrt(size.x*size.x+size.y*size.y)==0.f)
    {PyErr_Format(PyExc_RuntimeError,"addDecal - decal size is zero.\n");return NULL;}
    Renderer* renderer=Renderer::pInstance();
    if(!renderer || renderer->pipelineType()!=IRendererPipeline::TYPE_DEFERRED_SHADING)Py_RETURN_NONE;
    auto* owner=DynamicDecalManager::pInstance();
    if(!owner) {ERROR_MSG("StockGroundDecals: The native dynamic decal registry is unavailable\n");Py_RETURN_NONE;}
    std::string error;
    if(!addStockDynamicDecal(stockDynamicDecalGroupLifetime(group),start,end,size,yaw,
        owner->textureName(diffuse),owner->textureName(normal),owner->textureName(specular),false,error))
        ERROR_MSG("StockGroundDecals: %s\n",error.c_str());
    Py_RETURN_NONE;
}
// RETVOID returns None even with PyErr set. Return NULL for the size error
// so CPython preserves the original RuntimeError instead of raising SystemError.
PY_AUTO_MODULE_FUNCTION(RETOWN,wg_addDecal,ARG(BW::string,ARG(Vector3,ARG(Vector3,ARG(Vector2,
    ARG(float,ARG(int,ARG(int,ARG(int,END)))))))),BigWorld)
BW_END_NAMESPACE
