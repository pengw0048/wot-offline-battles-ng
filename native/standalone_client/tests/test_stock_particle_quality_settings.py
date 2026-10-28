"""Execute the production quality callbacks against the native registry contract."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ParticleQualitySettingsTest(unittest.TestCase):
    def test_original_option_profiles_and_hdr_ownership(self):
        if not shutil.which(os.environ.get('CXX', 'clang++')):
            raise unittest.SkipTest('C++ compiler unavailable: ' + os.environ.get('CXX', 'clang++'))
        source = (ROOT / 'bigworld/stock_particle_quality.cpp').read_text()
        callbacks = source[source.index('class ParticleQualitySetting'):
                           source.index('wot::StockParticleQualityProfile readProfile')]
        accessor = source[source.index('wot::StockParticleQuality & stockParticleQuality()'):
                          source.index('bool initialiseStockParticleQualityProfiles()')]
        registration = source[source.index('bool initialiseStockParticleQualitySettings()'):
                              source.index('BW_END_NAMESPACE')]
        host = r'''
#include "wot/stock_particle_quality.hpp"
#include <cassert>
#include <map>
#include <memory>
int errors=0;
void* PyExc_RuntimeError=nullptr;
void PyErr_Format(void*,const char*,const char*) { ++errors; }
struct HDRSupport {
    static int writes; static bool enabled;
    static void setGlobalEnabled(bool value) { ++writes; enabled=value; }
};
int HDRSupport::writes=0;
bool HDRSupport::enabled=false;
bool initialiseStockParticleQualitySetting(bool);
namespace Moo {
template<class T> struct Ptr : std::shared_ptr<T> {
    Ptr()=default; Ptr(T* p):std::shared_ptr<T>(p) {}
    T* getObject() const { return this->get(); }
};
struct GraphicsSetting {
    using GraphicsSettingPtr=Ptr<GraphicsSetting>;
    struct Option { std::string name; bool supported; };
    std::string label; int active;
    std::vector<Option> options;
    GraphicsSetting(const char* l,const char*,int a,bool delayed,bool restart)
        :label(l),active(a) { assert(!delayed && !restart); }
    virtual ~GraphicsSetting()=default;
    virtual void onOptionSelected(int)=0;
    void addOption(const char* name,const char*,bool supported) {
        options.push_back({name,supported});
    }
    static std::map<std::string,GraphicsSettingPtr> registry;
    static std::map<std::string,int> latent;
    static GraphicsSettingPtr getFromLabel(const char* label) { return registry[label]; }
    static void add(GraphicsSettingPtr value) {
        const auto previous=latent.find(value->label);
        if(previous!=latent.end() && previous->second!=value->active) {
            value->active=previous->second;
            value->onOptionSelected(value->active);
        }
        registry[value->label]=value;
    }
};
std::map<std::string,GraphicsSetting::GraphicsSettingPtr> GraphicsSetting::registry;
std::map<std::string,int> GraphicsSetting::latent;
}
'''
        cases = r'''
int main() {
    auto& quality=stockParticleQuality();
    for(const char* label:{"MAX","HIGH","MEDIUM","LOW","SNIPER"}) {
        quality.register_profile(label,false,{});
        quality.register_profile(label,true,{});
    }
    assert(initialiseStockParticleQualitySettings());
    auto normal=Moo::GraphicsSetting::getFromLabel("EFFECTS_QUALITY");
    auto sniper=Moo::GraphicsSetting::getFromLabel("SNIPER_MODE_EFFECTS_QUALITY");
    assert(normal && sniper && normal->active==0 && sniper->active==0);
    assert(normal->options.size()==5 && sniper->options.size()==4);
    assert(normal->options[0].name=="MAX" && sniper->options[0].name=="HIGH");
    assert(quality.normal().id==2 && quality.sniper().id==6 && HDRSupport::writes==0);
    for(int option=0;option<5;++option) {
        normal->onOptionSelected(option);
        assert(quality.normal().id==(option==4?9:option+1));
        assert(HDRSupport::enabled==(option<3));
        assert(HDRSupport::writes==option+1);
    }
    const int expected[]={5,7,8,9};
    for(int option=0;option<4;++option) {
        sniper->onOptionSelected(option);
        assert(quality.sniper().id==expected[option]);
        assert(quality.normal().id==9 && HDRSupport::writes==5);
    }
    normal->onOptionSelected(-1);normal->onOptionSelected(5);
    sniper->onOptionSelected(-1);sniper->onOptionSelected(4);
    assert(quality.normal().id==9 && quality.sniper().id==9 && HDRSupport::writes==5);
    assert(initialiseStockParticleQualitySettings());
    assert(Moo::GraphicsSetting::getFromLabel("EFFECTS_QUALITY")==normal);
    assert(Moo::GraphicsSetting::getFromLabel("SNIPER_MODE_EFFECTS_QUALITY")==sniper);
    assert(HDRSupport::writes==5 && errors==0);
    Moo::GraphicsSetting::registry.clear();
    Moo::GraphicsSetting::latent["EFFECTS_QUALITY"]=1;
    Moo::GraphicsSetting::latent["SNIPER_MODE_EFFECTS_QUALITY"]=2;
    assert(initialiseStockParticleQualitySettings());
    assert(quality.normal().id==2 && quality.sniper().id==8);
    assert(HDRSupport::enabled && HDRSupport::writes==6);
}
'''
        with tempfile.TemporaryDirectory(prefix='stock-particle-settings-') as directory:
            path = Path(directory)
            (path / 'probe.cpp').write_text(host + accessor + callbacks + registration + cases)
            subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17',
                            '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                            '-I', str(ROOT / 'include'), str(path / 'probe.cpp'),
                            '-o', str(path / 'probe')], check=True)
            subprocess.run([str(path / 'probe')], check=True)


if __name__ == '__main__':
    unittest.main()
