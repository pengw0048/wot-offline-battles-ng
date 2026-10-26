"""Compile the production four-class BWLC scene owner and patched GPU bodies."""
import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = Path('/tmp/wot-bigworld-engine-14.4.1.tar.gz')
PREFIX = '/programming/bigworld/'
FILES = tuple('lib/moo/%s_light.%s' % (kind, suffix)
              for kind in ('omni', 'spot') for suffix in ('hpp', 'cpp', 'ipp'))
PROBE = r'''
#include "stock_space_lights.cpp"
#include <fstream>
using namespace BW;
using namespace BW::CompiledSpace;
std::vector<unsigned char> source;
void reset(BinaryFormat& file){file.bytes=source;}
int main(int argc,char**argv){
    assert(argc==2);std::ifstream input(argv[1],std::ios::binary);
    source.assign(std::istreambuf_iterator<char>(input),{});
    ClientSpace space;BinaryFormat file;StringTable strings;Matrix transform;transform.v[12]=100;
    StockLightSceneLoader loader;
    Moo::LightContainer empty;assert(!loader.intersect(AABB({0,0,0},{10,10,10}),empty));
    loader.tick(.1f);loader.unload();loader.unload();
    reset(file);assert(loader.loadFromSpace(&space,file,{},transform,strings));
    assert(file.opens==1&&file.closes==1);assert(Moo::OmniLight::alive==4);
    assert(!loader.intersect(AABB({0,0,0},{10,10,10}),empty));
    assert(loader.bind()&&loader.bind()&&space.value.adds==1);
    const auto opens=file.opens;
    reset(file);assert(!loader.doLoadFromSpace(&space,file,{},transform,strings));
    assert(file.opens==opens&&space.value.providers.size()==1);
    auto* provider=static_cast<SceneProvider*>(&loader);
    assert(provider->getView<ILightSceneViewProvider>()==static_cast<ILightSceneViewProvider*>(&loader));
    assert(provider->getView<ITickSceneProvider>()==static_cast<ITickSceneProvider*>(&loader));
    Moo::LightContainer lights;
    assert(loader.intersect(AABB({1,2,3},{3,4,5}),lights)==4);
    assert(lights.omnis.size()==2&&lights.spots.size()==2);
    assert(lights.omnis[0]->worldPosition().x==102&&lights.spots[0]->worldPosition().x==102);
    assert(lights.omnis[0]->multiplier()==25&&lights.spots[0]->multiplier()==50);
    assert(lights.omnis[0]->stockSourceFlags()==1&&lights.spots[0]->stockSourceFlags()==0);
    Moo::LightContainer worldQuery;
    assert(loader.intersect(AABB({101,2,3},{103,4,5}),worldQuery)==0);
    assert(loader.intersect(ConvexHull{AABB({1,2,3},{3,4,5})},worldQuery)==4);
    loader.tick(.5f);
    // Original pulse omni writes base+animation to worldPosition. The spot
    // wrapper updates only colour and discards that temporary position.
    assert(lights.omnis[1]->worldPosition().x==17&&lights.omnis[1]->position().x==2);
    assert(lights.spots[1]->worldPosition().x==102&&lights.spots[1]->position().x==2);
    assert(std::fabs(lights.omnis[1]->colour().r-.15f)<1e-7f&&lights.omnis[1]->colour().a==1);
    loader.tick(2.f);
    assert(lights.omnis[1]->worldPosition().x==17);
    loader.updateAnimations(10.f);assert(lights.omnis[1]->worldPosition().x==17);
    loader.debugDrawLights();assert(DebugDraw::calls==4);
    space.value.onRemove=[&]{loader.unload();reset(file);
        assert(!loader.loadFromSpace(&space,file,{},transform,strings));
        assert(file.opens==opens);Moo::LightContainer v;
        assert(!loader.intersect(AABB({0,0,0},{10,10,10}),v));};
    loader.unload();loader.unload();assert(space.value.removes==1);
    assert(space.value.providers.empty()&&Moo::OmniLight::alive==4); // containers really retain their lights
    lights={};worldQuery={};assert(Moo::OmniLight::alive==0);
    space.value.onRemove={};space.value.onAdd=[&]{loader.unload();};
    reset(file);assert(loader.loadFromSpace(&space,file,{},transform,strings));assert(!loader.bind());
    assert(space.value.providers.empty()&&Moo::OmniLight::alive==0);
    space.value.onAdd={};
    reset(file);strings.missing=true;assert(!loader.loadFromSpace(&space,file,{},transform,strings));
    assert(!loader.bind()&&Moo::OmniLight::alive==0&&file.opens==file.closes);
    strings.missing=false;Moo::AnimationManager::missing=true;
    reset(file);assert(loader.loadFromSpace(&space,file,{},transform,strings)&&loader.bind());
    Moo::LightContainer missing;assert(loader.intersect(AABB({0,0,0},{10,10,10}),missing)==4);
    loader.tick(.5f);assert(missing.omnis[1]->worldPosition().x==102&&missing.spots[1]->worldPosition().x==102);
    loader.unload();missing={};assert(Moo::OmniLight::alive==0);
    reset(file);file.bytes.pop_back();assert(!loader.loadFromSpace(&space,file,{},transform,strings));
    assert(file.opens==file.closes&&!loader.bind());
    reset(file);file.version=2;assert(!loader.loadFromSpace(&space,file,{},transform,strings));
}
'''


def load_patch():
    spec = importlib.util.spec_from_file_location('stock_space_lights_patch', ROOT / 'bigworld/patch_stock_space_lights.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def extract(root, files):
    with tarfile.open(ARCHIVE) as archive:
        for name in files:
            member = next(path for path in archive.getnames() if path.endswith(PREFIX + name))
            content = archive.extractfile(member).read()
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)


class StockSpaceLightTests(unittest.TestCase):
    def setUp(self):
        if not ARCHIVE.exists():
            self.skipTest('Pinned public source archive required')
        self.temporary = tempfile.TemporaryDirectory(prefix='stock-space-lights-')
        self.root = Path(self.temporary.name)
        extract(self.root, FILES)

    def tearDown(self):
        if hasattr(self, 'temporary'):
            self.temporary.cleanup()

    def compile_run(self, source, extra=()):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler required')
        path = self.root / 'probe.cpp'
        path.write_text(source)
        command = [compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-I', str(self.root / 'lib'),
                   str(path), '-o', str(self.root / 'probe')]
        if sys.platform != 'win32':
            command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([str(self.root / 'probe'), *map(str, extra)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_apply_twice_and_unknown_anchor_is_atomic(self):
        patch = load_patch()
        patch.apply(self.root)
        before = {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        patch.apply(self.root)
        self.assertEqual(before, {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob('*') if p.is_file()})
        # A drifted final seam must not leave any preceding successful edits.
        extract(self.root, FILES)
        target = self.root / 'lib/moo/spot_light.ipp'
        target.write_text(target.read_text().replace('#ifdef EDITOR_ENABLED', '#ifdef UNKNOWN_EDITOR_GUARD'))
        before = {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        with self.assertRaises(ValueError):
            patch.apply(self.root)
        self.assertEqual(before, {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob('*') if p.is_file()})

    def test_actual_scene_owner_lifecycle_query_and_pulse(self):
        load_patch().apply(self.root)
        library = self.root / 'lib'
        engine = ROOT / 'tests/stock_space_lights_test_engine.hpp'
        sources = [(library / 'stock_space_lights.hpp').read_text(), (library / 'stock_space_lights.cpp').read_text()]
        includes = set(re.findall(r'#include "([^"]+)"', '\n'.join(sources)))
        for name in includes - {'stock_space_lights.hpp', 'wot/stock_light_binary.hpp', 'math/linear_animation.hpp'}:
            path = library / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "%s"\n' % engine)
        from test_stock_light_binary import fixture
        source = self.root / 'fixture'
        source.write_bytes(fixture())
        self.compile_run(PROBE, (source,))

    def test_actual_gpu_bodies_consume_runtime_intensity(self):
        load_patch().apply(self.root)
        # These are the exact production gpu() functions after the patch, with
        # only their external class/value declarations supplied by the host.
        functions = []
        for kind, cls in (('omni', 'OmniLight'), ('spot', 'SpotLight')):
            source = (self.root / ('lib/moo/%s_light.ipp' % kind)).read_text()
            start = source.index(cls + '::GPU ' + cls + '::gpu(')
            stop = source.index('\n}', start) + 2
            functions.append(source[start:stop])
        source = r'''
#include <cassert>
struct Vector3{float x,y,z;};struct Vector4{float x,y,z,w;Vector4(){}Vector4(Vector3 p,float d):x(p.x),y(p.y),z(p.z),w(d){}Vector4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}Vector4 operator*(float s)const{return {x*s,y*s,z*s,w*s};}};
struct Colour{float r,g,b,a;};
#define BW_GUARD
namespace Moo {
class OmniLight{public:struct GPU{Vector4 m_pos,m_color,m_attenuation,m_padding;};Vector3 worldPosition_{2,3,4};float innerRadius_=5,outerRadius_=6;Colour colour_{.25f,.5f,.75f,1};float multiplier_=250;GPU gpu()const;};
class SpotLight{public:struct GPU{Vector4 m_pos,m_color,m_attenuation,m_dir;};Vector3 worldPosition_{2,3,4},worldDirection_{0,0,1};float innerRadius_=5,outerRadius_=6,cosConeAngle_=.8f;Colour colour_{.25f,.5f,.75f,1};float multiplier_=50;GPU gpu()const;};
@FUNCTIONS@
}
int main(){Moo::OmniLight omni;Moo::SpotLight spot;const auto o=omni.gpu();const auto s=spot.gpu();assert(o.m_color.x==62.5f&&o.m_color.w==250);assert(s.m_color.z==37.5f&&s.m_color.w==50);assert(o.m_attenuation.x==6&&s.m_attenuation.z==.8f);}
'''.replace('@FUNCTIONS@', '\n'.join(functions))
        self.compile_run(source)

    def test_actual_compiled_transform_bodies(self):
        load_patch().apply(self.root)
        functions = []
        for kind, cls in (('omni', 'OmniLight'), ('spot', 'SpotLight')):
            source = (self.root / ('lib/moo/%s_light.cpp' % kind)).read_text()
            start = source.index('void ' + cls + '::stockCompiledWorldTransform(')
            end = source.index('\n}', start) + 2
            functions.append(source[start:end])
        source = r'''
#include <cassert>
#include <cmath>
struct Vector3{float x,y,z;Vector3(){}Vector3(float a,float b,float c):x(a),y(b),z(c){}Vector3 operator+(Vector3 b)const{return {x+b.x,y+b.y,z+b.z};}Vector3 operator-(Vector3 b)const{return {x-b.x,y-b.y,z-b.z};}void normalise(){float r=std::sqrt(x*x+y*y+z*z);if(r){x/=r;y/=r;z/=r;}}};
struct Vector4{float x,y,z,w;};
struct Matrix{Vector4 rows[4]{{2,0,0,.25f},{0,1,0,0},{0,0,.5f,0},{100,200,300,1}};const Vector4& row(unsigned i)const{return rows[i];}Vector3 applyVector(Vector3 p)const{return {p.x*rows[0].x+p.y*rows[1].x+p.z*rows[2].x,p.x*rows[0].y+p.y*rows[1].y+p.z*rows[2].y,p.x*rows[0].z+p.y*rows[1].z+p.z*rows[2].z};}Vector3 applyPoint(Vector3 p)const{return applyVector(p)+Vector3(rows[3].x,rows[3].y,rows[3].z);}};
struct BoundingBox{Vector3 low,high;BoundingBox(){}BoundingBox(Vector3 a,Vector3 b):low(a),high(b){}void transformBy(const Matrix&) {}};
namespace Moo {
class OmniLight{public:Vector3 position_{2,3,4},worldPosition_;float innerRadius_=5,outerRadius_=6,worldInnerRadius_=0,worldOuterRadius_=0;BoundingBox worldBounds_;void stockCompiledWorldTransform(const Matrix&);};
class SpotLight{public:Vector3 position_{2,3,4},direction_{1,1,0},worldPosition_,worldDirection_;float innerRadius_=5,outerRadius_=6,worldInnerRadius_=0,worldOuterRadius_=0;bool dirty_=false;void stockCompiledWorldTransform(const Matrix&);};
@FUNCTIONS@
}
int main(){Matrix m;Moo::OmniLight omni;Moo::SpotLight spot;omni.stockCompiledWorldTransform(m);spot.stockCompiledWorldTransform(m);assert(omni.worldPosition_.x==104&&spot.worldPosition_.z==302);assert(omni.position_.x==2&&spot.direction_.x==1);assert(omni.worldOuterRadius_==12);assert(spot.worldOuterRadius_==6*std::sqrt(4.0625f));assert(std::fabs(spot.worldDirection_.x-2/std::sqrt(5.f))<1e-7f&&spot.dirty_);}
'''.replace('@FUNCTIONS@', '\n'.join(functions))
        self.compile_run(source)


if __name__ == '__main__':
    unittest.main()
