"""Validate the production original ground-effect owners and exact grid math."""
import importlib.util
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def compile_run(test, source, include=(), data=None):
    with tempfile.TemporaryDirectory(prefix='stock-ground-') as directory:
        cpp, binary = Path(directory) / 'probe.cpp', Path(directory) / 'probe'
        cpp.write_text(source)
        command = ['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off']
        if sys.platform != 'win32':
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        for path in include:
            command += ['-I', str(path)]
        result = subprocess.run(command + [str(cpp), '-o', str(binary)], capture_output=True, text=True)
        test.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([str(binary)], input=data, capture_output=True)
        test.assertEqual(result.returncode, 0, result.stderr.decode())
        return result.stdout


def body(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class GroundEffectsTests(unittest.TestCase):
    def test_actual_resource_and_playback_owners(self):
        source = (ROOT / 'bigworld/stock_ground_effects.cpp').read_text()
        production = '\n'.join(body(source, signature) + ';' for signature in (
            'struct Sound', 'struct Decal', 'struct Settings', 'struct Artillery', 'struct Strike', 'struct Manager'))
        probe = (ROOT / 'tests/stock_ground_effects_probe.cpp').read_text().replace('@PRODUCTION@', production)
        compile_run(self, probe, (ROOT / 'bigworld',))

    def test_all_authored_resources_through_production_readers(self):
        client = Path(os.environ.get('WOT_0922_CLIENT', '/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD'))
        if not client.is_dir():self.skipTest('Pinned original resources required')
        sys.path.insert(0,str(ROOT.parents[1] / 'tools'))
        from prepare_native_client import OriginalResources
        from packed_xml import read_packed_xml
        import base64
        resources = OriginalResources(client)
        root = read_packed_xml(resources.read('scripts/item_defs/vehicles/common/shot_effects.xml'))
        sections = [(dict(dict(root.children)[name].value.children)[child].value, artillery) for name,child,artillery in (
            (b'ArtilleryPreparation',b'artillery',True),(b'artilleryStrike',b'artillery',True),(b'airbombStrike',b'airstrike',False))]
        cpp = (ROOT / 'bigworld/stock_ground_effects.cpp').read_text()
        production = '\n'.join(body(cpp, signature) + ';' for signature in (
            'struct Sound','struct Decal','struct Settings','struct Artillery','struct Strike','struct Manager'))
        probe = (ROOT / 'tests/stock_ground_effects_probe.cpp').read_text().replace('@PRODUCTION@', production)
        probe = probe[:probe.index('int main(){')]
        builder = []
        count = 0
        paths = set()
        def literal(value):return json.dumps(value)
        def scalar(value):
            if value.value_type==0:return scalar(value.value.value)
            if value.value_type==1:return value.value.decode()
            if value.value_type==5:return base64.b64encode(value.value).decode()
            if value.value_type==3:return ' '.join(str(f) for f in struct.unpack('<'+'f'*(len(value.value)//4),value.value))
            if value.value_type==4:return 'true' if value.value else 'false'
            return str(value.value)
        def node(element):
            nonlocal count
            name='section%d'%count;count+=1
            builder.append('auto %s=section();'%name)
            builder.append('%s->value=%s;'%(name,literal(scalar(element.value))))
            for key,value in element.children:
                if value.value_type==0:
                    child=node(value.value);builder.append('%s->children[%s]=%s;'%(name,literal(key.decode()),child))
                else:builder.append('set(%s,%s,%s);'%(name,literal(key.decode()),literal(scalar(value))))
            return name
        owners=[]
        for section,artillery in sections:
            owners.append((node(section),artillery))
            for surface in (b'ground',b'stone',b'wood',b'metal',b'snow',b'sand',b'water'):
                source=dict(dict(section.children)[surface].value.children)
                for effect in (b'effect_explosion',b'effect_explosion_add',b'effect_explosion_snp',b'effect_explosion_fwd',b'effect_smoke'):
                    name=scalar(source[effect])
                    if name:paths.add(name)
        self.assertEqual(len(paths),43)
        main=['int main(){std::string error;']+builder
        for index,(name,artillery) in enumerate(owners):
            main += ['Settings settings%d;assert(settings%d.load(%s,%s,error));'%(index,index,name,'true' if artillery else 'false'),
                     'assert(settings%d.decal.group=="fast");'%index]
        main += ['assert(settings0.smokeVariation==0.f&&settings0.optimisation==8);',
                 'assert(settings1.smokeVariation==.1f&&settings1.addVariation==.4f&&settings1.optimisation==10&&settings1.sniperOptimisation==20);',
                 'assert(settings2.addVariation==.2f&&settings2.stepOffset==.6f&&settings2.optimisation==6);']
        with tempfile.TemporaryDirectory(prefix='ground-original-resources-') as directory:
            for index,name in enumerate(sorted(paths)):
                path=Path(directory)/str(index);path.write_bytes(resources.read(name))
                main.append('{std::ifstream file(%s,std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});wot::StockTPSResource resource;assert(resource.read(bytes.data(),bytes.size(),error));for(const auto& effect:resource.effects()){assert(effect.scalar(0x54)==0.f);for(const auto& emitter:effect.emitters)assert(!(emitter.word(0xac)&0x4200u));}}'%literal(str(path)))
            main += ['}']
            compile_run(self,'#include <fstream>\n#include <iterator>\n#define WOT_STOCK_TPS_RESOURCES_PORTABLE\n#include "stock_tps_resources.cpp"\n'+probe+'\n'.join(main),(ROOT/'bigworld',))

    def test_actual_one_shot_runtime_submission(self):
        source = (ROOT / 'bigworld/stock_tps_scene.cpp').read_text()
        production = body(source,'bool playStockTPSOneShot(')
        probe = r'''
#include <cassert>
#include <memory>
#include <string>
#include <vector>
namespace BW{using string=std::string;}
struct Vector3{float x,y,z;};using TPSVector=Vector3;TPSVector vector(const Vector3& v){return v;}
struct Matrix{Vector3 position;void setTranslate(const Vector3& p){position=p;}};
struct SceneEffect;
struct Runtime {bool closed=false,resourceReady=true,sourceFailed=false,closeOnCreate=false;std::vector<std::shared_ptr<SceneEffect>> pendingEffects;
std::shared_ptr<int> preload(const BW::string&,std::string& error){if(!resourceReady){error="missing PFX";return nullptr;}return std::make_shared<int>(1);}};
std::shared_ptr<Runtime> runtime;
std::shared_ptr<Runtime> currentRuntime(){return runtime;}
unsigned destroyed=0;
struct Source{bool failed;};
struct Group{std::vector<std::shared_ptr<Source>> sources;};
struct SceneEffect {
std::vector<std::shared_ptr<Group>> groups;Matrix pose;std::shared_ptr<int> resource;
SceneEffect(Runtime& owner,std::shared_ptr<int> data,const Matrix& initial):pose(initial),resource(data){
 auto group=std::make_shared<Group>();group->sources.push_back(std::make_shared<Source>(Source{owner.sourceFailed}));groups.push_back(group);if(owner.closeOnCreate)owner.closed=true;}
~SceneEffect(){++destroyed;}
void setImplicitPose(const TPSVector& value,const TPSVector& direction){assert(direction.x==0&&direction.y==1&&direction.z==0);pose.position=value;}
};
''' + production + r'''
int main(){std::string error;Vector3 position{2,3,4};
assert(!playStockTPSOneShot("pfx",position,error)&&error=="tPS runtime is unavailable");
runtime=std::make_shared<Runtime>();runtime->closed=true;assert(!playStockTPSOneShot("pfx",position,error)&&runtime->pendingEffects.empty());
runtime->closed=false;runtime->resourceReady=false;assert(!playStockTPSOneShot("pfx",position,error)&&error=="missing PFX"&&runtime->pendingEffects.empty());
runtime->resourceReady=true;runtime->sourceFailed=true;assert(!playStockTPSOneShot("pfx",position,error)&&destroyed==1&&runtime->pendingEffects.empty());
runtime->sourceFailed=false;runtime->closeOnCreate=true;assert(!playStockTPSOneShot("pfx",position,error)&&destroyed==2&&runtime->pendingEffects.empty());
runtime->closed=false;runtime->closeOnCreate=false;assert(playStockTPSOneShot("pfx",position,error)&&error.empty()&&runtime->pendingEffects.size()==1);
std::weak_ptr<SceneEffect> weak=runtime->pendingEffects.front();assert(runtime->pendingEffects.front()->pose.position.y==3&&runtime->pendingEffects.front()->resource);
assert(!weak.expired());runtime->pendingEffects.clear();assert(weak.expired()&&destroyed==3);
}
'''
        compile_run(self,probe)

    def test_actual_surface_queries_and_water_guards(self):
        source=(ROOT / 'bigworld/stock_ground_effects.cpp').read_text()
        water=body(source,'float Water::stockGroundRayDistance(')
        surface=body(source,'struct Hit')+';\n'+body(source,'float waterDistance(')+'\n'+body(source,'Vector3 groundPoint(')
        probe=(ROOT / 'tests/stock_ground_effects_surface_probe.cpp').read_text().replace('@WATER@',water).replace('@SURFACE@',surface)
        compile_run(self,probe)

    def test_grid_original_instruction_oracle(self):
        executable = Path(os.environ.get('WOT_0922_CLIENT', '/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD')) / 'WorldOfTanks.exe'
        interpreter = Path(os.environ.get('WOT_ORACLE_PYTHON', '/tmp/wot-native-boundaries-venv/bin/python'))
        if not executable.is_file() or not interpreter.is_file():
            self.skipTest('Pinned original PE and Unicorn interpreter required')
        generator = random.Random(1513)
        cases = []
        axes = ((0., 0., 1.), (1., 0., 0.), (0., 0., -1.), (-1., 0., 0.), (.6, 0., .8), (0., .8, .6))
        for index in range(72):
            across, along = generator.randrange(1, 9), generator.randrange(1, 8)
            direction = axes[index % len(axes)]
            # The imported D3DX result is supplied independently to both paths.
            cases.append(dict(origin=[generator.uniform(-1000, 1000) for _ in range(3)],
                direction=direction, velocity=[v * 25 for v in direction],
                width=generator.uniform(.01, 70), length=generator.uniform(.01, 200),
                across=across, along=along, variation=(0., .2, .5, 1.)[index % 4],
                offset=(0., .1, .6, 1.)[index % 4],
                random=[generator.randrange(32768) for _ in range(across * along * 3)]))
        script = ('import json,sys;sys.path.insert(0,sys.argv[1]);'
                  'from stock_ground_effects_oracle import airstrikes;'
                  'sys.stdout.buffer.write(b"".join(airstrikes(sys.argv[2],json.load(sys.stdin))))')
        original = subprocess.run([str(interpreter), '-c', script, str(ROOT / 'tests'), str(executable)],
                                  input=json.dumps(cases).encode(), capture_output=True)
        self.assertEqual(original.returncode, 0, original.stderr.decode() + original.stdout[:500].decode(errors='replace'))
        source = r'''
#include "stock_ground_effects_data.hpp"
#include <cstdio>
#include <cassert>
int main() {
unsigned count;assert(scanf("%u",&count)==1);
for(unsigned k=0;k<count;++k) {
    wot::StockGroundEffectMath::Vector origin,direction,velocity;
    for(float& f:origin)assert(scanf("%f",&f)==1);
    for(float& f:direction)assert(scanf("%f",&f)==1);
    for(float& f:velocity)assert(scanf("%f",&f)==1);
    float width,length,variation,offset;int across,along;
    assert(scanf("%f%f%d%d%f%f",&width,&length,&across,&along,&variation,&offset)==6);
    std::vector<int> random(static_cast<size_t>(across*along*3));
    for(int& v:random)assert(scanf("%d",&v)==1);
    size_t cursor=0;
    const float speed=sqrtf(velocity[0]*velocity[0]+velocity[1]*velocity[1]+velocity[2]*velocity[2]);
    const auto plan=wot::StockGroundEffectMath::airstrike(origin,direction,speed,width,length,
        across,along,variation,offset,[&](){return random.at(cursor++);});
    assert(cursor==random.size());
    for(const auto& impact:plan) {
        assert(fwrite(impact.position.data(),4,3,stdout)==3);
        assert(fwrite(&impact.delay,4,1,stdout)==1);
        assert(fwrite(&impact.kind,4,1,stdout)==1);
    }
}
}'''
        tokens = [str(len(cases))]
        for case in cases:
            tokens += [str(v) for name in ('origin', 'direction', 'velocity') for v in case[name]]
            tokens += [str(case[name]) for name in ('width', 'length', 'across', 'along', 'variation', 'offset')]
            tokens += [str(v) for v in case['random']]
        actual = compile_run(self, source, (ROOT / 'bigworld',), ' '.join(tokens).encode())
        if actual != original.stdout:
            for index, (a, b) in enumerate(zip(struct.iter_unpack('<I', actual), struct.iter_unpack('<I', original.stdout))):
                if a != b:
                    record=index//5;case_index=0
                    while record>=cases[case_index]['across']*cases[case_index]['along']+1:
                        record-=cases[case_index]['across']*cases[case_index]['along']+1;case_index+=1
                    self.fail('Original grid word %d differs: production=%08x original=%08x case=%s record=%s' % (index, a[0], b[0], cases[case_index], record))
        self.assertEqual(actual, original.stdout)

    def test_decal_original_submission_instruction_oracle(self):
        executable=Path(os.environ.get('WOT_0922_CLIENT','/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD'))/'WorldOfTanks.exe'
        interpreter=Path(os.environ.get('WOT_ORACLE_PYTHON','/tmp/wot-native-boundaries-venv/bin/python'))
        if not executable.is_file() or not interpreter.is_file():self.skipTest('Pinned original PE and Unicorn interpreter required')
        cpp=(ROOT/'bigworld/stock_ground_effects.cpp').read_text()
        probe=(ROOT/'tests/stock_ground_effects_probe.cpp').read_text().split('@PRODUCTION@')[0]
        old=body(probe,'bool addStockDynamicDecal(')
        new=r'''bool addStockDynamicDecal(float lifetime,const Vector3& start,const Vector3& end,const Vector2& size,float yaw,
const std::string&,const std::string&,const std::string&,bool simple,std::string&){assert(simple);
float values[]={lifetime,start.x,start.y,start.z,end.x,end.y,end.z,size.x,size.y,yaw};
assert(fwrite(values,4,10,stdout)==10);return true;}'''
        probe=probe.replace(old,new)+r'''
unsigned explicitRandom[2],randomCursor=0;
extern "C" int rand(void) noexcept(noexcept(::rand())) {assert(randomCursor<2);return static_cast<int>(explicitRandom[randomCursor++]);}
'''+body(cpp,'struct Decal')+';'
        probe+=r'''int main(){unsigned count;assert(scanf("%u",&count)==1);for(unsigned i=0;i<count;++i){
unsigned yaw;Vector3 point;Decal decal;decal.group="fast";
assert(scanf("%f%f%f%f%f%f%u%u%u",&point.x,&point.y,&point.z,&decal.size.x,&decal.size.y,&decal.variation,&yaw,&explicitRandom[0],&explicitRandom[1])==9);
decal.randomYaw=yaw!=0;randomCursor=0;std::string error;assert(decal.play(point,error));assert(randomCursor==(yaw?2u:1u));}}
'''
        generator=random.Random(1513);cases=[];tokens=['64']
        for index in range(64):
            case=dict(point=[generator.uniform(-1000,1000),generator.choice((.00001,-.0001,2.,999.99,10000.1,-300.5)),generator.uniform(-1000,1000)],
                size=[generator.uniform(.01,30),generator.uniform(.01,30)],variation=(0.,.15,.25,.9)[index%4],yaw=bool(index%2),random=[generator.randrange(32768),generator.randrange(32768)])
            cases.append(case)
            tokens += [str(v) for name in ('point','size') for v in case[name]]+[str(case['variation']),str(int(case['yaw']))]+[str(v) for v in case['random']]
        actual=compile_run(self,probe,(ROOT/'bigworld',),' '.join(tokens).encode())
        for case in cases:
            if not case['yaw']:case['random']=case['random'][:1]
        geometry=actual
        script='import json,sys;sys.path.insert(0,sys.argv[1]);from stock_ground_effects_oracle import decals;sys.stdout.buffer.write(b"".join(decals(sys.argv[2],json.load(sys.stdin))))'
        original=subprocess.run([str(interpreter),'-c',script,str(ROOT/'tests'),str(executable)],input=json.dumps(cases).encode(),capture_output=True)
        self.assertEqual(original.returncode,0,original.stderr.decode()+original.stdout[:500].decode(errors='replace'))
        self.assertEqual(geometry,original.stdout)

    def test_patch_is_transactional_and_idempotent(self):
        path = ROOT / 'bigworld/patch_stock_ground_effects.py'
        spec = importlib.util.spec_from_file_location('ground_patch', path)
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            self.skipTest('Pinned public source archive required')
        with tarfile.open(archive) as source:
            member = next(m for m in source.getmembers() if m.name.endswith('/programming/bigworld/lib/romp/water.hpp'))
            data = source.extractfile(member).read()
            water_cpp = source.extractfile(next(m for m in source.getmembers() if m.name.endswith('/programming/bigworld/lib/romp/water.cpp'))).read()
            client_files = {}
            for name in ('world_app.cpp', 'script_app.cpp', 'script_bigworld.cpp'):
                member = next(m for m in source.getmembers() if m.name.endswith('/programming/bigworld/client/' + name))
                client_files[name] = source.extractfile(member).read().decode('utf-8-sig')
        # The ground-effect adapter follows the already reviewed tPS seams.
        client_files['world_app.cpp'] = client_files['world_app.cpp'].replace(
            '        GlobalEmbodiments::tick( dGameTime );',
            '        GlobalEmbodiments::tick( dGameTime );\n        tickStockTPSRuntime( dGameTime );')
        client_files['script_app.cpp'] = client_files['script_app.cpp'].replace(
            '\tBigWorldClientScript::fini();', '\tfiniStockTPSRuntime();\n\tBigWorldClientScript::fini();')
        def clients(root):
            (root / 'client').mkdir()
            for name, content in client_files.items():
                (root / 'client' / name).write_text(content)
        water_spec=importlib.util.spec_from_file_location('water_patch',ROOT/'bigworld/patch_stock_space_water.py')
        water_patch=importlib.util.module_from_spec(water_spec);water_spec.loader.exec_module(water_patch)
        for sequence in ((patch.apply,water_patch.patch),(water_patch.patch,patch.apply)):
            with tempfile.TemporaryDirectory(prefix='ground-water-patch-order-') as directory:
                root=Path(directory);header=root/'lib/romp/water.hpp';header.parent.mkdir(parents=True)
                clients(root)
                header.write_bytes(data);(header.parent/'water.cpp').write_bytes(water_cpp)
                for apply in sequence:apply(root)
                once={p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()}
                for apply in sequence:apply(root)
                self.assertEqual(once,{p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()})
                self.assertEqual(header.read_text().count('stockGroundRayDistance('),1)
                self.assertIn('stockVisibleCells_',header.read_text())
        with tempfile.TemporaryDirectory(prefix='ground-patch-') as directory:
            root = Path(directory)
            header = root / 'lib/romp/water.hpp';header.parent.mkdir(parents=True)
            clients(root)
            header.write_bytes(data)
            patch.apply(root)
            once = {p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()}
            patch.apply(root)
            self.assertEqual(once,{p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()})
            world = (root / 'client/world_app.cpp').read_text()
            teardown = (root / 'client/script_app.cpp').read_text()
            self.assertLess(world.index('tickStockGroundEffects('), world.index('tickStockTPSRuntime('))
            self.assertLess(teardown.index('finiStockGroundEffects('), teardown.index('finiStockTPSRuntime('))
            self.assertIn('StockGroundEffects_token |', (root / 'client/script_bigworld.cpp').read_text())
            broken = root / 'client/world_app.cpp'
            broken.write_text(broken.read_text().replace('tickStockGroundEffects( dGameTime );', 'lostGroundOwner();')
                              .replace('tickStockTPSRuntime( dGameTime );', 'lostTPSOwner();'))
            before = {p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()}
            with self.assertRaises(ValueError):patch.apply(root)
            self.assertEqual(before, {p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()})
            header.write_bytes(b'changed anchor')
            with self.assertRaises(ValueError):patch.apply(root)
            self.assertEqual(header.read_bytes(),b'changed anchor')


if __name__ == '__main__':
    unittest.main()
