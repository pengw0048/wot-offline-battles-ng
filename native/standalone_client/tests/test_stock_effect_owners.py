"""Exercise the production Moo owner switch with a strict effect lifecycle."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest


PATCH_PATH = Path(__file__).parents[1] / "bigworld" / "patch_stock_effect_owners.py"
spec = importlib.util.spec_from_file_location("stock_effect_owners_patch", PATCH_PATH)
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


class StockEffectOwnerTests(unittest.TestCase):
    def test_original_screen_uses_live_reciprocals_without_changing_legacy_effects(self):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        compiler = shutil.which('c++')
        if not archive.is_file() or not compiler:
            self.skipTest('Pinned public source archive and host C++ compiler are required')
        with tempfile.TemporaryDirectory(prefix='wot-screen-producer-') as directory:
            root = Path(directory)
            with tarfile.open(archive) as original:
                for relative in ('lib/moo/effect_visual_context.hpp', 'lib/moo/effect_visual_context.cpp'):
                    member = next(name for name in original.getnames()
                                  if name.endswith('/programming/bigworld/' + relative))
                    path = root / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(original.extractfile(member).read())
            patch.patch_context(root)
            path = root / 'lib/moo/effect_visual_context.cpp'
            generated = path.read_text()
            patch.patch_context(root)
            self.assertEqual(path.read_text(), generated)
            # Upgrade an already patched engine, not just a fresh extraction.
            begin = generated.index('            // #1513 Screen')
            end = generated.index('            if (!mapping', begin)
            path.write_text(generated[:begin] +
                '            EffectConstantValuePtr* mapping = getMapping(BW::StringRef(semantic.data(), semantic.size()), false);\n' +
                generated[end:])
            patch.patch_context(root)
            self.assertEqual(path.read_text(), generated)
            constants = generated[generated.index('class ScreenConstant'):generated.index('class WorldViewConstant')]
            begin = generated.index('[this](const std::string& semantic')
            end = generated.index('}, [](const std::string& error)', begin) + 1
            provider = generated[begin:end]
            source = r'''
#include <cassert>
#include <string>
#define BW_GUARD
namespace BW {using StringRef=std::string;}
struct Vector4 {float x,y,z,w;Vector4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}};
using D3DXHANDLE=const char*;
struct ID3DXEffect {Vector4 value{0,0,0,0};void SetVector(D3DXHANDLE,const Vector4* v){value=*v;}};
struct RenderContext {
    float width=0,height=0;
    float screenWidth(){return width;} float screenHeight(){return height;}
    float halfScreenWidth(){return width*.5f;} float halfScreenHeight(){return height*.5f;}
};
RenderContext context;
RenderContext& rc(){return context;}
struct EffectConstantValue {virtual bool operator()(ID3DXEffect*,D3DXHANDLE)=0;};
''' + constants + r'''
struct EffectConstantValuePtr {
    EffectConstantValue* value;
    bool exists(){return value!=nullptr;}
    EffectConstantValue& operator*(){return *value;}
};
struct Context {
    ScreenConstant screen;InvScreenConstant inverse;
    EffectConstantValuePtr legacy{&screen},current{&inverse};
    EffectConstantValuePtr* getMapping(const BW::StringRef& name,bool create){
        assert(!create);
        return name=="Screen"?&legacy:name=="InvScreen"?&current:nullptr;
    }
    void check(){
        auto provider=PROVIDER;
        ID3DXEffect effect;
        assert(provider("Screen",nullptr,nullptr));
        assert(!provider("Unavailable",nullptr,nullptr));
        for(auto size:{Vector4(1920,1080,0,0),Vector4(960,540,0,0),Vector4(1279,719,0,0)}){
            context.width=size.x;context.height=size.y;
            assert(provider("Screen",&effect,"g_screen"));
            assert(effect.value.x==size.x&&effect.value.y==size.y);
            assert(effect.value.z==1.f/size.x&&effect.value.w==1.f/size.y);
            assert((**getMapping("Screen",false))(&effect,"legacy_screen"));
            assert(effect.value.z==size.x*.5f&&effect.value.w==size.y*.5f);
        }
    }
};
int main(){Context owner;owner.check();}
'''.replace('PROVIDER', provider)
            (root / 'probe.cpp').write_text(source)
            subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror',
                            str(root / 'probe.cpp'), '-o', str(root / 'probe')], check=True)
            subprocess.run([str(root / 'probe')], check=True)

    def test_changed_source_is_rejected_and_reapplication_is_stable(self):
        self.assertEqual(patch.replace_once("before", "before", "after", "probe"), "after")
        self.assertEqual(patch.replace_once("after", "before", "after", "probe"), "after")
        for text in ("missing", "before before"):
            with self.assertRaises(RuntimeError):
                patch.replace_once(text, "before", "after", "probe")

    def test_binding_accessor_upgrades_fresh_and_cached_properties(self):
        old = patch.PROPERTIES.replace(
            "        ID3DXEffect* existingStockBinding() const { return binding_.pComObject(); }\n", "")
        for properties in ("typedef BW::map< D3DXHANDLE, EffectPropertyPtr > Properties;", old):
            with self.subTest(properties=properties[:30]), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                header = root / 'lib/moo/effect_material.hpp'
                header.parent.mkdir(parents=True)
                header.write_text('prefix\n' + properties + '\nsuffix\n')
                patch.patch_properties(root)
                expected = 'prefix\n' + patch.PROPERTIES + '\nsuffix\n'
                self.assertEqual(header.read_text(), expected)
                patch.patch_properties(root)
                self.assertEqual(header.read_text(), expected)

    def test_actual_adapter_failed_pass_can_end_or_retry(self):
        source = PATCH_PATH.with_name('stock_effect_adapter.cpp').read_text()
        def body(signature):
            start = source.index(signature)
            end = source.index('{', start)
            level = 1
            while level:
                end += 1
                level += (source[end] == '{') - (source[end] == '}')
            return source[start:end+1]
        methods = '\n'.join(body('HRESULT STDMETHODCALLTYPE StockEffectAdapter::' + name) for name in (
            'Begin(', 'BeginPass(', 'CommitChanges()', 'EndPass()', 'End()'))
        probe = r'''#include <cassert>
#include <memory>
#include <optional>
#include <vector>
using HRESULT=int;using UINT=unsigned;using DWORD=unsigned;
#define STDMETHODCALLTYPE
#define FAILED(v) ((v)<0)
constexpr int S_OK=0,E_FAIL=-1,D3DERR_INVALIDCALL=-2,E_NOTIMPL=-3,E_INVALIDARG=-4,D3DXFX_DONOTSAVESTATE=1;
struct Binary{struct Technique{std::vector<int> passes{1,2};};std::vector<Technique> techniques{Technique()};};
struct Reflection{std::optional<unsigned> technique_source_index(unsigned v){return v==1?std::optional<unsigned>(0):std::nullopt;}};
class StockEffectAdapter{public:std::unique_ptr<int> material_=std::make_unique<int>(1);std::optional<unsigned> pass_;bool begun_=false,failApply=false;unsigned applies=0,technique_=1;Reflection reflection_;std::shared_ptr<Binary> definition_=std::make_shared<Binary>();
unsigned technique_number(unsigned v){return v;}HRESULT apply_current(){assert(begun_&&pass_);++applies;return failApply?E_FAIL:S_OK;}
HRESULT Begin(UINT*,DWORD);HRESULT BeginPass(UINT);HRESULT CommitChanges();HRESULT EndPass();HRESULT End();};
@PRODUCTION@
int main(){StockEffectAdapter fx;unsigned passes=999;assert(fx.BeginPass(0)==D3DERR_INVALIDCALL&&!fx.pass_&&!fx.begun_);assert(fx.Begin(nullptr,1)==D3DERR_INVALIDCALL);assert(fx.Begin(&passes,0)==E_NOTIMPL&&!fx.begun_);assert(fx.Begin(&passes,1)==S_OK&&passes==2);assert(fx.BeginPass(99)==E_INVALIDARG&&!fx.pass_&&fx.begun_);assert(fx.End()==S_OK);
for(bool retry:{false,true}){assert(fx.Begin(&passes,1)==S_OK);fx.failApply=true;assert(fx.BeginPass(0)==E_FAIL);assert(!fx.pass_&&fx.begun_);assert(fx.EndPass()==D3DERR_INVALIDCALL);assert(fx.CommitChanges()==D3DERR_INVALIDCALL);fx.failApply=false;if(retry){assert(fx.BeginPass(1)==S_OK&&fx.pass_==1);assert(fx.EndPass()==S_OK);}assert(fx.End()==S_OK&&!fx.begun_);assert(fx.Begin(&passes,1)==S_OK);assert(fx.BeginPass(0)==S_OK);fx.failApply=true;assert(fx.CommitChanges()==E_FAIL&&fx.pass_==0);assert(fx.End()==D3DERR_INVALIDCALL&&fx.begun_);assert(fx.EndPass()==S_OK&&fx.End()==S_OK);fx.failApply=false;}
}'''.replace('@PRODUCTION@', methods)
        with tempfile.TemporaryDirectory(prefix='actual-adapter-pass-failure-') as directory:
            root = Path(directory)
            (root/'probe.cpp').write_text(probe)
            result = subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(root/'probe.cpp'),'-o',str(root/'probe')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result = subprocess.run([str(root/'probe')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_material_switch_preserves_pass_and_independent_values(self):
        compiler = shutil.which("c++")
        if not compiler:
            self.skipTest("A host C++ compiler is required")
        # Compile the actual generated owner class/function. The effect double
        # rejects incorrect Begin/Pass order rather than accepting every call.
        prefix = r'''
#include <cassert>
#include <climits>
#include <cstdint>
#include <map>
#include <string>
using uint32 = std::uint32_t;
using UINT = unsigned;
using HRESULT = int;
using D3DXHANDLE = const char*;
constexpr HRESULT E_FAIL = -1;
constexpr unsigned D3DXFX_DONOTSAVESTATE = 1;
#define FAILED(value) ((value) < 0)
#define SUCCEEDED(value) ((value) >= 0)
#define BW_GUARD
#define MF_ASSERT(value) assert(value)
#define ERROR_MSG(...) ((void)0)
namespace BW { template<class K,class V> using map = std::map<K,V>; }
struct ID3DXEffect {
    int refs = 1;
    int value = 0;
    bool begun = false;
    bool pass = false;
    bool fail_begin = false, fail_pass = false, fail_technique = false, fail_end_pass = false, fail_end = false;
    void AddRef() { ++refs; }
    void Release() { if (!--refs) delete this; }
    HRESULT SetTechnique(D3DXHANDLE) { return begun || fail_technique ? E_FAIL : 0; }
    HRESULT Begin(UINT* count, unsigned) {
        if (begun || fail_begin) { *count = 999; return E_FAIL; }
        begun = true; *count = 1; return 0;
    }
    HRESULT BeginPass(unsigned index) {
        if (!begun || pass || index != 0 || fail_pass) return E_FAIL;
        pass = true; return 0;
    }
    HRESULT EndPass() {
        if (!begun || !pass || fail_end_pass) return E_FAIL;
        pass = false; return 0;
    }
    HRESULT End() {
        if (!begun || pass || fail_end) return E_FAIL;
        begun = false; return 0;
    }
    HRESULT CommitChanges() { return begun && pass ? 0 : E_FAIL; }
};
template<class T> struct ComObjectWrap {
    T* pointer = nullptr;
    ComObjectWrap() = default;
    ComObjectWrap(T* value) : pointer(value) { if (pointer) pointer->AddRef(); }
    ComObjectWrap(const ComObjectWrap& value) : ComObjectWrap(value.pointer) {}
    ~ComObjectWrap() { if (pointer) pointer->Release(); }
    ComObjectWrap& operator=(const ComObjectWrap& value) {
        T* next = value.pointer;
        if (next) next->AddRef();
        if (pointer) pointer->Release();
        pointer = next; return *this;
    }
    T* pComObject() const { return pointer; }
    T* operator->() const { return pointer; }
    explicit operator bool() const { return pointer != nullptr; }
    bool operator!=(const T* value) const { return pointer != value; }
};
namespace wot {
    bool fail_create = false;
    bool is_stock_effect(ID3DXEffect*) { return true; }
    HRESULT create_stock_effect_material(ID3DXEffect*, ID3DXEffect** output) {
        if (fail_create) return E_FAIL;
        *output = new ID3DXEffect; return 0;
    }
}
using EffectPropertyPtr = int;
struct ManagedEffect {
    using CompileMark = uint32;
    ComObjectWrap<ID3DXEffect> pEffect_;
    ComObjectWrap<ID3DXEffect> activeBinding_;
    CompileMark compileMark_ = 1;
    unsigned numPasses_ = 0;
    unsigned stockPass_ = UINT_MAX;
    bool stockBindingValid_ = true;
    std::string resourceID_ = "original.fx";
    ID3DXEffect* pEffect() { return activeBinding_ ? activeBinding_.pComObject() : pEffect_.pComObject(); }
    D3DXHANDLE currentTechnique() { return "OriginalTechnique"; }
    bool selectMaterialBinding(ComObjectWrap<ID3DXEffect>&, ComObjectWrap<ID3DXEffect>&, CompileMark&);
    bool beginCalled() const { return numPasses_ != 0; }
    void setAutoConstants() {}
    bool begin(bool); bool beginPass(uint32); bool endPass(); bool end(); bool commitChanges();
};
struct EffectMaterial {
'''
        suffix = r'''
int main() {
    ManagedEffect effect;
    auto* definition = new ID3DXEffect;
    effect.pEffect_ = definition;
    definition->Release();
    definition->value = 99; // An independent utility draw must not seed materials.
    EffectMaterial::Properties first, second;
    assert(!first.existingStockBinding());
    assert(first.select(effect));
    auto* first_binding = effect.pEffect();
    assert(first_binding != definition && first_binding->value == 0);
    assert(first.existingStockBinding() == first_binding);
    first_binding->value = 11;
    assert(!FAILED(first_binding->SetTechnique(effect.currentTechnique())));
    assert(!FAILED(first_binding->Begin(&effect.numPasses_, D3DXFX_DONOTSAVESTATE)));
    effect.stockPass_ = 0;
    assert(!FAILED(first_binding->BeginPass(0)));
    assert(second.select(effect));
    auto* second_binding = effect.pEffect();
    assert(second_binding != first_binding && second_binding->value == 0);
    assert(second_binding->begun && second_binding->pass);
    assert(!first_binding->begun && !first_binding->pass);
    second_binding->value = 22;
    assert(first.select(effect));
    assert(effect.pEffect() == first_binding && first_binding->value == 11);
    assert(first_binding->begun && first_binding->pass);
    assert(!second_binding->begun && !second_binding->pass);
    first["OriginalProperty"] = 7;
    EffectMaterial::Properties copied(first);
    assert(copied["OriginalProperty"] == 7);
    assert(copied.select(effect));
    assert(effect.pEffect() != first_binding && effect.pEffect()->value == 0);
    EffectMaterial::Properties failure;
    wot::fail_create = true;
    assert(!failure.select(effect));
    assert(!effect.stockBindingValid_);
    wot::fail_create = false;
    assert(failure.select(effect) && effect.stockBindingValid_);
    assert(effect.pEffect()->begun && effect.pEffect()->pass);
    const auto* before_recompile = effect.pEffect();
    ++effect.compileMark_;
    wot::fail_create = true;
    assert(failure.existingStockBinding() == before_recompile);
    wot::fail_create = false;
    assert(failure.select(effect));
    assert(effect.pEffect() != before_recompile);
    assert(effect.pEffect()->begun && effect.pEffect()->pass);
    assert(!FAILED(effect.pEffect()->EndPass()));
    assert(!FAILED(effect.pEffect()->End()));
    for (bool switchBinding : {false, true}) {
        ManagedEffect owner;auto* original=new ID3DXEffect;owner.pEffect_=original;original->Release();
        EffectMaterial::Properties firstProps, nextProps;
        assert(firstProps.select(owner));auto* first=owner.pEffect();
        first->fail_begin=true;assert(!owner.begin(false)&&owner.numPasses_==0&&owner.stockPass_==UINT_MAX);first->fail_begin=false;
        first->fail_technique=true;assert(!owner.begin(false)&&owner.numPasses_==0);first->fail_technique=false;
        assert(nextProps.select(owner));auto* next=owner.pEffect();assert(firstProps.select(owner));
        assert(owner.begin(true));const auto count=owner.numPasses_;assert(!owner.begin(false)&&owner.numPasses_==count&&first->begun);
        if(switchBinding){assert(owner.beginPass(0));next->fail_pass=true;assert(!nextProps.select(owner));assert(!first->begun&&next->begun&&!next->pass);assert(owner.pEffect()==next&&!owner.stockBindingValid_);}
        else {first->fail_pass=true;assert(!owner.beginPass(0));assert(first->begun&&!first->pass);}
        assert(owner.stockPass_==UINT_MAX&&owner.numPasses_==1&&!owner.commitChanges());
        assert(owner.end()&&owner.numPasses_==0&&owner.stockPass_==UINT_MAX&&!owner.activeBinding_);
        first->fail_pass=next->fail_pass=false;assert(firstProps.select(owner));assert(owner.begin(false)&&owner.beginPass(0));
        assert(!owner.end()&&owner.numPasses_==1&&owner.stockPass_==0&&owner.activeBinding_);
        first->fail_end_pass=true;assert(!owner.endPass()&&owner.stockPass_==0&&owner.activeBinding_);first->fail_end_pass=false;
        assert(owner.endPass()&&owner.stockPass_==UINT_MAX);first->fail_end=true;assert(!owner.end()&&owner.numPasses_==1&&owner.activeBinding_);first->fail_end=false;
        assert(owner.end()&&!first->begun&&!owner.activeBinding_);assert(firstProps.select(owner)&&owner.begin(false)&&owner.beginPass(0));assert(owner.endPass()&&owner.end());
    }
    for(unsigned stage=0;stage<5;++stage){
        ManagedEffect owner;auto* original=new ID3DXEffect;owner.pEffect_=original;original->Release();
        EffectMaterial::Properties a,b;assert(a.select(owner));auto* first=owner.pEffect();assert(b.select(owner));auto* next=owner.pEffect();assert(a.select(owner)&&owner.begin(false)&&owner.beginPass(0));
        first->fail_end_pass=stage==0;first->fail_end=stage==1;next->fail_technique=stage==2;next->fail_begin=stage==3;next->fail_pass=stage==4;
        assert(!b.select(owner)&&!owner.stockBindingValid_&&!owner.commitChanges());
        if(stage==0)assert(owner.pEffect()==first&&owner.stockPass_==0&&owner.numPasses_==1&&first->pass);
        else if(stage==1)assert(owner.pEffect()==first&&owner.stockPass_==UINT_MAX&&owner.numPasses_==1&&!first->pass);
        else if(stage<4)assert(owner.pEffect()==next&&owner.stockPass_==UINT_MAX&&owner.numPasses_==0&&!next->begun);
        else assert(owner.pEffect()==next&&owner.stockPass_==UINT_MAX&&owner.numPasses_==1&&next->begun&&!next->pass);
        first->fail_end_pass=first->fail_end=next->fail_technique=next->fail_begin=next->fail_pass=false;
        if(owner.beginCalled()){if(owner.stockPass_!=UINT_MAX)assert(owner.endPass());assert(owner.end());}
        assert(b.select(owner)&&owner.begin(false)&&owner.beginPass(0)&&owner.endPass()&&owner.end());
    }
}
'''
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            self.skipTest('Pinned public ManagedEffect source required')
        with tarfile.open(archive) as original:
            member = next(name for name in original.getnames() if name.endswith('/programming/bigworld/lib/moo/managed_effect.cpp'))
            raw = original.extractfile(member).read().decode('utf-8-sig')
        generated = patch.patch_managed_lifecycle(raw)
        self.assertEqual(patch.patch_managed_lifecycle(generated), generated)
        # A cached preparation still contains the previous eager pass write.
        cached = generated.replace('const HRESULT result = pEffect()->BeginPass(pass);\n    if (SUCCEEDED(result)) stockPass_ = pass;', 'stockPass_ = pass;\n    const HRESULT result = pEffect()->BeginPass(pass);')
        self.assertEqual(patch.patch_managed_lifecycle(cached), generated)
        self.assertEqual(generated.count('bool ManagedEffect::selectMaterialBinding('), 1)
        signatures = ('bool ManagedEffect::begin(', 'bool ManagedEffect::beginPass(', 'bool ManagedEffect::endPass()', 'bool ManagedEffect::end()', 'bool ManagedEffect::commitChanges()')
        methods = []
        for signature in signatures:
            first = generated.index(signature)
            last = generated.index('{', first)
            depth = 1
            while depth:
                last += 1
                depth += (generated[last] == '{') - (generated[last] == '}')
            methods.append(generated[first:last+1])
        source = prefix + patch.PROPERTIES + "\n};\n" + patch.SELECT_BINDING + '\n'.join(methods) + suffix
        with tempfile.TemporaryDirectory(prefix="wot-effect-owner-test-") as directory:
            path = Path(directory)
            (path / "test.cpp").write_text(source)
            command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
                       str(path / "test.cpp"), "-o", str(path / "test")]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(path / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
