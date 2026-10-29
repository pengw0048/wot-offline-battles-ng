"""Run the generated GUI material consumers against actual #1513 reflection."""
import ast
import importlib.util
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    'gui_material_patch', ROOT / 'bigworld/patch_stock_effect_owners.py')
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)
FILES = ('simple_gui_component.hpp', 'simple_gui_component.cpp', 'text_gui_component.cpp',
         'simple_gui.cpp')


def function(source, signature):
    start = source.index(signature)
    cursor = source.index('{', start)
    depth = 1
    while depth:
        cursor += 1
        depth += (source[cursor] == '{') - (source[cursor] == '}')
    return source[start:cursor + 1]


class StockGUIMaterialTests(unittest.TestCase):
    def setUp(self):
        archive = Path(os.environ.get(
            'WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            self.skipTest('Pinned public engine archive is required')
        temporary = tempfile.TemporaryDirectory(prefix='wot-gui-material-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.ashes = self.root / 'lib/ashes'
        self.ashes.mkdir(parents=True)
        with tarfile.open(archive) as original:
            for name in FILES:
                member = next(n for n in original.getnames()
                              if n.endswith('/programming/bigworld/lib/ashes/' + name))
                (self.ashes / name).write_bytes(original.extractfile(member).read())

    def test_gui_profiler_patch_is_only_four_scopes_and_is_transactional(self):
        spec = importlib.util.spec_from_file_location(
            'gui_profiler_patch', ROOT / 'bigworld/patch_stock_gui_profiler.py')
        profiler = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(profiler)
        archive = Path(os.environ.get(
            'WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        with tarfile.open(archive) as original:
            for name in profiler.TOKENS:
                member = next(n for n in original.getnames()
                              if n.endswith('/programming/bigworld/' + name))
                path = self.root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(original.extractfile(member).read())
        pristine = {name: (self.root / name).read_text() for name in profiler.TOKENS}
        profiler.patch(self.root)
        final = {name: (self.root / name).read_text() for name in profiler.TOKENS}
        profiler.patch(self.root)
        self.assertEqual(final, {name: (self.root / name).read_text() for name in final})
        self.assertEqual(sum(text.count('PROFILER_SCOPED(') for text in final.values()), 4)
        for name in final:
            reverted = final[name]
            for relative, before, after in profiler.SEAMS:
                if relative == name:
                    reverted = reverted.replace(after, before, 1)
            self.assertEqual(reverted, pristine[name])
        # A partial or duplicated insertion never writes any companion file.
        for name, before, after in profiler.SEAMS:
            for damaged in (final[name].replace(after, before, 1), final[name] + after,
                            final[name] + before, pristine[name].replace(before, '', 1)):
                for relative, source in pristine.items():
                    (self.root / relative).write_text(source)
                (self.root / name).write_text(damaged)
                snapshot = {relative: (self.root / relative).read_bytes() for relative in pristine}
                with self.assertRaises(ValueError):
                    profiler.patch(self.root)
                self.assertEqual(snapshot, {relative: (self.root / relative).read_bytes()
                                            for relative in pristine})

    def test_fresh_and_cached_preparations_are_identical(self):
        PATCH.patch_gui_materials(self.root)
        first = {name: (self.ashes / name).read_bytes() for name in FILES}
        PATCH.patch_gui_materials(self.root)
        self.assertEqual(first, {name: (self.ashes / name).read_bytes() for name in FILES})
        module = ast.parse((ROOT / 'bigworld/patch_stock_effect_owners.py').read_text())
        entry = next(node for node in module.body
                     if isinstance(node, ast.FunctionDef) and node.name == 'patch')
        self.assertTrue(any(isinstance(node, ast.Call) and
                            isinstance(node.func, ast.Name) and
                            node.func.id == 'patch_gui_materials'
                            for node in ast.walk(entry)))

    def test_changed_ordinal_consumer_is_rejected(self):
        path = self.ashes / 'text_gui_component.cpp'
        path.write_text(path.read_text().replace('GetTechnique(materialFX()-FX_ADD)',
                                                'GetTechnique(0)'))
        with self.assertRaises(RuntimeError):
            PATCH.patch_gui_materials(self.root)

    def test_generated_cpp_resolves_all_modes_for_original_and_legacy_effects(self):
        compiler = shutil.which('c++')
        client = Path(os.environ.get('WOT_0922_CLIENT',
            '/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD'))
        package = client / 'res/packages/misc.pkg'
        if not compiler or not package.is_file():
            self.skipTest('C++ compiler and exact #1513 misc.pkg are required')
        effects = []
        with zipfile.ZipFile(package) as original:
            for material in ('ashes', 'font'):
                for variant in ('10', '11'):
                    name = 'system/materials/%s.%s.dx9.fxo' % (material, variant)
                    with zipfile.ZipFile(io.BytesIO(original.read(name))) as nested:
                        path = self.root / ('%s-%s.effect' % (material, variant))
                        path.write_bytes(nested.read('effect'))
                        effects.append(path)
        PATCH.patch_gui_materials(self.root)
        header = (self.ashes / FILES[0]).read_text()
        simple = (self.ashes / FILES[1]).read_text()
        text = (self.ashes / FILES[2]).read_text()
        enum = re.search(r'typedef enum\s*\{\s*FX_ADD,.*?\} eMaterialFX;',
                         header, re.S).group(0)
        names = re.findall(r'\bFX_\w+\b', enum)
        consumers = '\n'.join((
            function(simple, 'D3DXHANDLE SimpleGUIComponent::materialTechnique('),
            function(simple, 'bool SimpleGUIComponent::setupTechniqueTable('),
            function(text, 'bool TextGUIComponent::buildMaterial()')))
        harness = r'''
#include "wot/stock_effect_reflection.hpp"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#define BW_GUARD
#define MF_ASSERT assert
#define MF_ASSERT_DEV assert
#define ERROR_MSG(...) ((void)0)
using D3DXHANDLE = const char*;
struct ID3DXEffect {
    std::shared_ptr<wot::StockEffectBinary> definition;
    wot::StockEffectReflection reflection;
    bool native;
    std::string missing;
    ID3DXEffect(std::shared_ptr<wot::StockEffectBinary> d, bool n)
        : definition(d), reflection(d, {}), native(n) {}
    D3DXHANDLE GetTechnique(unsigned i) {
        return reinterpret_cast<D3DXHANDLE>(std::uintptr_t(native ? reflection.technique(i) : i+1));
    }
    D3DXHANDLE GetTechniqueByName(const char* name) {
        if (missing == name) return NULL;
        if (native) return reinterpret_cast<D3DXHANDLE>(std::uintptr_t(reflection.technique_by_name(name)));
        for(unsigned i=0;i<definition->techniques.size();++i)
            if(definition->techniques[i].name==name) return GetTechnique(i);
        return NULL;
    }
    std::string name(D3DXHANDLE h) {
        auto index = std::uintptr_t(h);
        if(native) index = *reflection.technique_source_index(static_cast<unsigned>(index));
        else --index;
        return definition->techniques.at(index).name;
    }
};
namespace Moo {
struct ManagedEffect {ID3DXEffect* effect; ID3DXEffect* pEffect(){return effect;}};
using ManagedEffectPtr = std::shared_ptr<ManagedEffect>;
struct EffectMaterial {ManagedEffectPtr effect; ManagedEffectPtr pEffect(){return effect;}};
using EffectMaterialPtr = std::shared_ptr<EffectMaterial>;
}
template<class T> struct ComObjectWrap {
    T* value; ComObjectWrap(T* p):value(p){} T* operator->(){return value;}
    bool operator!=(std::nullptr_t) const{return value!=nullptr;}
};
class SimpleGUIComponent {
public:
    ENUM
    static std::vector<D3DXHANDLE> s_techniqueTable;
    static D3DXHANDLE materialTechnique(Moo::EffectMaterialPtr, eMaterialFX);
    static bool setupTechniqueTable(Moo::EffectMaterialPtr);
};
std::vector<D3DXHANDLE> SimpleGUIComponent::s_techniqueTable;
static Moo::ManagedEffectPtr s_guiEffect;
struct FontManager {
    Moo::EffectMaterialPtr value;
    static FontManager& instance(){static FontManager manager; return manager;}
    Moo::EffectMaterialPtr material(){return value;}
};
struct TextGUIComponent:SimpleGUIComponent {
    Moo::EffectMaterialPtr material_;
    eMaterialFX mode;
    D3DXHANDLE technique_=NULL;
    eMaterialFX materialFX(){return mode;}
    bool buildMaterial();
};
CONSUMERS
const char* names[]={NAMES};
void check(std::shared_ptr<wot::StockEffectBinary> definition,bool native) {
    ID3DXEffect effect(definition,native);
    auto material=std::make_shared<Moo::EffectMaterial>();
    material->effect=std::make_shared<Moo::ManagedEffect>();
    material->effect->effect=&effect;
    FontManager::instance().value=material;
    using GUI=SimpleGUIComponent;
    if(native) {
        assert(effect.name(effect.GetTechnique(GUI::FX_SOLID))=="FX_BLEND2X");
        assert(effect.name(effect.GetTechnique(GUI::FX_BLEND))=="FX_ADD_SIGNED");
    }
    assert(GUI::setupTechniqueTable(material));
    assert(GUI::s_techniqueTable.size()==10);
    for(int i=0;i<10;++i) {
        assert(effect.name(GUI::s_techniqueTable[i])==names[i]);
        TextGUIComponent label; label.mode=static_cast<GUI::eMaterialFX>(i);
        assert(label.buildMaterial());
        assert(effect.name(label.technique_)==names[i]);
        effect.missing=names[i];
        assert(!GUI::setupTechniqueTable(material));
        assert(GUI::s_techniqueTable.empty());
        assert(!label.buildMaterial() && label.technique_==NULL);
        effect.missing.clear();
        assert(GUI::setupTechniqueTable(material));
    }
    assert(!GUI::materialTechnique(material,static_cast<GUI::eMaterialFX>(-1)));
    assert(!GUI::materialTechnique(material,static_cast<GUI::eMaterialFX>(10)));
    assert(!GUI::materialTechnique({},GUI::FX_SOLID));
}
int main(int argc,char**argv) {
    assert(argc==5);
    for(int i=1;i<argc;++i) {
        std::ifstream input(argv[i],std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
        auto definition=std::make_shared<wot::StockEffectBinary>(); std::string error;
        assert(wot::decode_stock_dx9_effect(bytes.data(),bytes.size(),*definition,error));
        check(definition,true);
    }
    auto legacy=std::make_shared<wot::StockEffectBinary>();
    for(const auto* name:names) {wot::StockEffectTechnique t;t.name=name;legacy->techniques.push_back(t);}
    check(legacy,false);
    std::reverse(legacy->techniques.begin(),legacy->techniques.end());
    check(legacy,false);
    std::cout<<"All 10 GUI modes: four original effects and two legacy source orders passed.\n";
}
'''.replace('ENUM', enum).replace('CONSUMERS', consumers).replace(
            'NAMES', ','.join('"' + name + '"' for name in names))
        source = self.root / 'probe.cpp'
        source.write_text(harness)
        binary = self.root / 'probe'
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT / 'include'), str(source),
                        str(ROOT / 'src/stock_shader_binary.cpp'),
                        str(ROOT / 'src/stock_effect_reflection.cpp'),
                        '-o', str(binary)], check=True)
        subprocess.run([str(binary)] + [str(path) for path in effects], check=True)



PIXEL_SNAP_HARNESS = r"""
#include "wot/stock_effect_binding.hpp"
#include "wot/stock_effect_reflection.hpp"
#include "wot/stock_shader_runtime.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#define BW_GUARD
#define SUCCEEDED(value) ((value) >= 0)
struct Vector4 {float x,y,z,w;Vector4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}};
using D3DXHANDLE=const char*;
// ARIE SetVector resolves the named uniform and writes the material Binding.
struct ID3DXEffect {
    wot::StockEffectReflection* reflection;
    wot::StockEffectBinding* binding;
    int SetVector(D3DXHANDLE name,const Vector4* value) {
        const auto* parameter=reflection->parameter_reference(reflection->parameter_by_name(name));
        std::string error;
        return parameter&&binding->set_uniform(parameter->owner,parameter->member,value,
            sizeof(*value),3,error)?0:-1;
    }
};
namespace wot {bool is_stock_effect(ID3DXEffect* effect){return effect!=nullptr;}}
struct RenderContext {
    float width=0,height=0;
    float screenWidth(){return width;} float screenHeight(){return height;}
    float halfScreenWidth(){return width*.5f;} float halfScreenHeight(){return height*.5f;}
};
RenderContext context;
RenderContext& rc(){return context;}
struct EffectConstantValue {virtual bool operator()(ID3DXEffect*,D3DXHANDLE)=0;};
namespace generated {
GENERATED
}
namespace original {
PUBLIC
}
using Register=std::array<float,4>;
// Evaluates only the vs_3_0 instructions present in the original GUI shaders.
Register run(const std::vector<std::uint32_t>& code,const std::vector<std::uint32_t>& floats,
             const Register& position) {
    Register r[32]={},c[256]={},o[12]={};
    for(std::size_t i=0;i*4<floats.size()&&i<256;++i) std::memcpy(c[i].data(),&floats[i*4],16);
    const Register v[3]={position,{1,1,1,1},{0,0,0,0}};
    auto bank=[&](std::uint32_t token)->Register* {
        const auto type=((token>>28)&7)|((token>>8)&0x18),index=token&0x7ff;
        if(token&0x2000) throw std::runtime_error("relative addressing");
        if(type==0) return &r[index]; if(type==1) return const_cast<Register*>(&v[index]);
        if(type==2) return &c[index]; if(type==6) return &o[index];
        throw std::runtime_error("register type");
    };
    auto source=[&](std::uint32_t token) {
        const auto& value=*bank(token);Register out;
        for(int k=0;k<4;++k) out[k]=value[(token>>(16+2*k))&3];
        const auto modifier=(token>>24)&15;
        if(modifier==1) for(auto& x:out) x=-x; else if(modifier) throw std::runtime_error("modifier");
        return out;
    };
    assert(code.at(0)==0xfffe0300u);
    for(std::size_t i=1;i<code.size();) {
        const auto token=code[i],op=token&0xffff,length=(token>>24)&15;
        if(op==0xfffe) {i+=1+((token>>16)&0x7fff);continue;}
        if(op==0xffff) return o[0];
        const auto* a=&code[i+1];
        if(op==81) std::memcpy(c[a[0]&0x7ff].data(),a+1,16);
        else if(op!=31) {
            if((a[0]>>20)&15) throw std::runtime_error("result modifier");
            Register s0=source(a[1]),s1=length>2?source(a[2]):Register{},s2=length>3?source(a[3]):Register{},out{};
            for(int k=0;k<4;++k) switch(op) {
                case 1: out[k]=s0[k];break;
                case 2: out[k]=s0[k]+s1[k];break;
                case 4: out[k]=s0[k]*s1[k]+s2[k];break;
                case 5: out[k]=s0[k]*s1[k];break;
                case 6: assert(s0[0]==s0[3]);out[k]=1.f/s0[0];break;
                case 19: out[k]=s0[k]-std::floor(s0[k]);break;
                default: throw std::runtime_error("opcode "+std::to_string(op));
            }
            auto& destination=*bank(a[0]);
            for(int k=0;k<4;++k) if((a[0]>>(16+k))&1) destination[k]=out[k];
        }
        i+=1+length;
    }
    throw std::runtime_error("missing end");
}
template<class Producer>
Register draw(const std::shared_ptr<wot::StockEffectBinary>& effect,bool snap,
              Producer& producer,const Register& position) {
    wot::StockEffectUniformPool pool;wot::StockEffectUniformContext shared(pool);
    wot::StockEffectBinding binding(effect,shared);wot::StockEffectReflection reflection(effect,{});
    ID3DXEffect handle{&reflection,&binding};
    auto write=[&](const char* name,const void* data,std::size_t size,unsigned type) {
        const auto* parameter=reflection.parameter_reference(reflection.parameter_by_name(name));
        std::string error;
        assert(parameter&&binding.set_uniform(parameter->owner,parameter->member,data,size,type,error));
    };
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},divisor=1;
    const int enabled=snap;
    write("worldViewProj",identity,sizeof(identity),3);
    write("pixelSnapEnabled",&enabled,sizeof(enabled),4);
    if(reflection.parameter_by_name("uvDivisor")) write("uvDivisor",&divisor,sizeof(divisor),3);
    assert(producer(&handle,"resolution"));
    std::size_t solid=effect->techniques.size();
    for(std::size_t i=0;i<effect->techniques.size();++i)
        if(effect->techniques[i].name=="FX_SOLID") solid=i;
    wot::StockDx9PassSelection pass;std::string error;
    assert(binding.select_pass(solid,0,pass,error)&&pass.vertex_shader);
    const auto& shader=effect->shaders[*pass.vertex_shader];
    std::vector<wot::StockShaderUniformView> views;
    std::vector<std::shared_ptr<wot::StockEffectUniformStorage>> owners;
    for(const auto& resource:shader.resources) if(resource.kind==3) {
        std::optional<std::uint32_t> buffer;
        assert(wot::resolve_stock_effect_reference(*effect,binding.selection(),
            wot::StockEffectResourceTable::uniform_buffer,resource.resource_index,buffer,error)&&buffer);
        auto storage=binding.uniform_buffer(*buffer);
        assert(storage);
        if(resource.shader_slot>=views.size()) views.resize(resource.shader_slot+1);
        views[resource.shader_slot]={storage->bytes().data(),storage->bytes().size()};
        owners.push_back(storage);
    }
    wot::StockShaderRegisters registers;
    assert(wot::stage_stock_shader_registers(shader,views,registers,error));
    return run(shader.bytecode,registers[2].words,position);
}
int main(int argc,char**argv) {
    // Room START button and pointer corners at their logged clip positions.
    const Register corners[]={{-.25f,-.98f,.05f,1},{.25f,-.82f,.05f,1},{.0449f,.6875f,.03f,1},
                              {.3203f,1.0026f,.03f,1},{-.34f,.505f,.1f,1}};
    for(int n=1;n<argc;++n) {
        std::ifstream input(argv[n],std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
        auto effect=std::make_shared<wot::StockEffectBinary>();std::string error;
        assert(wot::decode_stock_dx9_effect(bytes.data(),bytes.size(),*effect,error));
        for(const auto size:{std::array<float,2>{1024,768},std::array<float,2>{1920,1080},
                             std::array<float,2>{1279,719}}) {
            context.width=size[0];context.height=size[1];
            generated::ScreenConstant current;original::ScreenConstant unpatched;
            for(const auto& corner:corners) {
                const auto snapped=draw(effect,true,current,corner);
                assert(std::fabs(snapped[0]-corner[0])<=2.f/size[0]+1e-5f);
                assert(std::fabs(snapped[1]-corner[1])<=2.f/size[1]+1e-5f);
                assert(snapped[2]==corner[2]&&snapped[3]==1);
                // Failing control: public half sizes move every snapped vertex off screen.
                const auto broken=draw(effect,true,unpatched,corner);
                assert(std::fabs(broken[0])>8||std::fabs(broken[1])>8);
                // The unsnapped shader does not read the screen constant.
                const auto plain=draw(effect,false,unpatched,corner);
                assert(plain==corner);
            }
        }
    }
    std::cout<<"Original GUI pixel-snap shaders keep every corner within one pixel.\n";
}
"""


class StockGUIPixelSnapTests(unittest.TestCase):
    def test_original_pixel_snap_shaders_read_generated_screen_producer(self):
        archive = Path(os.environ.get(
            'WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        compiler = shutil.which('c++')
        client = os.environ.get('WOT_0922_CLIENT')
        if not archive.is_file() or not compiler or not client:
            self.skipTest('Set WOT_0922_CLIENT; the public engine archive and a C++ compiler are required')
        package = Path(client) / 'res/packages/misc.pkg'
        if not package.is_file():
            self.skipTest('WOT_0922_CLIENT has no exact #1513 res/packages/misc.pkg')
        with tempfile.TemporaryDirectory(prefix='wot-gui-pixel-snap-') as directory:
            root = Path(directory)
            with tarfile.open(archive) as original:
                for relative in ('lib/moo/effect_visual_context.hpp', 'lib/moo/effect_visual_context.cpp'):
                    member = next(name for name in original.getnames()
                                  if name.endswith('/programming/bigworld/' + relative))
                    path = root / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(original.extractfile(member).read())
            public = (root / 'lib/moo/effect_visual_context.cpp').read_text()
            PATCH.patch_context(root)
            generated = (root / 'lib/moo/effect_visual_context.cpp').read_text()
            def constants(text):
                return text[text.index('class ScreenConstant'):text.index('class InvScreenConstant')]
            effects = []
            with zipfile.ZipFile(package) as original:
                for material in ('ashes', 'font'):
                    for variant in ('10', '11'):
                        name = 'system/materials/%s.%s.dx9.fxo' % (material, variant)
                        with zipfile.ZipFile(io.BytesIO(original.read(name))) as nested:
                            path = root / ('%s-%s.effect' % (material, variant))
                            path.write_bytes(nested.read('effect'))
                            effects.append(path)
            source = root / 'probe.cpp'
            source.write_text(PIXEL_SNAP_HARNESS.replace('GENERATED', constants(generated)).replace(
                'PUBLIC', constants(public)))
            binary = root / 'probe'
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'include'), str(source),
                            str(ROOT / 'src/stock_shader_binary.cpp'),
                            str(ROOT / 'src/stock_shader_runtime.cpp'),
                            str(ROOT / 'src/stock_effect_binding.cpp'),
                            str(ROOT / 'src/stock_effect_reflection.cpp'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)] + [str(path) for path in effects], check=True)


FACTOR_HARNESS = r"""
#include "wot/stock_effect_binding.hpp"
#include "wot/stock_effect_reflection.hpp"
#include "wot/stock_shader_runtime.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#define BW_GUARD
#define SUCCEEDED(value) ((value) >= 0)
using DWORD = unsigned; using uint32 = unsigned;
struct Vector4 {float x,y,z,w;Vector4(){}Vector4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}};
namespace Colour {
inline Vector4 getVector4Normalised(uint32 c) {
    return Vector4(((c>>16)&255)/255.f,((c>>8)&255)/255.f,(c&255)/255.f,((c>>24)&255)/255.f);
}
}
using D3DXHANDLE=const char*;
// ARIE SetVector resolves the named uniform and writes the material Binding;
// a legacy D3DX effect simply records the value.
struct ID3DXEffect {
    wot::StockEffectReflection* reflection;
    wot::StockEffectBinding* binding;
    Vector4 legacy{0,0,0,0};
    int SetVector(D3DXHANDLE name,const Vector4* value) {
        if (!binding) { legacy=*value; return 0; }
        const auto* parameter=reflection->parameter_reference(reflection->parameter_by_name(name));
        std::string error;
        return parameter&&binding->set_uniform(parameter->owner,parameter->member,value,
            sizeof(*value),3,error)?0:-1;
    }
};
namespace wot {bool is_stock_effect(ID3DXEffect* effect){return effect&&effect->binding;}}
namespace Moo {struct EffectConstantValue {virtual bool operator()(ID3DXEffect*,D3DXHANDLE)=0;};}
namespace generated {
GENERATED
}
namespace original {
PUBLIC
}
// Registers that BeginPass uploads from the GUIColour uniform for one technique.
template<class Producer>
std::vector<Vector4> uploadedFactor(const std::shared_ptr<wot::StockEffectBinary>& effect,
                                    std::size_t technique,const wot::StockEffectParameterReference& factor,
                                    Producer& producer) {
    wot::StockEffectUniformPool pool;wot::StockEffectUniformContext shared(pool);
    wot::StockEffectBinding binding(effect,shared);wot::StockEffectReflection reflection(effect,{});
    ID3DXEffect handle{&reflection,&binding};
    assert(producer(&handle,factor.name.c_str()));
    const auto offset=effect->uniform_buffers[factor.owner].uniforms[factor.member].byte_offset;
    wot::StockDx9PassSelection pass;std::string error;
    assert(binding.select_pass(technique,0,pass,error));
    std::vector<Vector4> values;
    for(const auto selected:{pass.vertex_shader,pass.pixel_shader}) {
        if(!selected) continue;
        const auto& shader=effect->shaders[*selected];
        std::vector<wot::StockShaderUniformView> views;
        std::vector<std::shared_ptr<wot::StockEffectUniformStorage>> owners;
        std::vector<std::uint32_t> slotBuffer;
        for(const auto& resource:shader.resources) if(resource.kind==3) {
            std::optional<std::uint32_t> buffer;
            assert(wot::resolve_stock_effect_reference(*effect,binding.selection(),
                wot::StockEffectResourceTable::uniform_buffer,resource.resource_index,buffer,error)&&buffer);
            auto storage=binding.uniform_buffer(*buffer);
            if(!storage) continue;
            if(resource.shader_slot>=views.size()) {
                views.resize(resource.shader_slot+1);slotBuffer.resize(resource.shader_slot+1,~0u);
            }
            views[resource.shader_slot]={storage->bytes().data(),storage->bytes().size()};
            slotBuffer[resource.shader_slot]=*buffer;
            owners.push_back(storage);
        }
        wot::StockShaderRegisters registers;
        assert(wot::stage_stock_shader_registers(shader,views,registers,error));
        for(const auto& copy:shader.register_copies)
            if(copy.kind==wot::StockShaderRegisterKind::floating&&copy.uniform_slot<slotBuffer.size()&&
               slotBuffer[copy.uniform_slot]==factor.owner&&copy.source_offset==offset) {
                Vector4 value;
                std::memcpy(&value,registers[2].words.data()+copy.destination_offset/4,sizeof(value));
                values.push_back(value);
            }
    }
    return values;
}
std::shared_ptr<wot::StockEffectBinary> load(const char* path) {
    std::ifstream input(path,std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
    auto effect=std::make_shared<wot::StockEffectBinary>();std::string error;
    assert(wot::decode_stock_dx9_effect(bytes.data(),bytes.size(),*effect,error));
    return effect;
}
std::string semantic(const std::shared_ptr<wot::StockEffectBinary>& effect,const char* name) {
    wot::StockEffectReflection reflection(effect,{});
    const auto* parameter=reflection.parameter_reference(reflection.parameter_by_name(name));
    assert(parameter&&parameter->semantic_present);
    return parameter->semantic;
}
bool same(const Vector4& a,const Vector4& b){return a.x==b.x&&a.y==b.y&&a.z==b.z&&a.w==b.w;}
// argv: font 10/11, then every original dx9 effect in the client catalogue.
int main(int argc,char**argv) {
    assert(argc>3);
    const DWORD button=0xf518374e; // waiting_room_ui control colour (24,55,78,245)
    const Vector4 tint=Colour::getVector4Normalised(button),white(1,1,1,1);
    // GUI.Text keeps its colour: the font factor is FontColour, not GUIColour.
    for(int n=1;n<3;++n) assert(semantic(load(argv[n]),"factor")=="FontColour");
    int consumers=0;
    for(int n=3;n<argc;++n) {
        auto effect=load(argv[n]);
        wot::StockEffectReflection reflection(effect,{});
        for(std::size_t p=0;p<reflection.parameter_count();++p) {
            const auto* factor=reflection.parameter_reference(reflection.parameter(p));
            if(factor->kind!=wot::StockEffectParameterKind::uniform||!factor->semantic_present||
               factor->semantic!="GUIColour") continue;
            ++consumers;
            std::size_t read=0;
            for(std::size_t t=0;t<effect->techniques.size();++t) {
                generated::TFactorSetter current;current.colour(button);
                for(const auto& value:uploadedFactor(effect,t,*factor,current)) {
                    assert(same(value,white));++read;
                }
                // Failing control: the public producer tints the textured quad.
                original::TFactorSetter unpatched;unpatched.colour(button);
                for(const auto& value:uploadedFactor(effect,t,*factor,unpatched)) assert(same(value,tint));
            }
            assert(read>0); // a declared consumer must actually upload the constant
            std::cout<<"GUIColour consumer "<<argv[n]<<" "<<factor->name<<" registers="<<read<<"\n";
        }
    }
    assert(consumers>=4); // at least ashes and gobo 10/11 in misc.pkg
    // Legacy D3DX effects keep the public component colour.
    generated::TFactorSetter current;current.colour(button);
    ID3DXEffect legacy{nullptr,nullptr};
    assert(current(&legacy,"factor")&&same(legacy.legacy,tint));
    std::cout<<"Every original GUIColour consumer uploads the #1513 constant factor; text and legacy keep colour.\n";
}
"""


class StockGUIFactorTests(unittest.TestCase):
    def test_every_original_gui_colour_consumer_uses_the_constant_factor(self):
        archive = Path(os.environ.get(
            'WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        compiler = shutil.which('c++')
        client = os.environ.get('WOT_0922_CLIENT')
        if not archive.is_file() or not compiler or not client:
            self.skipTest('Set WOT_0922_CLIENT; the public engine archive and a C++ compiler are required')
        package = Path(client) / 'res/packages/misc.pkg'
        if not package.is_file():
            self.skipTest('WOT_0922_CLIENT has no exact #1513 res/packages/misc.pkg')
        with tempfile.TemporaryDirectory(prefix='wot-gui-factor-') as directory:
            root = Path(directory)
            ashes = root / 'lib/ashes'
            ashes.mkdir(parents=True)
            with tarfile.open(archive) as original:
                for name in FILES:
                    member = next(n for n in original.getnames()
                                  if n.endswith('/programming/bigworld/lib/ashes/' + name))
                    (ashes / name).write_bytes(original.extractfile(member).read())
            public = (ashes / 'simple_gui.cpp').read_text()
            PATCH.patch_gui_materials(root)
            generated = (ashes / 'simple_gui.cpp').read_text()
            PATCH.patch_gui_materials(root)
            self.assertEqual((ashes / 'simple_gui.cpp').read_text(), generated)
            def producer(text):
                return text[text.index('class TFactorSetter'):text.index('SimpleGUI::SimpleGUI() :')]
            effects = []
            with zipfile.ZipFile(package) as original:
                for variant in ('10', '11'):
                    name = 'system/materials/font.%s.dx9.fxo' % variant
                    with zipfile.ZipFile(io.BytesIO(original.read(name))) as nested:
                        path = root / ('font-%s.effect' % variant)
                        path.write_bytes(nested.read('effect'))
                        effects.append(path)
            # Every original DX9 effect is audited; none is excluded by name.
            catalogue = root / 'catalogue'
            catalogue.mkdir()
            for archive_path in sorted((Path(client) / 'res/packages').glob('*.pkg')):
                with zipfile.ZipFile(archive_path) as original:
                    for name in original.namelist():
                        if name.endswith('.dx9.fxo'):
                            with zipfile.ZipFile(io.BytesIO(original.read(name))) as nested:
                                path = catalogue / (archive_path.stem + '__' + name.replace('/', '__'))
                                path.write_bytes(nested.read('effect'))
                                effects.append(path)
            self.assertGreater(len(effects), 400)
            source = root / 'probe.cpp'
            source.write_text(FACTOR_HARNESS.replace('GENERATED', producer(generated)).replace(
                'PUBLIC', producer(public)))
            binary = root / 'probe'
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-private-field', '-I', str(ROOT / 'include'), str(source),
                            str(ROOT / 'src/stock_shader_binary.cpp'),
                            str(ROOT / 'src/stock_shader_runtime.cpp'),
                            str(ROOT / 'src/stock_effect_binding.cpp'),
                            str(ROOT / 'src/stock_effect_reflection.cpp'),
                            '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)] + [str(path) for path in effects],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('materials__gobo.10.dx9.fxo factor', result.stdout)
            self.assertIn('materials__ashes.11.dx9.fxo factor', result.stdout)

if __name__ == '__main__':
    unittest.main()
