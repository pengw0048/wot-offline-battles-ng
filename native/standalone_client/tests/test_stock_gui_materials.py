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
FILES = ('simple_gui_component.hpp', 'simple_gui_component.cpp', 'text_gui_component.cpp')


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


if __name__ == '__main__':
    unittest.main()
