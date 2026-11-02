"""Original #1513 region receipts and real fashion mask/restore dispatch.

The bytecode checks prove the stock caller contract. Native instruction checks
prove alpha decoding; neither proves a rendered or clickable Windows frame.
"""
from pathlib import Path
import json
import re
import os
import struct
import subprocess
import tempfile
import tarfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CustomizationHelperTest(unittest.TestCase):
    def test_exact_pyc_required_customization_surface_is_registered(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Set WOT_0922_CLIENT for exact bytecode dependency coverage')
        python = os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7')
        surface = json.loads(subprocess.check_output([python,
            str(ROOT / 'tests/stock_customization_surface.py'),
            str(Path(client) / 'res/packages/scripts.pkg')], text=True))
        native = '\n'.join(path.read_text() for path in (ROOT / 'bigworld').glob('*.cpp'))
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            self.skipTest('Pinned public engine sources are required for base exports')
        with tarfile.open(archive) as sdk:
            for member in sdk:
                if member.name.endswith(('/client/script_bigworld.cpp', '/lib/pyscript/py_callback.cpp')):
                    native += '\n' + sdk.extractfile(member).read().decode('utf-8')
        # Native registration macros are the shipped surface. Definitions or
        # string mentions elsewhere cannot satisfy an unguarded stock call.
        registered = set(re.findall(r'PY_FACTORY\(\s*(\w+)\s*,\s*BigWorld\s*\)', native))
        registered.update(re.findall(r'PY_AUTO_MODULE_FUNCTION\(\s*[^,]+,\s*(\w+)', native))
        registered.update(re.findall(r'PY_MODULE_FUNCTION\(\s*(\w+)\s*,\s*BigWorld', native))
        required = {ref['name'] for ref in surface['references']}
        missing = sorted(required - registered)
        self.assertEqual(missing, [], 'Missing exact customization native exports: %s' % missing)
        self.assertTrue({'PyCustomizationHelper', 'PyCamoHandler', 'PyRepaintHandler'} <= required)
        source = (ROOT / 'bigworld/stock_customization_helper.cpp').read_text()
        block = source.split('PY_BEGIN_METHODS( PyCustomizationHelper )', 1)[1].split('PY_END_METHODS()', 1)[0]
        methods = set(re.findall(r'PY_METHOD\(\s*(\w+)\s*\)', block))
        self.assertEqual(methods, {'getSelectionResult', 'setSelectionCallback', 'setSelectionMode',
            'setSelectingEnabled', 'setHighlightingEnabled', 'setCursorDragging'})
        self.assertTrue(set(surface['helper_methods']) <= methods)
        fashions = (ROOT / 'bigworld/stock_vehicle_fashions.cpp').read_text()
        for cls, schema in {
                'PyCamoHandler': {'setCamoParams'},
                'PyRepaintHandler': {'setRepaintParams'},
                'WGBaseFashion': {'activate', 'deactivate', 'initAlphaMaterials',
                    'setAlpha', 'setCamouflage', 'removeCamouflage', 'addMaterialHandler'},
                'WGVehicleFashion': {'setTracks', 'setLods', 'hideTracks',
                    'setCrashEffectCoeff', 'setSplineTrack'}}.items():
            block = fashions.split('PY_BEGIN_METHODS( %s )' % cls, 1)[1].split('PY_END_METHODS()', 1)[0]
            self.assertEqual(set(re.findall(r'PY_METHOD\(\s*(\w+)\s*\)', block)), schema)

        # Negative control: the original missing mandatory constructor must
        # fail coverage even though service.startHighlighter is conditional.
        without_helper = registered - {'PyCustomizationHelper'}
        self.assertIn('PyCustomizationHelper', required - without_helper)

    def test_compound_visit_and_render_target_surface_use_sdk_types(self):
        appearance = (ROOT / 'bigworld/vehicle_appearance.cpp').read_text()
        start = appearance.index('    bool visitParts( ')
        end = appearance.index('    virtual void enterWorld()', start)
        visitor = appearance[start:end]
        helper = (ROOT / 'bigworld/stock_customization_helper.cpp').read_text()
        start = helper.index('            ComObjectWrap<DX::Surface> surface;')
        end = helper.index('            if (!success) return false;', start)
        readback = helper[start:end] + '            if (!success) return false;'
        host = r"""
#include <cassert>
#include <functional>
#include <memory>
#include <vector>
using std::size_t;
struct Matrix { int pose=7; };
struct PyModel { int draws=0; };
template<class T> struct Borrowed {
    T* object;
    T* get() { return object; }
    const T* get() const { return object; }
};
struct CompoundPartRef { Borrowed<PyModel> model; size_t mount; };
struct Node { Matrix world; };
struct State { bool active=true; std::vector<Node> nodes{{},{}}; };
struct CompoundEvaluationScope { explicit CompoundEvaluationScope(const std::shared_ptr<State>&) {} };
PyModel models[2];
std::vector<CompoundPartRef> compoundPartRefs(State&) { return {{{&models[0]},0},{{&models[1]},1}}; }
struct Owner {
    std::shared_ptr<State> state_=std::make_shared<State>();
    bool shown=true;
    bool visible() const { return shown; }
@VISITOR@
};
namespace DX {
struct Surface { unsigned char pixel[4]={1,2,3,85}; bool failLock=false; };
struct BaseTexture {}; // Real RenderTarget::pTexture has this base interface.
}
struct D3DLOCKED_RECT { const void* pBits; };
const unsigned D3DLOCK_READONLY=1;
using HRESULT=int;
bool SUCCEEDED(HRESULT h) { return h>=0; }
template<class T> struct ComObjectWrap {
    T* object=nullptr;
    T* pComObject() const { return object; }
    T* operator->() { return object; }
};
struct SystemSurface : DX::Surface {
    unsigned locks=0,unlocks=0;
    HRESULT LockRect(D3DLOCKED_RECT* out,void*,unsigned) {
        ++locks;if(failLock)return -1;out->pBits=pixel;return 0;
    }
    void UnlockRect() { ++unlocks; }
};
struct Target {
    DX::Surface surface;unsigned calls=0;bool fail=false;
    DX::BaseTexture* pTexture() { return nullptr; }
    HRESULT pSurface(ComObjectWrap<DX::Surface>& out) { ++calls;out.object=&surface;return fail?-1:0; }
};
struct Device {
    unsigned copies=0;bool fail=false;
    HRESULT GetRenderTargetData(DX::Surface* source,SystemSurface* target) {
        ++copies;if(fail)return -1;
        for(unsigned i=0;i<4;++i) {
            target->pixel[i]=source->pixel[i];
        }
        return 0;
    }
};
namespace Moo {
struct RC { Device d;Device* device() { return &d; } };
RC& rc() { static RC r;return r; }
}
struct Readback {
    Target target;Target* readback_=&target;
    SystemSurface system;ComObjectWrap<SystemSurface> systemSurface_{&system};
    bool read(unsigned& raw,bool success=true) {
@READBACK@
        return true;
    }
};
int main() {
    Owner owner;unsigned visits=0;
    auto visit=[&](PyModel* model,const Matrix& pose,int part) {
        assert(model==&models[part] && pose.pose==7);++model->draws;++visits;
    };
    assert(owner.visitParts(visit) && visits==2);
    owner.shown=false;assert(!owner.visitParts(visit) && visits==2);
    owner.shown=true;
    assert(!owner.visitParts([&](PyModel*,const Matrix&,int) { owner.state_->active=false; }));
    assert(!owner.visitParts(visit) && visits==2);
    Readback gpu;unsigned raw=999;
    assert(gpu.read(raw) && raw==85 && gpu.system.unlocks==1);
    gpu.target.fail=true;assert(!gpu.read(raw) && gpu.system.locks==1);
    gpu.target.fail=false;Moo::rc().d.fail=true;
    assert(!gpu.read(raw) && gpu.system.locks==1);
    Moo::rc().d.fail=false;gpu.system.failLock=true;
    assert(!gpu.read(raw) && gpu.system.unlocks==1);
}
""".replace('@VISITOR@', visitor).replace('@READBACK@', readback)
        with tempfile.TemporaryDirectory(prefix='stock-customization-sdk-') as tmp:
            path = Path(tmp)
            source, binary = path / 'host.cpp', path / 'host'
            source.write_text(host)
            command = ['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)]
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True)
            # The two observed Windows errors must also fail this host. Its
            # pointer constness and texture inheritance reproduce the SDK ABI.
            for old in (
                    host.replace('std::vector<CompoundPartRef> parts =',
                                 'const std::vector<CompoundPartRef> parts ='),
                    host.replace('readback_->pSurface( surface )',
                                 'readback_->pTexture()->GetSurfaceLevel( 0, &surface )')):
                source.write_text(old)
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0, 'Old Windows compile regression escaped the host')

    def test_region_decoder_matches_original_instructions(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Set WOT_0922_CLIENT for exact native instructions')
        from stock_tree_lod_oracle import OriginalTreeInstructionOracle
        from unicorn import UC_HOOK_CODE
        from unicorn import x86_const as r
        original = OriginalTreeInstructionOracle(Path(client) / 'WorldOfTanks.exe')
        u = original.emulator
        owner = original.allocate(0x100)
        sample = original.allocate(8)
        pixel = original.allocate(4)
        stack = original.allocate(0x200)
        original.emulator.mem_write(sample, struct.pack('<I', pixel))
        u.reg_write(r.UC_X86_REG_EBX, owner)
        u.reg_write(r.UC_X86_REG_EBP, stack + 0x180)

        def conversion(unused, address, size, data):
            if address == 0x13d81d0:
                value = struct.unpack('<f', struct.pack('<I',
                    u.reg_read(r.UC_X86_REG_XMM0) & 0xffffffff))[0]
                esp = u.reg_read(r.UC_X86_REG_ESP)
                u.reg_write(r.UC_X86_REG_EAX, int(value))
                u.reg_write(r.UC_X86_REG_EIP, original.word(esp))
                u.reg_write(r.UC_X86_REG_ESP, esp + 4)
        u.hook_add(UC_HOOK_CODE, conversion)
        expected = [(-1, -1)]
        for raw in range(1, 256):
            u.mem_write(pixel, bytes([0, 0, 0, raw]))
            u.reg_write(r.UC_X86_REG_EAX, sample)
            u.emu_start(0xa40174, 0xa40208, count=1000)
            expected.append((original.word(owner + 0x44), original.word(owner + 0x48)))
        source = '''#include "stock_customization_selection.hpp"
#include <iostream>
int main() { for(unsigned value=0;value!=256;++value) {
const auto region=wot::customizationRegion(value);
std::cout<<region.area<<" "<<region.region<<"\\n"; } }
'''
        with tempfile.TemporaryDirectory(prefix='stock-customization-') as tmp:
            path = Path(tmp)
            (path / 'host.cpp').write_text(source)
            subprocess.run(['c++', '-std=c++14', '-ffp-contract=off', '-Wall', '-Werror',
                '-I', str(ROOT / 'bigworld'), str(path / 'host.cpp'), '-o', str(path / 'host')], check=True)
            actual = [tuple(map(int, row.split())) for row in
                subprocess.check_output([str(path / 'host')], text=True).splitlines()]
        self.assertEqual(expected, actual)
        self.assertGreater(len(set(actual)), 10)

    def test_modes_preserve_authored_parts_and_tracks_occlusion(self):
        source = '''#include "stock_customization_selection.hpp"
#include <cassert>
int main() {
for(int mode=0;mode!=5;++mode) for(int part=0;part!=4;++part) {
assert(wot::customizationMaskMode(mode,part)==
 (mode==4 ? 1 : mode==3 && part!=3 ? 1 : mode));
assert(wot::customizationDepthOnly(mode,part,true));
assert(wot::customizationDepthOnly(mode,part,false)==
 (((mode==1 || mode==4) && part==0) || (mode==4 && part==2)));
}
assert(wot::customizationMaskMode(9,3)==9);
assert(!wot::customizationDepthOnly(9,3,false));
}
'''
        with tempfile.TemporaryDirectory(prefix='stock-customization-mode-') as tmp:
            path = Path(tmp)
            (path / 'host.cpp').write_text(source)
            subprocess.run(['c++', '-std=c++14', '-Wall', '-Werror', '-I', str(ROOT / 'bigworld'),
                str(path / 'host.cpp'), '-o', str(path / 'host')], check=True)
            subprocess.run([str(path / 'host')], check=True)
