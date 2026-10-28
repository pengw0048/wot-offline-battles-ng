"""Production compiled-model patch, pose packets and actual LOD setting probes."""
import importlib.util
import itertools
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ['lib/moo/' + name for name in ('draw_context.hpp', 'draw_context.cpp',
         'effect_visual_context.hpp', 'effect_visual_context.cpp', 'animation.hpp',
         'animation.cpp', 'lod_settings.hpp', 'lod_settings.cpp')] + ['client/finale_app.cpp',
         'lib/moo/fog_helper.hpp', 'lib/romp/enviro_minder.hpp', 'lib/romp/enviro_minder.cpp',
         'lib/scene/tick_scene_view.cpp']


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'bigworld' / (name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


patch = module('patch_stock_space_models')
effects = module('patch_stock_effect_owners')
decor = module('patch_stock_scene_settings')


def effect_publication(root):
    effects.patch_context(root)
    effects.patch_instancing(root)
    effects.patch_environment(root)


def definition(text, signature):
    start = text.index(signature)
    begin = text.index('{', start)
    end, depth = begin + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


PACKET_PRELUDE = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#define BW_GUARD
#define MF_ASSERT_DEV(x) assert(x)
#define SUCCEEDED(x) ((x)>=0)
using uint32=std::uint32_t;
struct Vector4 {float x,y,z,w;};
struct Matrix {
 float v[16]{};
 static const Matrix identity;
 bool invert(const Matrix&){return true;}
 float uniformScale()const{return 1.f;}
};
const Matrix Matrix::identity{};
struct Allocator {
 std::vector<std::unique_ptr<unsigned char[]>> data;
 void* allocate(size_t count){data.emplace_back(new unsigned char[count]);std::memset(data.back().get(),0xa5,count);return data.back().get();}
};
struct DrawContext {
 @INSTANCE@
 Allocator instanceDataAllocator_;
 InstanceData* allocInstanceData(uint32);
 InstanceData* allocStockInstanceData(uint32);
};
@ALLOCATORS@
struct EffectVisualContext {
 const Matrix* stockPreviousWorld_=nullptr;
 uint32 stockPreviousPaletteEntries_=0;
 const Matrix* worldMatrix_=&Matrix::identity;
 const Matrix* invWorldMatrix_=&Matrix::identity;
 Matrix invWorldCached_;
 float invWorldScale_=1;
 const Vector4* worldMatrixPalette_=nullptr;
 uint32 worldMatrixPaletteSize_=0;
 void stockPreviousWorld(const Matrix* value){stockPreviousWorld_=value;}
 const Matrix* stockPreviousWorld()const{return stockPreviousWorld_;}
 void stockPreviousPaletteEntries(uint32 value){stockPreviousPaletteEntries_=value;}
 uint32 stockPreviousPaletteEntries()const{return stockPreviousPaletteEntries_;}
 void worldMatrix(const Matrix*);
 void worldMatrixPalette(const Vector4*,uint32);
};
@SETTERS@
struct Processor {
 EffectVisualContext& effectVisualContext_;
 @APPLY@
};
using D3DXHANDLE=const char*;
struct ID3DXEffect {
 Matrix matrix;float offset=-1;unsigned matrices=0,offsets=0;
 int SetMatrix(D3DXHANDLE,const Matrix* value){assert(value);matrix=*value;++matrices;return 0;}
 int SetFloat(D3DXHANDLE,float value){offset=value;++offsets;return 0;}
};
struct EffectConstantValue {virtual bool operator()(ID3DXEffect*,D3DXHANDLE)=0;};
EffectVisualContext visual;
struct RenderContext {EffectVisualContext& effectVisualContext(){return visual;}} context;
RenderContext& rc(){return context;}
@PROVIDERS@
int main(){
 DrawContext draw;Processor processor{visual};ID3DXEffect effect;
 StockPreviousWorldConstant previous;StockPreviousWorldOffsetConstant offset;
 auto* legacy=draw.allocInstanceData(0);assert(legacy->paletteSize_==0);
 assert(!legacy->stockPreviousWorld_&&!legacy->stockAnimatedWorld_&&!legacy->stockPreviousPaletteEntries_);
 auto* first=draw.allocStockInstanceData(0);first->matrix()[0].v[12]=25;first->matrix()[1].v[12]=-9;
 assert(first->stockPreviousWorld_&&!first->stockAnimatedWorld_);
 processor.applyInstanceData(first);
 assert(visual.worldMatrix_->v[12]==25&&visual.stockPreviousWorld()->v[12]==-9);
 assert(previous(&effect,"PrevWorld")&&effect.matrix.v[12]==-9);
 auto* second=draw.allocStockInstanceData(0);second->matrix()[0].v[12]=40;second->matrix()[1].v[12]=11;
 second->stockAnimatedWorld_=true;processor.applyInstanceData(second);
 assert(previous(&effect,"PrevWorld")&&effect.matrix.v[12]==11);
 // Previous data belongs to its own packet and is not a borrowed mutable State pose.
 assert(first->matrix()[1].v[12]==-9&&second->matrix()[1].v[12]==11);
 auto* skin=draw.allocStockInstanceData(2);assert(skin->paletteSize_==4&&skin->stockPreviousPaletteEntries_==2);
 for(unsigned n=0;n<12;++n)skin->palette()[n]={float(n),float(n+1),float(n+2),float(n+3)};
 processor.applyInstanceData(skin);assert(!visual.stockPreviousWorld());
 assert(visual.worldMatrixPaletteSize_==4&&visual.worldMatrixPalette_[6].x==6);
 assert(offset(&effect,"PrevWorldOffset")&&effect.offset==6);
 // A subsequent legacy object must clear both borrowed histories.
 processor.applyInstanceData(legacy);assert(!visual.stockPreviousWorld()&&!visual.stockPreviousPaletteEntries());
 assert(!previous(&effect,"PrevWorld")&&effect.matrices==2);
 assert(offset(&effect,"PrevWorldOffset")&&effect.offset==0);
 // RenderOpProcessor::stop uses this setter before packet storage is released.
 visual.worldMatrix(nullptr);draw.instanceDataAllocator_.data.clear();
 assert(!visual.stockPreviousWorld()&&!visual.stockPreviousPaletteEntries());
}
'''


LOD_PRELUDE = r'''
#include <algorithm>
#include <cassert>
#include <map>
#include <memory>
#include <string>
#include <vector>
#define BW_GUARD
#define MF_ASSERT(x) assert(x)
namespace BW {using string=std::string;}
struct DataSection {std::map<std::string,float> values;float readFloat(const std::string& key,float fallback){auto p=values.find(key);return p==values.end()?fallback:p->second;}};
using DataSectionPtr=std::shared_ptr<DataSection>;
struct Setting {int active=0;int activeOption(){return active;}void selectOption(int index){active=index;}};
std::shared_ptr<Setting> sharedDecor;
namespace Moo {struct GraphicsSetting {static std::shared_ptr<Setting> getFromLabel(const char* name){assert(std::string(name)=="DECOR_LEVEL");return sharedDecor;}};}
struct LodSettings {
 float stockCompiledLods_[5]={1,1,1,1,1};std::vector<float> lodOptions_;
 std::shared_ptr<Setting> lodSettings_;
 void initStockCompiled(DataSectionPtr);float stockCompiledLodScale()const;float stockCompiledLodDefault(unsigned)const;
 void setLodOption(int);
};
@METHODS@
struct Streamer {std::vector<float> requests;void touch(float frame){requests.push_back(frame);}};
struct Animation {std::shared_ptr<Streamer> pStreamer_;float totalTime_=20;void touchStockCompiledFrame(float)const;};
@TOUCH@
int main(){
 LodSettings lod;assert(lod.stockCompiledLodScale()==1);
 auto config=std::make_shared<DataSection>();config->values["lod/default/staticModel/lod0"]=2;
 config->values["lod/default/staticModel/lod4"]=.25f;lod.initStockCompiled(config);
 assert(lod.stockCompiledLodDefault(0)==2&&lod.stockCompiledLodDefault(1)==1&&lod.stockCompiledLodDefault(4)==.25f);
 lod.lodOptions_={1,.8f,.6f,.4f};lod.lodSettings_=std::make_shared<Setting>();
 for(int n=0;n<4;++n){lod.lodSettings_->active=n;assert(lod.stockCompiledLodScale()==lod.lodOptions_[n]);}
 lod.setLodOption(1);assert(!sharedDecor);
 sharedDecor=std::make_shared<Setting>();for(int n=0;n<4;++n){lod.setLodOption(n);assert(sharedDecor->active==std::min(2,3-n));}
 Animation animation;animation.touchStockCompiledFrame(3);
 animation.pStreamer_=std::make_shared<Streamer>();animation.touchStockCompiledFrame(3);
 animation.touchStockCompiledFrame(18);
 assert((animation.pStreamer_->requests==std::vector<float>{3,11,0,18,20,10}));
}
'''


class StockSpaceModelsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file() or not shutil.which('c++'):
            raise unittest.SkipTest('Pinned public archive and C++ compiler required')
        with tarfile.open(archive) as original:
            cls.original = {relative: original.extractfile(next(name for name in original.getnames()
                if name.endswith('/programming/bigworld/' + relative))).read() for relative in FILES}

    def fixture(self, root):
        for relative, data in self.original.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)

    def compile(self, root, source):
        path, binary = root / 'probe.cpp', root / 'probe'
        path.write_text(source)
        command = ['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', str(path), '-o', str(binary)]
        if sys.platform != 'win32':
            command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_related_patches_apply_twice_in_all_six_orders(self):
        for order in itertools.permutations((patch.patch, effect_publication, decor.patch)):
            with self.subTest(order=[p.__qualname__ for p in order]), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.fixture(root)
                for function in order:
                    function(root)
                first = {path: path.read_bytes() for path in root.rglob('*') if path.is_file()}
                for function in order:
                    function(root)
                self.assertEqual(first, {path: path.read_bytes() for path in root.rglob('*') if path.is_file()})
                self.assertEqual((root / 'client/finale_app.cpp').read_text().count('initStockCompiled(configSection)'), 1)

    def test_failed_known_seam_has_no_partial_public_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            path = root / 'lib/moo/draw_context.cpp'
            path.write_text(path.read_text().replace('data->paletteSize_ = numPalletteEntries;', 'changedAllocator();'))
            original = {p: p.read_bytes() for p in root.rglob('*') if p.is_file()}
            with self.assertRaises(ValueError):
                patch.patch(root)
            self.assertEqual(original, {p: p.read_bytes() for p in root.rglob('*') if p.is_file()})

    def test_production_pose_allocator_publication_semantics_and_clear(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            patch.patch(root)
            dh, dc, vc = [(root / 'lib/moo' / name).read_text() for name in
                           ('draw_context.hpp', 'draw_context.cpp', 'effect_visual_context.cpp')]
            source = PACKET_PRELUDE.replace('@INSTANCE@', definition(dh, 'struct InstanceData') + ';')
            source = source.replace('@ALLOCATORS@', definition(dc, 'DrawContext::InstanceData* DrawContext::allocStockInstanceData(') +
                '\n' + definition(dc, 'DrawContext::InstanceData*\tDrawContext::allocInstanceData('))
            source = source.replace('@SETTERS@', definition(vc, 'void EffectVisualContext::worldMatrix(') + '\n' +
                definition(vc, 'void\tEffectVisualContext::worldMatrixPalette('))
            source = source.replace('@APPLY@', definition(dc, 'inline void applyInstanceData('))
            source = source.replace('@PROVIDERS@', patch.PREVIOUS_CONSTANTS)
            self.compile(root, source)

    def test_native_transparent_sort_uses_actual_world_bounds_without_visual_origins(self):
        source = r'''
#include <cassert>
#include <cmath>
struct Vector3{
 float x,y,z;
 Vector3 operator+(const Vector3& b)const{return {x+b.x,y+b.y,z+b.z};}
 Vector3 operator-(const Vector3& b)const{return {x-b.x,y-b.y,z-b.z};}
 float length()const{return std::sqrt(x*x+y*y+z*z);}
};
struct Matrix{Vector3 position{0,0,0};Vector3 applyToOrigin()const{return position;}Vector3 applyPoint(const Vector3& v)const{return v;}};
struct RenderContext{Matrix camera;const Matrix& invView()const{return camera;}const Matrix& view()const{return camera;}} context;
RenderContext& rc(){return context;}
struct Box{Vector3 centre()const{return {3,4,12};}};
struct Primitive{unsigned calls=0;Vector3 origin(unsigned group){assert(group==2);++calls;return {0,0,5};}};
struct Packet{bool stockPreviousWorld_=false;unsigned stockPreviousPaletteEntries_=0;};
float distance(Packet* instanceData,Primitive* primitives){Box worldBB;unsigned primitiveGroupIndex=2;struct{float cameraDistance_=0;}sortOp;
@SORT@
return sortOp.cameraDistance_;}
int main(){Primitive primitive;Packet rigid;rigid.stockPreviousWorld_=true;
 assert(distance(&rigid,&primitive)==13&&primitive.calls==0);
 context.camera.position={0,0,10};assert(distance(&rigid,&primitive)==std::sqrt(29.f)&&primitive.calls==0);
 Packet skin;skin.stockPreviousPaletteEntries_=2;assert(distance(&skin,&primitive)==std::sqrt(29.f)&&primitive.calls==0);
 Packet legacy;assert(distance(&legacy,&primitive)==17&&primitive.calls==1);}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            patch.patch(root)
            self.assertIn(patch.NATIVE_SORT_DISTANCE, (root / 'lib/moo/draw_context.cpp').read_text())
            self.compile(root, source.replace('@SORT@', patch.NATIVE_SORT_DISTANCE))

    def test_complete_production_scene_resources_draw_animation_collision_and_teardown(self):
        matrix = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 10., 0., 100., 1.]
        identity = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]
        child = list(identity)
        child[12] = 1.
        bounds = struct.pack('<6f', -1, -1, -1, 1, 1, 1)
        array = lambda stride, rows=b'': struct.pack('<II', stride, len(rows) // stride) + rows
        i = [array(64, struct.pack('<32f', *(matrix + matrix))), array(8, struct.pack('<4I', 35, 1, 97, 1)),
             array(4, struct.pack('<2I', 2, 2)), array(4, bytes(8)), array(4, struct.pack('<2i', -1, 0)),
             array(16, struct.pack('<Iiff', 1, 0, 0, 1)), array(4, struct.pack('<2I', 0, 1)),
             array(12, bytes(24)), array(4), array(20, struct.pack('<10f', *([-1.] * 10)))]
        renders = b''.join(struct.pack('<7I', first, last, 0, group, 1, 2, skinned)
                           for first, last, group, skinned in ((0xffffffff, 0xffffffff, 0, 0), (0, 0, 1, 0), (0, 1, 2, 1)))
        o = [array(8, bytes(8)), array(36, bounds + struct.pack('<3I', 3, 0, 0)),
             array(8, struct.pack('<2I', 1, 0x4a00)), array(24, bounds), array(8, bytes(8)), array(4, bytes(4)),
             array(4, struct.pack('<f', 3.4028234663852886e38)), array(8, struct.pack('<2I', 0, 2)), array(28, renders),
             array(4, struct.pack('<2I', 0, 1)), array(36, struct.pack('<Ifii5I', 100, 10, 0, 20, 0, 101, 0, 1, 102)),
             array(4, struct.pack('<2I', 0, 1)), array(68, struct.pack('<I16fI16f', 0xffffffff, *identity, 1, *child)),
             array(64), array(44), array(36), array(8)]
        float_bits = struct.unpack('<I', struct.pack('<f', 1.5))[0]
        properties = b''.join(struct.pack('<3I', name, kind, value) for name, kind, value in
                              ((201, 1, 2), (202, 2, float_bits), (203, 3, 0xffffffff),
                               (204, 4, 0), (205, 5, 0), (206, 6, 207), (0xfdead, 0, 123)))
        a = [array(12, struct.pack('<3i', 0, 0, 6)), array(4, struct.pack('<I', 200)), array(12, properties),
             array(64, struct.pack('<16f', *identity)), array(16, struct.pack('<4f', .1, .2, .3, .4))]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            input_file = root / 'models.bin'
            datasets=[(i,o,a)]
            # Real wire fixtures for fragile hide, authored replacement and merged modules.
            no_links=array(12,struct.pack('<6I',*([0xffffffff]*6)))
            fragile=list(o);fragile[4]=array(8,struct.pack('<2I',2,0))
            fragile[15]=array(36,struct.pack('<9I',0,0,0,0,0,struct.unpack('<I',struct.pack('<f',1.25))[0],0xffffffff,0xffffffff,0))
            plain=list(i);plain[7]=no_links
            datasets.append((plain,fragile,a))
            replacement=list(fragile)
            replacement[0]=array(8,struct.pack('<4I',0,0,1,1))
            replacement[1]=array(36,bounds+struct.pack('<3I',3,0,0)+bounds+struct.pack('<3I',3,1,1))
            replacement[2]=array(8,struct.pack('<4I',1,0x4a00,1,0x100))
            replacement[3]=array(24,bounds+bounds);replacement[4]=array(8,struct.pack('<4I',2,0,0,0))
            replacement[5]=array(4,struct.pack('<2I',0xffffffff,0xffffffff))
            replacement[6]=array(4,struct.pack('<2f',3.4028234663852886e38,3.4028234663852886e38))
            replacement[7]=array(8,struct.pack('<4I',0,2,0,2))
            replacement[15]=array(36,struct.pack('<9I',0,0,0,0,0,struct.unpack('<I',struct.pack('<f',1.25))[0],0xffffffff,1,0))
            datasets.append((plain,replacement,a))
            merged=[array(64,struct.pack('<48f',*(matrix*3))),array(8,struct.pack('<6I',35,1,35,0,35,0)),
                array(4,struct.pack('<3I',2,2,2)),array(4,bytes(12)),array(4,struct.pack('<3i',-1,-1,-1)),array(16),array(4),
                array(12,struct.pack('<9I',1,2,0xffffffff,0xffffffff,0xffffffff,0,0xffffffff,0xffffffff,0)),array(4),
                array(20,struct.pack('<15f',*([-1.]*15)))]
            datasets.append((merged,replacement,a))
            debris=list(plain);debris[7]=array(12,struct.pack('<6I',0,0,0xffffffff,0xffffffff,0xffffffff,0xffffffff))
            debris[8]=array(4,struct.pack('<I',1))
            datasets.append((debris,fragile,a))
            # One animated instance with varying real scene membership counts.
            for count in (1, 7, 64, 1763):
                population = list(i)
                population[0] = array(64, struct.pack('<%df' % (16 * count), *(matrix * count)))
                population[1] = array(8, b''.join(struct.pack('<2I', 97 if n == count - 1 else 35, 1)
                                                  for n in range(count)))
                population[2] = array(4, struct.pack('<%dI' % count, *([2] * count)))
                population[3] = array(4, bytes(4 * count))
                population[4] = array(4, struct.pack('<%di' % count, *([-1] * (count - 1) + [0])))
                population[5] = array(16, struct.pack('<Iiff', count - 1, 0, 0, 1))
                population[7] = array(12, bytes(12 * count))
                population[9] = array(20, struct.pack('<%df' % (5 * count), *([-1.] * (5 * count))))
                datasets.append((population, o, a))
            input_file.write_bytes(b''.join(struct.pack('<I',len(data))+data
                for dataset in datasets for data in map(b''.join,dataset)))
            engine = (ROOT / 'tests/stock_static_model_scene_test_engine.hpp').read_text()
            dispatch = self.original['lib/scene/tick_scene_view.cpp'].decode()
            operations = '\n'.join(definition(dispatch, signature) for signature in (
                'void TickOperation::tick( SceneObject*',
                'void TickOperation::tick( SceneTypeSystem::RuntimeTypeID',
                'void TickOperation::updateAnimations( SceneObject*',
                'void TickOperation::updateAnimations( SceneTypeSystem::RuntimeTypeID'))
            header = (ROOT / 'bigworld/stock_static_model_scene.hpp').read_text()
            destructibles = (ROOT / 'bigworld/stock_destructibles.hpp').read_text()
            source = (ROOT / 'bigworld/stock_static_model_scene.cpp').read_text()
            without_includes = lambda value: '\n'.join(line for line in value.splitlines()
                if not line.startswith(('#include', '#pragma once')))
            driver = root / 'scene.cpp'
            driver.write_text(engine + '\nnamespace BW {\n' + operations + '\n}\n'
                '#include "wot/stock_static_model_binary.hpp"\n'
                '#include "stock_scene_identity_data.hpp"\n#include "stock_destructibles_data.hpp"\n' +
                without_includes(destructibles) + '\n' + (ROOT / 'tests/stock_destructibles_registration_host.hpp').read_text() + '\n' +
                without_includes(header) + '\n' + without_includes(source) + '\n' +
                (ROOT / 'tests/stock_static_model_scene_probe.cpp').read_text())
            binary = root / 'scene'
            command = ['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                       '-I', str(ROOT / 'include'), '-I', str(ROOT / 'bigworld'),
                       str(driver), '-o', str(binary)]
            if sys.platform != 'win32':
                command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary), str(input_file)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('Static mapping frame ownership: 1, 7, 64, 1763 instances', result.stdout)
            print(result.stdout.strip())

    def test_production_lod_option_payload_config_callback_and_animation_windows(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            patch.patch(root)
            lod = (root / 'lib/moo/lod_settings.cpp').read_text()
            source = LOD_PRELUDE.replace('@METHODS@', patch.COMPILED_LOD + definition(lod, 'void LodSettings::setLodOption('))
            source = source.replace('@TOUCH@', patch.TOUCH)
            self.compile(root, source)


if __name__ == '__main__':
    unittest.main()
