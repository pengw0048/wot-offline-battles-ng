"""Run the actual SDK shadow retire methods with retaining legacy D3DX effects."""
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

def function(source,signature):
    start=source.index(signature);end=source.index('{',start);depth=1
    while depth:
        end+=1;depth+=(source[end]=='{')-(source[end]=='}')
    return source[start:end+1]

HOST=r'''
#include <cassert>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#define BW_GUARD
struct Texture{unsigned refs=1;};
struct Target{Texture* value=nullptr;void assign(Texture& v){value=&v;}Target&operator=(std::nullptr_t){if(value){if(value->refs!=1)throw std::runtime_error("retirement before receiver input release");--value->refs;value=nullptr;}return *this;}};
struct ID3DXEffect{std::map<std::string,Texture*> textures;
void SetTexture(const char* name,Texture* value){auto&old=textures[name];if(old)--old->refs;old=value;if(value)++value->refs;}};
struct Managed{ID3DXEffect* effect=nullptr;ID3DXEffect*pEffect(){return effect;}};
struct Material{Managed managed;Managed*pEffect(){return &managed;}};
using MaterialPtr=std::shared_ptr<Material>;
struct DynamicShadow{Target m_dynamicShadowMapAtlas;bool m_unmanagedObjectsCreated=true;
std::vector<float>m_stockShadowDepthBiases={.1f,.2f};MaterialPtr m_fillScreenMap;
void deleteUnmanagedObjects();};
struct SemiDynamicShadow{struct Impl{static const int NUM_INDIRECTION_STAGING_TEXTURES=2,QL_TOTAL=2;
Target m_indirectionMapProjStagingPtr[2],m_indirectionMapTcStagingPtr[2];Target m_indirectionMapProjPtr,m_indirectionMapTcPtr;
Target m_shadowMapAtlas[2],m_shadowMapRT,m_emptyRT;MaterialPtr m_receiveEM;void deleteUnmanagedObjects();};};
@DYNAMIC@
@SEMI@
int main(){try{
Texture dynamicTexture;ID3DXEffect dynamicEffect;DynamicShadow dynamic;
dynamic.m_dynamicShadowMapAtlas.assign(dynamicTexture);dynamicEffect.SetTexture("g_shadowMapAtlas",&dynamicTexture);
dynamic.m_fillScreenMap=std::make_shared<Material>();dynamic.m_fillScreenMap->managed.effect=&dynamicEffect;
dynamic.deleteUnmanagedObjects();dynamic.deleteUnmanagedObjects();assert(dynamicTexture.refs==0&&!dynamic.m_unmanagedObjectsCreated);
Texture projection,coordinates,high,low;ID3DXEffect semiEffect;SemiDynamicShadow::Impl semi;
semi.m_indirectionMapProjPtr.assign(projection);semi.m_indirectionMapTcPtr.assign(coordinates);
semi.m_shadowMapAtlas[0].assign(high);semi.m_shadowMapAtlas[1].assign(low);
semiEffect.SetTexture("g_indirectionMapProj",&projection);semiEffect.SetTexture("g_indirectionMapTc",&coordinates);
semiEffect.SetTexture("g_shadowMapHeight",&high);semiEffect.SetTexture("g_shadowMapLow",&low);
semi.m_receiveEM=std::make_shared<Material>();semi.m_receiveEM->managed.effect=&semiEffect;
semi.deleteUnmanagedObjects();semi.deleteUnmanagedObjects();assert(projection.refs==0&&coordinates.refs==0&&high.refs==0&&low.refs==0);
// Partial startup and already-retired managed effects are valid teardown states.
DynamicShadow partial;partial.deleteUnmanagedObjects();partial.m_fillScreenMap=std::make_shared<Material>();partial.deleteUnmanagedObjects();
SemiDynamicShadow::Impl partialSemi;partialSemi.deleteUnmanagedObjects();partialSemi.m_receiveEM=std::make_shared<Material>();partialSemi.deleteUnmanagedObjects();
std::cout<<"Legacy shadow receiver releases all DEFAULT-pool inputs before targets\n";
}catch(const std::exception&error){std::cerr<<error.what()<<'\n';return 1;}}
'''

class ShadowTextureRetirementTests(unittest.TestCase):
    def test_actual_sdk_retirement_and_old_retaining_effect_controls(self):
        archive=Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():self.skipTest('Pinned public SDK archive required')
        spec=importlib.util.spec_from_file_location('retirement_patch',ROOT/'bigworld/patch_stock_deferred_buffers.py')
        patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)
        with tempfile.TemporaryDirectory(prefix='shadow-texture-retirement-') as temporary:
            root=Path(temporary)
            files=set(re.findall(r"['\"]((?:lib|client)/[^'\"]+\.(?:cpp|hpp|ipp))['\"]",(ROOT/'bigworld/patch_stock_deferred_buffers.py').read_text()))
            with tarfile.open(archive) as sdk:
                prefix=sdk.getmembers()[0].name.split('/')[0]+'/programming/bigworld/'
                members={m.name:m for m in sdk.getmembers()}
                for name in files:
                    if prefix+name not in members:continue
                    out=root/name;out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(sdk.extractfile(members[prefix+name]).read())
            dynamic=root/'lib/moo/dynamic_shadow.cpp';semi=root/'lib/moo/semi_dynamic_shadow.cpp'
            old_dynamic=function(dynamic.read_text(),'void DynamicShadow::deleteUnmanagedObjects()')
            old_semi=function(semi.read_text(),'void SemiDynamicShadow::Impl::deleteUnmanagedObjects()')
            patch.apply(root)
            fixed_dynamic=function(dynamic.read_text(),'void DynamicShadow::deleteUnmanagedObjects()')
            fixed_semi=function(semi.read_text(),'void SemiDynamicShadow::Impl::deleteUnmanagedObjects()')
            for label,dynamic_body,semi_body in [('fixed',fixed_dynamic,fixed_semi),('old_dynamic',old_dynamic,fixed_semi),('old_semi',fixed_dynamic,old_semi)]:
                source=HOST.replace('@DYNAMIC@',dynamic_body).replace('@SEMI@',semi_body)
                (root/'probe.cpp').write_text(source)
                binary=root/label
                result=subprocess.run(['c++','-std=c++14','-Wall','-Wextra','-Werror',str(root/'probe.cpp'),'-o',str(binary)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                result=subprocess.run([str(binary)],capture_output=True,text=True)
                if label=='fixed':self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                else:
                    self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
                    self.assertIn('retirement before receiver input release',result.stderr)

if __name__=='__main__':unittest.main()
