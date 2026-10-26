"""Exercise the original BWfr2 wire, colour and real LensEffect scene owner."""
import importlib.util
import os
import random
from pathlib import Path
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def patch_module():
    path = ROOT / 'bigworld/patch_stock_space_flares.py'
    spec = importlib.util.spec_from_file_location('flare_patch', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def body(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class StockFlareTests(unittest.TestCase):
    def compile_run(self, directory, source, arguments=(), extra=()):
        root = Path(directory)
        (root / 'probe.cpp').write_text(source)
        flags = [] if sys.platform == 'win32' else ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        result = subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', *flags,
            '-I', str(root), '-I', str(ROOT / 'bigworld'), str(root / 'probe.cpp'), *map(str, extra), '-o', str(root / 'probe')], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([str(root / 'probe'), *map(str, arguments)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def fresh_engine(self, root):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.exists():
            self.skipTest('Pinned public source archive required')
        with tarfile.open(archive) as source:
            for name in ('lib/romp/lens_effect.hpp', 'lib/romp/lens_effect.cpp'):
                member = next(m for m in source.getmembers() if m.name.endswith('/programming/bigworld/' + name))
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(source.extractfile(member).read())

    def test_patch_twice_and_atomic_unknown_source(self):
        with tempfile.TemporaryDirectory(prefix='flare-patch-') as directory:
            root = Path(directory)
            self.fresh_engine(root)
            patch = patch_module()
            patch.apply(root)
            first = {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()}
            patch.apply(root)
            self.assertEqual(first, {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()})
            path = root / 'lib/romp/lens_effect.cpp'
            path.write_text(path.read_text().replace('stockActType_ = other.stockActType_;', '').replace('visibilityType_ = other.visibilityType_;', 'visibilityType_ = changed;'))
            before = {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()}
            with self.assertRaises(ValueError):
                patch.apply(root)
            self.assertEqual(before, {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()})

    def test_actual_lens_activation_producer_copy_and_consumers(self):
        with tempfile.TemporaryDirectory(prefix='actual-lens-activation-') as directory:
            root = Path(directory)
            self.fresh_engine(root)
            patch_module().apply(root)
            cpp = (root/'lib/romp/lens_effect.cpp').read_text()
            production = '\n'.join(body(cpp, signature) for signature in ('LensEffect::LensEffect()', 'bool LensEffect::load(', 'void LensEffect::tick(', 'void LensEffect::draw()', 'LensEffect & LensEffect::operator=('))
            source = r'''#include <cassert>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>
#define BW_GUARD
#define MF_ASSERT(x) assert(x)
#define ERROR_MSG(...) ((void)0)
using uint32=unsigned;
namespace BW{using string=std::string;template<class T>using vector=std::vector<T>;}
const float OLDEST_LENS_EFFECT=.15f;
struct Vector3{float x,y,z;Vector3(float a,float b,float c):x(a),y(b),z(c){}Vector3 operator-(const Vector3& b)const{return {x-b.x,y-b.y,z-b.z};}float length()const{return std::sqrt(x*x+y*y+z*z);}};
struct Vector4{float x,y,z,w;Vector4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}};
struct Matrix{void applyPoint(Vector4& out,const Vector4& p){out=p;}Vector3 applyToOrigin(){return {0,0,0};}};
namespace Moo{struct RC{Matrix m;Matrix& viewProjection(){return m;}Matrix& invView(){return m;}};RC& rc(){static RC r;return r;}}
struct DataSection{std::map<std::string,int> integers;float readFloat(const char*,float value){return value;}int readInt(const char* key,unsigned value){return integers.count(key)?integers[key]:int(value);}void openSections(const char*,std::vector<std::shared_ptr<DataSection>>&){}};using DataSectionPtr=std::shared_ptr<DataSection>;
struct FlareData{using Flares=std::vector<FlareData>;static unsigned draws;Flares children;void load(DataSectionPtr){}float age()const{return 0.f;}const Flares& secondaries()const{return children;}void draw(const Vector4&,float,float,uint32,bool)const{++draws;}const char* material()const{return "test";}};unsigned FlareData::draws=0;
struct LensEffectManager{static unsigned s_drawCounter_;};unsigned LensEffectManager::s_drawCounter_=100;
struct LensEffect{using OcclusionLevels=std::map<float,FlareData>;enum VisibilityType{VT_UNSET,VT_TYPE1,VT_TYPE2};
size_t id_;Vector3 position_;float maxDistance_,area_,fadeSpeed_,visibility_;unsigned added_,colour_,stockActType_;bool clampToFarPlane_;VisibilityType visibilityType_;OcclusionLevels occlusionLevels_;unsigned ages=0;
static constexpr float DEFAULT_MAXDISTANCE=150,DEFAULT_AREA=1,DEFAULT_FADE_SPEED=1;
LensEffect();bool load(DataSectionPtr);void tick(float,float);void draw();LensEffect& operator=(const LensEffect&);
void area(float v){area_=v;}void fadeSpeed(float v){fadeSpeed_=v;}void ageBy(float v){assert(v==-1);++ages;}const Vector3& position()const{return position_;}float distanceToAlpha(float)const{return 1;}};
@PRODUCTION@
int main(){LensEffect original;assert(original.area_==1&&original.visibility_==0&&original.stockActType_==3);auto section=std::make_shared<DataSection>();
for(unsigned flag=0;flag<16;++flag){section->integers["actType"]=int(flag);assert(original.load(section)&&original.stockActType_==flag);original.occlusionLevels_[1]=FlareData();LensEffect copy;copy=original;assert(copy.stockActType_==flag);copy.tick(.1f,.75f);assert(copy.visibility_==((flag&1)?.75f:0.f)&&copy.ages==unsigned(bool(flag&1)));auto draws=FlareData::draws;copy.draw();assert(FlareData::draws==draws+unsigned(bool(flag&2)));section->integers.clear();assert(copy.load(section)&&copy.stockActType_==flag);}
}'''.replace('@PRODUCTION@', production)
            self.compile_run(root, source)

    def test_all_original_map_sections_and_malformed_arrays(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT required')
        sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
        from space_bin_0922 import CompiledSpace
        cases, total, nonempty = [], 0, []
        for package in (Path(client) / 'res/packages').glob('*.pkg'):
            with zipfile.ZipFile(package) as archive:
                for name in archive.namelist():
                    if not name.endswith('/space.bin'):
                        continue
                    space = CompiledSpace(archive.read(name))
                    self.assertEqual(space.sections['BWfr'].version, 2)
                    data = space.section_data('BWfr')
                    stride, count = struct.unpack_from('<II', data)
                    self.assertEqual((stride, len(data)), (48, 8 + count * 48))
                    total += count
                    if count:
                        nonempty.append(name)
                    cases.append(data)
        self.assertEqual((len(cases), total, len(nonempty)), (52, 91, 1))
        self.assertIn('hangar_premium_igr_cn', nonempty[0].lower())
        good = next(data for data in cases if len(data) > 8)
        bad = [good[:n] for n in (0, 1, 7, 9, len(good)-1)] + [good+b'x', struct.pack('<II', 44, 0), struct.pack('<II', 48, 0xffffffff)]
        poison = bytearray(good)
        struct.pack_into('<f', poison, 8 + 0x10, float('nan'))
        bad.append(bytes(poison))
        with tempfile.TemporaryDirectory(prefix='flare-all-maps-') as directory:
            root = Path(directory)
            payload = bytearray(struct.pack('<I', len(cases)+len(bad)))
            expected = bytearray()
            for data, valid in [(data, True) for data in cases] + [(data, False) for data in bad]:
                payload += struct.pack('<I', len(data)) + data
                expected += struct.pack('<I', int(valid))
                if valid:
                    expected += data
            (root / 'input').write_bytes(payload)
            self.compile_run(root, r'''#include "stock_space_flares_data.hpp"
#include <cassert>
#include <fstream>
int main(int argc,char** argv){assert(argc==3);std::ifstream in(argv[1],std::ios::binary);std::ofstream out(argv[2],std::ios::binary);unsigned n;in.read(reinterpret_cast<char*>(&n),4);wot::StockFlareData owner;std::string error;assert(!owner.read(nullptr,0,error));while(n--){unsigned size;in.read(reinterpret_cast<char*>(&size),4);std::vector<char> data(size);in.read(data.data(),size);unsigned ok=owner.read(data.data(),size,error);out.write(reinterpret_cast<char*>(&ok),4);if(ok){unsigned header[]={48,unsigned(owner.definitions.size())};out.write(reinterpret_cast<char*>(header),8);if(header[1])out.write(reinterpret_cast<char*>(owner.definitions.data()),header[1]*48);}else assert(owner.definitions.empty()&&!error.empty());}}''', (root/'input', root/'output'))
            self.assertEqual((root/'output').read_bytes(), bytes(expected))
        print('BWfr2: 52 original sections, 91 owned records, malformed input rejected')

    def test_original_colour_instruction_match(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT required')
        try:
            import unicorn  # noqa: F401
            import pefile  # noqa: F401
        except ImportError:
            self.skipTest('Original instruction oracle requires Unicorn and PE reader')
        from stock_space_flares_oracle import colours
        rng = random.Random(1513)
        cases = [(rng.getrandbits(32), tuple(rng.uniform(-100, 1000) for _ in range(3))) for _ in range(4000)]
        cases += [(c, (v, v, v)) for c in (0, 0xffffffff, 0x01177f80, 0x7ffefdfc) for v in (-1e30, -1., 0., .01, .5, 1., 254.999, 255., 256., 1e30)]
        expected = colours(Path(client) / 'WorldOfTanks.exe', cases)
        with tempfile.TemporaryDirectory(prefix='flare-colour-oracle-') as directory:
            root = Path(directory)
            (root/'input').write_bytes(b''.join(struct.pack('<I3f', c, *t) for c, t in cases))
            self.compile_run(root, r'''#include "stock_space_flares_data.hpp"
#include <cassert>
#include <fstream>
int main(int argc,char** argv){assert(argc==3);std::ifstream in(argv[1],std::ios::binary);std::ofstream out(argv[2],std::ios::binary);unsigned colour;float tint[3];while(in.read(reinterpret_cast<char*>(&colour),4)){in.read(reinterpret_cast<char*>(tint),12);assert(in);auto value=wot::StockFlareData::modulate(colour,tint);out.write(reinterpret_cast<char*>(&value),4);}}''', (root/'input', root/'output'))
            self.assertEqual((root/'output').read_bytes(), struct.pack('<%dI' % len(expected), *expected))
        print('Original flare tint: %d unchanged instruction records match' % len(cases))

    def test_actual_scene_lifecycle_mask_and_draw(self):
        with tempfile.TemporaryDirectory(prefix='actual-flare-scene-') as directory:
            root = Path(directory)
            engine = (ROOT / 'tests/stock_space_flares_test_engine.hpp').read_text()
            (root / 'engine.hpp').write_text(engine)
            (root / 'pch.hpp').write_text('#include "engine.hpp"\n')
            for relative in ('compiled_space/loader.hpp', 'compiled_space/binary_format.hpp', 'compiled_space/compiled_space.hpp', 'compiled_space/string_table.hpp',
                             'space/dynamic_scene_provider.hpp', 'scene/scene.hpp', 'scene/scene_object.hpp', 'scene/scene_draw_context.hpp', 'scene/draw_operation.hpp',
                             'scene/spatial_query_operation.hpp', 'romp/lens_effect.hpp', 'romp/lens_effect_manager.hpp', 'resmgr/bwresource.hpp'):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#include "engine.hpp"\n')
            source = r'''
#include "stock_space_flares.cpp"
using namespace BW;using namespace BW::CompiledSpace;
void input(BinaryFormat& file){using D=wot::StockFlareData::Definition;std::vector<D> defs(5);for(unsigned i=0;i<5;++i){defs[i]={100u+i,99,2,3,{10.f+float(i),20,30},{20,40,60},1,i==1?2u:1u};}file.stream.bytes.resize(8+48*defs.size());uint32 header[]={48,uint32(defs.size())};std::memcpy(file.stream.bytes.data(),header,8);std::memcpy(file.stream.bytes.data()+8,defs.data(),48*defs.size());}
struct Loader:StockFlareSceneLoader{Loader():StockFlareSceneLoader(1){}float progress()const{return percentLoaded();}};
int main(){BW::CompiledSpace::CompiledSpace space;ClientSpace wrong;BinaryFormat file;StringTable strings;strings.values={{100,"active"},{101,"masked"},{102,"missing"},{103,"invalid"}};Matrix mapping;mapping.p={1000,1000,1000};Loader loader;assert(loader.progress()==0);BWResource::onOpen=[&]{assert(loader.progress()==0);};
StockFlareSceneLoader::registerHandlers(space.scene());loader.unload();loader.unload();input(file);assert(!loader.loadFromSpace(&wrong,file,{},mapping,strings)&&!file.opens);
input(file);assert(loader.loadFromSpace(&space,file,{},mapping,strings)&&file.opens==1&&file.closes==1);assert(loader.progress()==1);assert(BWResource::opened==std::vector<std::string>({"active","masked","missing","invalid"}));assert(LensEffect::loads==2);assert(strings.lookups==std::vector<uint32>({100,101,102,103,104}));assert(loader.bind()&&loader.bind()&&space.provider.objects.size()==1);
const auto object=space.provider.objects.begin()->second;auto box=space.provider.boxes.begin()->second;assert(box.low.x==9.5f&&box.high.x==10.5f&&box.low.y==19.5f&&box.high.z==30.5f);
SceneDrawContext context;context.flares=false;space.scene().draw.handler->doDraw(context,&object,1);assert(LensEffectManager::instance().entries.empty());context.flares=true;space.scene().draw.handler->doDraw(context,&object,1);auto entries=LensEffectManager::instance().entries;assert(entries.size()==1);auto id=entries.begin()->first;assert(id&&entries.begin()->second.p.x==10);assert(entries.begin()->second.effect.max==99&&entries.begin()->second.effect.area_==2&&entries.begin()->second.effect.fade==3);const auto first=entries.begin()->second.effect.colour();space.scene().draw.handler->doDraw(context,&object,1);assert(LensEffectManager::instance().entries[id].effect.colour()==first);
Matrix actual;AABB bounds;space.scene().spatial.handler->getWorldTransform(object,&actual);space.scene().spatial.handler->getWorldVisibilityBoundingBox(object,&bounds);assert(actual.p.x==10&&bounds.high.x==10.5f);
const auto opens=file.opens;input(file);assert(!loader.loadFromSpace(&space,file,{},mapping,strings)&&file.opens==opens);
space.provider.onRemove=[&]{assert(loader.progress()==0);loader.unload();input(file);assert(!loader.loadFromSpace(&space,file,{},mapping,strings));space.scene().draw.handler->doDraw(context,&object,1);};loader.unload();loader.unload();assert(space.provider.objects.empty()&&LensEffectManager::instance().entries.empty());assert(LensEffectManager::instance().forgotten.size()==5);
space.provider.onRemove={};input(file);uint32 flags=0xfffffffe;std::memcpy(file.stream.bytes.data()+8+0x28,&flags,4);assert(loader.loadFromSpace(&space,file,{},mapping,strings)&&loader.bind());auto untinted=space.provider.objects.begin()->second;space.scene().draw.handler->doDraw(context,&untinted,1);assert(LensEffectManager::instance().entries.size()==1&&LensEffectManager::instance().entries.begin()->second.effect.colour()==0xffffffff);loader.unload();
space.provider.onAdd=[&]{loader.unload();};input(file);assert(loader.loadFromSpace(&space,file,{},mapping,strings));assert(!loader.bind()&&space.provider.objects.empty());space.provider.onAdd={};input(file);assert(loader.loadFromSpace(&space,file,{},mapping,strings)&&loader.bind());loader.unload();file.version=1;input(file);assert(!loader.loadFromSpace(&space,file,{},mapping,strings));file.version=2;input(file);file.stream.bytes.pop_back();assert(!loader.loadFromSpace(&space,file,{},mapping,strings));assert(file.opens==file.closes);
}
'''
            self.compile_run(root, source)


if __name__ == '__main__':
    unittest.main()
