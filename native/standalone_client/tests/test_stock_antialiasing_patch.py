"""Verify actual RenderContext reset consumption and transactional AA patches."""
import importlib.util
import itertools
import os
import re
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load_patch():
    spec = importlib.util.spec_from_file_location('antialiasing_patch', ROOT/'bigworld/patch_stock_antialiasing.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fresh_engine(root):
    archive = Path(os.environ.get('WOT_PUBLIC_BIGWORLD_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    with tarfile.open(archive) as original:
        prefix = original.getmembers()[0].name.split('/')[0] + '/programming/bigworld/'
        for name in ('lib/moo/render_context.hpp', 'lib/moo/render_context.cpp', 'client/device_app.cpp',
                     'lib/moo/deferred_pipeline.cpp','client/lens_app.cpp','lib/romp/hdr_support.cpp'):
            target = root/name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(original.extractfile(prefix+name).read())


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end+1]


class AntialiasingPatchTests(unittest.TestCase):
    def test_actual_device_preference_read_write_and_initial_queue(self):
        with tempfile.TemporaryDirectory()as folder:
            root=Path(folder);fresh_engine(root);load_patch().patch(root)
            source=(root/'client/device_app.cpp').read_text()
            initialise=function(source,'bool DeviceApp::init()')
            save=function(source,'bool DeviceApp::savePreferences()')
            reads='\n'.join(line.strip()for line in initialise.splitlines()if
                'stockCustomAAMode = graphicsPreferences->readUInt('in line or
                'stockMultisamplingType = static_cast<uint32>(devicePreferences->readInt('in line or
                'stockMultisamplingQuality = static_cast<uint32>(devicePreferences->readInt('in line)
            writes='\n'.join(line.strip()for line in save.splitlines()if'devPref->writeUInt("multisampling'in line)
            writes+=function(save,'if (const auto* stockAAOwner = stockDeferredBuffers())')
            header=(root/'lib/moo/render_context.hpp').read_text()
            restore=function(header,'void restoreStockMultisampling(')
            self.assertLess(initialise.index('restoreStockMultisampling('),initialise.index('Moo::rc().createDevice('))
            self.assertGreater(initialise.index('stockAAOwner->customAAMode('),initialise.index('Could not create Direct3D device'))
            host=r'''
#include "wot/stock_antialiasing.hpp"
#include <cassert>
#include <map>
#include <string>
using uint32=unsigned;
struct Data{std::map<std::string,unsigned>values;unsigned readUInt(const char*n,unsigned d){auto p=values.find(n);return p==values.end()?d:p->second;}
 int readInt(const char*n,int d){return static_cast<int>(readUInt(n,static_cast<unsigned>(d)));}
 void writeUInt(const char*n,unsigned v){values[n]=v;}};
struct Context{wot::StockMultisamplingSelection stockMultisampling_;
 unsigned stockMultisamplingType()const{return stockMultisampling_.type;}
 unsigned stockMultisamplingQuality()const{return stockMultisampling_.quality;}
 @RESTORE@};
namespace Moo{Context&rc(){static Context c;return c;}}
struct Owner{unsigned mode=0;unsigned customAAMode()const{return mode;}};
Owner owner;bool deferred=true;Owner*stockDeferredBuffers(){return deferred?&owner:nullptr;}
void read(Data*graphicsPreferences,Data*devicePreferences){unsigned stockCustomAAMode=0,stockMultisamplingType=0,stockMultisamplingQuality=0;
 @READS@
 Moo::rc().restoreStockMultisampling(stockMultisamplingType,stockMultisamplingQuality);owner.mode=stockCustomAAMode<4?stockCustomAAMode:0;}
void write(Data*grPref,Data*devPref){@WRITES@}
int main(){Data graphics,device;read(&graphics,&device);
 assert(!owner.mode&&!Moo::rc().stockMultisampling_.pending&&!Moo::rc().stockMultisamplingType());
 for(unsigned mode:{0u,1u,2u,3u,4u,0xffffffffu})for(unsigned ms:{0u,4u,0x408u,0xffffffffu})for(unsigned q:{0u,7u}){
 graphics.values["customAAMode"]=mode;device.values["multisamplingType"]=ms;device.values["multisamplingQuality"]=q;
 Moo::rc().stockMultisampling_.request(16);read(&graphics,&device);
 assert(!Moo::rc().stockMultisampling_.pending&&Moo::rc().stockMultisamplingType()==ms&&Moo::rc().stockMultisamplingQuality()==q);
 Data outGraphics,outDevice;write(&outGraphics,&outDevice);
 assert(outGraphics.values["customAAMode"]==(mode<4?mode:0)&&outDevice.values["multisamplingType"]==ms&&outDevice.values["multisamplingQuality"]==q);
 // A same-type Python request must clear a restored nonzero quality and reset.
 Moo::rc().stockMultisampling_.request(ms);assert(Moo::rc().stockMultisampling_.pending==(q!=0)&&!Moo::rc().stockMultisamplingQuality());
 }
 deferred=false;Data outGraphics,outDevice;write(&outGraphics,&outDevice);assert(outGraphics.values.empty());
}
'''.replace('@RESTORE@',restore).replace('@READS@',reads).replace('@WRITES@',writes)
            cpp=root/'probe.cpp';binary=root/'probe';cpp.write_text(host)
            result=subprocess.run(['c++','-std=c++14','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),str(cpp),'-o',str(binary)],text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(binary)],text=True,capture_output=True);self.assertEqual(result.returncode,0,result.stderr)

    def test_all_resolution_deferred_hdr_orders_and_second_apply(self):
        names=('stock_antialiasing','stock_dynamic_resolution','stock_deferred_buffers','stock_startup_services')
        modules={};files=set()
        for name in names:
            path=ROOT/'bigworld'/('patch_'+name+'.py')
            spec=importlib.util.spec_from_file_location('aa_order_'+name,path)
            module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);modules[name]=module
            files.update(re.findall(r"['\"]((?:lib|client)/[^'\"]+\.(?:cpp|hpp|ipp))['\"]",path.read_text()))
        with tarfile.open(os.environ.get('WOT_PUBLIC_BIGWORLD_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz'))as source:
            members={m.name:m for m in source.getmembers()};prefix=next(iter(members)).split('/')[0]+'/programming/bigworld/'
            contents={name:source.extractfile(members[prefix+name]).read()for name in files if prefix+name in members}
        for order in itertools.permutations(names):
            with self.subTest(order=order),tempfile.TemporaryDirectory()as folder:
                root=Path(folder)
                for name,data in contents.items():
                    path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
                def apply(name):
                    module=modules[name];(getattr(module,'apply',None)or module.patch)(root)
                for name in order:apply(name)
                text=(root/'lib/moo/deferred_pipeline.cpp').read_text()
                body=function(text,'void DeferredPipeline::beginOpaqueDraw()')
                self.assertLess(body.index('setStockSceneViewport(&sceneViewport, true)'),body.index('beginOpaqueVelocity()'))
                self.assertLess(body.index('beginOpaqueVelocity()'),body.index('if (g_clearGBuffer)'))
                before={p.relative_to(root):p.read_bytes()for p in root.rglob('*')if p.is_file()}
                for name in reversed(order):apply(name)
                self.assertEqual(before,{p.relative_to(root):p.read_bytes()for p in root.rglob('*')if p.is_file()})

    def test_actual_render_context_reset_failure_and_reentry(self):
        patch = load_patch()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fresh_engine(root)
            patch.patch(root)
            source = (root/'lib/moo/render_context.cpp').read_text()
            body = function(source, 'bool RenderContext::applyStockMultisampling()')
            enabled = function(source, 'bool RenderContext::isMultisamplingEnabled() const')
            reset_begin=source.index('        const HRESULT stockResetResult = deviceEx_->ResetEx(')
            reset_end=source.index('\n    }',reset_begin)
            reset=source[reset_begin:reset_end]
            host = r'''
#include "wot/stock_antialiasing.hpp"
#include <cassert>
#include <functional>
#define FAILED(x) ((x)<0)
using HRESULT=int;
struct ResetDevice{int result=0;int ResetEx(void*,void*){return result;}};
const unsigned D3DMULTISAMPLE_NONE=0;
class RenderContext{public:
 wot::StockMultisamplingSelection stockMultisampling_;
 void*device_=this;bool succeeds=true;unsigned resets=0;
 struct{unsigned MultiSampleType=0;}presentParameters_;
 std::function<void()> callback;
 bool resetDevice(){++resets;const auto desired=stockMultisampling_.type;
  if(callback)callback();if(succeeds)presentParameters_.MultiSampleType=desired;return succeeds;}
 bool applyStockMultisampling();bool isMultisamplingEnabled()const;
};
struct ResetBoundary{ResetDevice device;ResetDevice*deviceEx_=&device;int presentParameters_=0;int errors=0;
 void printErrorMsg_(int,const char*){++errors;}bool reset();};
'''
            driver = r'''
int main(){RenderContext c;
 ResetBoundary ex;assert(ex.reset()&&!ex.errors);ex.device.result=-1;
 assert(!ex.reset()&&ex.errors==1);ex.device.result=0;assert(ex.reset()&&ex.errors==1);
 assert(c.applyStockMultisampling()&&!c.resets&&!c.isMultisamplingEnabled());
 c.stockMultisampling_.request(4);assert(!c.resets&&!c.isMultisamplingEnabled());
 c.device_=nullptr;assert(!c.applyStockMultisampling()&&c.stockMultisampling_.pending&&!c.resets);
 c.device_=&c;c.succeeds=false;assert(!c.applyStockMultisampling()&&c.resets==1&&c.stockMultisampling_.pending&&!c.isMultisamplingEnabled());
 c.succeeds=true;c.callback=[&](){c.stockMultisampling_.request(8);};
 assert(c.applyStockMultisampling()&&c.resets==2&&c.presentParameters_.MultiSampleType==4);
 assert(c.stockMultisampling_.type==8&&c.stockMultisampling_.pending);
 c.callback=nullptr;assert(c.applyStockMultisampling()&&c.resets==3&&c.presentParameters_.MultiSampleType==8&&!c.stockMultisampling_.pending);
 c.stockMultisampling_.request(8);assert(c.applyStockMultisampling()&&c.resets==3);
 c.stockMultisampling_.quality=7;c.stockMultisampling_.request(8);
 assert(c.stockMultisampling_.quality==0&&c.stockMultisampling_.pending);
 assert(c.applyStockMultisampling()&&c.resets==4);
 c.stockMultisampling_.request(0);assert(c.applyStockMultisampling()&&!c.isMultisamplingEnabled());
}
'''
            cpp = root/'probe.cpp'
            binary = root/'probe'
            cpp.write_text(host+body+enabled+'\nbool ResetBoundary::reset(){void*pFullScreenDesc=nullptr;\n'+reset+'\nreturn true;}\n'+driver)
            result = subprocess.run(['c++','-std=c++14','-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'include'),str(cpp),'-o',str(binary)],text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result = subprocess.run([str(binary)],text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_patch_twice_and_transactional_anchor_failure(self):
        patch = load_patch()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fresh_engine(root)
            patch.patch(root)
            names = ('lib/moo/render_context.hpp','lib/moo/render_context.cpp','client/device_app.cpp',
                     'lib/moo/deferred_pipeline.cpp','client/lens_app.cpp','lib/romp/hdr_support.cpp')
            once = [(root/name).read_bytes() for name in names]
            patch.patch(root)
            self.assertEqual(once,[(root/name).read_bytes() for name in names])
            self.assertIn('presentParameters_.MultiSampleQuality = stockSamples.quality;',once[1].decode())
            self.assertNotIn('customAA_.reset(new CustomAA());',once[1].decode())
            hdr=once[5].decode()
            self.assertLess(hdr.index('calcAvgLumOnGPU();'),hdr.index('if (!resolveStockTemporalHDR('))
            self.assertLess(hdr.index('if (!resolveStockTemporalHDR('),hdr.index('        calcBloom();'))
            fresh_engine(root)
            path = root/'client/device_app.cpp'
            path.write_text(path.read_text().replace('    Moo::rc().nextFrame();','    changedFrameOwner();'))
            original = [(root/name).read_bytes() for name in names]
            with self.assertRaises(ValueError):patch.patch(root)
            self.assertEqual(original,[(root/name).read_bytes() for name in names])
