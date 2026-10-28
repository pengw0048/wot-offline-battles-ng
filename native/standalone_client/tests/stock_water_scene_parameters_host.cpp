// Patched Water::setupRenderState with recording fakes; @...@ markers are filled by the test.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#define BW_GUARD_PROFILER(name)
#define MF_ASSERT(condition) assert(condition)
#define MF_WATCH(...) (void)0
#define EDITOR_ENABLED 0
#define WATER_REFLECTION_FADE_ENABLED 1
typedef unsigned uint;
struct Vector3 { float x = 0, y = 0, z = 0; Vector3() {} Vector3(float a, float b, float c) : x(a), y(b), z(c) {}
    Vector3 operator-(const Vector3& o) const { return Vector3(x - o.x, y - o.y, z - o.z); }
    float length() const { return std::sqrt(x * x + y * y + z * z); } };
struct Vector2 { float x = 0, y = 0; float length() const { return std::sqrt(x * x + y * y); } };
struct Vector4 { float x = 0, y = 0, z = 0, w = 0; Vector4() {} Vector4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
    float& operator[](uint i) { return (&x)[i]; } };
struct Matrix { Vector4 row(int) const { return Vector4(); } };
namespace Math {
template<class T> T clamp(T low, T value, T high) { return value < low ? low : (value > high ? high : value); }
inline float lerp(float t, float a, float b) { return a + (b - a) * t; }
inline float lerp(float x, float x0, float x1, float y0, float y1) { return y0 + (y1 - y0) * (x - x0) / (x1 - x0); }
}
namespace Watcher { enum { WT_READ_WRITE }; }
struct IRendererPipeline { enum EType { TYPE_DEFERRED_SHADING, TYPE_FORWARD_SHADING }; };
struct Renderer { IRendererPipeline::EType type = IRendererPipeline::TYPE_DEFERRED_SHADING;
    static Renderer& instance() { static Renderer value; return value; }
    IRendererPipeline::EType pipelineType() const { return type; } };
namespace Moo {
enum { CONSTANTS_PER_VIEW };
struct EffectVisualContext { void initConstants() {} void updateSharedConstants(int) {} };
struct RenderContext { EffectVisualContext context; Matrix view, worldMatrix;
    EffectVisualContext& effectVisualContext() { return context; } const Matrix& world() { return worldMatrix; }
    float screenWidth() { return 640.f; } float screenHeight() { return 480.f; }
    const Matrix& invView() { return view; } void setFVF(unsigned) {} };
inline RenderContext& rc() { static RenderContext value; return value; }
}
struct Texture { void* pTexture() { return this; } bool exists() const { return true; } Texture* operator->() { return this; } };
// Records every published effect parameter, as the real EffectParameterCache does per effect.
struct ParameterCache {
    std::map<std::string, float> floats; std::map<std::string, Vector4> vectors;
    bool hasEffect() { return true; } void effect(void*) {}
    void setMatrix(const char*, const Matrix*) {}
    void setFloat(const char* name, float value) { floats[name] = value; }
    void setVector(const char* name, const Vector4* value) { vectors[name] = *value; }
    void setBool(const char*, bool) {} void setTexture(const char*, void*) {}
};
struct Effect { bool hasObject() const { return true; } Effect* operator->() { return this; }
    bool checkEffectRecompiled() { return false; } Effect* pEffect() { return this; } };
struct WaterScene { bool underWater = false; bool eyeUnderWater() const { return underWater; } Texture* reflectionTexture() { return nullptr; } };
struct WaterSceneRenderer { static bool s_simpleScene_; };
bool WaterSceneRenderer::s_simpleScene_ = false;
struct Waters {
    static Effect s_effect_; static bool s_highQuality_;
    static Waters& instance() { static Waters value; return value; }
    void selectTechnique() {} float time() const { return 2.f; } bool insideVolume() const { return false; } float rainAmount() const { return 0.f; }
};
Effect Waters::s_effect_; bool Waters::s_highQuality_ = false;
struct VertexBuffer { void activate() {} VertexBuffer* operator->() { return this; } };
struct VERTEX_TYPE { static unsigned fvf() { return 0; } };
struct RenderTarget { void* pTexture() { return this; } RenderTarget* get() { return this; } RenderTarget* operator->() { return this; } };

class Water
{
public:
    enum { INSIDE_ONLY = 2 };
    struct WaterState {
        Vector2 size_, scrollSpeed1_, scrollSpeed2_, waveScale_;
        Vector4 deepColour_, reflectionTint_, refractionTint_, forwardWaterParameters_;
        float depth_ = 2.f, fadeDepth_ = 1.f, deepColourScale_ = 1.f, foamIntersection_ = 0, foamMultiplier_ = 0, foamTiling_ = 0,
            foamFrequency_ = 0, foamWidth_ = 0, foamAmplitude_ = 0, causticsPower = 0, refractionPower_ = 0, waveHeight = 0,
            windVelocity_ = 0, reflectionScale_ = 0, refractionScale_ = 0, shallowWaterBiasForward_ = 0, freqX = 0, freqZ = 0,
            textureTessellation_ = 0, sunPower_ = 0, sunScale_ = 0, sunScaleDeferred_ = 0, smoothness_ = 0, animationSpeed_ = 1.f;
        bool bypassDepth_ = false, useShadows_ = false; int visibility_ = 0, waveTextureNumber_ = 1;
    };
    static float s_fTexScale, s_fFreqX, s_fFreqZ, s_fBankLineInterpolationFactor, s_sceneCullDistance_, s_sceneCullFadeDistance_;
    void setupRenderState(float dTime);
    ParameterCache cache; ParameterCache* paramCache_ = &cache;
    WaterState state_; WaterScene scene; WaterScene* waterScene_ = &scene;
    Texture* foamTexture_ = nullptr; Texture* screenFadeTexture_ = nullptr; Texture* reflectionCubeMap_ = nullptr;
    Texture* normalTextures_ = nullptr; VertexBuffer buffer; VertexBuffer* vertexBuffers_[1] = {&buffer};
    float animTime_ = 0.f, simpleReflection_ = 0.f;
    static RenderTarget s_rt;
    void copyRT(RenderTarget*, int) {}
    Vector3 position() const { return Vector3(); } Vector2 size() const { return state_.size_; }
};
RenderTarget Water::s_rt;
float Water::s_fBankLineInterpolationFactor, Water::s_sceneCullDistance_ = 300.f, Water::s_sceneCullFadeDistance_ = 30.f;
@STATICS@
@SETUP@
@PRODUCTION@

float published(Water& water, IRendererPipeline::EType type, const char* name)
{
    Renderer::instance().type = type;
    water.cache.floats.clear();
    water.setupRenderState(0.f);
    assert(water.cache.floats.count(name) == 1);
    return water.cache.floats[name];
}

int main(int argc, char** argv)
{
    Water water;
    water.state_.freqX = 1.7f; water.state_.freqZ = 2.2f;
    // Before the first battle: the public defaults 0.5 / 1 / 1.
    assert(Water::s_fTexScale == 0.5f && Water::s_fFreqX == 1.f && Water::s_fFreqZ == 1.f);
    assert(published(water, IRendererPipeline::TYPE_DEFERRED_SHADING, "texScale") == 0.5f);
    for (int index = 1; index + 2 < argc; index += 3)
    {
        const float texScale = std::strtof(argv[index], nullptr), freqX = std::strtof(argv[index + 1], nullptr),
            freqZ = std::strtof(argv[index + 2], nullptr);
        wg_setWaterTexScale(texScale); wg_setWaterFreqX(freqX); wg_setWaterFreqZ(freqZ);
        assert(Water::s_fTexScale == texScale && Water::s_fFreqX == freqX && Water::s_fFreqZ == freqZ);
        for (auto type : {IRendererPipeline::TYPE_DEFERRED_SHADING, IRendererPipeline::TYPE_FORWARD_SHADING})
        {
            assert(published(water, type, "texScale") == texScale);
            // The shader frequencies remain the surface's authored BWWa2 values.
            assert(published(water, type, "freqX") == 1.7f && published(water, type, "freqZ") == 2.2f);
            assert(published(water, type, "textureTesselation") == water.state_.textureTessellation_);
        }
        // Space teardown has no writer: the value stays until the next battle entry.
        Water next;
        assert(published(next, IRendererPipeline::TYPE_FORWARD_SHADING, "texScale") == texScale);
        std::printf("map %g %g %g published\n", texScale, freqX, freqZ);
    }
    std::puts("water scene parameters passed");
}
