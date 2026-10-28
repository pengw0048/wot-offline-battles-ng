#pragma once
#include "cstdmf/main_loop_task.hpp"
#include "moo/device_callback.hpp"
#include "moo/draw_context.hpp"
#include "moo/effect_material.hpp"
#include "moo/effect_visual_context.hpp"
#include "moo/renderer.hpp"
#include "moo/fullscreen_quad.hpp"
#include "moo/primitive.hpp"
#include "moo/render_context.hpp"
#include "moo/texture_manager.hpp"
#include "moo/vertices.hpp"
#include "moo/visual_manager.hpp"
#include "space/client_space.hpp"
#include "space/deprecated_space_helpers.hpp"
#include <memory>
#include <vector>

BW_BEGIN_NAMESPACE
namespace StockSplodge
{
inline bool forward()
{
    return Renderer::pInstance() &&
        Renderer::instance().pipelineType() == IRendererPipeline::TYPE_FORWARD_SHADING;
}
inline Matrix adjustedWorld(const Matrix & world, float height)
{
    // 13f317e transforms (0,-height,0) as a normal, then offsets world origin.
    Matrix result = world;
    result.translation(world.applyToOrigin() + world.applyVector(Vector3(0.f, -height, 0.f)));
    return result;
}
struct Record
{
    Matrix world, local;
    std::weak_ptr<unsigned> generation;
    unsigned expected;
    ClientSpacePtr space;
};
class Renderer : public Moo::DeviceCallback
{
public:
    ~Renderer() override { deleteManagedObjects(); }
    std::vector<Record> queue;
    Moo::EffectMaterialPtr material;
    Moo::VisualPtr cube, plane;
    Moo::BaseTexturePtr texture;
    void deleteManagedObjects() override
    {
        queue.clear();
        if (material && material->pEffect() && material->pEffect()->pEffect())
            material->pEffect()->pEffect()->SetTexture("g_shadowMap", NULL);
        material = NULL; cube = NULL; plane = NULL; texture = NULL;
    }
    void deleteUnmanagedObjects() override { queue.clear(); }
    bool recreateForD3DExDevice() const override { return true; }
    bool load()
    {
        if (material && cube && plane && texture) return true;
        auto next = new Moo::EffectMaterial;
        Moo::EffectMaterialPtr effect = next;
        if (!next->initFromEffect("shaders/shadow/wg_forward_tank_shadow.fx")) return false;
        auto box = Moo::VisualManager::instance()->get("system/models/fx_unite_cube.visual");
        auto flat = Moo::VisualManager::instance()->get("system/models/plane.visual");
        auto map = Moo::TextureManager::instance()->get("maps/spots/TankOcclusion/TankOcclusionMap_forward.dds");
        if (!box || !box->isOK() || !flat || !flat->isOK() || !map || !map->pTexture()) return false;
        material = effect; cube = box; plane = flat; texture = map;
        return true;
    }
    bool geometry(const Moo::VisualPtr & visual)
    {
        bool submitted = false;
        for (const auto & set : visual->renderSets())
            for (const auto & geometry : set.geometry_)
            {
                if (!geometry.vertices_ || !geometry.primitives_ ||
                    FAILED(geometry.vertices_->setVertices(false, false)) ||
                    FAILED(geometry.primitives_->setPrimitives())) return false;
                for (const auto & group : geometry.primitiveGroups_)
                {
                    if (group.groupIndex_ >= geometry.primitives_->nPrimGroups() ||
                        FAILED(geometry.primitives_->drawPrimitiveGroup(group.groupIndex_))) return false;
                    submitted = true;
                }
            }
        return submitted;
    }
    void draw()
    {
        // Detach the batch before any native operation can re-enter Python.
        std::vector<Record> batch; batch.swap(queue);
        if (batch.empty() || !forward()) return;
        ClientSpacePtr current = DeprecatedSpaceHelpers::cameraSpace();
        if (!current || current->cleared() || !Moo::rc().device()) return;
        if (!load()) { ERROR_MSG("PySplodge: original forward shadow resources are unavailable\n"); return; }
        ComObjectWrap<IDirect3DStateBlock9> state;
        if (FAILED(Moo::rc().device()->CreateStateBlock(D3DSBT_ALL, &state)) || !state || FAILED(state->Capture()))
        { ERROR_MSG("PySplodge: cannot preserve device state\n"); return; }
        const Matrix * previousWorld = Moo::rc().effectVisualContext().worldMatrix();
        Moo::rc().effectVisualContext().worldMatrix(NULL);
        Moo::rc().effectVisualContext().updateSharedConstants(Moo::CONSTANTS_PER_VIEW);
        // 13f32db..13f3302 clears stencil once before the cube/plane pairs.
        const auto & size = Moo::rc().backBufferDesc();
        const bool cleared = material->setTechnique("CLEAR_STENCIL") &&
            Moo::rc().fsQuad().drawChecked(*material, size.Width, size.Height);
        if (!cleared) ERROR_MSG("PySplodge: original stencil clear failed\n");
        static const char * const techniques[] = {"STENCIL_PREPARATION", "SHADOW_PLANE"};
        if (cleared) for (const auto & record : batch)
        {
            auto live = record.generation.lock();
            if (!live || *live != record.expected || record.space != current || current->cleared()) continue;
            for (unsigned technique = 0; technique != 2; ++technique)
            {
                ID3DXEffect * effect = material->pEffect()->pEffect();
                if (!effect || !material->setTechnique(techniques[technique]) || !material->begin())
                { ERROR_MSG("PySplodge: original shadow technique is unavailable\n"); break; }
                bool success = true;
                for (uint32 pass = 0; pass != material->numPasses(); ++pass)
                {
                    if (!material->beginPass(pass)) { success = false; break; }
                    effect = material->pEffect()->pEffect();
                    const Vector4 border(0.f, 0.f, 1.f, 1.f);
                    const bool bound = effect && SUCCEEDED(effect->SetMatrix("g_worldMat", &record.world)) &&
                        SUCCEEDED(effect->SetMatrix("g_transform", &record.local)) &&
                        SUCCEEDED(effect->SetTexture("g_shadowMap", texture->pTexture())) &&
                        SUCCEEDED(effect->SetVector("g_borderTC", &border)) && SUCCEEDED(effect->CommitChanges());
                    const bool drawn = bound && geometry(technique == 0 ? cube : plane);
                    const bool ended = material->endPass();
                    if (!drawn || !ended) { success = false; break; }
                }
                if (!material->end()) success = false;
                if (!success) { ERROR_MSG("PySplodge: original forward shadow draw failed\n"); break; }
            }
        }
        if (FAILED(state->Apply())) ERROR_MSG("PySplodge: cannot restore device state\n");
        Moo::rc().invalidateStateCache();
        Moo::rc().effectVisualContext().worldMatrix(previousWorld);
    }
};
class Task : public MainLoopTask
{
public:
    Task() { MainLoopTasks::root().add(this, "Flora/Splodges", ">App", NULL); }
    std::unique_ptr<Renderer> owner;
    bool init() override { return true; }
    void fini() override { owner.reset(); }
    void draw() override { if (owner) owner->draw(); }
    Renderer & renderer() { if (!owner) owner.reset(new Renderer); return *owner; }
};
static Task task;
}

PY_TYPEOBJECT(PySplodge)
PY_BEGIN_METHODS(PySplodge)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(PySplodge)
    PY_ATTRIBUTE(maxLod)
PY_END_ATTRIBUTES()
PY_FACTORY_NAMED(PySplodge, "Splodge", BigWorld)

PySplodge::PySplodge(const Matrix & transform, float maximum, float height, PyTypeObject * type) :
    PyAttachment(type), transform_(transform), heightOffset_(height), generation_(new unsigned(0)),
    lod_(0.f), maxLod_(maximum), outsideNow_(true) {}
PySplodge::~PySplodge() { ++*generation_; }
void PySplodge::detach() { ++*generation_; PyAttachment::detach(); }
void PySplodge::leaveWorld() { ++*generation_; PyAttachment::leaveWorld(); }
PyObject * PySplodge::New(const Matrix & transform, float maximum, float height)
{
    // 13f2b60: deferred does not create this forward-only attachment.
    if (!StockSplodge::forward()) { Py_INCREF(Py_None); return Py_None; }
    return new PySplodge(transform, maximum, height);
}
// 13f30ff/13f3103 rejects only ordered greater-than; NaN does not invent a cull.
bool PySplodge::isLodVisible() const { return !(lod_ > maxLod_); }
void PySplodge::updateAnimations(const Matrix &, float lod) { lod_ = lod; }
void PySplodge::tossed(bool outside) { outsideNow_ = outside; }
void PySplodge::draw(Moo::DrawContext & context, const Matrix & world)
{
    if (!isLodVisible() || !outsideNow_ || s_ignoreSplodge_ || !StockSplodge::forward() ||
        context.renderingPassType() != Moo::RENDERING_PASS_COLOR) return;
    ClientSpacePtr space = DeprecatedSpaceHelpers::cameraSpace();
    if (!space || space->cleared()) return;
    StockSplodge::task.renderer().queue.push_back({StockSplodge::adjustedWorld(world, heightOffset_),
        transform_, generation_, *generation_, space});
}
void PySplodge::worldBoundingBox(BoundingBox & bounds, const Matrix & world, bool)
{
    BoundingBox local(Vector3(-1.f, -1.f, -1.f), Vector3(1.f, 1.f, 1.f));
    local.transformBy(transform_);
    local.transformBy(StockSplodge::adjustedWorld(world, heightOffset_));
    bounds.addBounds(local);
}
void PySplodge::worldVisibilityBox(BoundingBox & bounds, const Matrix & world, bool skinny)
{ worldBoundingBox(bounds, world, skinny); }
bool PySplodge::s_ignoreSplodge_ = false;
uint32 PySplodge::s_splodgeIntensity_ = 160;
BW_END_NAMESPACE
