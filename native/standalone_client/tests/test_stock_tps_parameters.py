"""Original particle fade constants, lifecycle and authored shot shader use."""
import io
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import tempfile
import unittest
import zipfile

from native_test_compile_flags import native_test_compile_flags
from test_stock_cursor_camera import function

ROOT = Path(__file__).resolve().parents[1]
CORE = '#include "stock_tps_parameters.hpp"\n'
ORACLE = r'''
#include <iostream>
int main() {
    float fov;int sniper,quality,offscreen;
    while(std::cin.read(reinterpret_cast<char*>(&fov),4)) {
        std::cin.read(reinterpret_cast<char*>(&sniper),4);
        std::cin.read(reinterpret_cast<char*>(&quality),4);
        std::cin.read(reinterpret_cast<char*>(&offscreen),4);
        if(!std::cin)return 2;
        const auto value=wot::stockTPSParticleParameters(fov,sniper!=0,quality,offscreen!=0);
        std::cout.write(reinterpret_cast<const char*>(value.data()),sizeof(value));
    }
    return std::cin.eof()&&std::cout?0:2;
}
'''
LIFECYCLE = r'''
#include <atomic>
#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#define SUCCEEDED(value) ((value)>=0)
using D3DXHANDLE=const char*;
struct ID3DXEffect {
    std::array<float,8> value{};bool failed=false;
    int SetValue(D3DXHANDLE name,const void* bytes,unsigned size) {
        assert(std::string(name)=="ParticlesParams"&&size==sizeof(value));
        if(failed)return -1;std::memcpy(value.data(),bytes,size);return 0;
    }
};
namespace Moo {
struct EffectConstantValue {virtual ~EffectConstantValue()=default;virtual bool operator()(ID3DXEffect*,D3DXHANDLE)=0;};
struct EffectVisualContext {
    enum {AUTO_CONSTANT_TYPE_PER_FRAME};
    std::shared_ptr<EffectConstantValue> value;unsigned adds=0,removes=0;
    void registerAutoConstant(int kind,const char* name,EffectConstantValue* callback) {
        assert(kind==AUTO_CONSTANT_TYPE_PER_FRAME&&std::string(name)=="ParticlesParams"&&!value);
        value.reset(callback);++adds;
    }
    void unregisterAutoConstant(int kind,const char* name) {
        assert(kind==AUTO_CONSTANT_TYPE_PER_FRAME&&std::string(name)=="ParticlesParams"&&value);
        value.reset();++removes;
    }
};
struct Context {EffectVisualContext visual;EffectVisualContext& effectVisualContext(){return visual;}};
Context& rc(){static Context result;return result;}
}
struct Runtime {
    std::atomic<bool> closed{false};std::atomic<float> time{0.f};
    float fov=1.0471975f;bool sniper=false;struct {int render_quality=4;} quality;
    void close(){closed=true;}
};
bool qualityReady=true;
bool initialiseStockTPSQuality(){return qualityReady;}
void ensureStockTPSPerformer(){}
void finiStockTPSQuality(){}
std::vector<int> deferredDraws;
@PRODUCTION@
int main() {
    auto& visual=Moo::rc().visual;
    finiStockTPSRuntime();assert(visual.adds==0&&visual.removes==0);
    qualityReady=false;assert(!initialiseStockTPSRuntime()&&!currentRuntime()&&!visual.value);
    qualityReady=true;assert(initialiseStockTPSRuntime());auto first=currentRuntime();
    assert(initialiseStockTPSRuntime()&&currentRuntime()==first&&visual.adds==1);
    ID3DXEffect effect;assert((*visual.value)(&effect,"ParticlesParams"));
    const auto expected=wot::stockTPSParticleParameters(first->fov,false,4,false);
    assert(effect.value==expected&&effect.value[1]>0.f&&effect.value[2]<0.f);
    first->fov=.035f;first->sniper=true;first->quality.render_quality=3;
    assert((*visual.value)(&effect,"ParticlesParams"));
    assert(effect.value==wot::stockTPSParticleParameters(.035f,true,3,false));
    effect.failed=true;assert(!(*visual.value)(&effect,"ParticlesParams"));effect.failed=false;
    auto retained=visual.value;first->closed=true;assert(!(*retained)(&effect,"ParticlesParams"));
    finiStockTPSRuntime();assert(!visual.value&&!currentRuntime()&&visual.removes==1);
    assert(!(*retained)(&effect,"ParticlesParams"));
    finiStockTPSRuntime();assert(visual.removes==1);
    assert(initialiseStockTPSRuntime()&&currentRuntime()!=first&&visual.adds==2);
    assert((*visual.value)(&effect,"ParticlesParams")&&effect.value==expected);
    finiStockTPSRuntime();assert(visual.removes==2);
}
'''


class StockTPSParametersTests(unittest.TestCase):
    def client(self):
        value = os.environ.get('WOT_0922_CLIENT')
        if not value:
            self.skipTest('WOT_0922_CLIENT required for original particle evidence')
        result = Path(value)
        self.assertTrue((result / 'WorldOfTanks.exe').is_file())
        return result

    def compile(self, folder, source, extra=()):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('Host C++ compiler required')
        path, binary = folder / 'probe.cpp', folder / 'probe'
        path.write_text(CORE + source)
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                        '-ffp-contract=off', *native_test_compile_flags(compiler),
                        '-I'+str(ROOT/'bigworld'), '-I'+str(ROOT/'include'),
                        str(path), *(str(p) for p in extra), '-o', str(binary)], check=True)
        return binary

    def test_production_registration_retirement_and_reinitialisation(self):
        source = (ROOT/'bigworld/stock_tps_scene.cpp').read_text()
        start = source.index('std::shared_ptr<Runtime> runtime;')
        lease = source[start:source.index('\nstruct SceneSource\n', start)]
        production = lease + '\n' + '\n'.join(function(source, name) for name in (
            'bool initialiseStockTPSRuntime()', 'void finiStockTPSRuntime()'))
        with tempfile.TemporaryDirectory(prefix='tps-parameters-owner-') as folder:
            binary = self.compile(Path(folder), LIFECYCLE.replace('@PRODUCTION@', production))
            subprocess.run([str(binary)], check=True)

    def test_original_tick_and_semantic_callback(self):
        client = self.client()
        try:
            import pefile
            from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
            from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_MXCSR
        except ImportError as error:
            self.skipTest('Original PE oracle dependency missing: ' + str(error))
        pe = pefile.PE(str(client/'WorldOfTanks.exe'))
        base, image = pe.OPTIONAL_HEADER.ImageBase, pe.get_memory_mapped_image()
        self.assertEqual(image[0xf98c90-base:0xf98c96-base], bytes.fromhex('55 8b ec 83 ec 0c'))
        self.assertEqual(image[0xf9a370-base:0xf9a376-base], bytes.fromhex('55 8b ec 83 ec 3c'))
        machine = Uc(UC_ARCH_X86, UC_MODE_32)
        machine.mem_map(base, (len(image)+4095)&~4095);machine.mem_write(base,image)
        memory = 0x30000000;machine.mem_map(memory,0x20000)
        vector, fov_data, stack = memory, memory+0x100, memory+0x10000
        renderer, table, mode, mode_getter = (memory+x for x in (0x200,0x300,0x500,0x1000))
        fov_getter = memory+0x1100
        machine.mem_write(fov_getter,b'\xd9\x05'+struct.pack('<I',fov_data)+b'\xc3')
        machine.mem_write(renderer,struct.pack('<I',table));machine.mem_write(table+0xb4,struct.pack('<I',mode_getter))
        captured=[];current={}
        def returning(uc,value=0):
            sp=uc.reg_read(UC_X86_REG_ESP);target=struct.unpack('<I',uc.mem_read(sp,4))[0]
            uc.reg_write(UC_X86_REG_EAX,value);uc.reg_write(UC_X86_REG_ESP,sp+4);uc.reg_write(UC_X86_REG_EIP,target)
        def code(uc,address,unused_size,unused):
            if address==0xaeb4d0:returning(uc,vector)
            elif address==0xfa8100:returning(uc)
            elif address==0xaeb410:uc.reg_write(UC_X86_REG_EIP,fov_getter)
            elif address==0xaeb910:returning(uc,current['sniper'])
            elif address in (0xf9afa0,0xf9b120):
                # Profile publication is covered by stock_particle_quality tests.
                uc.mem_write(0x1a5be54,struct.pack('<i',current['quality']));returning(uc)
            elif address==0xf98dd9:uc.emu_stop()
            elif address==0x941a10:returning(uc,renderer)
            elif address==mode_getter:returning(uc,mode if current['offscreen'] else 0)
            elif address==0x74b870:
                sp=uc.reg_read(UC_X86_REG_ESP);start,end=struct.unpack('<II',uc.mem_read(sp+4,8))
                self.assertEqual(end-start,32);captured.append(bytes(uc.mem_read(start,32)));uc.emu_stop()
        machine.hook_add(UC_HOOK_CODE,code)
        rows=[(f,s,q,o) for f in (0.,.01,.035,.1,.15,.35,.75,1.0471975,2.8)
              for s in (0,1) for q in (-1,0,1,3,4,7) for o in (0,1)]
        rng=random.Random(1513)
        rows += [(rng.uniform(.001,3),i%2,i%5,i%3==0) for i in range(128)]
        inputs=bytearray()
        for fov,sniper,quality,offscreen in rows:
            encoded=struct.pack('<fiii',fov,sniper,quality,offscreen);inputs.extend(encoded)
            current.update(sniper=sniper,quality=quality,offscreen=offscreen)
            machine.mem_write(fov_data,encoded[:4]);machine.mem_write(mode+4,bytes([offscreen]))
            machine.mem_write(0x1a5be40,struct.pack('<5f',2.,7.,5.,2.,5.))
            machine.mem_write(0x2132f04,bytes(8));machine.mem_write(stack,bytes(16))
            machine.reg_write(UC_X86_REG_ECX,memory+0x2000);machine.reg_write(UC_X86_REG_ESP,stack)
            machine.reg_write(UC_X86_REG_MXCSR,0x1f80);machine.emu_start(0xf98c90,0xf98e39,count=1000)
            machine.mem_write(stack,struct.pack('<III',0,0,memory+0x3000))
            machine.reg_write(UC_X86_REG_ESP,stack);machine.emu_start(0xf9a370,0xf9a55a,count=1000)
        self.assertEqual(len(captured),len(rows))
        with tempfile.TemporaryDirectory(prefix='tps-parameters-native-') as folder:
            binary=self.compile(Path(folder),ORACLE)
            actual=subprocess.check_output([str(binary)],input=inputs)
        differences=[(rows[index],struct.unpack('<8f',actual[index*32:index*32+32]),struct.unpack('<8f',expected))
                     for index,expected in enumerate(captured) if actual[index*32:index*32+32]!=expected]
        self.assertEqual(len(differences), 0, differences[:4])

    def test_authored_shot_shader_alpha_consumes_parameters(self):
        client=self.client()
        with tempfile.TemporaryDirectory(prefix='tps-shot-parameters-') as folder:
            root=Path(folder);paths=[]
            with zipfile.ZipFile(client/'res/packages/shaders.pkg') as shaders:
                for pipeline in ('10','11'):
                    packed=shaders.read('shaders/wg_particles/ps.%s.dx9.fxo'%pipeline)
                    path=root/('ps-'+pipeline+'.effect')
                    path.write_bytes(zipfile.ZipFile(io.BytesIO(packed)).read('effect'));paths.append(path)
            with zipfile.ZipFile(client/'res/packages/particles.pkg') as particles:
                for pipeline in ('forward','deferred'):
                    path=root/('shot-'+pipeline+'.bin')
                    path.write_bytes(particles.read('particles/content_%s/PFX/Tank/shots/shot_small_01.bin'%pipeline))
                    paths.append(path)
            source=(ROOT/'tests/stock_tps_parameters_shader.cpp').read_text()
            production=(ROOT/'bigworld/stock_tps_scene.cpp').read_text()
            source=source.replace('@RESOURCE_STRING@',function(production,'BW::string resourceString('))
            start=production.index('        BW::string shader = resourceString(')
            end=production.index('\n        float values[12];',start)
            source=source.replace('@RENDER_KIND@',production[start:end])
            extra=[ROOT/'src'/name for name in ('stock_shader_binary.cpp','stock_effect_binding.cpp','stock_shader_runtime.cpp')]
            # The production resource reader keeps the exact descriptor offsets.
            source='#define WOT_STOCK_TPS_RESOURCES_PORTABLE\n#include "'+str(ROOT/'bigworld/stock_tps_resources.cpp')+'"\n'+source
            binary=self.compile(root,source,extra)
            subprocess.run([str(binary),*(str(p) for p in paths)],check=True)


if __name__=='__main__':
    unittest.main()
