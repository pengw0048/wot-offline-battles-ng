"""Deferred owners stay CPU-only until RenderContext::createDevice runs device callbacks.

Hosts compile unmodified production and patched public init bodies. Fakes abort
where the real native singleton or Renderer assertion would fire.
"""
from pathlib import Path
import contextlib
import importlib.util
import io
import os
import re
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))


def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


def line(source, text):
    return next(entry for entry in source.splitlines() if entry.startswith(text))


def run_host(code, prefix):
    with tempfile.TemporaryDirectory(prefix=prefix) as directory:
        directory = Path(directory)
        (directory / 'host.cpp').write_text(code)
        subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                        str(directory / 'host.cpp'), '-o', str(directory / 'host')], check=True)
        return subprocess.run([str(directory / 'host')], capture_output=True, text=True)


NATIVE = r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
using uint32 = unsigned;
int errors = 0;
std::string lastError;
#define ERROR_MSG(format, text) (++errors, lastError = (text))
#define BW_GUARD
#define FAILED(value) ((value) < 0)
[[noreturn]] void boundary(const char* what) { std::fprintf(stderr, "NATIVE BOUNDARY: %s\n", what); std::abort(); }
namespace BW { using string = std::string; template<class T> using vector = std::vector<T>; }
template<class T> struct Ptr
{
    std::shared_ptr<T> value;
    Ptr(T* raw = nullptr) : value(raw) {}
    T* operator->() const { return value.get(); }
    T* get() const { return value.get(); }
    explicit operator bool() const { return bool(value); }
    bool operator!() const { return !value; }
};
struct Native
{
    bool device = false, effectManager = false, visualManager = false, textureManager = false;
    bool missingEffect = false, missingVisual = false;
    int effects = 0, visuals = 0, textures = 0;
} native;
'''


SPACE_HOST = NATIVE + r'''
enum D3DFORMAT { D3DFMT_UNKNOWN, D3DFMT_DXT5 };
struct D3DXVECTOR3 { float x = 0, y = 0, z = 0; };
struct D3DXQUATERNION { float x = 0, y = 0, z = 0, w = 1; };
struct ID3DXEffect { int cleared = 0; long SetTexture(const char*, void*) { ++cleared; return 0; } };
namespace DX { struct BaseTexture {}; struct Texture {}; }
template<class T> struct ComObjectWrap { T* object = nullptr; };
struct Vector2 { float x, y; Vector2(float a, float b) : x(a), y(b) {} };
struct Vector3
{
    float x, y, z;
    Vector3(float a, float b, float c) : x(a), y(b), z(c) {}
    Vector3 operator-(const Vector3& o) const { return Vector3(x - o.x, y - o.y, z - o.z); }
    Vector3 operator+(const Vector3& o) const { return Vector3(x + o.x, y + o.y, z + o.z); }
    Vector3 operator*(float s) const { return Vector3(x * s, y * s, z * s); }
};
struct Matrix
{
    float m[4][4] = {};
    void setZero() { *this = Matrix(); }
    void setScale(const Vector3& s) { m[0][0] = s.x; m[1][1] = s.y; m[2][2] = s.z; }
    void translation(const Vector3& t) { m[3][0] = t.x; m[3][1] = t.y; m[3][2] = t.z; }
    const float* operator[](int row) const { return m[row]; }
};
struct BoundingBox { static BoundingBox s_insideOut_; void setBounds(const Vector3&, const Vector3&) {} };
BoundingBox BoundingBox::s_insideOut_;

namespace Moo
{
// Registration and iteration order follow lib/moo/device_callback.cpp.
struct DeviceCallback
{
    static std::list<DeviceCallback*>& callbacks() { static std::list<DeviceCallback*> value; return value; }
    DeviceCallback() { callbacks().push_back(this); }
    virtual ~DeviceCallback() { callbacks().remove(this); }
    virtual void createManagedObjects() {}
    virtual void deleteManagedObjects() {}
    virtual void createUnmanagedObjects() {}
    virtual void deleteUnmanagedObjects() {}
    virtual bool recreateForD3DExDevice() const { return false; }
    static void createAllManaged() { for (auto* item : callbacks()) item->createManagedObjects(); }
    static void deleteAllManaged() { for (auto* item : callbacks()) item->deleteManagedObjects(); }
    static void createAllUnmanaged(bool force)
    { for (auto* item : callbacks()) if (force || item->recreateForD3DExDevice()) item->createUnmanagedObjects(); }
    static void deleteAllUnmanaged(bool force)
    { for (auto* item : callbacks()) if (force || item->recreateForD3DExDevice()) item->deleteUnmanagedObjects(); }
};
struct EffectManager { static EffectManager* pInstance() { static EffectManager value; return native.effectManager ? &value : nullptr; } };
struct Properties { ID3DXEffect* binding; ID3DXEffect* existingStockBinding() const { return binding; } };
struct EffectMaterial { ID3DXEffect effect; Properties value{&effect}; Properties& properties() { return value; } };
using EffectMaterialPtr = Ptr<EffectMaterial>;
bool createEffect(EffectMaterialPtr& material, const std::string& name)
{
    // Singleton<EffectManager>::instance() asserts in EffectMaterial::loadInternal.
    if (!EffectManager::pInstance()) boundary("Singleton<Moo::EffectManager>::instance() is null");
    assert(name == "shaders/std_effects/wg_deferred_decal.fx");
    ++native.effects;
    if (native.missingEffect) return false;
    material = new EffectMaterial;
    return true;
}
struct Visual { bool isOK() const { return true; } };
using VisualPtr = Ptr<Visual>;
struct VisualManager
{
    static VisualManager* instance() { static VisualManager value; return native.visualManager ? &value : nullptr; }
    VisualPtr get(const std::string& name)
    {
        assert(name == "system/models/fx_unit_plane_05.visual" || name == "system/models/fx_unite_cube_decal.visual");
        ++native.visuals;
        return native.missingVisual ? NULL : new Visual;
    }
};
struct VertexBuffer { int releases = 0; void release() { ++releases; } };
struct BaseTexture {};
using BaseTexturePtr = Ptr<BaseTexture>;
struct FormatNameMap { static D3DFORMAT formatType(const std::string& name) { return name == "DXT5" ? D3DFMT_DXT5 : D3DFMT_UNKNOWN; } };
}

struct Section;
using DataSectionPtr = Ptr<Section>;
struct Section
{
    std::string text;
    std::vector<std::pair<std::string, DataSectionPtr>> children;
    DataSectionPtr openSection(const std::string& name) const
    { for (const auto& child : children) if (child.first == name) return child.second; return NULL; }
    void openSections(const std::string& name, std::vector<DataSectionPtr>& output) const
    { for (const auto& child : children) if (child.first == name) output.push_back(child.second); }
    std::string asString() const { return text; }
    int readInt(const std::string& name, int fallback = 0) const { auto s = openSection(name); return s ? std::stoi(s->text) : fallback; }
    uint32 readUInt(const std::string& name, uint32 fallback = 0) const { return uint32(readInt(name, int(fallback))); }
    bool readBool(const std::string& name, bool fallback) const { auto s = openSection(name); return s ? s->text == "true" : fallback; }
    std::string readString(const std::string& name) const { auto s = openSection(name); return s ? s->text : ""; }
    Vector2 readVector2(const std::string& name, const Vector2& fallback) const
    { auto s = openSection(name); if (!s) return fallback; float x, y; std::sscanf(s->text.c_str(), "%f %f", &x, &y); return Vector2(x, y); }
};
DataSectionPtr node(const std::string& text, std::vector<std::pair<std::string, DataSectionPtr>> children = {})
{ DataSectionPtr result = new Section; result->text = text; result->children = std::move(children); return result; }
DataSectionPtr configurationRoot;
namespace BWResource { DataSectionPtr openSection(const std::string& name) { return name == "system/decalsConfig" ? configurationRoot : NULL; } }
struct AutoConfigString { std::string path; explicit AutoConfigString(const char* value) : path(value) {} const std::string& value() const { return path; } };

struct IRendererPipeline { enum EType { TYPE_DEFERRED_SHADING, TYPE_FORWARD_SHADING }; };
struct Renderer
{
    bool inited = false;
    IRendererPipeline::EType type = IRendererPipeline::TYPE_DEFERRED_SHADING;
    static Renderer& instance() { static Renderer value; return value; }
    IRendererPipeline::EType pipelineType() const { if (!inited) boundary("Renderer::pipelineType MF_ASSERT(m_inited)"); return type; }
};

namespace wot
{
struct StockSpaceDecalRecord { uint32 visibilityMask() const { return 1; } };
struct StockSpaceDecalGroup { float scalar(unsigned offset) const { return offset < 12 ? 0.f : 1.f; } };
struct StockSpaceDecalsData
{
    std::vector<StockSpaceDecalRecord> decals;
    std::vector<StockSpaceDecalGroup> groups;
    bool read(const void*, size_t length, std::string& error)
    { if (!length) { error = "empty"; return false; } decals.resize(2); groups.resize(1); return true; }
};
}

using DynamicObjectHandle = unsigned;
struct Flags { void isDynamic(bool) {} };
struct SceneObject { void* handle = nullptr; Flags value; template<class T> void handleAndType(T* item) { handle = item; } Flags& flags() { return value; } };
struct Operation
{
    bool registered = false;
    template<class T> void* getHandler() { return registered ? this : nullptr; }
    template<class T, class H> void addHandler() { registered = true; }
};
struct DrawOperation : Operation {};
struct SpatialQueryOperation : Operation {};
struct Scene
{
    DrawOperation draw; SpatialQueryOperation spatial;
    template<class T> T* getObjectOperation();
};
template<> DrawOperation* Scene::getObjectOperation<DrawOperation>() { return &draw; }
template<> SpatialQueryOperation* Scene::getObjectOperation<SpatialQueryOperation>() { return &spatial; }
struct DynamicScene
{
    unsigned live = 0, next = 0;
    DynamicObjectHandle addObject(const SceneObject&, const Matrix&, const BoundingBox&, const BoundingBox&) { ++live; return ++next; }
    void removeObject(DynamicObjectHandle) { assert(live); --live; }
};
struct ClientSpace { virtual ~ClientSpace() {} };
struct FourCC { explicit FourCC(const char*) {} };
namespace CompiledSpace
{
struct StockSpaceDecalState;
class CompiledSpace : public ClientSpace
{
public:
    Scene value; DynamicScene dynamic;
    Scene& scene() { return value; }
    DynamicScene& dynamicScene() { return dynamic; }
};
struct StringTable {};
struct BinaryFormat
{
    static const size_t NOT_FOUND = size_t(-1);
    struct Stream
    {
        std::string bytes = "WGSD2";
        size_t length() const { return bytes.size(); }
        const void* readRaw(size_t) const { return bytes.data(); }
    } stream;
    size_t findSection(const FourCC&) const { return 0; }
    uint32 sectionVersion(size_t) const { return 2; }
    Stream* openSection(size_t) { return &stream; }
    void closeSection(Stream*) {}
};
class ILoader
{
public:
    virtual ~ILoader() {}
    virtual bool doLoadFromSpace(ClientSpace*, BinaryFormat&, const DataSectionPtr&, const Matrix&, const StringTable&) = 0;
    virtual bool doBind() = 0;
    virtual void doUnload() = 0;
    virtual float percentLoaded() const = 0;
};
}

namespace
{
@DEFINITIONS@
}
namespace CompiledSpace
{
@STATE@
}
namespace
{
struct DecalDrawHandler {};
struct DecalSpatialHandler {};
@MEMBERSHIP@
bool loadDecalResources(DecalInstance&, const wot::StockSpaceDecalRecord&, const CompiledSpace::StringTable&, bool)
{
    // The real reader calls TextureManager::instance()->getSystemMemoryTexture.
    if (!native.textureManager) boundary("Moo::TextureManager::instance() is null");
    ++native.textures;
    return true;
}
}
namespace CompiledSpace
{
@LOADER@
}
namespace
{
@LIFECYCLE@
}
@PUBLIC@

// RenderContext::createDevice constructs the managers before createAllManaged/createAllUnmanaged.
void createDevice()
{
    assert(!native.device);
    native.device = native.effectManager = native.visualManager = native.textureManager = true;
    Moo::DeviceCallback::createAllManaged();
    Moo::DeviceCallback::createAllUnmanaged(true);
}
void resetDevice() { Moo::DeviceCallback::deleteAllUnmanaged(false); Moo::DeviceCallback::createAllUnmanaged(false); }
void releaseDevice() { Moo::DeviceCallback::deleteAllUnmanaged(true); Moo::DeviceCallback::deleteAllManaged(); native.device = false; }
void destroyManagers() { native.effectManager = native.visualManager = native.textureManager = false; }

DataSectionPtr atlas()
{
    return node("", {{"constrains", node("", {{"format", node("DXT5")}, {"minWidth", node("4")}, {"minHeight", node("4")},
        {"maxWidth", node("4096")}, {"maxHeight", node("4096")}})}});
}
DataSectionPtr configuration()
{
    return node("", {
        {"scene", node("", {{"anisotropy", node("2")},
            {"quality", node("", {{"option", node("MAX", {{"mip", node("0")}, {"matTypeDownsample", node("1 1")}})}})},
            {"type", node("", {{"name", node("GBufferDecals")}, {"atlas", atlas()}})},
            {"type", node("", {{"name", node("OtherDecals")}, {"atlas", atlas()}})}})},
        {"render", node("", {{"quality", node("", {{"option", node("MAX", {{"maxVisibleDecals", node("512")}})}})}})}});
}
bool load(CompiledSpace::StockSpaceDecalsLoader& loader, ::CompiledSpace::CompiledSpace& space)
{
    CompiledSpace::BinaryFormat reader; CompiledSpace::StringTable strings; Matrix transform;
    return loader.doLoadFromSpace(&space, reader, NULL, transform, strings);
}

int main()
{
    ::CompiledSpace::CompiledSpace space;
    auto& registered = Moo::DeviceCallback::callbacks();
    configurationRoot = configuration();

    // Renderer::init -> DeferredPipeline::init -> DecalsManager::init precedes createDevice.
    assert(initialiseStockSpaceDecals());
    assert(renderer && !renderer->active && registered.size() == 1);
    assert(native.effects == 0 && native.visuals == 0 && errors == 0);
    assert(renderer->quality[0].visibleLimit == 512 && renderer->channels[0].size() == 1 && renderer->channels[1].size() == 1);
    const auto* first = renderer.get();
    assert(initialiseStockSpaceDecals() && renderer.get() == first && registered.size() == 1);
    beginStockSpaceDecals();
    assert(!renderer->frame);
    // A callback before the managers exist is a logged local failure.
    renderer->createManagedObjects();
    assert(!renderer->active && native.effects == 0 && errors == 1);

    // A space load before GPU readiness fails locally and reads no texture.
    Renderer::instance().inited = true;
    {
        CompiledSpace::StockSpaceDecalsLoader early(1);
        assert(!load(early, space) && native.textures == 0 && early.percentLoaded() == 0.f);
        assert(lastError.find("renderer is unavailable") != std::string::npos);
    }

    createDevice();
    assert(renderer->active && renderer->material && renderer->plane && renderer->cube);
    assert(native.effects == 1 && native.visuals == 2);
    CompiledSpace::StockSpaceDecalsLoader loader(1);
    assert(load(loader, space) && loader.doBind() && loader.percentLoaded() == 1.f);
    assert(native.textures == 2 && space.dynamic.live == 1 && renderer->mappings.size() == 1);
    auto state = renderer->mappings.front().lock();
    assert(state && state->active);
    beginStockSpaceDecals();
    assert(renderer->frame);
    endStockSpaceDecals();
    assert(!renderer->frame && renderer->material->effect.cleared == 5);

    // ResetEx releases unmanaged views but keeps the managed owner active.
    state->textureViews[std::make_pair(static_cast<DX::BaseTexture*>(nullptr), 0)];
    resetDevice();
    assert(renderer->active && state->textureViews.empty() && renderer->instances.releases == 1);
    assert(native.effects == 1);

    // A full device release retires managed resources; the next device restores them.
    releaseDevice();
    assert(!renderer->active && !renderer->material && !renderer->plane && !renderer->cube);
    beginStockSpaceDecals();
    assert(!renderer->frame && state->active);
    createDevice();
    assert(renderer->active && native.effects == 2 && state->active && renderer->mappings.size() == 1);

    // Missing effect or geometry stays inactive and rejects new loads; recreation recovers.
    releaseDevice();
    native.missingEffect = true;
    errors = 0;
    createDevice();
    assert(!renderer->active && !renderer->material && errors == 1 && native.visuals == 4);
    {
        CompiledSpace::StockSpaceDecalsLoader rejected(1);
        assert(!load(rejected, space) && native.textures == 2);
    }
    releaseDevice();
    native.missingEffect = false;
    native.missingVisual = true;
    errors = 0;
    createDevice();
    assert(!renderer->active && !renderer->plane && !renderer->cube && errors == 1);
    releaseDevice();
    native.missingVisual = false;
    createDevice();
    assert(renderer->active);

    // DeviceApp::fini releases the device before Renderer destroys DecalsManager.
    loader.doUnload();
    assert(space.dynamic.live == 0 && !state->active);
    releaseDevice();
    destroyManagers();
    finaliseStockSpaceDecals();
    assert(!renderer && registered.empty());
    finaliseStockSpaceDecals();
    assert(!renderer && registered.empty());
    state.reset();

    // Failed pipeline init tears down before any device exists.
    configurationRoot = NULL;
    assert(!initialiseStockSpaceDecals() && !renderer && registered.empty());
    configurationRoot = configuration();
    assert(initialiseStockSpaceDecals() && registered.size() == 1);
    finaliseStockSpaceDecals();
    assert(!renderer && registered.empty());

    // A later device and owner generation activates normally.
    const int effects = native.effects;
    setStockSpaceDecalsQuality(2);
    assert(initialiseStockSpaceDecals() && renderer->selection == 2 && !renderer->active);
    createDevice();
    assert(renderer->active && native.effects == effects + 1);
    releaseDevice();
    destroyManagers();
    finaliseStockSpaceDecals();

    // ForwardPipeline never constructs DecalsManager; WGSD2 loads stay CPU-only.
    Renderer::instance().type = IRendererPipeline::TYPE_FORWARD_SHADING;
    createDevice();
    {
        CompiledSpace::StockSpaceDecalsLoader forward(1);
        assert(load(forward, space) && forward.percentLoaded() == 1.f);
    }
    assert(!renderer && registered.empty() && native.textures == 2);
    releaseDevice();
    std::puts("deferred decal device-callback lifecycle passed");
}
'''


def space_decal_host(source, header):
    definitions = '\n'.join([
        line(source, 'AutoConfigString decalsConfiguration('),
        method(source, 'bool decalFailure('),
        *(method(source, 'struct ' + name + '\n') + ';' for name in (
            'DecalResource', 'DecalInstance', 'DecalGroup', 'QueuedDecal', 'DecalQuality', 'DecalChannel', 'DecalTextureView')),
        method(source, 'class DecalRenderer : public Moo::DeviceCallback') + ';',
        line(source, 'std::shared_ptr<DecalRenderer> renderer;'),
        line(source, 'int selectedQuality'),
        method(source, 'void clearDecalBinding(')])
    loader = '\n'.join([
        method(header, 'class StockSpaceDecalsLoader : public ILoader') + ';',
        *(method(source, signature) for signature in (
            'StockSpaceDecalsLoader::StockSpaceDecalsLoader(', 'StockSpaceDecalsLoader::~StockSpaceDecalsLoader(',
            'bool StockSpaceDecalsLoader::doLoadFromSpace(', 'bool StockSpaceDecalsLoader::doBind()',
            'void StockSpaceDecalsLoader::doUnload()', 'float StockSpaceDecalsLoader::percentLoaded()'))])
    lifecycle = '\n'.join(method(source, signature) for signature in (
        'bool DecalRenderer::initialise()', 'void DecalRenderer::createManagedObjects()',
        'void DecalRenderer::deleteManagedObjects()', 'void DecalRenderer::deleteUnmanagedObjects()'))
    public = '\n'.join(method(source, signature) for signature in (
        'bool initialiseStockSpaceDecals()', 'void finaliseStockSpaceDecals()', 'void beginStockSpaceDecals()',
        'void endStockSpaceDecals()', 'void setStockSpaceDecalsQuality(int option)'))
    return (SPACE_HOST.replace('@DEFINITIONS@', definitions)
            .replace('@STATE@', method(source, 'struct StockSpaceDecalState') + ';')
            .replace('@MEMBERSHIP@', method(source, 'void removeDecalMembership('))
            .replace('@LOADER@', loader).replace('@LIFECYCLE@', lifecycle).replace('@PUBLIC@', public))


PIPELINE_HOST = NATIVE + r'''
#define MF_ASSERT(condition) do { if (!(condition)) boundary("MF_ASSERT(" #condition ")"); } while (0)
typedef unsigned uint;
struct IRendererPipeline
{
    enum EType { TYPE_DEFERRED_SHADING, TYPE_FORWARD_SHADING };
    enum EGraphicsSetting { GRAPHICS_SETTING_SHADOWS_QUALITY, GRAPHICS_SETTING_DECALS_QUALITY,
        GRAPHICS_SETTING_LIGHTING_QUALITY, GRAPHICS_SETTING_COUNT };
    virtual ~IRendererPipeline() {}
    virtual bool init() = 0;
    virtual void onGraphicsOptionSelected(EGraphicsSetting, int) = 0;
};
struct Calls { int deferredPipelines = 0, forwardPipelines = 0, spaceDecals = 0, options = 0; unsigned traceQuality = 0; } calls;
// Every GPU owner must wait for createAllManaged/createAllUnmanaged.
void gpu(const char* owner) { if (!native.effectManager || !native.device) boundary(owner); }
namespace Moo
{
struct GraphicsSetting
{
    int option;
    int activeOption() const { return option; }
    static Ptr<GraphicsSetting> getFromLabel(const std::string& label)
    { return label == "DECALS_QUALITY" ? new GraphicsSetting{3} : NULL; }
};
}
struct MacroSetting
{
    int active = 0;
    void addOption(const char*, const char*, bool, const char*) {}
    int activeOption() const { return active; }
    void stockForceActiveOption(int option) { active = option; }
} pipelineSetting, *g_pipelineMacroSetting = &pipelineSetting;
namespace Moo { struct GraphicsSettingRegistry { static void add(MacroSetting*) {} }; }
struct StockTraceTextureOwner {};
class Renderer
{
public:
    bool init(bool isAssetProcessor, bool supportDS);
    IRendererPipeline::EType pipelineType() const;
    void initGraphicsSettings() {}
    static Renderer& instance() { assert(current); return *current; }
    static Renderer* current;
    bool m_inited = false;
    IRendererPipeline::EType m_type = IRendererPipeline::TYPE_FORWARD_SHADING;
    std::unique_ptr<IRendererPipeline> m_pipeline;
    std::unique_ptr<StockTraceTextureOwner> m_stockTraceTextures;
};
Renderer* Renderer::current = nullptr;

struct TraceRenderer
{
    unsigned quality = 0;
    void createManagedObjects() { gpu("TraceRenderer::createManagedObjects before createDevice"); }
    void setQuality(unsigned option) { quality = option; calls.traceQuality = option; }
    bool ready() const { gpu("TraceRenderer::ready before createDevice"); return false; }
};
std::unique_ptr<TraceRenderer> traceRenderer;
struct DynamicDecalRenderer { bool load() { return true; } };
std::shared_ptr<DynamicDecalRenderer> renderer;
bool initialiseStockSpaceDecals() { ++calls.spaceDecals; return true; }
class StockDeferredBuffers
{
public:
    bool initialise();
    void createManagedObjects() { gpu("StockDeferredBuffers::createManagedObjects before createDevice"); }
    void createUnmanagedObjects() { gpu("StockDeferredBuffers::createUnmanagedObjects before createDevice"); }
    bool ready() const { return false; }
};
class StockLocalLighting
{
public:
    bool initialise();
    void createManagedObjects() { gpu("StockLocalLighting::createManagedObjects before createDevice"); }
    void createUnmanagedObjects() { gpu("StockLocalLighting::createUnmanagedObjects before createDevice"); }
};
struct DynamicDecalManager {};
class DecalsManager { public: bool init(); std::unique_ptr<DynamicDecalManager> m_dynamicDecalsManager; };
class LightsManager { public: bool init(); std::unique_ptr<StockLocalLighting> stockLocalLighting_{new StockLocalLighting}; };
struct ShadowManager { bool init(void*) { return true; } };
class DeferredPipeline : public IRendererPipeline
{
public:
    DeferredPipeline() { ++calls.deferredPipelines; }
    bool init();
    void onGraphicsOptionSelected(EGraphicsSetting setting, int option)
    { ++calls.options; if (setting == GRAPHICS_SETTING_DECALS_QUALITY && traceRenderer) traceRenderer->setQuality(unsigned(option)); }
    std::unique_ptr<DecalsManager> m_decalsManager{new DecalsManager};
    std::unique_ptr<LightsManager> m_lightsManager{new LightsManager};
    std::unique_ptr<ShadowManager> m_dynamicShadowManager{new ShadowManager};
    std::unique_ptr<StockDeferredBuffers> m_stockDeferredBuffers{new StockDeferredBuffers};
};
class ForwardPipeline : public IRendererPipeline
{
public:
    ForwardPipeline() { ++calls.forwardPipelines; }
    bool init() { return true; }
    void onGraphicsOptionSelected(EGraphicsSetting, int) { ++calls.options; }
};
#define GraphicsSetting GraphicsSettingRegistry
@RENDERER@
#undef GraphicsSetting
@PRODUCTION@
@PIPELINE@

int main()
{
    // DeviceApp::init constructs Renderer and calls init before Moo::rc().createDevice.
    {
        pipelineSetting.active = 0;
        Renderer deferred; Renderer::current = &deferred;
        assert(deferred.init(false, true));
        assert(deferred.m_inited && deferred.pipelineType() == IRendererPipeline::TYPE_DEFERRED_SHADING);
        assert(calls.deferredPipelines == 1 && calls.spaceDecals == 1 && renderer && traceRenderer);
        // 93fef0 transfers option 0; restored values arrive with the explicit apply.
        assert(traceRenderer->quality == 0 && calls.options == 3 && deferred.m_stockTraceTextures);
        assert(initialiseStockDynamicDecals() && initialiseStockVehicleTraceRenderer());
        traceRenderer.reset(); renderer.reset();
    }
    {
        pipelineSetting.active = 1;
        calls = Calls();
        Renderer forward; Renderer::current = &forward;
        assert(forward.init(false, true));
        assert(forward.pipelineType() == IRendererPipeline::TYPE_FORWARD_SHADING && calls.forwardPipelines == 1);
        assert(!calls.deferredPipelines && !calls.spaceDecals && !renderer && !traceRenderer && !forward.m_stockTraceTextures);
    }
    std::puts("deferred pipeline init precedes every device owner");
}
'''


STARTUP_HOST = r'''
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
typedef unsigned uint;
typedef unsigned uint32;
int errors = 0;
[[noreturn]] void boundary(const char* what) { std::printf("BOUNDARY %s\n", what); std::fflush(stdout); std::abort(); }
#define MF_ASSERT(condition) do { if (!(condition)) boundary("MF_ASSERT(" #condition ")"); } while (0)
#define BW_GUARD
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define DECLARE_DEBUG_COMPONENT2(component, level)
#define REGISTER_SINGLETON_FUNC(type, function)
#define SINGLETON_MANAGER_WRAPPER_FUNC(type, function)
#define ERROR_MSG(...) (++errors)
#define INFO_MSG(...) ((void)0)
#define DEBUG_MSG(...) ((void)0)
#define bw_safe_delete(pointer) (delete (pointer), (pointer) = NULL)
namespace BW
{
using string = std::string;
using stringstream = std::stringstream;
template<class T> using vector = std::vector<T>;
template<class K, class V> using map = std::map<K, V>;
struct StringRef { std::string value; StringRef(const std::string& v) : value(v) {} StringRef(const char* v) : value(v) {} };
inline bool operator==(const std::string& a, const StringRef& b) { return a == b.value; }
}
struct ReferenceCount
{
    int references_ = 0;
    virtual ~ReferenceCount() {}
    void incRef() { ++references_; }
    void decRef() { if (--references_ == 0) delete this; }
};
template<class T> class SmartPointer
{
public:
    SmartPointer(T* object = 0) : object_(object) { if (object_) object_->incRef(); }
    SmartPointer(const SmartPointer& other) : object_(other.object_) { if (object_) object_->incRef(); }
    template<class U> SmartPointer(const SmartPointer<U>& other) : object_(other.getObject()) { if (object_) object_->incRef(); }
    ~SmartPointer() { if (object_) object_->decRef(); }
    SmartPointer& operator=(SmartPointer other) { std::swap(object_, other.object_); return *this; }
    T* operator->() const { return object_; }
    T& operator*() const { return *object_; }
    T* getObject() const { return object_; }
    bool exists() const { return object_ != 0; }
    bool hasObject() const { return object_ != 0; }
    explicit operator bool() const { return object_ != 0; }
    bool operator==(const SmartPointer& other) const { return object_ == other.object_; }
private:
    T* object_;
};
struct DataSection;
typedef SmartPointer<DataSection> DataSectionPtr;
struct DataSection : ReferenceCount
{
    std::string label; int option = 0; std::vector<DataSectionPtr> entries;
    void openSections(const std::string& name, std::vector<DataSectionPtr>& out) { if (name == "entry") out = entries; }
    std::string readString(const std::string&, const std::string& = "") const { return label; }
    int readInt(const std::string& name, int otherwise = 0) const { return name == "activeOption" ? option : otherwise; }
    std::string asString() const { return label; }
};
// No system/data/graphics_settings_presets.xml: the catalogue holds CUSTOM only.
const char* PyExc_RuntimeError = "RuntimeError";
void PyErr_SetString(const char*, const char*) { ++errors; }
struct BWResource { static DataSectionPtr openSection(const BW::string&) { return DataSectionPtr(); } };
namespace Moo { struct GraphicsSettingsAutoDetector { enum { NO_OVERRIDE = -1 }; }; }
namespace Moo
{
@GRAPHICS_SETTING@
}
@PRESETS_INTERFACE@
// Preference migration and auto-detection are a separate owner.
bool prepareStockGraphicsPreferences(DataSectionPtr, Moo::GraphicsSetting::SettingsVector&, int) { return true; }
@REGISTRY@
@PRESETS@

struct SimpleMutex {};
struct SimpleMutexHolder { explicit SimpleMutexHolder(SimpleMutex&) {} };
namespace Moo
{
class EffectManager;
struct EffectCompiler
{
    typedef std::map<BW::string, BW::string> StringStringMap;
    static void setMacroDefinition(const BW::string& macro, const BW::string& value);
    static const BW::string& fxoInfix();
    static void fxoInfix(const BW::string& infix);
    static SimpleMutex s_runtimeCompileMutex_;
    static BW::string s_fxoInfix_;
    static StringStringMap s_macros_;
};
SimpleMutex EffectCompiler::s_runtimeCompileMutex_;
BW::string EffectCompiler::s_fxoInfix_;
EffectCompiler::StringStringMap EffectCompiler::s_macros_;
@COMPILER@
@MACRO_SETTING@
}
@MACRO_SOURCE@

struct IRendererPipeline
{
    enum EType { TYPE_DEFERRED_SHADING, TYPE_FORWARD_SHADING };
    @SETTING_ENUM@
    virtual ~IRendererPipeline() {}
    virtual bool init() = 0;
    virtual void onGraphicsOptionSelected(EGraphicsSetting, int) = 0;
};
struct Calls { int shadowBuffers = 0, lightingBuffers = 0, traces = 0, pipelineInits = 0; } calls;
bool failPipelineInit = false, deviceCreated = false;
int spaceDecalQuality = -9, dynamicDecalQuality = -9, traceQuality = -9;
struct StockDeferredBuffers
{
    int shadows = -9, lighting = -9;
    void setShadowQuality(int option) { ++calls.shadowBuffers; shadows = option; }
    void setLightingQuality(int option) { ++calls.lightingBuffers; lighting = option; }
};
void setStockSpaceDecalsQuality(int option) { if (option < 0 || option >= 5) { ++errors; return; } spaceDecalQuality = option; }
void setStockDynamicDecalQuality(int option) { if (option < 0 || option >= 5) { ++errors; return; } dynamicDecalQuality = option; }
void setStockVehicleTraceQuality(unsigned option) { ++calls.traces; traceQuality = int(option); }
namespace Moo
{
class ShadowManager
{
public:
    @SHADOW_QUALITY@
    void setQualityOption(int optionIndex);
    bool m_dynamicEnabled = false, m_semiEnabled = false, m_isShadowQualityChanged = false;
    float m_semiViewDistanceFrom = 0, m_dynamicViewDistanceTo = 0, m_semiBlendFactor = 0;
    int m_shadowQualityIndex = -9;
};
@SHADOW@
}
bool g_bluring = true, g_useNormal = true;
int g_sampleNum = 2;
class SSAOSupport
{
public:
    void setQualityOption(int optionIndex);
    void enable(bool flag);
    bool m_enabled = false;
};
class HDRSupport { public: void setQualityOption(int); };
struct Vector2 { float x, y; };
class DecalsManager
{
public:
    DecalsManager() { for (float bias : {1.0f, 0.8f, 0.6f, 0.4f, 0.0f}) m_qualityOptions.push_back(QualityOption(bias)); }
    void setQualityOption(int option);
    void setFadeRange(float, float) { fadeBias = m_qualityOptions.at(m_activeQualityOption).m_lodBias; }
    struct QualityOption { QualityOption(float lodBias) : m_lodBias(lodBias) {} float m_lodBias; };
    uint m_activeQualityOption = 0;
    std::vector<QualityOption> m_qualityOptions;
    Vector2 m_fadeRange = {100.0f, 200.0f};
    float fadeBias = -1.0f;
};
@CONSUMERS@
class DeferredPipeline : public IRendererPipeline
{
public:
    bool init() { ++calls.pipelineInits; return !failPipelineInit; }
    void onGraphicsOptionSelected(EGraphicsSetting setting, int option);
    std::unique_ptr<Moo::ShadowManager> m_dynamicShadowManager{new Moo::ShadowManager};
    std::unique_ptr<StockDeferredBuffers> m_stockDeferredBuffers{new StockDeferredBuffers};
    std::unique_ptr<DecalsManager> m_decalsManager{new DecalsManager};
    std::unique_ptr<SSAOSupport> m_SSAOSupport{new SSAOSupport};
    std::unique_ptr<HDRSupport> m_HDRSupport{new HDRSupport};
};
@PIPELINE@
class ForwardPipeline : public IRendererPipeline
{
public:
    bool init() { ++calls.pipelineInits; return !failPipelineInit; }
    void onGraphicsOptionSelected(EGraphicsSetting, int) {}
};
struct StockTraceTextureOwner {};
@RENDERER_OWNER@
class Renderer
{
public:
    bool init(bool isAssetProcessor, bool supportDS);
    void initGraphicsSettings();
    @RENDERER_CALLBACK@
    bool m_inited = false;
    IRendererPipeline::EType m_type = IRendererPipeline::TYPE_FORWARD_SHADING;
    std::unique_ptr<IRendererPipeline> m_pipeline;
    std::unique_ptr<StockTraceTextureOwner> m_stockTraceTextures;
    int m_options[IRendererPipeline::GRAPHICS_SETTING_COUNT];
    Moo::GraphicsSetting::GraphicsSettingPtr m_settings[IRendererPipeline::GRAPHICS_SETTING_COUNT];
};
@RENDERER@

// Early RenderContext registration (FXAA_PROCESSING) precedes preference publication.
std::vector<int> earlyCalls;
void earlyCallback(int option) { earlyCalls.push_back(option); }
int compiles = 0;
std::string compiled;
void compileEffect()
{
    // EffectCompiler::loadOrCompile names output by fxoInfix and compiles with the macro map.
    if (!deviceCreated) boundary("effect compiled before createDevice");
    const auto& macros = Moo::EffectCompiler::s_macros_;
    auto value = [&](const char* name) { auto found = macros.find(name); return found == macros.end() ? std::string("-") : found->second; };
    compiled = Moo::EffectCompiler::fxoInfix() + "/" + value("WG_DEFERRED_SHADING") + value("BW_DEFERRED_SHADING") + value("SHADER_DEBUG");
    ++compiles;
}
int active(const char* label) { auto setting = Moo::GraphicsSetting::getFromLabel(label); return setting ? setting->activeOption() : -9; }
void report(const char* phase, Renderer& renderer)
{
    auto* deferred = dynamic_cast<DeferredPipeline*>(renderer.m_inited ? renderer.m_pipeline.get() : NULL);
    std::printf("%s registry=%d,%d,%d,%d", phase, active("RENDER_PIPELINE"), active("SHADOWS_QUALITY"),
        active("DECALS_QUALITY"), active("LIGHTING_QUALITY"));
    if (deferred)
        std::printf(" shadow=%d,%d,%d decals=%u,%d,%d,%d ssao=%d,%d lighting=%d calls=%d,%d,%d",
            deferred->m_dynamicShadowManager->m_shadowQualityIndex, int(deferred->m_dynamicShadowManager->m_dynamicEnabled),
            deferred->m_stockDeferredBuffers->shadows, deferred->m_decalsManager->m_activeQualityOption,
            spaceDecalQuality, dynamicDecalQuality, traceQuality, int(deferred->m_SSAOSupport->m_enabled), g_sampleNum,
            deferred->m_stockDeferredBuffers->lighting, calls.shadowBuffers, calls.traces, calls.lightingBuffers);
    std::printf(" errors=%d\n", errors);
}

int main(int argc, char** argv)
{
    if (argc != 8) return 2;
    const bool restored = argv[1][0] == '1', supportDS = argv[2][0] == '1';
    const int pipeline = std::atoi(argv[3]), shadows = std::atoi(argv[4]), decals = std::atoi(argv[5]);
    const int lighting = std::atoi(argv[6]);
    failPipelineInit = argv[7][0] == '1';

    Moo::GraphicsSetting::GraphicsSettingPtr early(
        new Moo::StaticCallbackGraphicsSetting("FXAA_PROCESSING", "FXAA Processing", &earlyCallback, -1, false, false));
    for (const char* option : {"OFF", "LOW", "MED", "HIGH"}) early->addOption(option, option, true);
    Moo::GraphicsSetting::add(early);

    // DeviceApp::init publishes graphicsPreferences only when preferences.xml loaded.
    if (restored)
    {
        DataSectionPtr section(new DataSection);
        const std::pair<const char*, int> saved[] = {{"FXAA_PROCESSING", 2}, {"RENDER_PIPELINE", pipeline},
            {"SHADER_DEBUG", 1}, {"SHADOWS_QUALITY", shadows}, {"DECALS_QUALITY", decals},
            {"LIGHTING_QUALITY", lighting}};
        for (const auto& entry : saved)
        {
            DataSectionPtr child(new DataSection);
            child->label = entry.first; child->option = entry.second;
            section->entries.push_back(child);
        }
        if (!Moo::GraphicsSetting::init(section)) boundary("GraphicsSetting::init failed");
    }
    std::printf("early calls=");
    for (int option : earlyCalls) std::printf("%d;", option);
    std::printf("\n");

    // DeviceApp constructs Renderer and initialises it before Moo::rc().createDevice.
    Renderer renderer;
    const bool inited = renderer.init(false, supportDS);
    std::printf("renderer result=%d type=%d pipelines=%d\n", int(inited), int(renderer.m_type), calls.pipelineInits);
    report("init", renderer);
    // A failed Renderer::init makes DeviceApp::init fail; no device or game.init follows.
    if (!inited) return 0;

    // RenderContext::createDevice: EffectManager constructor, then managed device callbacks.
    deviceCreated = true;
    Moo::EffectMacroSetting::setupSettings();
    Moo::EffectMacroSetting::updateManagerInfix();
    compileEffect();
    std::printf("setup compiled=%s\n", compiled.c_str());

    // game.init: BigWorld.wg_initCustomSettings, then SettingsCore.init -> generateGfxSettings.
    if (!initialiseStockGraphicsPresets()) boundary("presets registration failed");
    report("custom", renderer);
    prepareStockGraphicsSettingsPublication();
    report("apply", renderer);
    compileEffect();
    std::printf("applied compiled=%s\n", compiled.c_str());

    // A later GUI change of a live setting.
    auto setting = Moo::GraphicsSetting::getFromLabel("SHADOWS_QUALITY");
    setting->selectOption(4, true);
    report("change", renderer);
    // Changing the restart-required pipeline keeps this session's compiled permutation.
    auto pipelineSetting = Moo::GraphicsSetting::getFromLabel("RENDER_PIPELINE");
    pipelineSetting->selectOption(1 - pipelineSetting->activeOption());
    compileEffect();
    std::printf("switched pipeline=%d compiled=%s\n", pipelineSetting->activeOption(), compiled.c_str());
    return 0;
}
'''


STARTUP_PATCHERS = ('shader_compiler', 'stock_effect_owners', 'stock_graphics_settings_consumers',
                    'stock_graphics_presets', 'stock_graphics_settings', 'stock_time_of_day',
                    'stock_deferred_lighting', 'stock_dynamic_resolution', 'stock_decals',
                    'stock_deferred_buffers', 'stock_antialiasing', 'stock_vehicle_traces',
                    'stock_space_decals', 'stock_dynamic_decals', 'stock_local_lighting')
STARTUP_SOURCES = ('client/', 'lib/moo/', 'lib/romp/', 'lib/duplo/', 'lib/terrain/', 'lib/speedtree/',
                   'lib/chunk/', 'lib/space/', 'lib/compiled_space/', 'lib/scene/', 'lib/model/',
                   'lib/particle/', 'lib/cstdmf/', 'lib/resmgr/', 'lib/pyscript/', 'lib/math/', 'lib/physics2/')


def startup_engine(directory):
    """Apply every startup-graphics owner patch in the actual build-script order."""
    order = re.findall(r'patch_(\w+)\.py', (ROOT.parents[1] / 'tools/build_bigworld_client.ps1').read_text())
    names = [name for name in order if name in STARTUP_PATCHERS]
    assert sorted(names) == sorted(STARTUP_PATCHERS), names
    with tarfile.open(ARCHIVE) as source:
        prefix = None
        for member in source:
            prefix = prefix or member.name.split('/')[0] + '/programming/bigworld/'
            relative = member.name[len(prefix):] if member.name.startswith(prefix) else ''
            if member.isfile() and relative.startswith(STARTUP_SOURCES) and '/unit_test' not in relative:
                target = directory / relative; target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(source.extractfile(member).read())
    for name in names:
        spec = importlib.util.spec_from_file_location('startup_owner_' + name, ROOT / 'bigworld' / ('patch_' + name + '.py'))
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        with contextlib.redirect_stdout(io.StringIO()):
            (getattr(module, 'apply', None) or getattr(module, 'patch', None) or module.patch_engine)(directory)
    return directory / 'lib'


def startup_host(lib):
    moo, romp = lib / 'moo', lib / 'romp'
    header = (moo / 'graphics_settings.hpp').read_text()
    registry = (moo / 'graphics_settings.cpp').read_text()
    registry = registry.replace(method(registry, 'void GraphicsSetting::write(DataSectionPtr section)'), '')
    macro_header = (moo / 'effect_macro_setting.hpp').read_text()
    renderer = (moo / 'renderer.cpp').read_text()
    renderer_header = (moo / 'renderer.hpp').read_text()
    presets = (ROOT / 'bigworld/stock_graphics_presets.cpp').read_text()
    interface = (ROOT / 'bigworld/stock_graphics_presets.hpp').read_text()
    callback = renderer_header.index('template<IRendererPipeline::EGraphicsSetting setting>')
    shadow_header = (moo / 'shadow_manager.hpp').read_text()

    def source(text):
        return '\n'.join(entry for entry in text.splitlines() if not entry.startswith(('#include', '#pragma')))

    replacements = {
        '@GRAPHICS_SETTING@': header[header.index('class GraphicsSetting : public ReferenceCount'):
                                     header.index('} // namespace Moo')],
        '@PRESETS_INTERFACE@': '#include "wot/stock_graphics_presets.hpp"\n' + source(interface),
        '@REGISTRY@': source(registry),
        '@PRESETS@': presets[presets.index('namespace\n{'):presets.index('PyObject* dictionary(')] + '}\n' + '\n'.join(
            method(presets, signature) for signature in (
                'void prepareStockGraphicsSettingsPublication()', 'bool stockGraphicsPresetDesiredOption(',
                'bool stockGraphicsPresetsActive()', 'void publishStockGraphicsPreferences(',
                'bool initialiseStockGraphicsPresets()')),
        '@MACRO_SETTING@': method(macro_header, 'class EffectMacroSetting : public GraphicsSetting') + ';',
        '@COMPILER@': '\n'.join(method((moo / 'effect_compiler.cpp').read_text(), signature) for signature in (
            'void EffectCompiler::setMacroDefinition(', 'const BW::string& EffectCompiler::fxoInfix()',
            'void EffectCompiler::fxoInfix( const BW::string & infix )')),
        '@MACRO_SOURCE@': source((moo / 'effect_macro_setting.cpp').read_text()),
        '@SETTING_ENUM@': method(renderer_header, 'enum EGraphicsSetting') + ';',
        '@SHADOW_QUALITY@': method(shadow_header, 'enum Quality') + ';',
        '@SHADOW@': method((moo / 'shadow_manager.cpp').read_text(), 'void ShadowManager::setQualityOption(int optionIndex)'),
        '@CONSUMERS@': '\n'.join([
            method((romp / 'ssao_support.cpp').read_text(), 'void SSAOSupport::setQualityOption(int optionIndex)'),
            method((romp / 'ssao_support.cpp').read_text(), 'void SSAOSupport::enable(bool flag)'),
            method((romp / 'hdr_support.cpp').read_text(), 'void HDRSupport::setQualityOption('),
            method((moo / 'deferred_decals_manager.cpp').read_text(), 'void DecalsManager::setQualityOption(int option)')]),
        '@PIPELINE@': method((moo / 'deferred_pipeline.cpp').read_text(),
                             'void DeferredPipeline::onGraphicsOptionSelected(EGraphicsSetting setting, int option)'),
        '@RENDERER_OWNER@': renderer[renderer.index('namespace\n{'):renderer.index('//-- define singleton.')],
        '@RENDERER_CALLBACK@': method(renderer_header[callback:], 'template<IRendererPipeline::EGraphicsSetting setting>'),
        '@RENDERER@': method(renderer, 'bool Renderer::init(bool isAssetProcessor, bool supportDS)') + '\n' +
                      method(renderer, 'void Renderer::initGraphicsSettings()'),
    }
    host = STARTUP_HOST
    for key, value in replacements.items():
        host = host.replace(key, value)
    return host


@contextlib.contextmanager
def run_startup_host(code):
    with tempfile.TemporaryDirectory(prefix='startup-transfer-') as directory:
        directory = Path(directory)
        (directory / 'host.cpp').write_text(code)
        # Public registry bodies carry their upstream reorder/sign-compare warnings.
        built = subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', '-Wno-unused-parameter',
                                '-Wno-reorder', '-Wno-sign-compare', '-Wno-unused-function',
                                '-I', str(ROOT / 'include'), str(directory / 'host.cpp'), '-o', str(directory / 'host')],
                               capture_output=True, text=True)
        if built.returncode:
            raise AssertionError(built.stderr[-6000:])

        def run(*arguments):
            result = subprocess.run([str(directory / 'host')] + [str(value) for value in arguments],
                                    capture_output=True, text=True)
            return result.returncode, result.stdout
        yield run


class DeferredStartupLifecycleTests(unittest.TestCase):
    def test_space_decal_owner_activates_only_from_device_callbacks(self):
        source = (ROOT / 'bigworld/stock_space_decals.cpp').read_text()
        header = (ROOT / 'bigworld/stock_space_decals.hpp').read_text()
        result = run_host(space_decal_host(source, header), 'deferred-decal-lifecycle-')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('lifecycle passed', result.stdout)

    def test_patched_deferred_init_chain_creates_no_device_resource(self):
        if not ARCHIVE.exists(): self.skipTest('Pristine public-engine archive is required')
        with tempfile.TemporaryDirectory(prefix='deferred-init-chain-') as directory:
            moo = startup_engine(Path(directory)) / 'moo'
            renderer = (moo / 'renderer.cpp').read_text()
            pipeline = '\n'.join([
                method((moo / 'deferred_pipeline.cpp').read_text(), 'bool DeferredPipeline::init()'),
                method((moo / 'deferred_decals_manager.cpp').read_text(), 'bool DecalsManager::init()'),
                method((moo / 'deferred_lights_manager.cpp').read_text(), 'bool LightsManager::init()')])
        bigworld = ROOT / 'bigworld'
        production = '\n'.join([
            method((bigworld / 'stock_vehicle_traces.cpp').read_text(), 'bool initialiseStockVehicleTraceRenderer()'),
            method((bigworld / 'stock_dynamic_decals.cpp').read_text(), 'bool initialiseStockDynamicDecals()'),
            method((bigworld / 'stock_deferred_buffers.cpp').read_text(), 'bool StockDeferredBuffers::initialise()'),
            method((bigworld / 'stock_local_lighting.cpp').read_text(), 'bool StockLocalLighting::initialise()')])
        host = (PIPELINE_HOST
                .replace('@RENDERER@', method(renderer, 'bool Renderer::init(bool isAssetProcessor, bool supportDS)') + '\n' +
                         method(renderer, 'IRendererPipeline::EType Renderer::pipelineType() const'))
                .replace('@PRODUCTION@', production).replace('@PIPELINE@', pipeline))
        result = run_host(host, 'deferred-init-chain-host-')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('precedes every device owner', result.stdout)

    def test_restored_options_reach_pipeline_owners_and_shader_macros(self):
        if not ARCHIVE.exists(): self.skipTest('Pristine public-engine archive is required')
        with tempfile.TemporaryDirectory(prefix='startup-transfer-engine-') as directory:
            host = startup_host(startup_engine(Path(directory)))

        def lines(output):
            return dict(entry.split(' ', 1) for entry in output.splitlines() if ' ' in entry)

        def deferred(registry, shadows, decals, lighting, calls):
            # Real ShadowManager/SSAOSupport tables: OFF disables, LIGHTING < 3 enables SSAO.
            samples = (4, 3, 2, 1, 1)[lighting]
            return ('registry={} shadow={},{},{} decals={},{},{},{} ssao={},{} lighting={} calls={} errors=0'
                    .format(registry, shadows, int(shadows != 4), shadows, decals, decals, decals, decals,
                            int(lighting < 3), samples, lighting, calls))

        with run_startup_host(host) as run:
            # Matching CDB evidence: restored 4/4/4 with RENDER_PIPELINE 0. Each value 0..4 appears
            # once per setting across the remaining rows.
            for shadows, decals, lighting in ((4, 4, 4), (0, 1, 2), (1, 2, 3), (2, 3, 4), (3, 4, 0), (4, 0, 1)):
                code, output = run(1, 1, 0, shadows, decals, lighting, 0)
                self.assertEqual(code, 0, output)
                seen = lines(output)
                registry = '0,{},{},{}'.format(shadows, decals, lighting)
                self.assertEqual(seen['early'], 'calls=0;2;')
                self.assertEqual(seen['renderer'], 'result=1 type=0 pipelines=1')
                self.assertEqual(seen['init'], deferred(registry, 0, 0, 0, '1,1,1'), '#1513 93fef0 transfers option 0')
                self.assertEqual(seen['setup'], 'compiled=10/110')
                self.assertEqual(seen['custom'], seen['init'])
                self.assertEqual(seen['apply'], deferred(registry, shadows, decals, lighting, '2,2,2'))
                self.assertEqual(seen['applied'], 'compiled=10/110')
                self.assertEqual(seen['change'], deferred('0,4,{},{}'.format(decals, lighting), 4, decals, lighting, '3,2,2'))
                self.assertEqual(seen['switched'], 'pipeline=1 compiled=10/110')
            # Restored forward: the forward pipeline has no quality owners; macros still match the 11 permutation.
            code, output = run(1, 1, 1, 4, 4, 4, 0)
            self.assertEqual(code, 0, output)
            seen = lines(output)
            self.assertEqual(seen['renderer'], 'result=1 type=1 pipelines=1')
            self.assertEqual(seen['init'], 'registry=1,4,4,4 errors=0')
            self.assertEqual(seen['setup'], 'compiled=11/000')
            self.assertEqual(seen['applied'], 'compiled=11/000')
            self.assertEqual(seen['switched'], 'pipeline=0 compiled=11/000')
            # No preference file: public registration callbacks select first supported options.
            code, output = run(0, 1, 0, 0, 0, 0, 0)
            self.assertEqual(code, 0, output)
            seen = lines(output)
            self.assertEqual(seen['early'], 'calls=0;')
            self.assertEqual(seen['init'], deferred('0,0,0,0', 0, 0, 0, '1,1,1'))
            self.assertEqual(seen['setup'], 'compiled=10/110')
            self.assertEqual(seen['apply'], seen['init'])
            code, output = run(0, 0, 0, 0, 0, 0, 0)
            self.assertEqual(code, 0, output)
            seen = lines(output)
            self.assertEqual(seen['init'], 'registry=1,4,4,4 errors=0')
            self.assertEqual(seen['setup'], 'compiled=11/000')
            # Restored DEFERRED on hardware without deferred support: 93fd7e forces FORWARD.
            code, output = run(1, 0, 0, 2, 2, 2, 0)
            self.assertEqual(code, 0, output)
            seen = lines(output)
            self.assertEqual(seen['renderer'], 'result=1 type=1 pipelines=1')
            self.assertEqual(seen['init'], 'registry=1,2,2,2 errors=0')
            self.assertEqual(seen['setup'], 'compiled=11/000')
            # 91ce80 still restores the saved option; this session keeps its FORWARD permutation.
            self.assertEqual(seen['apply'], 'registry=0,2,2,2 errors=0')
            self.assertEqual(seen['applied'], 'compiled=11/000')
            # A failed pipeline initialisation forwards no option and stops startup.
            code, output = run(1, 1, 0, 4, 4, 4, 1)
            self.assertEqual(code, 0, output)
            seen = lines(output)
            self.assertEqual(seen['renderer'], 'result=0 type=0 pipelines=1')
            self.assertEqual(seen['init'], 'registry=0,4,4,4 errors=0')
            self.assertNotIn('setup', seen)


if __name__ == '__main__':
    unittest.main()
