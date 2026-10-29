"""Compile the real track kernel and material clone against shared properties.

The host reproduces the public SDK's shallow EffectPropertyPtr copy. This
checks per-vehicle isolation and original signed scroll receipts; it cannot
prove DX9 shader selection or rendering on Windows.
"""
from pathlib import Path
import importlib.util
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class VehicleFashionsTest(unittest.TestCase):
    def test_original_native_optional_material_binding(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Set WOT_0922_CLIENT for exact #1513 native evidence')
        executable = Path(client) / 'WorldOfTanks.exe'
        self.assertTrue(executable.is_file(), 'Configured exact client is missing')
        try:
            import pefile  # noqa: F401
            import unicorn  # noqa: F401
        except ImportError:
            self.skipTest('pefile and unicorn are required for original native instructions')
        from stock_vehicle_material_binding_oracle import audit
        self.assertEqual(audit(executable), 12)
        self.assertEqual(audit(executable, "vTransform_diffuse"), 12)

    def test_original_handler_shader_receipts(self):
        source = (ROOT / 'bigworld/stock_vehicle_fashions.cpp').read_text()
        begin = source.index('Vector4 packedVehicleColour(')
        end = source.index('\nstruct VehicleMaterialHandlerEntry', begin)
        host = r'''
#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
using uint32=std::uint32_t;
struct Vector4 {
    float x,y,z,w;
    Vector4(float a=0,float b=0,float c=0,float d=0):x(a),y(b),z(c),w(d) {}
    static Vector4 zero() { return {}; }
    float operator[](std::size_t i) const { return (&x)[i]; }
};
namespace Moo {
using BaseTexturePtr=std::shared_ptr<int>;
struct EffectMaterial;
struct Property {
    EffectMaterial* owner;
    std::string name;
    BaseTexturePtr texture;
    Property(EffectMaterial* o,const std::string& n):owner(o),name(n) {}
    bool getTexture(BaseTexturePtr& out) { out=texture;return true; }
    bool be(const Vector4& value);
    bool be(bool value);
    bool be(const BaseTexturePtr& value);
};
using EffectPropertyPtr=std::shared_ptr<Property>;
struct EffectMaterial {
    std::map<std::string,Vector4> vectors;
    std::map<std::string,bool> booleans;
    std::map<std::string,BaseTexturePtr> textures;
    std::map<std::string,EffectPropertyPtr> properties;
    unsigned missingWrites=0;
    EffectPropertyPtr expose(const std::string& n) {
        return properties[n]=std::make_shared<Property>(this,n);
    }
    template<class Value> bool setProperty(const std::string& n,const Value& v,bool) {
        auto property=getProperty(n);
        if(property) return property->be(v);
        ++missingWrites;return false;
    }
    EffectPropertyPtr getProperty(const std::string& n) {
        auto found=properties.find(n);
        return found==properties.end()?EffectPropertyPtr():found->second;
    }
};
bool Property::be(const Vector4& v) { owner->vectors[name]=v;return true; }
bool Property::be(bool v) { owner->booleans[name]=v;return true; }
bool Property::be(const BaseTexturePtr& v) { owner->textures[name]=v;return true; }
}
'''
        cases = r'''
int main() {
    auto red=packedVehicleColour(0xff0000ffU);
    assert(red.x==1.f && red.y==0.f && red.z==0.f && red.w==1.f);
    VehicleCamoState camo;
    camo.tiling={2.f,3.f,4.f,5.f};camo.weights={0.1f,0.2f,0.3f,0.4f};
    camo.colours[0]=0x00332211U;
    camo.tint=std::make_shared<int>(1);
    Moo::EffectMaterial legacy;
    for(const char* name:{"tintMap","exclusionMap","tintTiling","colorTint1",
        "colorTint2","colorTint3","colorTint4","g_useOldCamoDetail"}) legacy.expose(name);
    camo.apply(legacy);
    assert(legacy.textures.at("tintMap")==camo.tint);
    assert(legacy.vectors.at("tintTiling").w==5.f);
    assert(legacy.vectors.at("colorTint1").w==0.1f);
    assert(legacy.booleans.at("g_useOldCamoDetail"));
    legacy.expose("colorIdMap")->texture=std::make_shared<int>(2);
    camo.apply(legacy);
    assert(!legacy.booleans.at("g_useOldCamoDetail"));
    VehicleRepaintState repaint;
    repaint.base=packedVehicleColour(0xff0000ffU);
    Moo::EffectMaterial disabled;
    for(const char* name:{"g_useRepaint","g_baseColor","g_repaintColor1",
        "g_repaintColor2","g_repaintColor3","g_repaintMetallic","g_repaintGloss"})
        disabled.expose(name);
    repaint.apply(disabled);
    assert(!disabled.booleans.at("g_useRepaint"));
    assert(disabled.vectors.size()==1);
    repaint.enabled=true;repaint.metallic={1,2,3,4};repaint.gloss={5,6,7,8};
    repaint.fading=0.25f;repaint.quality=0.75f;
    repaint.apply(disabled);
    assert(disabled.booleans.at("g_useRepaint"));
    assert(disabled.vectors.at("g_repaintMetallic").w==0.75f);
    assert(disabled.vectors.at("g_repaintGloss").w==0.25f);

    // Track, non-colourised and partial effects share the same handlers.
    // An absent parameter is not an asset error and must not be created.
    Moo::EffectMaterial track,partial,unrelated;
    track.expose("g_useRepaint");
    partial.expose("colorTint2");partial.expose("g_repaintGloss");
    unrelated.expose("diffuseMap");
    const auto unrelatedProperty=unrelated.getProperty("diffuseMap");
    for(unsigned frame=0;frame<200;++frame) {
        for(auto* material:{&track,&partial,&unrelated}) {
            camo.apply(*material);repaint.apply(*material);
        }
    }
    if(track.missingWrites || partial.missingWrites || unrelated.missingWrites) return 11;
    assert(track.properties.size()==1 && track.vectors.empty() && track.textures.empty());
    assert(track.booleans.at("g_useRepaint"));
    assert(partial.properties.size()==2 && partial.vectors.size()==2);
    assert(partial.vectors.at("colorTint2").w==0.2f);
    assert(partial.vectors.at("g_repaintGloss").w==0.25f);
    assert(unrelated.properties.size()==1 && unrelated.vectors.empty() &&
        unrelated.booleans.empty() && unrelated.textures.empty());
    assert(unrelated.getProperty("diffuseMap")==unrelatedProperty);
    // A later material binding can expose a previously absent property.
    track.expose("g_baseColor");repaint.apply(track);
    assert(track.vectors.at("g_baseColor").x==1.f);
    assert(!legacy.missingWrites && !disabled.missingWrites);
}
'''
        with tempfile.TemporaryDirectory(prefix='stock-handler-') as directory:
            path = Path(directory)
            (path / 'probe.cpp').write_text(host + source[begin:end] + cases)
            subprocess.run([
                os.environ.get('CXX', 'clang++'), '-std=c++17', '-Wall',
                '-Wextra', '-Werror', '-ffp-contract=off', str(path / 'probe.cpp'),
                '-o', str(path / 'probe')], check=True)
            subprocess.run([str(path / 'probe')], check=True)
            # Restore the former unconditional writer in the actual handler
            # path. The realistic missing-property guard must reject it.
            body = source[begin:end]
            old = ('Moo::EffectPropertyPtr property = material.getProperty( name );\n'
                   '    if (property)\n        property->be( value );')
            self.assertEqual(body.count(old), 1)
            body = body.replace(old, 'material.setProperty( name, value, false );')
            (path / 'probe.cpp').write_text(host + body + cases)
            subprocess.run([
                os.environ.get('CXX', 'clang++'), '-std=c++17', '-Wall',
                '-Wextra', '-Werror', str(path / 'probe.cpp'),
                '-o', str(path / 'before')], check=True)
            self.assertEqual(subprocess.run([str(path / 'before')]).returncode, 11)

    def test_optional_track_properties_and_lod_restoration(self):
        source = (ROOT / 'bigworld/stock_vehicle_fashions.cpp').read_text()
        binding_begin = source.index('struct VehicleTrackMaterialBinding\n')
        binding_end = source.index('\n// WGBaseFashion embeds', binding_begin)
        clone_begin = source.index('Moo::ComplexEffectMaterialPtr cloneMaterial(')
        clone_end = source.index('\nMoo::ComplexEffectMaterialPtr materialVariant(', clone_begin)
        dress_begin = source.index('    void undress()\n')
        dress_end = source.index('\n};\n\nclass VehicleMaterialFashion', dress_begin)
        host = r"""
#include <cassert>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
struct Vector4 {
    float x,y,z,w;
    Vector4(float a=0,float b=0,float c=0,float d=0):x(a),y(b),z(c),w(d) {}
};
struct XMLSection {
    Vector4 value;
    explicit XMLSection(const char*) {}
    void setVector4(const Vector4& v) { value=v; }
};
template<class T> struct Ptr : std::shared_ptr<T> {
    Ptr()=default;
    Ptr(T* p):std::shared_ptr<T>(p) {}
    Ptr& operator=(T* p) { this->reset(p);return *this; }
};
using DataSectionPtr=Ptr<XMLSection>;
namespace Moo {
struct EffectProperty {
    bool vector;
    Vector4 value;
    unsigned writes=0;
    explicit EffectProperty(bool v=true):vector(v) {}
    EffectProperty* clone() const { return new EffectProperty(*this); }
    bool getVector(Vector4& v) { if(!vector)return false;v=value;return true; }
    bool be(const Vector4& v) { ++writes;if(!vector)return false;value=v;return true; }
    bool be(float v) { ++writes;value.w=v;return true; }
};
using EffectPropertyPtr=Ptr<EffectProperty>;
struct EffectPropertyFactory {
    static const Ptr<EffectPropertyFactory>& findFactory(const char*) {
        static const Ptr<EffectPropertyFactory> factory=new EffectPropertyFactory();
        return factory;
    }
    EffectPropertyPtr create(const char*,DataSectionPtr section) {
        EffectPropertyPtr property=new EffectProperty();
        property->value=section->value;return property;
    }
};
using EffectPropertyFactoryPtr=Ptr<EffectPropertyFactory>;
struct EffectMaterial {
    using Properties=std::map<std::string,EffectPropertyPtr>;
    Properties values;
    unsigned absentWrites=0;
    Properties& properties() { return values; }
    EffectPropertyPtr getProperty(const char* name) {
        auto found=values.find(name);
        return found==values.end()?EffectPropertyPtr():found->second;
    }
    bool replaceProperty(const char* name,EffectPropertyPtr value) {
        auto found=values.find(name);
        if(found==values.end()) { ++absentWrites;return false; }
        found->second=value;return true;
    }
    template<class T> bool setProperty(const char* name,const T& value,bool) {
        auto property=getProperty(name);
        if(!property) { ++absentWrites;return false; }
        return property->be(value);
    }
};
using EffectMaterialPtr=Ptr<EffectMaterial>;
struct ComplexEffectMaterial {
    Ptr<EffectMaterial> material=new EffectMaterial();
    ComplexEffectMaterial()=default;
    ComplexEffectMaterial(const ComplexEffectMaterial& other):
        material(new EffectMaterial(*other.material)) {}
    Ptr<EffectMaterial>& baseMaterial() { return material; }
};
using ComplexEffectMaterialPtr=Ptr<ComplexEffectMaterial>;
struct Visual { struct PrimitiveGroup { ComplexEffectMaterialPtr material_; }; };
}
"""
        state = r"""
struct Handler { void apply(Moo::EffectMaterial&) {} };
struct HandlerEntry { std::shared_ptr<Handler> state; };
struct VehicleFashionState {
    std::vector<VehicleMaterialBinding> bindings;
    std::vector<std::pair<Moo::Visual::PrimitiveGroup*,Moo::ComplexEffectMaterialPtr>> saved;
    std::vector<HandlerEntry> handlers;
    unsigned hiddenTracks=0;
    float alpha=1.f;
    bool trackConfigured=true,crashed=false,tracksBeyondLod=false;
    float crashCoefficient[2]={0.25f,0.75f};
    struct { float left=0.125f,right=-0.375f; } tracks;
"""
        cases = r"""
};
int main() {
    const char* uv="vTransform_diffuse";
    Moo::ComplexEffectMaterialPtr original=new Moo::ComplexEffectMaterial();
    original->baseMaterial()->values[uv]=new Moo::EffectProperty();
    original->baseMaterial()->values[uv]->value={3,4,5,6};
    original->baseMaterial()->values["crash_coefficient"]=new Moo::EffectProperty(false);
    auto first=cloneMaterial(original),second=cloneMaterial(original);
    auto authoredFirst=first->baseMaterial()->getProperty(uv);
    auto authoredSecond=second->baseMaterial()->getProperty(uv);
    auto authoredOriginal=original->baseMaterial()->getProperty(uv);
    assert(authoredFirst!=authoredSecond && authoredFirst!=authoredOriginal);
    Moo::ComplexEffectMaterialPtr distant=new Moo::ComplexEffectMaterial();
    distant->baseMaterial()->values["crash_coefficient"]=new Moo::EffectProperty(false);
    Moo::ComplexEffectMaterialPtr alpha=new Moo::ComplexEffectMaterial();
    alpha->baseMaterial()->values[uv]=new Moo::EffectProperty();
    alpha->baseMaterial()->values[uv]->value={7,8,9,10};
    alpha->baseMaterial()->values["wgOutputAlphaModifier"]=new Moo::EffectProperty(false);
    const auto authoredAlpha=alpha->baseMaterial()->getProperty(uv);
    Moo::Visual::PrimitiveGroup nearGroup{original},farGroup{distant};
    VehicleMaterialBinding near{},far{};
    near.group=&nearGroup;near.original=original;near.normal=first;near.alpha=alpha;
    near.trackSide=1;
    assert(near.normalTracks.bind(*first->baseMaterial()));
    assert(near.alphaTracks.bind(*alpha->baseMaterial()));
    far.group=&farGroup;far.original=distant;far.normal=distant;far.trackSide=2;
    assert(far.normalTracks.bind(*distant->baseMaterial()));
    assert(!far.normalTracks.authoredUV && !far.normalTracks.scrollingUV);
    VehicleFashionState fashion;fashion.bindings={near,far};
    fashion.dress();
    auto scroll=first->baseMaterial()->getProperty(uv);
    assert(scroll!=authoredFirst && scroll->value.y==1.f && scroll->value.w==0.125f);
    assert(authoredFirst->value.w==6.f && authoredFirst->writes==0);
    assert(authoredSecond->value.w==6.f && authoredOriginal->value.w==6.f);
    assert(farGroup.material_==distant && distant->baseMaterial()->values.size()==1);
    fashion.tracksBeyondLod=true;fashion.dress();
    assert(first->baseMaterial()->getProperty(uv)==authoredFirst);
    fashion.alpha=0.5f;fashion.tracksBeyondLod=false;fashion.dress();
    assert(nearGroup.material_==alpha);
    assert(alpha->baseMaterial()->getProperty(uv)!=authoredAlpha);
    assert(alpha->baseMaterial()->getProperty(uv)->value.w==0.125f);
    fashion.tracksBeyondLod=true;fashion.dress();
    assert(alpha->baseMaterial()->getProperty(uv)==authoredAlpha);
    assert(authoredAlpha->value.w==10.f && authoredAlpha->writes==0);
    // An alpha variant with no UV slot remains optional independently.
    fashion.bindings[0].alpha=distant;
    fashion.bindings[0].alphaTracks=VehicleTrackMaterialBinding();
    assert(fashion.bindings[0].alphaTracks.bind(*distant->baseMaterial()));
    distant->baseMaterial()->values["wgOutputAlphaModifier"]=new Moo::EffectProperty(false);
    fashion.dress();assert(nearGroup.material_==distant);
    assert(!distant->baseMaterial()->getProperty(uv));
    // The original binder replaces a matching slot without consulting its
    // previous property type, then restores that exact pointer beyond LOD.
    Moo::EffectMaterial wrongType;
    wrongType.values[uv]=new Moo::EffectProperty(false);
    auto authoredWrong=wrongType.getProperty(uv);
    VehicleTrackMaterialBinding replacement;
    assert(replacement.bind(wrongType));
    replacement.apply(wrongType,false,{0,1,0,-0.25f});
    assert(wrongType.getProperty(uv)!=authoredWrong);
    assert(wrongType.getProperty(uv)->vector && wrongType.getProperty(uv)->value.w==-0.25f);
    replacement.apply(wrongType,true,{0,1,0,0});
    assert(wrongType.getProperty(uv)==authoredWrong && authoredWrong->writes==0);
    // UV capability must not erase track identity: hiding and crash handling
    // still affect the distant material which lacks the scroll parameter.
    fashion.alpha=1.f;fashion.hiddenTracks=2;fashion.dress();
    assert(!farGroup.material_);
    fashion.hiddenTracks=0;fashion.crashed=true;fashion.dress();
    assert(farGroup.material_==distant);
    assert(distant->baseMaterial()->getProperty("crash_coefficient")->value.w==0.75f);
    fashion.undress();
    assert(nearGroup.material_==original && farGroup.material_==distant);
    assert(!first->baseMaterial()->absentWrites && !alpha->baseMaterial()->absentWrites &&
        !distant->baseMaterial()->absentWrites && !wrongType.absentWrites);
}
"""
        with tempfile.TemporaryDirectory(prefix='stock-track-binding-') as directory:
            path = Path(directory)
            (path / 'probe.cpp').write_text(host + source[clone_begin:clone_end] +
                source[binding_begin:binding_end] + state + source[dress_begin:dress_end] + cases)
            subprocess.run([
                os.environ.get('CXX', 'clang++'), '-std=c++17', '-Wall',
                '-Wextra', '-Werror', str(path / 'probe.cpp'), '-o', str(path / 'probe')], check=True)
            subprocess.run([str(path / 'probe')], check=True)

    def test_track_receipts_and_property_isolation(self):
        source = (ROOT / 'bigworld/stock_vehicle_fashions.cpp').read_text()
        begin = source.index('struct VehicleTrackTextureState\n')
        end = source.index('\nbool excludedFromCamouflage(', begin)
        clone_begin = source.index('Moo::ComplexEffectMaterialPtr cloneMaterial(')
        clone_end = source.index('\nMoo::ComplexEffectMaterialPtr materialVariant(', clone_begin)
        host = r'''
#include <cassert>
#include <cmath>
#include <map>
#include <memory>
template<class T> struct Ptr : std::shared_ptr<T> {
    Ptr()=default;
    Ptr(T* p):std::shared_ptr<T>(p) {}
    Ptr& operator=(T* p) { this->reset(p);return *this; }
};
namespace Moo {
struct Property {
    float value=0.f;
    Property* clone() const { return new Property(*this); }
};
struct EffectMaterial {
    using Properties=std::map<int,Ptr<Property>>;
    Properties values;
    Properties& properties() { return values; }
};
struct ComplexEffectMaterial {
    Ptr<EffectMaterial> material=new EffectMaterial();
    ComplexEffectMaterial()=default;
    ComplexEffectMaterial(const ComplexEffectMaterial& other):
        material(new EffectMaterial(*other.material)) {}
    Ptr<EffectMaterial>& baseMaterial() { return material; }
};
using ComplexEffectMaterialPtr=Ptr<ComplexEffectMaterial>;
}
'''
        cases = r'''
int main() {
    Moo::ComplexEffectMaterialPtr source=new Moo::ComplexEffectMaterial();
    source->baseMaterial()->values[0]=new Moo::Property();
    source->baseMaterial()->values[0]->value=0.75f;
    auto first=cloneMaterial(source);
    auto second=cloneMaterial(source);
    first->baseMaterial()->values[0]->value=0.125f;
    assert(source->baseMaterial()->values[0]->value==0.75f);
    assert(second->baseMaterial()->values[0]->value==0.75f);
    assert(first->baseMaterial()->values[0].get()!=source->baseMaterial()->values[0].get());

    VehicleTrackTextureState tracks;
    tracks.update(1.25f,-1.25f,1.f,0);
    assert(tracks.left==0.25f && tracks.right==-0.25f);
    tracks.update(-0.5f,0.5f,1.f,0);
    assert(tracks.left==-0.25f && tracks.right==0.25f);
    tracks.update(0.5f,0.5f,1.f,1);
    assert(tracks.left==-0.25f && tracks.right==0.75f);
    tracks.update(-150.f,150.f,1.f,0);
    assert(tracks.left==-0.25f && tracks.right==0.75f);
    tracks.update(101.f,0.f,1.f,3);
    assert(tracks.left==0.f && tracks.right==0.f);
}
'''
        with tempfile.TemporaryDirectory(prefix='stock-fashion-') as directory:
            path = Path(directory)
            (path / 'probe.cpp').write_text(
                host + source[begin:end] + source[clone_begin:clone_end] + cases)
            subprocess.run([
                os.environ.get('CXX', 'clang++'), '-std=c++17', '-Wall',
                '-Wextra', '-Werror', '-ffp-contract=off', str(path / 'probe.cpp'),
                '-o', str(path / 'probe')], check=True)
            subprocess.run([str(path / 'probe')], check=True)

    def test_null_material_patch_is_narrow_and_idempotent(self):
        spec = importlib.util.spec_from_file_location(
            'fashion_patch', ROOT / 'bigworld/patch_vehicle_fashions.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        before = ('\tconst bool canUseHWInstancing = this->hwInstancingAvailable() &&\n'
                  '\t\t\t\t\t\t\t\t\timmediateModeCounter_ == 0;')
        with tempfile.TemporaryDirectory(prefix='stock-fashion-patch-') as directory:
            path = Path(directory) / 'lib/moo/draw_context.cpp'
            path.parent.mkdir(parents=True)
            path.write_text(before)
            patch.patch_engine(directory)
            first = path.read_text()
            self.assertIn('if (!material)\n\t\treturn;', first)
            patch.patch_engine(directory)
            self.assertEqual(path.read_text(), first)


if __name__ == '__main__':
    unittest.main()
