"""Run the real disabled-bloom helpers and generated HDR lifecycle on a host."""
import os
from pathlib import Path
import shutil
import struct
import sys
import subprocess
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from test_stock_hdr import FILES, ROOT, patch, public_sources


LEGACY_TONEMAPPING = """void HDRSupport::doTonemapping()
{
    BW_GUARD;
    BW_SCOPED_DOG_WATCHER("tone mapping");
    MF_ASSERT( m_bloomMaps.size() );

    if (!drawStockToneMapping(m_toneMappingMat, g_cfg, m_finalAvgLumRT.get(), m_hdrRT.get(),
            m_bloomMaps[0].second.get(), m_stockColorCorrection))
        ERROR_MSG("HDR: original tone mapping failed\\n");
}
"""


def legacy_cached(values):
    """Reconstruct the previous cached patch from its exact changed anchors."""
    values = dict(values)
    header = 'lib/romp/hdr_support.hpp'
    support = 'lib/romp/hdr_support.cpp'
    values[header] = values[header].replace('\tMoo::BaseTexturePtr\t\t\t\tm_stockDisabledBloom;\n', '')
    values[support] = values[support].replace(
        '    m_stockDisabledBloom = loadStockDisabledBloom();\n'
        '    success &= bool(m_stockDisabledBloom);\n', '').replace(
        '    m_stockDisabledBloom  = NULL;\n', '').replace(
        '        // ToneMapping has no bloom owner; its finite black input is managed separately.\n',
        '        calcBloom();\n').replace(
        '            m_stockDisabledBloom, m_stockColorCorrection))',
        '            m_bloomMaps[0].second.get(), m_stockColorCorrection))')
    current = patch.function(values[support], 'void HDRSupport::doTonemapping(')
    values[support] = values[support].replace(current, LEGACY_TONEMAPPING.rstrip('\n'))
    return values


def host_source(patched, cached, mode):
    actual = (ROOT / 'bigworld/stock_hdr.cpp').read_text()
    owners = '\n'.join(patch.function(actual, signature) for signature in (
        'bool setOptionalVector(', 'int stockActiveToneMapping(',
        'Moo::BaseTexturePtr loadStockDisabledBloom(', 'bool drawStockToneMapping('))
    support = patched['lib/romp/hdr_support.cpp']
    lifecycle = '\n'.join(patch.function(support, signature) for signature in (
        'void HDRSupport::createManagedObjects(', 'void HDRSupport::deleteManagedObjects(',
        'void HDRSupport::resolve('))
    old = patch.function(cached['lib/romp/hdr_support.cpp'], 'void HDRSupport::resolve(')
    old = old.replace('HDRSupport::resolve()', 'HDRSupport::resolveOld()')
    if mode == 'missing-guard':
        owners = owners.replace('!disabledBloom->pTexture() ||', 'false ||')
    elif mode == 'missing-status':
        owners = owners.replace(' || texture->status() != Moo::BaseTexture::STATUS_READY', '')
    elif mode == 'missing-release':
        lifecycle = lifecycle.replace('    m_stockDisabledBloom  = NULL;', '')
    elif mode == 'retained-bloom':
        lifecycle = lifecycle.replace(
            '// ToneMapping has no bloom owner; its finite black input is managed separately.', 'calcBloom();')
    return HOST.replace('@OWNERS@', owners).replace('@LIFECYCLE@', lifecycle + '\n' + old)


HOST = r'''
#include "wot/stock_hdr_tonemapping.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
using HRESULT = int;
using D3DXHANDLE = const char*;
using uint = unsigned;
#define SUCCEEDED(result) ((result) >= 0)
#define FALSE 0
#define BW_GUARD
#define BW_SCOPED_DOG_WATCHER(name)
#define BW_SCOPED_RENDER_PERF_MARKER(name)
#define MF_ASSERT(value) require(bool(value), "managed HDR resource admission failed")
int errors = 0;
#define ERROR_MSG(...) (++errors)
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::vector<std::string> lifetime, calls;
struct Vector4 {
    float x, y, z, w;
    Vector4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
    float operator[](int i) const { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; }
};
struct NativeTexture { std::array<float, 3> rgb{{0, 0, 0}}; };
struct ID3DXEffect {
    std::map<std::string, NativeTexture*> textures;
    std::map<std::string, Vector4> vectors;
    std::map<std::string, int> integers;
    int writes = 0, invalidWrites = 0;
    HRESULT SetTexture(const char* name, NativeTexture* value) {
        ++writes;
        if (!value) { ++invalidWrites; return -1; }
        textures[name] = value; return 0;
    }
    HRESULT SetVector(const char* name, const Vector4* value) { ++writes; vectors.insert_or_assign(name, *value); return 0; }
    HRESULT SetBool(const char* name, int value) { ++writes; integers[name] = value; return 0; }
    HRESULT SetInt(const char* name, int value) { ++writes; integers[name] = value; return 0; }
    D3DXHANDLE GetParameterByName(void*, const char* name) { return name; }
};
struct HDRSettings {
    struct ToneMapping { float m_middleGray = .5f, m_whitePoint = 5.f, m_blackPoint = .15f; };
    ToneMapping m_tonemappings[3]; int m_activeToneMapping = 1;
};
namespace BWResource {
    bool exists = true;
    bool fileExists(const char* name) {
        require(std::string(name) == "system/maps/col_black.dds", "wrong black resource identity"); return exists;
    }
}
namespace Moo {
struct BaseTexture {
    enum { STATUS_LOADING, STATUS_READY, STATUS_FAILED };
    NativeTexture native;
    bool valid = true;
    int state = STATUS_READY;
    ~BaseTexture() { lifetime.push_back("texture"); }
    NativeTexture* pTexture() { return valid ? &native : nullptr; }
    int status() const { return state; }
};
using BaseTexturePtr = std::shared_ptr<BaseTexture>;
struct TextureManager {
    BaseTexturePtr next;
    int requests = 0;
    static TextureManager* instance() { static TextureManager manager; return &manager; }
    BaseTexturePtr get(const char* name, bool allowAnimation, bool mustExist, bool load, const char* group) {
        require(std::string(name) == "system/maps/col_black.dds" && allowAnimation && !mustExist && load &&
                std::string(group) == "Art/File Textures/", "black texture load allowed substitution");
        ++requests; return std::exchange(next, BaseTexturePtr());
    }
};
struct RenderTarget {
    NativeTexture native;
    NativeTexture* pTexture() { return &native; }
    void pop() { calls.push_back("pop"); }
};
struct Effect { ID3DXEffect binding; ID3DXEffect* pEffect() { return &binding; } };
struct Properties { bool select(Effect&) { return true; } };
struct EffectMaterial {
    Effect effect; Properties props;
    ~EffectMaterial() { lifetime.push_back("effect"); }
    Effect* pEffect() { return &effect; }
    Properties& properties() { return props; }
};
using EffectMaterialPtr = std::shared_ptr<EffectMaterial>;
struct EffectVisualContext { enum { AUTO_CONSTANT_TYPE_PER_FRAME }; };
struct VisualContext {
    template<class T> void registerAutoConstant(int, const char*, T* value) { delete value; }
    void unregisterAutoConstant(int, const char*) {}
};
struct Quad {
    int draws = 0;
    std::array<float, 3> output;
    bool drawChecked(EffectMaterial& material, uint, uint) {
        ++draws;
        auto& effect = material.effect.binding;
        auto* hdr = effect.textures.at("g_HDRMap");
        auto* bloom = effect.textures.at("g_bloomMap");
        const auto& tint = effect.vectors.at("g_bloomTint");
        // CPU model of the disabled-bloom MAD only, not a GPU image oracle.
        for (int channel = 0; channel < 3; ++channel)
            output[channel] = hdr->rgb[channel] + bloom->rgb[channel] * tint[channel];
        return true;
    }
};
struct Context {
    Quad quad; VisualContext visual;
    Quad& fsQuad() { return quad; }
    VisualContext& effectVisualContext() { return visual; }
    uint screenWidth() { return 1920; } uint screenHeight() { return 1080; }
};
Context& rc() { static Context context; return context; }
}
bool setStockHdrTarget(ID3DXEffect* effect, const char* name, Moo::RenderTarget* target) {
    // The independent HDR target projection is intentionally non-identity.
    Vector4 drr(.5f, .5f, 0.f, 0.f);
    return SUCCEEDED(effect->SetTexture(name, target->pTexture())) &&
        SUCCEEDED(effect->SetVector((std::string(name) + "DRRParams").c_str(), &drr));
}
@OWNERS@
bool createEffect(Moo::EffectMaterialPtr& material, const char*) {
    material = std::make_shared<Moo::EffectMaterial>(); return true;
}
bool g_readbackAvgLumToCPU = false;
float g_avgLum = 0;
struct HDRSupport {
    Moo::EffectMaterialPtr m_toneMappingMat, m_averageLuminanceMat, m_bloomMat, m_gaussianBlurMat;
    Moo::BaseTexturePtr m_stockDisabledBloom;
    std::shared_ptr<Moo::RenderTarget> m_hdrRT = std::make_shared<Moo::RenderTarget>();
    bool m_successed = true, m_update = true, m_valid = true, enabled = true;
    bool enable() const { return enabled; }
    void calcAvgLum() { calls.push_back("average"); }
    void calcAvgLumOnGPU() { calls.push_back("gpu-average"); }
    float readAvgLum() { calls.push_back("readback"); return .25f; }
    void calcBloom() { calls.push_back("bloom"); }
    void doTonemapping() { calls.push_back("tone"); }
    void createManagedObjects(); void deleteManagedObjects(); void resolve(); void resolveOld();
};
struct HDRParamsConstant { explicit HDRParamsConstant(HDRSupport&) {} };
struct GammaCorrectionConstant { explicit GammaCorrectionConstant(HDRSupport&) {} };
@LIFECYCLE@
void provide(int status = Moo::BaseTexture::STATUS_READY, bool native = true) {
    auto texture = std::make_shared<Moo::BaseTexture>(); texture->state = status; texture->valid = native;
    Moo::TextureManager::instance()->next = texture;
}
int main() {
    try {
        auto* manager = Moo::TextureManager::instance();
        BWResource::exists = false; const int before = manager->requests;
        require(!loadStockDisabledBloom() && manager->requests == before, "missing black resource requested fallback");
        BWResource::exists = true;
        require(!loadStockDisabledBloom(), "null black texture was admitted");
        for (int status : {Moo::BaseTexture::STATUS_LOADING, Moo::BaseTexture::STATUS_FAILED}) {
            provide(status);
            require(!loadStockDisabledBloom(), "unready black texture was admitted");
        }
        provide(Moo::BaseTexture::STATUS_READY, false);
        require(!loadStockDisabledBloom(), "missing native black texture was admitted");
        require(errors == 5, "black texture failures did not report local errors");
        provide(); auto black = loadStockDisabledBloom();
        require(bool(black) && black->pTexture(), "valid original black texture was rejected");
        for (float channel : black->native.rgb) require(channel == 0 && std::isfinite(channel), "black input was not finite zero");
        auto correction = std::make_shared<Moo::BaseTexture>();
        auto material = std::make_shared<Moo::EffectMaterial>();
        Moo::RenderTarget average, hdr;
        hdr.native.rgb = {{.01f, 100.f, 65504.f}};
        HDRSettings settings;
        for (int active : {0, 1, 2}) {
            settings.m_activeToneMapping = active;
            require(drawStockToneMapping(material, settings, &average, &hdr, black, correction), "valid tone mapping failed");
            auto& binding = material->effect.binding;
            require(binding.textures.at("g_bloomMap") == black->pTexture(), "tone mapping did not bind managed black input");
            const auto& projection = binding.vectors.at("g_bloomMapDRRParams");
            require(projection.x == 1 && projection.y == 1 && projection.z == 0 && projection.w == 0,
                    "disabled bloom inherited HDR dynamic-resolution projection");
            require(binding.integers.at("g_toneMappingFilter") == active && !binding.integers.at("g_enableLensFlare") &&
                    !binding.integers.at("g_enableGodRays"), "unrelated tone mapping parameters changed");
            const auto output = Moo::rc().quad.output;
            NativeTexture oldBloom; oldBloom.rgb = {{10000.f, .125f, -35.f}};
            binding.textures["g_bloomMap"] = &oldBloom;
            Moo::rc().quad.drawChecked(*material, 1920, 1080);
            require(Moo::rc().quad.output == output && output == hdr.native.rgb,
                    "finite disabled-bloom MAD changed CPU sample output");
            oldBloom.rgb[0] = std::numeric_limits<float>::quiet_NaN();
            Moo::rc().quad.drawChecked(*material, 1920, 1080);
            require(std::isnan(Moo::rc().quad.output[0]) && std::isfinite(output[0]),
                    "CPU model failed to distinguish zero tint from finite input");
        }
        // Neither unavailable owner nor unavailable native input may cause a Set or draw.
        for (bool absent : {true, false}) {
            auto unavailable = absent ? Moo::BaseTexturePtr() : std::make_shared<Moo::BaseTexture>();
            if (unavailable) unavailable->valid = false;
            const int writes = material->effect.binding.writes, draws = Moo::rc().quad.draws;
            require(!drawStockToneMapping(material, settings, &average, &hdr, unavailable, correction),
                    "missing disabled bloom input was drawn");
            require(material->effect.binding.writes == writes && Moo::rc().quad.draws == draws &&
                    material->effect.binding.invalidWrites == 0, "missing black input caused invalid effect writes");
        }
        {
            HDRSupport support;
            provide(); std::weak_ptr<Moo::BaseTexture> lifetimeProbe = manager->next;
            support.createManagedObjects();
            require(!lifetimeProbe.expired() && support.m_stockDisabledBloom,
                    "managed create failed to retain black texture owner");
            lifetime.clear(); support.deleteManagedObjects();
            require(lifetimeProbe.expired(), "managed delete retained disabled bloom texture");
            require(lifetime == std::vector<std::string>({"effect", "effect", "effect", "effect", "texture"}),
                    "black texture released before managed effects");
            support.deleteManagedObjects();
        }
        {
            HDRSupport support; BWResource::exists = false; bool rejected = false;
            try { support.createManagedObjects(); } catch (const std::runtime_error&) { rejected = true; }
            require(rejected && !support.m_stockDisabledBloom, "managed create admitted unavailable black resource");
            support.deleteManagedObjects(); BWResource::exists = true;
        }
        for (bool update : {false, true}) for (bool readback : {false, true}) {
            HDRSupport oldSupport, support; oldSupport.m_update = support.m_update = update;
            g_readbackAvgLumToCPU = readback;
            calls.clear(); oldSupport.resolveOld(); const auto oldCalls = calls;
            std::vector<std::string> expected{"pop"};
            if (update) expected.push_back("average");
            expected.push_back("gpu-average");
            if (readback) expected.push_back("readback");
            expected.push_back("bloom"); expected.push_back("tone");
            require(oldCalls == expected, "old resolve negative control did not schedule bloom");
            calls.clear(); support.resolve(); expected.erase(expected.end() - 2);
            require(calls == expected, "disabled bloom resolve changed retained pass order");
            calls.clear(); support.resolve(); require(calls.empty(), "resolve repeated a consumed HDR frame");
        }
        std::puts("Actual disabled bloom helpers, managed lifecycle and resolve scheduling passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what()); return 1;
    }
}
'''


class DisabledBloomTests(unittest.TestCase):
    def test_actual_helpers_lifecycle_and_resolve(self):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('Host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='hdr-disabled-bloom-') as directory:
            root = Path(directory)
            public_sources(self, root)
            patch.patch(root)
            patched = {name: (root / name).read_text() for name in FILES}
            cached = legacy_cached(patched)
            for mode, failure in (
                    ('after', None), ('missing-guard', 'missing black input caused invalid effect writes'),
                    ('missing-status', 'unready black texture was admitted'),
                    ('missing-release', 'managed delete retained disabled bloom texture'),
                    ('retained-bloom', 'disabled bloom resolve changed retained pass order')):
                with self.subTest(mode=mode):
                    source, binary = root / (mode + '.cpp'), root / (mode + '.exe')
                    source.write_text(host_source(patched, cached, mode))
                    built = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                            '-UNDEBUG', *native_test_compile_flags(compiler),
                                            '-I', str(ROOT / 'include'), str(source), '-o', str(binary)],
                                           capture_output=True, text=True, timeout=60)
                    self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
                    if mode == 'after':
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    else:
                        self.assertNotEqual(result.returncode, 0, mode + ' negative control passed')
                        if failure:
                            self.assertIn(failure, result.stderr)

    def test_patch_fresh_cached_idempotence_and_retained_downsample_targets(self):
        with tempfile.TemporaryDirectory(prefix='hdr-disabled-bloom-patch-') as directory:
            root = Path(directory)
            public_sources(self, root)
            original = {name: (root / name).read_text() for name in FILES}
            patch.patch(root)
            fresh = {name: (root / name).read_text() for name in FILES}
            patch.patch(root)
            self.assertEqual(fresh, {name: (root / name).read_text() for name in FILES})
            cached = legacy_cached(fresh)
            for name, value in cached.items():
                (root / name).write_text(value)
            patch.patch(root)
            self.assertEqual(fresh, {name: (root / name).read_text() for name in FILES})
            patch.patch(root)
            self.assertEqual(fresh, {name: (root / name).read_text() for name in FILES})
            support = 'lib/romp/hdr_support.cpp'
            for signature in ('void HDRSupport::calcAvgLum(', 'void HDRSupport::calcAvgLumOnGPU(',
                              'void HDRSupport::createUnmanagedObjects('):
                self.assertEqual(patch.function(fresh[support], signature),
                                 patch.function(cached[support], signature))
            self.assertIn('m_bloomMaps[0].first.get()', patch.function(fresh[support], 'void HDRSupport::calcAvgLum('))
            self.assertEqual(fresh['lib/romp/hdr_support.hpp'].count('m_stockDisabledBloom;'), 1)
            # Exposure histories are now explicitly initialized. The other
            # target allocations, including the downsample chain, stay intact.
            creation = patch.function(fresh[support], 'void HDRSupport::createUnmanagedObjects(')
            start = creation.index('\n    // #1513 a72669..a726b3')
            end = creation.index('\n    MF_ASSERT(success', start)
            self.assertEqual(creation[:start] + creation[end:],
                             patch.function(original[support], 'void HDRSupport::createUnmanagedObjects('))
            # patch_stock_antialiasing inserts its resolve immediately after this anchor.
            self.assertEqual(patch.function(fresh[support], 'void HDRSupport::resolve(').count(
                '        calcAvgLumOnGPU();'), 1)
            copied = root / 'lib/stock_hdr.hpp'
            self.assertEqual(copied.read_bytes(), (ROOT / 'bigworld/stock_hdr.hpp').read_bytes())

    def test_exact_black_resource_all_mips_are_finite_rgb_zero(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Exact #1513 original resources are required')
        sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
        from prepare_native_client import OriginalResources
        data = OriginalResources(client).read('system/maps/col_black.dds')
        self.assertEqual(data[:4], b'DDS ')
        header = struct.unpack_from('<31I', data, 4)
        self.assertEqual((header[0], header[2], header[3], header[6]), (124, 16, 16, 5))
        self.assertEqual(header[18:26],
                         (32, 0x41, 0, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000))
        pixels = sum((16 >> mip) ** 2 for mip in range(5))
        self.assertEqual(pixels, 341)
        self.assertEqual(len(data), 128 + pixels * 4)
        self.assertEqual(struct.unpack_from('<%dI' % pixels, data, 128), (0xff000000,) * pixels)
        # ARGB8 UNORM RGB bytes are exact finite zero at every mip; alpha is one.


if __name__ == '__main__':
    unittest.main()
