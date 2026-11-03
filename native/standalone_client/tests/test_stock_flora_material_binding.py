"""Exercise public Flora draw bodies with the actual private Moo material selector."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from test_stock_cursor_camera import function

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('flora_effect_owners', ROOT / 'bigworld/patch_stock_effect_owners.py')
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class FloraMaterialBindingTests(unittest.TestCase):
    def test_runtime_parameters_reach_the_draw_binding_after_begin_and_reload(self):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        source_root = os.environ.get('WOT_PUBLIC_ENGINE_SOURCE')
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if (not archive.is_file() and not source_root) or not compiler:
            self.skipTest('Pinned public SDK and host compiler required')
        def read(suffix):
            if source_root:
                return (Path(source_root) / suffix.lstrip('/')).read_text(encoding='utf-8-sig')
            with tarfile.open(archive) as sdk:
                name = next(name for name in sdk.getnames() if name.endswith(suffix))
                return sdk.extractfile(name).read().decode('utf-8-sig')
        flora = read('/lib/romp/flora_renderer.cpp')
        sky = read('/lib/romp/sky_gradient_dome.cpp')
        managed = PATCH.patch_managed_lifecycle(read('/lib/moo/managed_effect.cpp'))
        begin = function(read('/lib/moo/effect_material.cpp'), 'bool EffectMaterial::begin() const')
        with tempfile.TemporaryDirectory(prefix='flora-material-binding-') as temp:
            folder = Path(temp)
            (folder / 'lib/romp').mkdir(parents=True)
            (folder / 'lib/romp/flora_renderer.cpp').write_text(flora)
            (folder / 'lib/romp/sky_gradient_dome.cpp').write_text(sky)
            PATCH.patch_parameter_caches(folder)
            patched = (folder / 'lib/romp/flora_renderer.cpp').read_text()
            patched_sky = (folder / 'lib/romp/sky_gradient_dome.cpp').read_text()
            PATCH.patch_parameter_caches(folder)  # Cached SDK preparations remain idempotent.
            self.assertEqual(patched, (folder / 'lib/romp/flora_renderer.cpp').read_text())
            sky_draw = function(patched_sky, 'void SkyGradientDome::draw(')
            self.assertLess(sky_draw.index('properties().select'), sky_draw.index('parameters_.setTexture'))
            self.assertLess(sky_draw.index('parameters_.effect', sky_draw.index('properties().select')),
                            sky_draw.index('parameters_.setTexture'))
            methods = '\n'.join(function(managed, signature) for signature in (
                'bool ManagedEffect::begin(', 'bool ManagedEffect::beginPass(',
                'bool ManagedEffect::endPass()', 'bool ManagedEffect::end()',
                'bool ManagedEffect::commitChanges()'))
            # Reuse the guarded Moo selector fixture, ending before its tree-only owners.
            harness = (ROOT / 'tests/stock_tree_material_binding_host.hpp').read_text().split('using EffectMaterialPtr=')[0]
            harness = harness.replace('@INSTANCE@', 'struct TreeInstanceConstants { float values[4]; };')
            harness = harness.replace('@PROPERTIES@', PATCH.PROPERTIES)
            harness = harness.replace('@MANAGED@', PATCH.SELECT_BINDING + methods).replace('@BEGIN@', begin)
            harness = harness.replace('void checkEffectRecompiled(){}', 'bool checkEffectRecompiled(){return false;}')
            harness = harness.replace('    HRESULT SetFloat(const char *, float) { ++writes; return 0; }',
                '    std::map<std::string,float> floats;\n'
                '    HRESULT SetFloat(const char * name, float value) { floats[name]=value; ++writes; return 0; }\n'
                '    HRESULT SetFloatArray(const char *, const float *, unsigned) { ++writes; return 0; }')
            for label, source in (('production', patched), ('old-owner', flora)):
                bodies = '\n'.join(function(source, signature) for signature in (
                    'bool FloraRenderer::preDraw(', 'void FloraRenderer::beginAlphaTestPass('))
                target = folder / (label + '.cpp')
                target.write_text(harness + FIXTURE.replace('@BODIES@', bodies))
                binary = folder / label
                subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable', '-UNDEBUG',
                                *native_test_compile_flags(compiler), str(target), '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0 if label == 'production' else 41,
                                 (label, result.returncode, result.stdout, result.stderr))


FIXTURE = r'''
struct Device { void SetSoftwareVertexProcessing(bool){} };
struct RC {void setVertexDeclaration(int){}void setRenderState(int,unsigned){}bool mixedVertexProcessing(){return false;}
Device*device(){static Device value;return &value;}};
RC&rc(){static RC value;return value;}
}
constexpr unsigned D3DRS_TEXTUREFACTOR=1,D3DRS_ALPHATESTENABLE=2,D3DRS_ALPHABLENDENABLE=3,
D3DRS_ALPHAREF=4,D3DRS_SRCBLEND=5,D3DRS_DESTBLEND=6,D3DRS_ZWRITEENABLE=7,D3DBLEND_ONE=1,D3DBLEND_ZERO=0;
constexpr bool FALSE=false,TRUE=true;
namespace DX{using Device=Moo::Device;}
struct EffectParameterCache {
 ID3DXEffect*target=nullptr;void effect(ID3DXEffect*p){target=p;}
 void setFloat(const char*n,float v){target->SetFloat(n,v);}
 void setVector(const char*n,const Vector4*v){target->SetVector(n,v);}
 void setTexture(const char*n,void*v){target->SetTexture(n,v);}
 void setFloatArray(const char*n,const float*v,unsigned c){target->SetFloatArray(n,v,c);}
};
struct IRendererPipeline{enum{TYPE_DEFERRED_SHADING=1,STENCIL_USAGE_FLORA=2};void*getGBufferTextureCopyFrom(int){return this;}};
struct Renderer{static Renderer&instance(){static Renderer value;return value;}int pipelineType(){return 1;}
IRendererPipeline*pipeline(){static IRendererPipeline value;return &value;}};
struct Pipeline{void setupSystemStencil(int){}};Pipeline&rp(){static Pipeline value;return value;}
struct Colour{static unsigned getUint32FromNormalised(const Vector4&){return 0;}};
struct TimeOfDay{struct Lighting{Vector4 ambientColour{.1f,.2f,.3f,1.f};};Lighting&lighting(){static Lighting v;return v;}};
struct EnviroMinder{TimeOfDay*timeOfDay(){static TimeOfDay value;return &value;}};
struct Decl{int declaration(){return 0;}};
struct Buffer{void set(int,int,unsigned){}};
struct FloraVertex{float value;};
struct Animation{struct Grid{float x,y;}values[64]{};Grid*getGrid(){return values;}
struct Direction{float x=1,y=0;};Direction direction(){return {};}};
struct FloraRenderer {
 Moo::EffectMaterial*material_;EffectParameterCache standardParams_;EffectParameterCache*parameters_=nullptr;
 Decl decl;Decl*pDecl_=&decl;Animation m_localAnimation,m_globalAnimation;Buffer pVB_;int lastBlockID_=0;
 bool preDraw(EnviroMinder&,float,float);void beginAlphaTestPass(float,float,uint32);
};
@BODIES@
int main(){
 auto shared=std::make_shared<Moo::ManagedEffect>();Moo::EffectMaterial material(shared);FloraRenderer flora;
 flora.material_=&material;flora.standardParams_.effect(shared->pEffect());EnviroMinder env;
 for(unsigned frame=0;frame<3;++frame){
  if(frame==2)++shared->compileMark_; // Recompilation replaces the private binding.
  if(!flora.preDraw(env,.2f,.8f))return 10;
  flora.beginAlphaTestPass(25.f,70.f,64);
  auto*draw=shared->pEffect();
  if(draw->floats["g_alphaRef"]!=64.f/255.f||draw->floats["g_tintBegin"]!=.2f||
     !draw->vectors.count("VISIBILITY")||!draw->textures["g_tintMap"])return 41;
  if(!material.endPass()||!material.end())return 11;
 }
 return shared->pEffect_->writes?12:0;
}
'''


if __name__ == '__main__':
    unittest.main()
