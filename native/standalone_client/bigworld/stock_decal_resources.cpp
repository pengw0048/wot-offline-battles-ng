#include "pch.hpp"
#include "stock_decal_resources.hpp"
#include "stock_dynamic_decals.hpp"
#include "stock_vehicle_traces.hpp"
#include "moo/dynamic_decal.hpp"
#include "moo/renderer.hpp"
#include "moo/shadow_manager.hpp"
#include "moo/sticker_decal.hpp"
#include "moo/texture_manager.hpp"
#include "pyscript/script.hpp"
#include "romp/ssao_support.hpp"

BW_BEGIN_NAMESPACE
int StockDecalResources_token = 1;

namespace
{
// 1d577f8 is BSS, incremented only after successful registration. Destroying
// a renderer's texture owner must not recycle process-wide texture IDs.
std::uint32_t nextTraceTextureID = 0;
}

StockTraceTextureOwner::StockTraceTextureOwner() : registry_(nextTraceTextureID) {}

int StockTraceTextureOwner::textureIndex(const BW::string &name)
{
    return registry_.add(std::string(name.c_str(), name.size()),
        [](const std::string &resource) -> Moo::BaseTexturePtr
        {
            Moo::TextureManager *manager = Moo::TextureManager::instance();
            if (!manager)
                return NULL;
            // a62800 requests a real texture through the texture manager;
            // its reference remains held by this renderer's trace owner.
            return manager->get(resource.c_str(), true, true, true, "Art/File Textures/");
        });
}

void setDamageStickerCriticalAngle(float angle)
{
    // 9593d0 and public ForwardStickerModel both consume radians from this
    // owner when limiting the sticker projection angle against its surface.
    PyStickerModel::criticalAngle(wot::stockDamageStickerAngle(angle));
}
PY_AUTO_MODULE_FUNCTION(RETVOID, setDamageStickerCriticalAngle,
    OPTARG(float, 10.0f, END), BigWorld)

static PyObject *py_wg_traceTextureIndex(PyObject *args)
{
    PyObject *argument = NULL;
    if (!PyArg_UnpackTuple(args, "wg_traceTextureIndex", 1, 1, &argument))
        return NULL;
    BW::string name;
    if (Script::setData(argument, name, "wg_traceTextureIndex() argument 1") != 0)
        return NULL;
    Renderer *renderer = Renderer::pInstance();
    StockTraceTextureOwner *owner = renderer ? renderer->traceTextureOwner() : NULL;
    if (!owner)
        return PyInt_FromLong(-1);
    try
    {
        return PyInt_FromLong(owner->textureIndex(name));
    }
    catch (const std::out_of_range &error)
    {
        // Contain the original substr(npos) failure at the Python boundary.
        PyErr_SetString(PyExc_ValueError, error.what());
        return NULL;
    }
}
PY_MODULE_FUNCTION(wg_traceTextureIndex, BigWorld)

bool isForwardPipeline()
{
    // 650970 returns PyBool(Renderer m_type == TYPE_FORWARD_SHADING).
    return Renderer::instance().pipelineType() == IRendererPipeline::TYPE_FORWARD_SHADING;
}
PY_AUTO_MODULE_FUNCTION(RETDATA, isForwardPipeline, END, BigWorld)

bool isSSAOEnabled()
{
    // 006509c0 returns false without an SSAO owner; 00a44da0 queries both
    // its graphics setting and local enable flag, as SSAOSupport::enable does.
    Renderer * renderer = Renderer::pInstance();
    IRendererPipeline * pipeline = renderer ? renderer->pipeline() : NULL;
    SSAOSupport * ssao = pipeline ? pipeline->ssaoSupport() : NULL;
    return ssao && ssao->enable();
}
PY_AUTO_MODULE_FUNCTION(RETDATA, isSSAOEnabled, END, BigWorld)

bool isShadowsEnabled()
{
    // 00650a30/0096eb20 query active shadow subsystems and their manager's
    // enable flag. Read the live native owner rather than a quality index.
    Renderer * renderer = Renderer::pInstance();
    IRendererPipeline * pipeline = renderer ? renderer->pipeline() : NULL;
    Moo::ShadowManager * shadows = pipeline ? pipeline->dynamicShadow() : NULL;
    return shadows && shadows->isShadowsEnabled();
}
PY_AUTO_MODULE_FUNCTION(RETDATA, isShadowsEnabled, END, BigWorld)

void wg_addDecalGroup(const BW::string &name, float lifeTime, int trianglesCount)
{
    if (Renderer::instance().pipelineType() == IRendererPipeline::TYPE_FORWARD_SHADING)
        return;
    // Deferred groups carry lifetime; triangle buffers belong to forward Decal.
    (void)trianglesCount;
    DynamicDecalManager::instance().addDecalGroup(name, lifeTime);
}
PY_AUTO_MODULE_FUNCTION(RETVOID, wg_addDecalGroup,
    ARG(BW::string, ARG(float, ARG(int, END))), BigWorld)

void wg_clearDecals()
{
    // 931650 clears only the deferred owner's dynamic decals and vehicle traces.
    Renderer *renderer = Renderer::pInstance();
    if (!renderer || renderer->pipelineType() != IRendererPipeline::TYPE_DEFERRED_SHADING)
        return;
    clearStockDynamicDecals();
    clearStockVehicleTraceRenderer();
}
PY_AUTO_MODULE_FUNCTION(RETVOID, wg_clearDecals, END, BigWorld)

int wg_decalTextureIndex(const BW::string &name)
{
    if (Renderer::instance().pipelineType() == IRendererPipeline::TYPE_FORWARD_SHADING)
        return -1;
    return DynamicDecalManager::instance().textureIndex(name);
}
PY_AUTO_MODULE_FUNCTION(RETDATA, wg_decalTextureIndex, ARG(BW::string, END), BigWorld)

BW_END_NAMESPACE
