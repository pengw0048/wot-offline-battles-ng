#include "pch.hpp"
#include "stock_local_lighting.hpp"
#include "wot/stock_local_lighting.hpp"
#include "stock_deferred_buffers.hpp"
#include "stock_dynamic_resolution.hpp"
#include "moo/render_context.hpp"
#include "moo/effect_visual_context.hpp"
#include "moo/effect_constant_value.hpp"
#include "moo/effect_material.hpp"
#include "moo/moo_utilities.hpp"
#include "moo/visual.hpp"
#include "moo/visual_manager.hpp"
#include "moo/vertices.hpp"
#include "moo/primitive.hpp"
#include "moo/vertex_declaration.hpp"
#include "moo/vertex_streams.hpp"
#include "moo/vertex_buffer.hpp"
#include "moo/render_target.hpp"
#include "moo/draw_context.hpp"
#include "moo/fullscreen_quad.hpp"
#include "scene/scene.hpp"
#include "scene/scene_draw_context.hpp"
#include "scene/scene_intersect_context.hpp"
#include "scene/intersect_scene_view.hpp"
#include "scene/intersection_set.hpp"
#include "scene/draw_helpers.hpp"
#include "space/deprecated_space_helpers.hpp"
#include "space/client_space.hpp"
#include "math/convex_hull.hpp"
#include "math/planeeq.hpp"
#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <vector>

BW_BEGIN_NAMESPACE
namespace
{
thread_local const Moo::DrawContext * activeLocalShadowDraw = NULL;

class LocalShadowDrawScope
{
public:
    explicit LocalShadowDrawScope( const Moo::DrawContext & context ) :
        previous_( activeLocalShadowDraw ) { activeLocalShadowDraw = &context; }
    ~LocalShadowDrawScope() { activeLocalShadowDraw = previous_; }
private:
    const Moo::DrawContext * previous_;
    LocalShadowDrawScope( const LocalShadowDrawScope & ) = delete;
    LocalShadowDrawScope & operator=( const LocalShadowDrawScope & ) = delete;
};

using LocalMath = wot::StockLocalLightMath;
LocalMath::Vector values(const Vector3& value) { return {{value.x, value.y, value.z}}; }
LocalMath::Four values(const Moo::Colour& value) { return {{value.r, value.g, value.b, value.a}}; }
LocalMath::Bounds values(const BoundingBox& box) { return {values(box.minBounds()), values(box.maxBounds())}; }

struct ShadowParameters
{
    int algorithm = 0;
    Vector4 parameters = Vector4::zero();
    int colourMask = 15;
};
ShadowParameters shadowParameters;

bool detachTexture(DX::BaseTexture* texture)
{
    auto* device = Moo::rc().device();
    if (!texture || !device) return true;
    bool success = true;
    for (DWORD stage = 0; stage < 20; ++stage)
    {
        const DWORD slot = stage < 16 ? stage : D3DVERTEXTEXTURESAMPLER0 + stage - 16;
        ComObjectWrap<DX::BaseTexture> bound;
        if (FAILED(device->GetTexture(slot, &bound))) success = false;
        else if (bound.pComObject() == texture)
            success = SUCCEEDED(device->SetTexture(slot, NULL)) && success;
    }
    // ARIE submits textures through the device. Invalidate the public cache
    // after detaching so a subsequent legacy draw cannot skip its rebind.
    Moo::rc().invalidateStateCache();
    return success;
}

class ShadowConstant : public Moo::EffectConstantValue
{
public:
    explicit ShadowConstant(unsigned field) : field_(field) {}
    virtual bool operator()(ID3DXEffect* effect, D3DXHANDLE handle)
    {
        if (field_ == 1) return SUCCEEDED(effect->SetVector(handle, &shadowParameters.parameters));
        return SUCCEEDED(effect->SetInt(handle, field_ == 0 ? shadowParameters.algorithm : shadowParameters.colourMask));
    }
private:
    unsigned field_;
};

class CameraScope
{
public:
    CameraScope() : camera_(Moo::rc().camera()), view_(Moo::rc().view()), projection_(Moo::rc().projection()),
        parameters_(shadowParameters) {}
    ~CameraScope()
    {
        Moo::rc().camera(camera_);
        Moo::rc().view(view_);
        Moo::rc().projection(projection_);
        Moo::rc().updateViewTransforms();
        shadowParameters = parameters_;
        Moo::rc().effectVisualContext().updateSharedConstants(Moo::CONSTANTS_ALL);
    }
private:
    Moo::Camera camera_;
    Matrix view_, projection_;
    ShadowParameters parameters_;
};

class TargetScope
{
public:
    TargetScope() : saved_(Moo::rc().pushRenderTarget()) {}
    ~TargetScope()
    {
        if (!saved_) return;
        Moo::rc().popRenderTarget();
        Moo::rc().effectVisualContext().updateSharedConstants(Moo::CONSTANTS_PER_SCREEN);
    }
    bool bind(DX::Surface* surface, DX::Surface* depth, uint width, uint height)
    {
        if (!saved_ || !surface) return false;
        ComObjectWrap<DX::BaseTexture> targetTexture;
        if (SUCCEEDED(surface->GetContainer(IID_IDirect3DBaseTexture9, reinterpret_cast<void**>(&targetTexture))))
        {
            if (!detachTexture(targetTexture.pComObject())) return false;
        }
        if (FAILED(Moo::rc().device()->SetDepthStencilSurface(depth)) ||
            FAILED(Moo::rc().setRenderTarget(0, surface))) return false;
        for (unsigned target = 1; target < 4; ++target)
            if (FAILED(Moo::rc().setRenderTarget(target, NULL))) return false;
        DX::Viewport viewport = {0, 0, width, height, 0.f, 1.f};
        // Local shadows are separate allocations, with no scene DRR rectangle.
        if (FAILED(setStockSceneViewport(&viewport, false))) return false;
        Moo::rc().screenWidth(width); Moo::rc().screenHeight(height);
        Moo::rc().effectVisualContext().updateSharedConstants(Moo::CONSTANTS_PER_SCREEN);
        return true;
    }
private:
    bool saved_;
};

bool select(const Moo::EffectMaterialPtr& material, const char* technique)
{
    return material && material->setTechnique(technique) && material->properties().select(*material->pEffect());
}
bool optionalBool(ID3DXEffect* effect, const char* name, bool value)
{
    const D3DXHANDLE handle = effect->GetParameterByName(NULL, name);
    return !handle || SUCCEEDED(effect->SetBool(handle, value));
}
void clearTexture(const Moo::EffectMaterialPtr& material, const char* name)
{
    if (!material) return;
    ID3DXEffect* binding = material->properties().existingStockBinding();
    if (binding) binding->SetTexture(name, NULL);
}

bool drawInstances(const Moo::VisualPtr& geometry, const Moo::VertexBuffer& instances, uint count)
{
    // #1513's generic instancing format is a single instance id. Local lights
    // use light_instancing: four FLOAT4 rows at TEXCOORD4..7, stream 9.
    Moo::VertexDeclaration* extra = Moo::VertexDeclaration::get("light_instancing");
    if (!extra) return false;
    auto* device = Moo::rc().device();
    const uint stream = Moo::InstancingStream::STREAM_NUMBER;
    bool success = SUCCEEDED(device->SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA | count)) &&
        SUCCEEDED(device->SetStreamSourceFreq(stream, D3DSTREAMSOURCE_INSTANCEDATA | 1)) &&
        SUCCEEDED(instances.set(stream, 0, sizeof(LocalMath::Packet)));
    for (const auto& renderSet : geometry->renderSets())
    {
        if (!success) break;
        for (const auto& item : renderSet.geometry_)
        {
            if (!item.vertices_ || !item.primitives_ || FAILED(item.vertices_->setVertices(false)) ||
                FAILED(item.primitives_->setPrimitives())) { success = false; break; }
            const Moo::VertexDeclaration* base = item.vertices_->pDecl();
            // combine reads both declarations; its public signature predates
            // const accessors on Vertices.
            Moo::VertexDeclaration* declaration = base ? Moo::VertexDeclaration::combine(
                const_cast<Moo::VertexDeclaration*>(base), extra) : NULL;
            if (!declaration || FAILED(Moo::rc().setVertexDeclaration(declaration->declaration())))
            { success = false; break; }
            for (const auto& group : item.primitiveGroups_)
                if (FAILED(item.primitives_->drawInstancedPrimitiveGroup(group.groupIndex_, count)))
                { success = false; break; }
            if (!success) break;
        }
    }
    success = SUCCEEDED(device->SetStreamSourceFreq(0, 1)) && success;
    success = SUCCEEDED(device->SetStreamSourceFreq(stream, 1)) && success;
    success = SUCCEEDED(Moo::VertexBuffer::reset(stream)) && success;
    return success;
}

void spotMatrices(const Moo::SpotLight& light, Matrix& view, Matrix& projection)
{
    // 97bde0 uses world radius for the raster camera and fixed near=.1.
    view.lookAt(light.worldPosition(), light.worldDirection(), Vector3(0.f, 1.f, 0.f));
    projection.perspectiveProjection(acosf(light.cosConeAngle()) * 2.f, 1.f, .1f, light.worldOuterRadius());
}
Vector4 spotOrientation(const Vector3& direction)
{
    // 97bcf0 deliberately chooses X-up only for this narrow Y interval.
    Vector3 up(0.f, 1.f, 0.f);
    if (direction.y > -.0004f && direction.y < .0004f) up.set(1.f, 0.f, 0.f);
    Matrix view, inverse;
    view.lookAt(Vector3::zero(), direction, up);
    inverse.invert(view);
    D3DXQUATERNION result;
    D3DXQuaternionRotationMatrix(&result, &inverse);
    return Vector4(result.x, result.y, result.z, result.w);
}
BoundingBox spotBounds(const Moo::SpotLight& light)
{
    // 97bde0 constructs eight near/far cap corners with this exact basis.
    const Vector3& direction = light.worldDirection();
    Vector3 right(-direction.z, 0.f, direction.x);
    right.normalise();
    Vector3 vertical;
    vertical.crossProduct(right, direction);
    vertical.normalise();
    const float tangent = tanf(acosf(light.cosConeAngle()));
    BoundingBox result = BoundingBox::s_insideOut_;
    for (float distance : {.1f, light.worldOuterRadius()})
    {
        const Vector3 centre = light.worldPosition() + direction * distance;
        const Vector3 horizontal = right * (tangent * distance);
        const Vector3 up = vertical * (tangent * distance);
        result.addBounds(centre - up - horizontal);
        result.addBounds(centre + up - horizontal);
        result.addBounds(centre + up + horizontal);
        result.addBounds(centre - up + horizontal);
    }
    return result;
}

bool visible(BoundingBox bounds)
{
    if (bounds.insideOut()) return false;
    bounds.calculateOutcode(Moo::rc().viewProjection());
    return !bounds.combinedOutcode();
}

void cubeView(unsigned face, const Vector3& position, Matrix& view)
{
    Matrix world = Matrix::identity;
    if (face == 0) world.setRotateY(MATH_PI * .5f);
    else if (face == 1) world.setRotateY(-MATH_PI * .5f);
    else if (face == 2) world.setRotateX(-MATH_PI * .5f);
    else if (face == 3) world.setRotateX(MATH_PI * .5f);
    else if (face == 5) world.setRotateY(MATH_PI);
    world.translation(position);
    view.invert(world);
}

void drawShadowObjects(Scene& scene, Moo::DrawContext& draw, const IntersectionSet& objects)
{
    const LocalShadowDrawScope localShadow( draw );
    draw.begin(Moo::DrawContext::OPAQUE_CHANNEL_MASK);
    SceneDrawContext context(scene, draw);
    DrawHelpers::drawSceneVisibilitySet(context, scene, objects);
    draw.end(Moo::DrawContext::OPAQUE_CHANNEL_MASK);
    draw.flush(Moo::DrawContext::OPAQUE_CHANNEL_MASK);
}
}

bool stockLocalLightingShadowContext( const Moo::DrawContext & context )
{
    return activeLocalShadowDraw == &context;
}

struct StockLocalLighting::State
{
    struct Omni { Moo::OmniLightPtr light; LocalMath::Packet data; LocalMath::Group group; int shadow = -1; };
    struct Spot { Moo::SpotLightPtr light; LocalMath::Packet data; LocalMath::Group group; Matrix view, projection; int shadow = -1; };
    std::vector<Moo::OmniLightPtr> omnis;
    std::vector<Moo::SpotLightPtr> spots;
    std::vector<Omni> preparedOmnis;
    std::vector<Spot> preparedSpots;
    Moo::EffectMaterialPtr omniMaterial, spotMaterial, blurMaterial;
    Moo::VisualPtr sphere, cone;
    Moo::RenderTargetPtr spotTargets[3], spotScratch;
    ComObjectWrap<DX::CubeTexture> cube;
    ComObjectWrap<DX::Surface> cubeDepth;
    Moo::VertexBuffer instances;
    bool constants = false, shadows = false;
    bool prepared = false;

    bool drawShadow(const Moo::OmniLightPtr& light);
    bool drawShadow(Spot& light, unsigned index);
    bool blur(const Moo::RenderTargetPtr& source);
    bool drawVolumes(bool spot);
    bool drawOne(const Moo::EffectMaterialPtr& material, const Moo::VisualPtr& geometry,
        LocalMath::Group group, const LocalMath::Packet* data, size_t count, int shadow, const Matrix* matrix);
};

bool StockLocalLighting::State::drawShadow(const Moo::OmniLightPtr& light)
{
    ClientSpacePtr space = DeprecatedSpaceHelpers::cameraSpace();
    if (!space || !cube || !cubeDepth) return false;
    const BoundingBox& bounds = light->worldBounds();
    PlaneEq planes[] = {
        PlaneEq(Vector3(-1, 0, 0), -bounds.minBounds().x), PlaneEq(Vector3(1, 0, 0), bounds.maxBounds().x),
        PlaneEq(Vector3(0, -1, 0), -bounds.minBounds().y), PlaneEq(Vector3(0, 1, 0), bounds.maxBounds().y),
        PlaneEq(Vector3(0, 0, -1), -bounds.minBounds().z), PlaneEq(Vector3(0, 0, 1), bounds.maxBounds().z)};
    ConvexHull hull(6, planes);
    SceneIntersectContext query(space->scene());
    IntersectionSet objects;
    space->scene().getView<IntersectSceneView>()->intersect(query, hull, objects);
    CameraScope camera;
    shadowParameters.algorithm = 1;
    shadowParameters.parameters.set(light->outerRadius(), 0.f, 0.f, 0.f);
    Moo::rc().camera(Moo::Camera(.001f, light->outerRadius(), MATH_PI * .5f, 1.f));
    Matrix projection;
    projection.perspectiveProjection(MATH_PI * .5f, 1.f, .001f, light->outerRadius());
    Moo::rc().projection(projection);
    Moo::DrawContext draw(Moo::RENDERING_PASS_SHADOWS);
    for (unsigned face = 0; face < 6; ++face)
    {
        ComObjectWrap<DX::Surface> surface;
        if (FAILED(cube->GetCubeMapSurface(static_cast<D3DCUBEMAP_FACES>(face), 0, &surface))) return false;
        TargetScope target;
        if (!target.bind(surface.pComObject(), cubeDepth.pComObject(), 128, 128) ||
            FAILED(Moo::rc().device()->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                0xffffffffu, 1.f, 0))) return false;
        Matrix view;
        cubeView(face, light->worldPosition(), view);
        Moo::rc().view(view);
        Moo::rc().updateViewTransforms();
        Moo::rc().effectVisualContext().updateSharedConstants(Moo::CONSTANTS_ALL);
        drawShadowObjects(space->scene(), draw, objects);
    }
    return true;
}

bool StockLocalLighting::State::drawShadow(Spot& light, unsigned index)
{
    ClientSpacePtr space = DeprecatedSpaceHelpers::cameraSpace();
    if (!space || index >= 3 || !spotTargets[index]) return false;
    Matrix viewProjection;
    viewProjection.multiply(light.view, light.projection);
    SceneIntersectContext query(space->scene());
    IntersectionSet objects;
    DrawHelpers::cull(query, space->scene(), viewProjection, objects);
    {
        CameraScope camera;
        shadowParameters.algorithm = 1;
        shadowParameters.parameters.set(light.light->outerRadius(), 0.f, 0.f, 0.f);
        Moo::rc().camera(Moo::Camera(.1f, light.light->worldOuterRadius(),
            acosf(light.light->cosConeAngle()) * 2.f, 1.f));
        Moo::rc().view(light.view);
        Moo::rc().projection(light.projection);
        Moo::rc().updateViewTransforms();
        ComObjectWrap<DX::Surface> surface;
        if (FAILED(spotTargets[index]->pSurface(surface))) return false;
        TargetScope target;
        if (!target.bind(surface.pComObject(), spotTargets[index]->depthBuffer(), 256, 256) ||
            FAILED(Moo::rc().device()->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                0xffffffffu, 1.f, 0))) return false;
        Moo::rc().effectVisualContext().updateSharedConstants(Moo::CONSTANTS_ALL);
        Moo::DrawContext draw(Moo::RENDERING_PASS_SHADOWS);
        drawShadowObjects(space->scene(), draw, objects);
    }
    return blur(spotTargets[index]);
}

bool StockLocalLighting::State::blur(const Moo::RenderTargetPtr& source)
{
    // a69610 -> 9d62e0: Gaussian, scale=1, depth mode=2, no bilateral/mask.
    const Moo::RenderTargetPtr inputs[] = {source, spotScratch};
    const Moo::RenderTargetPtr outputs[] = {spotScratch, source};
    const char* techniques[] = {"HORIZONTAL_PASS", "VERTICAL_PASS"};
    const Vector4 screen(256.f, 256.f, 1.f / 256.f, 1.f / 256.f);
    const Vector4 coordinates(1.f, 1.f, 0.f, 0.f);
    for (unsigned pass = 0; pass < 2; ++pass)
    {
        ComObjectWrap<DX::Surface> surface;
        if (!outputs[pass] || FAILED(outputs[pass]->pSurface(surface))) return false;
        TargetScope target;
        if (!target.bind(surface.pComObject(), NULL, 256, 256) || !select(blurMaterial, techniques[pass])) return false;
        ID3DXEffect* effect = blurMaterial->pEffect()->pEffect();
        const bool success = !(FAILED(effect->SetInt("g_depthMode", 2)) || FAILED(effect->SetBool("g_useMaskRT", false)) ||
            FAILED(effect->SetBool("g_useBilateralFilter", false)) ||
            FAILED(effect->SetBool("g_bilateralFilterLowQuality", false)) ||
            FAILED(effect->SetBool("g_bilateralFilterConsiderSurfacePlane", false)) ||
            FAILED(effect->SetBool("g_processOnlyOpaque", false)) ||
            FAILED(effect->SetFloat("g_blurMip", 0.f)) ||
            FAILED(effect->SetVector("g_blurScreen", &screen)) ||
            FAILED(effect->SetVector("g_srcMapDRRParams", &coordinates)) ||
            FAILED(effect->SetTexture("g_srcMap", inputs[pass]->pTexture())) ||
            !Moo::rc().fsQuad().drawChecked(*blurMaterial, 256, 256, true));
        clearTexture(blurMaterial, "g_srcMap");
        if (!success) return false;
    }
    return true;
}

StockLocalLighting::StockLocalLighting() : state_(new State) {}
StockLocalLighting::~StockLocalLighting() { deleteUnmanagedObjects(); deleteManagedObjects(); }
bool StockLocalLighting::initialise()
{
    createManagedObjects();
    createUnmanagedObjects();
    return state_->omniMaterial && state_->spotMaterial && state_->blurMaterial &&
        state_->sphere && state_->sphere->isOK() && state_->cone && state_->cone->isOK() &&
        state_->instances.valid() && (!state_->shadows || (state_->cube && state_->cubeDepth &&
            state_->spotTargets[0] && state_->spotTargets[1] && state_->spotTargets[2] && state_->spotScratch));
}
void StockLocalLighting::tick()
{
    state_->omnis.clear(); state_->spots.clear();
    state_->preparedOmnis.clear(); state_->preparedSpots.clear(); state_->prepared = false;
}
void StockLocalLighting::gatherVisibleLights(const Moo::LightContainerPtr& lights)
{
    if (!lights) return;
    for (const auto& light : lights->omnis())
        if (light && std::find(state_->omnis.begin(), state_->omnis.end(), light) == state_->omnis.end() &&
            visible(light->worldBounds())) state_->omnis.push_back(light);
    for (const auto& light : lights->spots())
        if (light && std::find(state_->spots.begin(), state_->spots.end(), light) == state_->spots.end() &&
            visible(spotBounds(*light))) state_->spots.push_back(light);
}
void StockLocalLighting::createManagedObjects()
{
    if (!state_->constants)
    {
        const char* names[] = {"ShadowAlgo", "ShadowAlgoParams", "ShadowColorWriteMask"};
        for (unsigned index = 0; index < 3; ++index)
            Moo::rc().effectVisualContext().registerAutoConstant(Moo::EffectVisualContext::AUTO_CONSTANT_TYPE_PER_VIEW,
                names[index], new ShadowConstant(index));
        state_->constants = true;
    }
    if (!Moo::createEffect(state_->omniMaterial, "shaders/std_effects/resolve_omni_lighting.fx")) state_->omniMaterial = NULL;
    if (!Moo::createEffect(state_->spotMaterial, "shaders/std_effects/resolve_spot_lighting.fx")) state_->spotMaterial = NULL;
    if (!Moo::createEffect(state_->blurMaterial, "shaders/auxiliary/bilateral_blur.fx")) state_->blurMaterial = NULL;
    state_->sphere = Moo::VisualManager::instance()->get("system/models/fx_unit_sphere.visual");
    state_->cone = Moo::VisualManager::instance()->get("system/models/fx_unit_cone.visual");
}
void StockLocalLighting::deleteManagedObjects()
{
    clearTexture(state_->omniMaterial, "g_omniShadowMap");
    clearTexture(state_->spotMaterial, "g_spotShadowMap");
    clearTexture(state_->blurMaterial, "g_srcMap");
    if (state_->constants)
    {
        for (const char* name : {"ShadowAlgo", "ShadowAlgoParams", "ShadowColorWriteMask"})
            Moo::rc().effectVisualContext().unregisterAutoConstant(Moo::EffectVisualContext::AUTO_CONSTANT_TYPE_PER_VIEW, name);
        state_->constants = false;
    }
    state_->omniMaterial = NULL; state_->spotMaterial = NULL; state_->blurMaterial = NULL;
    state_->sphere = NULL; state_->cone = NULL;
}
void StockLocalLighting::createUnmanagedObjects()
{
    deleteUnmanagedObjects();
    auto* device = Moo::rc().device();
    if (!device) return;
    state_->instances.create(64 * 1000, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, "stock/local-light-instances");
    const StockDeferredBuffers* buffers = stockDeferredBuffers();
    state_->shadows = buffers && buffers->shadowQuality() < 2;
    if (!state_->shadows) return;
    for (unsigned index = 0; index < 3; ++index)
    {
        state_->spotTargets[index] = new Moo::RenderTarget("stock/spot-shadow");
        if (!state_->spotTargets[index]->create(256, 256, false, D3DFMT_G32R32F, NULL, D3DFMT_D24S8, false))
            state_->spotTargets[index] = NULL;
    }
    state_->spotScratch = new Moo::RenderTarget("stock/spot-shadow-scratch");
    if (!state_->spotScratch->create(256, 256, false, D3DFMT_G32R32F)) state_->spotScratch = NULL;
    device->CreateCubeTexture(128, 1, D3DUSAGE_RENDERTARGET, D3DFMT_G16R16F, D3DPOOL_DEFAULT, &state_->cube, NULL);
    device->CreateDepthStencilSurface(128, 128, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &state_->cubeDepth, NULL);
}
void StockLocalLighting::deleteUnmanagedObjects()
{
    clearTexture(state_->omniMaterial, "g_omniShadowMap");
    clearTexture(state_->spotMaterial, "g_spotShadowMap");
    clearTexture(state_->blurMaterial, "g_srcMap");
    state_->preparedOmnis.clear(); state_->preparedSpots.clear(); state_->prepared = false;
    state_->instances.release();
    bool detached = detachTexture(state_->cube.pComObject());
    for (auto& target : state_->spotTargets)
    {
        if (target) detached = detachTexture(target->pTexture()) && detached;
        target = NULL;
    }
    if (state_->spotScratch) detached = detachTexture(state_->spotScratch->pTexture()) && detached;
    if (!detached) ERROR_MSG("Local lighting: owned shadow texture could not be detached\n");
    state_->spotScratch = NULL; state_->cube = NULL; state_->cubeDepth = NULL;
}

bool StockLocalLighting::prepare()
{
    state_->prepared = false;
    state_->preparedOmnis.clear(); state_->preparedSpots.clear();
    const StockDeferredBuffers* buffers = stockDeferredBuffers();
    if (!buffers || !buffers->prepared()) return false;
    if (state_->shadows != (buffers->shadowQuality() < 2)) createUnmanagedObjects();
    if (!state_->instances.valid() || !state_->omniMaterial || !state_->spotMaterial) return false;
    if (state_->shadows && (!state_->cube || !state_->cubeDepth || !state_->spotTargets[0] ||
        !state_->spotTargets[1] || !state_->spotTargets[2] || !state_->spotScratch)) return false;
    const Vector3 camera = Moo::rc().invView().applyToOrigin();
    const float nearDistance = Moo::rc().camera().nearPlane() / cosf(Moo::rc().camera().fov() * .5f);
    bool usedCube = false;
    unsigned usedSpots = 0;
    for (const auto& light : state_->omnis)
    {
        State::Omni output;
        output.light = light;
        output.data = LocalMath::omni(values(light->worldPosition()), values(light->colour()),
            light->multiplier(), light->innerRadius(), light->outerRadius(), values(camera));
        output.group = LocalMath::omniGroup(values(light->worldPosition()), light->outerRadius(), values(camera),
            nearDistance, light->stockLocalInstancing(), light->stockSourceFlags() != 0, state_->shadows, false);
        if (state_->shadows && light->stockSourceFlags() && !usedCube)
        {
            if (!state_->drawShadow(light)) return false;
            output.shadow = 0;
            usedCube = true;
        }
        state_->preparedOmnis.push_back(output);
    }
    for (const auto& light : state_->spots)
    {
        State::Spot output;
        output.light = light;
        const float cosine = light->cosConeAngle(), angle = acosf(cosine);
        const Vector4 orientation = spotOrientation(light->worldDirection());
        output.data = LocalMath::spot(values(light->worldPosition()), values(light->colour()), light->multiplier(),
            light->innerRadius(), light->outerRadius(), cosine, tanf(angle),
            {{orientation.x, orientation.y, orientation.z, orientation.w}});
        output.group = LocalMath::spotGroup(values(light->worldPosition()), values(light->worldDirection()),
            light->outerRadius(), cosine, sinf(angle), values(spotBounds(*light)), values(camera), nearDistance,
            light->stockLocalInstancing(), light->stockSourceFlags() != 0, state_->shadows, false);
        spotMatrices(*light, output.view, output.projection);
        if (state_->shadows && light->stockSourceFlags() && usedSpots < 3)
        {
            if (!state_->drawShadow(output, usedSpots)) return false;
            output.shadow = static_cast<int>(usedSpots++);
        }
        state_->preparedSpots.push_back(output);
    }
    state_->prepared = true;
    return true;
}

bool StockLocalLighting::State::drawOne(const Moo::EffectMaterialPtr& material, const Moo::VisualPtr& geometry,
    LocalMath::Group group, const LocalMath::Packet* data, size_t count, int shadow, const Matrix* matrix)
{
    const bool spot = material == spotMaterial;
    const char* techniques[] = {spot ? "SPOT_INSTANCING" : "OMNI_INSTANCING",
        spot ? "SPOT" : "OMNI", spot ? "SPOT_FALLBACK" : "OMNI_FALLBACK"};
    if (!geometry || !geometry->isOK() || !data || !count || !select(material, techniques[group])) return false;
    if (group == LocalMath::Instanced)
    {
        if (count > static_cast<size_t>(UINT_MAX) / sizeof(LocalMath::Packet)) return false;
        const UINT bytes = static_cast<UINT>(count * sizeof(LocalMath::Packet));
        D3DVERTEXBUFFER_DESC description = {};
        if (FAILED(instances.getDesc(&description))) return false;
        if (description.Size < bytes && FAILED(instances.create(bytes, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
            0, D3DPOOL_DEFAULT, "stock/local-light-instances"))) return false;
        void* destination = NULL;
        if (FAILED(instances.lock(0, bytes, &destination, D3DLOCK_DISCARD))) return false;
        std::memcpy(destination, data, bytes);
        if (FAILED(instances.unlock())) return false;
        geometry->instancingStream(&instances);
    }
    const StockDeferredBuffers* buffers = stockDeferredBuffers();
    if (!buffers || !material->begin()) { geometry->instancingStream(NULL); return false; }
    bool success = true;
    for (uint32 pass = 0; success && pass < material->numPasses(); ++pass)
    {
        if (!material->beginPass(pass)) { success = false; break; }
        // beginPass can change active ARIE Properties bindings. Fetch after it.
        ID3DXEffect* effect = material->pEffect()->pEffect();
        success = SUCCEEDED(effect->SetBool("g_enableSSAO", buffers->ambientOcclusionEnabled())) &&
            optionalBool(effect, "g_enableSSR", false) && SUCCEEDED(effect->SetBool("g_showOverdraw", false)) &&
            SUCCEEDED(effect->SetFloat(spot ? "g_coneApproximationError" : "g_sphereApproximationError", spot ? .1f : .05f)) &&
            SUCCEEDED(effect->SetBool(spot ? "g_spotInstancing" : "g_omniInstancing", group == LocalMath::Instanced)) &&
            SUCCEEDED(effect->SetBool(spot ? "g_spotShadow" : "g_omniShadow", shadow >= 0));
        if (success && group != LocalMath::Instanced)
        {
            // PointLight's reflected size is 48, while its vertex stride is
            // 64. Do not overwrite the following approximation-error uniform.
            success = SUCCEEDED(effect->SetValue(spot ? "g_spotLight" : "g_omniLight", data, spot ? 64 : 48));
        }
        if (success && shadow >= 0)
        {
            success = SUCCEEDED(effect->SetTexture(spot ? "g_spotShadowMap" : "g_omniShadowMap",
                spot ? spotTargets[shadow]->pTexture() : cube.pComObject()));
            if (success && spot) success = matrix && SUCCEEDED(effect->SetMatrix("g_spotShadowMatrix", matrix));
        }
        if (success) success = SUCCEEDED(effect->CommitChanges());
        if (success)
        {
            if (group == LocalMath::Instanced) success = drawInstances(geometry, instances, static_cast<uint>(count));
            else geometry->justDrawPrimitives();
        }
        material->endPass();
    }
    material->end();
    geometry->instancingStream(NULL);
    clearTexture(material, spot ? "g_spotShadowMap" : "g_omniShadowMap");
    return success;
}

bool StockLocalLighting::State::drawVolumes(bool spot)
{
    if (!prepared) return false;
    // 964c50/965050 draw individual volumes, then instanced volumes, then
    // near-plane fallback volumes. Preserve candidate order within each group.
    for (LocalMath::Group group : {LocalMath::Volume, LocalMath::Instanced, LocalMath::Fallback})
    {
        std::vector<LocalMath::Packet> packets;
        if (spot)
        {
            for (const auto& light : preparedSpots)
            {
                if (light.group != group) continue;
                if (group == LocalMath::Instanced) packets.push_back(light.data);
                else
                {
                    Matrix matrix;
                    matrix.multiply(light.view, light.projection);
                    if (!drawOne(spotMaterial, cone, group, &light.data, 1, light.shadow, &matrix)) return false;
                }
            }
        }
        else
        {
            for (const auto& light : preparedOmnis)
            {
                if (light.group != group) continue;
                if (group == LocalMath::Instanced) packets.push_back(light.data);
                else if (!drawOne(omniMaterial, sphere, group, &light.data, 1, light.shadow, NULL)) return false;
            }
        }
        if (!packets.empty() && !drawOne(spot ? spotMaterial : omniMaterial, spot ? cone : sphere,
            group, packets.data(), packets.size(), -1, NULL)) return false;
    }
    return true;
}
bool StockLocalLighting::drawOmnis() { return state_->drawVolumes(false); }
bool StockLocalLighting::drawSpots() { return state_->drawVolumes(true); }
BW_END_NAMESPACE
