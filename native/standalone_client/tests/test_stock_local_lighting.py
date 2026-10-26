"""Check original local-light packing and camera/volume classification math."""
import importlib.util
import sys
import tarfile
import io
import zipfile
import itertools
import os
import re
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StockLocalLightingTests(unittest.TestCase):
    def test_actual_production_draw_binding_and_instancing(self):
        cpp = (ROOT / 'bigworld/stock_local_lighting.cpp').read_text()
        def body(signature):
            start = cpp.index(signature)
            brace = cpp.index('{', start)
            depth, end = 1, brace + 1
            while depth:
                depth += (cpp[end] == '{') - (cpp[end] == '}')
                end += 1
            return cpp[start:end]
        production = '\n'.join(body(signature) + (';' if signature.startswith('struct ') else '') for signature in (
            'bool select(', 'bool optionalBool(', 'void clearTexture(', 'struct StockLocalLighting::State',
            'bool StockLocalLighting::State::drawOne(', 'bool StockLocalLighting::State::drawVolumes('))
        probe = (ROOT / 'tests/stock_local_lighting_draw_probe.cpp').read_text().replace('@PRODUCTION@', production)
        with tempfile.TemporaryDirectory(prefix='stock-local-light-draw-') as directory:
            source, binary = Path(directory) / 'probe.cpp', Path(directory) / 'probe'
            source.write_text(probe)
            command = ['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'include'), str(source), '-o', str(binary)]
            if sys.platform != 'win32':
                command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_actual_production_default_resources_and_target_failures(self):
        cpp = (ROOT / 'bigworld/stock_local_lighting.cpp').read_text()
        def body(signature):
            start = cpp.index(signature)
            brace = cpp.index('{', start)
            depth, end = 1, brace + 1
            while depth:
                depth += (cpp[end] == '{') - (cpp[end] == '}')
                end += 1
            return cpp[start:end]
        production = '\n'.join(body(signature) + (';' if signature.startswith(('struct ', 'class ')) else '') for signature in (
            'bool detachTexture(', 'class CameraScope', 'class TargetScope', 'void cubeView(', 'void drawShadowObjects(', 'bool select(', 'void clearTexture(', 'struct StockLocalLighting::State',
            'bool StockLocalLighting::State::drawShadow(const Moo::OmniLightPtr& light)', 'bool StockLocalLighting::State::drawShadow(Spot& light, unsigned index)', 'bool StockLocalLighting::State::blur(', 'void StockLocalLighting::createUnmanagedObjects(', 'void StockLocalLighting::deleteUnmanagedObjects('))
        probe = (ROOT / 'tests/stock_local_lighting_resources_probe.cpp').read_text().replace('@PRODUCTION@', production)
        with tempfile.TemporaryDirectory(prefix='stock-local-light-draw-') as directory:
            source, binary = Path(directory) / 'probe.cpp', Path(directory) / 'probe'
            source.write_text(probe)
            command = ['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'include'), str(source), '-o', str(binary)]
            if sys.platform != 'win32':
                command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_related_patch_orders_apply_twice_and_atomic_failure(self):
        names = ('stock_local_lighting', 'stock_space_lights', 'stock_deferred_buffers', 'stock_deferred_lighting', 'stock_dynamic_resolution')
        modules, files = {}, set()
        for name in names:
            path = ROOT / 'bigworld' / ('patch_' + name + '.py')
            spec = importlib.util.spec_from_file_location(name, path)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            modules[name] = module
            files.update(re.findall(r"['\"]((?:lib|client)/[^'\"]+\.(?:cpp|hpp|ipp))['\"]", path.read_text()))
        files.update('lib/moo/%s_light.%s' % (kind, suffix) for kind in ('omni', 'spot') for suffix in ('hpp', 'cpp', 'ipp'))
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            self.skipTest('Pinned public source archive required')
        contents = {}
        with tarfile.open(archive) as source:
            for member in source.getmembers():
                if '/programming/bigworld/' in member.name:
                    relative = member.name.split('/programming/bigworld/', 1)[1]
                    if relative in files and member.isfile():
                        contents[relative] = source.extractfile(member).read()
        for sequence in itertools.permutations(names):
            with self.subTest(order=sequence), tempfile.TemporaryDirectory(prefix='local-light-patches-') as directory:
                root = Path(directory)
                for name, data in contents.items():
                    path = root / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(data)
                for name in sequence:
                    (getattr(modules[name], 'apply', None) or modules[name].patch)(root)
                first = {str(path.relative_to(root)): path.read_bytes() for path in root.rglob('*') if path.is_file()}
                for name in reversed(sequence):
                    (getattr(modules[name], 'apply', None) or modules[name].patch)(root)
                self.assertEqual(first, {str(path.relative_to(root)): path.read_bytes() for path in root.rglob('*') if path.is_file()})
                cpp = (root / 'lib/moo/deferred_lights_manager.cpp').read_text()
                for marker in ('if (!drawStockDeferredSunLight(m_material))', 'if (!initialiseStockDeferredLightingConstants())',
                               'finaliseStockDeferredLightingConstants();', 'stockLocalLighting_->createManagedObjects();',
                               'stockLocalLighting_->deleteManagedObjects();', 'stockLocalLighting_->createUnmanagedObjects();',
                               'stockLocalLighting_->deleteUnmanagedObjects();'):
                    self.assertEqual(cpp.count(marker), 1, marker)
                if sequence == names:
                    target = root / 'lib/moo/spot_light.hpp'
                    target.write_text(target.read_text().replace('bool stockLocalInstancing() const', 'bool renamedLocalInstancing() const').replace('int\t\t\t\t\tpriority() const', 'int brokenPriority() const'))
                    before = {str(path.relative_to(root)): path.read_bytes() for path in root.rglob('*') if path.is_file()}
                    with self.assertRaises(ValueError):
                        modules['stock_local_lighting'].apply(root)
                    self.assertEqual(before, {str(path.relative_to(root)): path.read_bytes() for path in root.rglob('*') if path.is_file()})

    def test_actual_light_instancing_declaration_and_state_restore(self):
        cpp = (ROOT / 'bigworld/stock_local_lighting.cpp').read_text()
        start = cpp.index('bool drawInstances(')
        end = cpp.index('void spotMatrices(', start)
        production = cpp[start:end]
        source = r'''#include "wot/stock_local_lighting.hpp"
#include <cassert>
#include <memory>
#include <vector>
#include <string>
using uint=unsigned;
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
enum{D3DSTREAMSOURCE_INDEXEDDATA=0x40000000,D3DSTREAMSOURCE_INSTANCEDATA=0x80000000};
using LocalMath=wot::StockLocalLightMath;
struct Device{unsigned calls=0,failAt=0,freq[2]={1,1};int SetStreamSourceFreq(unsigned stream,unsigned value){assert(stream==0||stream==9);++calls;if(calls==failAt)return -1;freq[stream==9]=value;return 0;}};
namespace Moo{
struct VertexDeclaration{static bool missing,failCombine;bool extra=false;static VertexDeclaration* get(const char* name){assert(std::string(name)=="light_instancing");static VertexDeclaration extra;extra.extra=true;return missing?nullptr:&extra;}
static VertexDeclaration* combine(VertexDeclaration* base,VertexDeclaration* extra){assert(base&&!base->extra&&extra->extra);static VertexDeclaration result;result.extra=true;return failCombine?nullptr:&result;}VertexDeclaration* declaration(){return this;}};bool VertexDeclaration::missing=false,VertexDeclaration::failCombine=false;
struct Context{Device gpu;bool failDecl=false;VertexDeclaration* active=nullptr;Device* device(){return &gpu;}int setVertexDeclaration(VertexDeclaration* d){assert(d&&d->extra);active=d;return failDecl?-1:0;}};
Context& rc(){static Context ctx;return ctx;}
struct InstancingStream{enum{STREAM_NUMBER=9};};
struct VertexBuffer{static bool bound,failBind;static unsigned resets;int set(unsigned stream,unsigned offset,unsigned stride)const{assert(stream==9&&!offset&&stride==64);if(failBind)return -1;bound=true;return 0;}static int reset(unsigned stream){assert(stream==9);bound=false;++resets;return 0;}};bool VertexBuffer::bound=false,VertexBuffer::failBind=false;unsigned VertexBuffer::resets=0;
struct Vertices{VertexDeclaration base;bool fail=false;int setVertices(bool software){assert(!software);return fail?-1:0;}const VertexDeclaration* pDecl()const{return &base;}};
struct Primitive{bool fail=false;unsigned draws=0;int setPrimitives(){return 0;}int drawInstancedPrimitiveGroup(unsigned index,unsigned count){assert(index==17&&count==1001&&VertexBuffer::bound);assert(rc().gpu.freq[0]==(D3DSTREAMSOURCE_INDEXEDDATA|count)&&rc().gpu.freq[1]==(D3DSTREAMSOURCE_INSTANCEDATA|1));assert(rc().active->extra);++draws;return fail?-1:0;}};
struct Visual{struct Group{unsigned groupIndex_=17;};struct Geometry{std::shared_ptr<Vertices> vertices_=std::make_shared<Vertices>();std::shared_ptr<Primitive> primitives_=std::make_shared<Primitive>();std::vector<Group> primitiveGroups_{Group()};};struct RenderSet{std::vector<Geometry> geometry_{Geometry()};};std::vector<RenderSet> sets{RenderSet()};std::vector<RenderSet>& renderSets(){return sets;}};using VisualPtr=std::shared_ptr<Visual>;
}
@PRODUCTION@
int main(){auto geometry=std::make_shared<Moo::Visual>();Moo::VertexBuffer instances;auto& rc=Moo::rc();assert(drawInstances(geometry,instances,1001));assert(!Moo::VertexBuffer::bound&&rc.gpu.freq[0]==1&&rc.gpu.freq[1]==1);
for(unsigned failure=0;failure<7;++failure){rc.gpu.calls=0;rc.gpu.failAt=failure<2?failure+1:0;Moo::VertexBuffer::failBind=failure==2;geometry->sets[0].geometry_[0].vertices_->fail=failure==3;Moo::VertexDeclaration::failCombine=failure==4;rc.failDecl=failure==5;geometry->sets[0].geometry_[0].primitives_->fail=failure==6;assert(!drawInstances(geometry,instances,1001));assert(!Moo::VertexBuffer::bound&&rc.gpu.freq[0]==1&&rc.gpu.freq[1]==1);}
rc.gpu.calls=0;Moo::VertexDeclaration::missing=true;assert(!drawInstances(geometry,instances,1001)&&!rc.gpu.calls);
}'''.replace('@PRODUCTION@', production)
        with tempfile.TemporaryDirectory(prefix='actual-local-light-instancing-') as directory:
            root = Path(directory)
            (root / 'probe.cpp').write_text(source)
            result = subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'include'), str(root / 'probe.cpp'), '-o', str(root / 'probe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(root / 'probe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_exact_effect_volume_and_shadow_abi(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT required')
        sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
        from prepare_native_client import OriginalResources
        probe = r'''#include "wot/stock_shader_binary.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
const wot::StockEffectUniform* uniform(const wot::StockEffectBinary& e,const std::string& name){for(const auto& b:e.uniform_buffers)for(const auto& u:b.uniforms)if(u.name==name)return &u;return nullptr;}
int main(int argc,char** argv){assert(argc==5);for(int i=1;i<4;++i){std::ifstream f(argv[i],std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),{});wot::StockEffectBinary e;std::string error;assert(wot::decode_stock_dx9_effect(bytes.data(),bytes.size(),e,error));
if(i<3&&std::string(argv[4])=="11"){assert(e.techniques.empty());continue;}if(i<3){std::string kind=i==1?"OMNI":"SPOT",lower=i==1?"omni":"spot";assert(e.techniques.size()==3);
for(const auto& t:e.techniques){assert(t.name==kind||t.name==kind+"_INSTANCING"||t.name==kind+"_FALLBACK");assert(t.passes.size()==(t.name==kind+"_FALLBACK"?1:3));}
auto* u=uniform(e,"g_"+lower+"Light");assert(u&&e.types.at(u->type_index).byte_size==unsigned(i==1?48:64));
assert(uniform(e,"g_"+lower+"Instancing")&&uniform(e,"g_"+lower+"Shadow")&&uniform(e,"g_enableSSAO"));assert(!uniform(e,"g_enableSSR"));
bool shadow=false;for(const auto& r:e.resource_views)shadow|=r.name=="g_"+lower+"ShadowMap";assert(shadow);
if(i==2){u=uniform(e,"g_spotShadowMatrix");assert(u&&e.types.at(u->type_index).byte_size==64);}
// Original volume geometry uses POSITION0; native instancing adds TEXCOORD4..6/7.
bool position=false;unsigned coordinates=0;for(const auto& shader:e.shaders)if(shader.stage==1){for(size_t at=1;at<shader.bytecode.size();){auto op=shader.bytecode[at]&0xffffu,len=(shader.bytecode[at]>>24)&15u;if(op==0xffff)break;if(op==0xfffe){at+=1+((shader.bytecode[at]>>16)&0x7fffu);continue;}if(op==31){auto semantic=shader.bytecode.at(at+1);auto usage=semantic&15u,index=(semantic>>16)&15u;position|=usage==0&&index==0;if(usage==5&&index>=4&&index<=7)coordinates|=1u<<index;}at+=1+len;}}
assert(position&&coordinates==unsigned(i==1?0x70:0xf0));
}else{bool h=false,v=false;for(const auto& t:e.techniques){h|=t.name=="HORIZONTAL_PASS";v|=t.name=="VERTICAL_PASS";}assert(h&&v);for(auto name:{"g_depthMode","g_useMaskRT","g_useBilateralFilter","g_bilateralFilterLowQuality","g_bilateralFilterConsiderSurfacePlane","g_processOnlyOpaque","g_blurMip","g_blurScreen","g_srcMapDRRParams"})assert(uniform(e,name));}
}}'''
        with tempfile.TemporaryDirectory(prefix='exact-local-light-fxo-') as directory, OriginalResources(client) as resources:
            root = Path(directory)
            (root / 'probe.cpp').write_text(probe)
            result = subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'include'), str(root / 'probe.cpp'), str(ROOT / 'src/stock_shader_binary.cpp'), '-o', str(root / 'probe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            from packed_xml import read_packed_xml
            for name, indices, data_type in (('instancing', [4], b'FLOAT1'), ('light_instancing', [4, 5, 6, 7], b'FLOAT4')):
                section = read_packed_xml(resources.read('shaders/formats/' + name + '.xml'))
                elements = dict(section.children)[b'elements'].value
                actual = []
                for semantic, value in elements.children:
                    self.assertEqual(semantic, b'TEXCOORD')
                    fields = {key: field.value for key, field in value.value.children}
                    self.assertEqual(fields[b'stream'], 9)
                    self.assertEqual(fields[b'type'], data_type)
                    actual.append(fields[b'semanticIndex'])
                self.assertEqual(actual, indices)
            for permutation in ('10', '11'):
                paths = []
                for index, name in enumerate(('std_effects/resolve_omni_lighting', 'std_effects/resolve_spot_lighting', 'auxiliary/bilateral_blur')):
                    resource = resources.read('shaders/%s.%s.dx9.fxo' % (name, permutation))
                    self.assertIsNotNone(resource)
                    with zipfile.ZipFile(io.BytesIO(resource)) as fxo:
                        path = root / str(index)
                        path.write_bytes(fxo.read('effect'))
                    paths.append(str(path))
                result = subprocess.run([str(root / 'probe'), *paths, permutation], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, permutation + ': ' + result.stderr)

    def test_original_light_packet_instruction_match(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT required')
        try:
            import unicorn  # noqa: F401
            import pefile  # noqa: F401
        except ImportError:
            self.skipTest('Unicorn and PE reader required')
        from stock_local_lighting_oracle import original_light_packets
        cases = [(kind, p, (.2, .6, .8, .7), intensity, inner, outer, .8, .75, (.1, .2, .3, .4), camera)
                 for kind, p, intensity, (inner, outer), camera in itertools.product((0, 1),
                 ((0., 0., 0.), (-731.25, 15., 931.125), (372.5, -101.5, -999.)),
                 (0., .6, 25., 9420.), ((.1, 3.), (5., 30.)), ((0., 0., 0.), (17.25, -53.25, 17.5)))]
        expected = original_light_packets(Path(client) / 'WorldOfTanks.exe', cases)
        source = r'''#include "wot/stock_local_lighting.hpp"
#include <cassert>
#include <fstream>
using M=wot::StockLocalLightMath;
int main(int argc,char** argv){assert(argc==3);std::ifstream in(argv[1],std::ios::binary);std::ofstream out(argv[2],std::ios::binary);
unsigned count;in.read(reinterpret_cast<char*>(&count),4);for(unsigned i=0;i<count;++i){unsigned kind;float v[22];in.read(reinterpret_cast<char*>(&kind),4);in.read(reinterpret_cast<char*>(v),sizeof v);
M::Vector p{{v[0],v[1],v[2]}},camera{{v[19],v[20],v[21]}};M::Four colour{{v[3],v[4],v[5],v[6]}},q{{v[15],v[16],v[17],v[18]}};
auto packet=kind?M::spot(p,colour,v[7],v[8],v[9],v[10],v[11],q):M::omni(p,colour,v[7],v[8],v[9],camera);out.write(reinterpret_cast<const char*>(&packet),kind?64:48);}}'''
        encoded = bytearray(struct.pack('<I', len(cases)))
        for kind, p, colour, intensity, inner, outer, cosine, tangent, orientation, camera in cases:
            encoded.extend(struct.pack('<I22f', kind, *p, *colour, intensity, inner, outer, cosine, tangent, 0, 0, 0, *orientation, *camera))
        with tempfile.TemporaryDirectory(prefix='original-local-packets-') as directory:
            root = Path(directory)
            (root / 'probe.cpp').write_text(source)
            (root / 'input').write_bytes(encoded)
            result = subprocess.run(['c++', '-std=c++14', '-ffp-contract=off', '-I', str(ROOT / 'include'), str(root / 'probe.cpp'), '-o', str(root / 'probe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(root / 'probe'), str(root / 'input'), str(root / 'output')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            actual = (root / 'output').read_bytes()
            self.assertEqual(actual, b''.join(expected))
        print('Original light packet instructions matched %d records bit for bit' % len(cases))

    def test_original_volume_classification_instruction_match(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT required')
        try:
            import unicorn  # noqa: F401
            import pefile  # noqa: F401
        except ImportError:
            self.skipTest('Unicorn and PE reader required for original instruction audit')
        from stock_local_lighting_oracle import original_light_groups
        cases = []
        for kind, flags in itertools.product((0, 1), itertools.product((False, True), repeat=4)):
            for camera in ((0., 0., 0.), (0., 0., -2.), (0., 0., -20.), (0., 0., 3.), (0., 0., 20.),
                           (0., 0., 30.), (0., 0., 35.), (2., 0., 0.), (7., 0., 7.), (20., 1., 0.),
                           (30., 30., 30.), (0., 30., 30.)):
                for near in (.1, 1., 2.):
                    cases.append((kind, (0., 0., 0.), (0., 0., 1.), 30., .8, .6,
                                  (-22.5, -22.5, .1), (22.5, 22.5, 30.), camera, near, *flags))
        original = list(cases)
        for position, direction in (((-372.5, 150.25, 980.125), (.6, 0., .8)),
                                    ((731.25, -50.125, -902.5), (0., -.6, .8)),
                                    ((-100., 50., 300.), (-.6, .8, 0.))):
            for kind, unused_p, unused_d, outer, cosine, sine, low, high, camera, near, *flags in original:
                translated_camera = tuple(camera[axis] + position[axis] for axis in range(3))
                low = tuple(position[axis] - 40. for axis in range(3))
                high = tuple(position[axis] + 40. for axis in range(3))
                cases.append((kind, position, direction, outer, cosine, sine, low, high, translated_camera, near, *flags))
        expected = original_light_groups(Path(client) / 'WorldOfTanks.exe', cases)
        self.assertEqual(set(expected), {0, 1, 2})
        with tempfile.TemporaryDirectory(prefix='stock-local-light-math-') as directory:
            root = Path(directory)
            source = root / 'probe.cpp'
            source.write_text(r'''
#include "wot/stock_local_lighting.hpp"
#include <cassert>
#include <fstream>
using M=wot::StockLocalLightMath;
int main(int argc,char**argv){assert(argc==3);std::ifstream in(argv[1],std::ios::binary);std::ofstream out(argv[2],std::ios::binary);unsigned count;in.read(reinterpret_cast<char*>(&count),4);
for(unsigned index=0;index<count;++index){unsigned kind,flags;M::Vector position,direction,camera;M::Bounds bounds;float outer,cosine,sine,near;
in.read(reinterpret_cast<char*>(&kind),4);in.read(reinterpret_cast<char*>(position.data()),12);in.read(reinterpret_cast<char*>(direction.data()),12);in.read(reinterpret_cast<char*>(&outer),4);in.read(reinterpret_cast<char*>(&cosine),4);in.read(reinterpret_cast<char*>(&sine),4);in.read(reinterpret_cast<char*>(bounds.minimum.data()),12);in.read(reinterpret_cast<char*>(bounds.maximum.data()),12);in.read(reinterpret_cast<char*>(camera.data()),12);in.read(reinterpret_cast<char*>(&near),4);in.read(reinterpret_cast<char*>(&flags),4);assert(in);
unsigned result=kind?M::spotGroup(position,direction,outer,cosine,sine,bounds,camera,near,flags&1,flags&2,flags&4,flags&8):M::omniGroup(position,outer,camera,near,flags&1,flags&2,flags&4,flags&8);out.write(reinterpret_cast<char*>(&result),4);}}
''')
            encoded = bytearray(struct.pack('<I', len(cases)))
            for kind, position, direction, outer, cosine, sine, low, high, camera, near, *flags in cases:
                encoded.extend(struct.pack('<I19fI', kind, *position, *direction, outer, cosine, sine,
                                           *low, *high, *camera, near, sum(int(flag) << i for i, flag in enumerate(flags))))
            input_file, output_file = root / 'input', root / 'output'
            input_file.write_bytes(encoded)
            binary = root / 'probe'
            compiled = subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                '-I', str(ROOT / 'include'), str(source), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            result = subprocess.run([str(binary), str(input_file), str(output_file)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            actual = struct.unpack('<%dI' % len(cases), output_file.read_bytes())
            mismatches = [(index, actual[index], expected[index], cases[index]) for index in range(len(cases)) if actual[index] != expected[index]]
            self.assertFalse(mismatches, repr(mismatches[:8]))
        print('Original local-light volume classifiers matched %d sphere/cone/camera/flag cases' % len(cases))


if __name__ == '__main__':
    unittest.main()
