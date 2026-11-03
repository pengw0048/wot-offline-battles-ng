"""Compile the complete production GPU owner against observable SDK boundaries.

These host tests prove lifecycle and query contracts, not Windows GPU visibility.
"""
from pathlib import Path
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
PROBE = r'''
using namespace BW;
static const BoundingBox distant({-1,-1,10},{1,1,12});
static const BoundingBox crossing({-1,-1,-1},{1,1,1});
static IDirect3DDevice9 device;
static IDirect3DSurface9 depth;
static Matrix originalWorld, previousWorld, instanceMatrices[3];
static Vector4 originalPalette[6];
StockSceneOcclusionView view(uint32 frame){
  StockSceneOcclusionView value;value.frame=frame;value.depth=&depth;
  value.viewport=device.viewport;value.nearPlane=.1f;value.farPlane=1000.f;
  return value;
}
void next(StockSceneOcclusionMapping& mapping,uint32 frame){auto value=view(frame);Moo::rc().projection_=value.viewProjection;mapping.beginFrame(value);}
IDirect3DQuery9& query(){assert(!fixtureQueries.empty());return *fixtureQueries.back();}
void ready(DWORD pixels){query().result=S_OK;query().pixels=pixels;}
void run(const std::string& requested){
  const bool paletteMode=requested.compare(0,8,"palette:")==0;
  const std::string scenario=paletteMode?requested.substr(8):requested;
  Moo::rc().device_=&device;device.depth=&depth;
  originalWorld[3][0]=71;
  auto& visual=Moo::rc().visual;
  if(paletteMode)visual.worldMatrixPalette(originalPalette,6);else visual.worldMatrix(&originalWorld);
  visual.stockPreviousWorld(&previousWorld);visual.stockPreviousPaletteEntries(4);
  visual.stockInstancingStream(instanceMatrices,3);
  fixture::onPass=[] {
    device.states[D3DRS_ZWRITEENABLE]=0;device.states[D3DRS_COLORWRITEENABLE]=0;
    Moo::rc().visual.stockPreviousWorld(nullptr);Moo::rc().visual.stockPreviousPaletteEntries(0);
    Moo::rc().visual.stockInstancingStream(nullptr,0);
  };
  if(scenario=="history"){
    StockSceneOcclusionMapping mapping(1);next(mapping,10);
    assert(mapping.visible(0,distant));mapping.issue();ready(0);
    assert(fixture::draws==1&&query().begins==1&&query().ends==1);
    next(mapping,11);assert(!mapping.visible(0,distant));
    mapping.issue();ready(50);next(mapping,12);assert(mapping.visible(0,distant));
    mapping.issue();ready(0);next(mapping,13);assert(!mapping.visible(0,distant));
    mapping.issue();query().result=S_FALSE;next(mapping,14);
    assert(mapping.visible(0,distant));
    next(mapping,15);assert(mapping.visible(0,distant));
    assert(fixture::draws==4); // Pending queries never allocate or issue again.
  }else if(scenario=="near"){
    StockSceneOcclusionMapping mapping(1);next(mapping,10);
    assert(mapping.visible(0,crossing));mapping.issue();assert(fixture::creates==0);
    assert(mapping.visible(0,distant));mapping.issue();ready(0);
    next(mapping,11);assert(mapping.visible(0,crossing));
    mapping.issue();assert(fixture::draws==1);
    next(mapping,12);assert(mapping.visible(0,distant)); // Forced visibility updated history.
    mapping.issue();
  }else if(scenario=="bounded"){
    StockSceneOcclusionMapping mapping(2);next(mapping,1);
    assert(mapping.visible(0,distant));assert(mapping.visible(0,distant));
    mapping.issue();mapping.issue();assert(fixture::creates==1&&fixture::draws==1);
    next(mapping,2);query().result=S_FALSE;
    assert(mapping.visible(0,distant));const auto reads=query().reads;
    assert(mapping.visible(0,distant));assert(query().reads==reads);
    assert(mapping.visible(1,distant));mapping.issue();assert(fixture::draws==2);
  }else if(scenario=="capacity"){
    StockSceneOcclusionMapping mapping(4001);next(mapping,1);
    for(unsigned i=0;i<4001;++i)assert(mapping.visible(i,distant));
    assert(fixture::creates==4000);mapping.issue();assert(fixture::draws==4000);
    for(auto& value:fixtureQueries){value->result=S_FALSE;}
    next(mapping,2);assert(mapping.visible(4000,distant));
    assert(fixture::creates==4000);
  }else if(scenario=="invalidate"){
    StockSceneOcclusionMapping mapping(1);next(mapping,1);
    assert(mapping.visible(0,distant));mapping.issue();ready(0);
    mapping.invalidate(0);next(mapping,2);assert(mapping.visible(0,distant));
    mapping.issue();ready(0);next(mapping,3);assert(!mapping.visible(0,distant));
    mapping.invalidate(0);assert(mapping.visible(0,distant));mapping.issue();
  }else if(scenario=="retire"){
    using Mapping=StockSceneOcclusionMapping;
    alignas(Mapping) unsigned char storage[sizeof(Mapping)];
    auto* mapping=new(storage) Mapping(1);next(*mapping,1);
    assert(mapping->visible(0,distant));mapping->issue();ready(0);
    mapping->retire();assert(mapping->visible(0,distant));mapping->issue();
    mapping->retire();mapping->~Mapping();
    mapping=new(storage) Mapping(1);next(*mapping,2);
    assert(mapping->visible(0,distant));mapping->issue();mapping->~Mapping();
    assert(Moo::DeviceCallback::owners.empty());
  }else if(scenario=="reset"||scenario=="reset-reverse"){
    StockSceneOcclusionMapping mapping(1);next(mapping,1);
    assert(mapping.visible(0,distant));mapping.issue();ready(0);
    next(mapping,2);assert(!mapping.visible(0,distant));mapping.issue();ready(0);
    device.viewport.Width=1024;next(mapping,3);assert(mapping.visible(0,distant));
    mapping.issue();ready(0);
    assert(Moo::DeviceCallback::owners.size()==2);
    auto callbacks=Moo::DeviceCallback::owners;
    if(scenario=="reset-reverse")std::reverse(callbacks.begin(),callbacks.end());
    for(auto* callback:callbacks)callback->deleteUnmanagedObjects();
    next(mapping,4);assert(mapping.visible(0,distant));mapping.issue();
  }else if(scenario=="camera"){
    StockSceneOcclusionMapping mapping(1);next(mapping,1);
    assert(mapping.visible(0,distant));mapping.issue();ready(0);
    auto moving=view(2);moving.viewProjection[3][0]=.05f;mapping.beginFrame(moving);
    Moo::rc().projection_=moving.viewProjection;
    assert(!mapping.visible(0,distant)); // Normal camera motion keeps fresh receipt.
    mapping.issue();
  }else if(scenario=="frozen"){
    StockSceneOcclusionMapping mapping(1);next(mapping,1);
    assert(mapping.visible(0,distant));
    assert(mapping.visible(0,BoundingBox({100,100,100},{200,200,200})));
    mapping.issue();assert(fixture::draws==1);
    const WORD expected[36]={0,3,1,0,2,3,6,5,7,6,4,5,2,4,6,2,0,4,
                            1,7,5,1,3,7,6,7,2,2,7,3,0,1,4,4,1,5};
    assert(std::equal(std::begin(expected),std::end(expected),fixture::indices.begin()));
    for(unsigned i=0;i<8;++i){
      const Vector3 corner(i&1?1.f:-1.f,i&2?1.f:-1.f,i&4?12.f:10.f);
      Vector3 delta(-corner.x,-corner.y,-corner.z);delta.normalise();
      const float offset=(1000.f-.1f)/(1000.f*.1f)*1.2e-6f;
      const auto expectedPoint=corner+delta*offset;
      assert(std::fabs(fixture::vertices[i*3]-expectedPoint.x)<1e-7f);
      assert(std::fabs(fixture::vertices[i*3+1]-expectedPoint.y)<1e-7f);
      assert(std::fabs(fixture::vertices[i*3+2]-expectedPoint.z)<1e-6f);
    }
    ready(0);next(mapping,8);assert(!mapping.visible(0,distant));
    mapping.issue(); // #1513 stamps completed receipts with resolveFrame-1.
  }else if(scenario=="timeout"||scenario=="timeout-ready"){
    StockSceneOcclusionMapping mapping(1);next(mapping,1);
    assert(mapping.visible(0,distant));mapping.issue();
    auto* abandoned=&query();
    if(scenario=="timeout-ready")ready(0);
    // This is a test of the explicit wall-clock expiry law, not an async ack.
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    next(mapping,2);assert(mapping.visible(0,distant));
    assert(abandoned->reads==1&&abandoned->refs==0&&fixture::creates==2);
    mapping.issue();ready(0);next(mapping,3);assert(!mapping.visible(0,distant));mapping.issue();
  }else if(scenario=="arrival-after-poll"){
    StockSceneOcclusionMapping mapping(1);next(mapping,1);
    assert(mapping.visible(0,distant));mapping.issue();
    auto* pendingQuery=&query();next(mapping,2);assert(pendingQuery->reads==1);
    ready(0);assert(mapping.visible(0,distant));assert(pendingQuery->reads==1);
    next(mapping,3);assert(pendingQuery->reads==2);assert(!mapping.visible(0,distant));mapping.issue();
  }else if(scenario=="off-frustum"){
    StockSceneOcclusionMapping mapping(2);next(mapping,1);
    assert(mapping.visible(0,distant));mapping.issue();auto* first=&query();ready(1);
    next(mapping,2);assert(first->reads==1); // No visible() call for object 0.
    assert(mapping.visible(1,distant));assert(fixture::creates==1);
    mapping.issue();assert(first->begins==2&&first->ends==2);
  }else if(scenario=="shared-reuse"){
    Scene scene;StockSceneOcclusionMapping a(4000),b(1),c(1);
    auto first=view(1);first.scene=&scene;a.beginFrame(first);b.beginFrame(first);c.beginFrame(first);
    bool admitted=false;
    for(unsigned i=0;i<4000;++i){assert(a.visible(i,distant,true,&admitted));assert(admitted);}
    a.issue();assert(b.visible(0,distant,true,&admitted)&&!admitted);
    assert(fixture::creates==4000);
    for(auto& item:fixtureQueries){item->result=S_OK;item->pixels=1;}
    auto second=view(2);second.scene=&scene;a.beginFrame(second);b.beginFrame(second);c.beginFrame(second);
    assert(fixture::gets==4000);
    assert(b.visible(0,distant,true,&admitted)&&admitted);b.issue();
    assert(c.visible(0,distant,true,&admitted)&&admitted);c.issue();
    assert(fixture::creates==4000&&fixture::draws==4002);
    a.retire(); // Cached objects remain owned by the shared pool, not mapping A.
    StockSceneOcclusionMapping d(4000);d.beginFrame(second);unsigned accepted=0;
    for(unsigned i=0;i<4000;++i){assert(d.visible(i,distant,true,&admitted));accepted+=admitted?1:0;}
    assert(accepted==3998&&fixture::creates==4000);d.issue();
  }else if(scenario=="shared-retire"){
    Scene scene;StockSceneOcclusionMapping a(4000),b(1);
    auto first=view(1);first.scene=&scene;a.beginFrame(first);b.beginFrame(first);
    for(unsigned i=0;i<4000;++i)assert(a.visible(i,distant));a.issue();
    auto* abandoned=fixtureQueries.front().get();bool admitted=true;
    assert(b.visible(0,distant,true,&admitted)&&!admitted);
    a.retire();assert(abandoned->refs==0);
    abandoned->result=S_OK;abandoned->pixels=0; // A late driver receipt belongs to retired A.
    auto second=view(2);second.scene=&scene;b.beginFrame(second);
    assert(b.visible(0,distant,true,&admitted)&&admitted);assert(fixture::creates==4001);
    assert(&query()!=abandoned);b.issue();auto third=view(3);third.scene=&scene;
    b.beginFrame(third);assert(b.visible(0,distant));
  }else if(scenario=="scene-off-frustum"){
    Scene scene;auto a=std::make_shared<StockSceneOcclusionMapping>(4000);
    auto b=std::make_shared<StockSceneOcclusionMapping>(4001);
    IntersectionSet first;first.buckets.resize(1);
    for(unsigned i=0;i<4000;++i)first.buckets[0].push_back({a,i,distant,true});
    first.buckets[0].push_back({b,0,distant,true});
    filterStockSceneOcclusion(scene,first,Matrix());issueStockSceneOcclusion(scene);
    assert(fixture::creates==4000&&fixture::draws==4000);
    for(auto& item:fixtureQueries){item->result=S_OK;item->pixels=1;}
    Moo::rc().frame=2;IntersectionSet second;second.buckets.resize(1);
    for(unsigned i=0;i<4001;++i)second.buckets[0].push_back({b,i,distant,true});
    filterStockSceneOcclusion(scene,second,Matrix());
    assert(fixture::gets==4000&&second.buckets[0].size()==4001);
    issueStockSceneOcclusion(scene);
    assert(fixture::creates==4000&&fixture::draws==8000); // Entire A mapping was off-frustum.
  }else if(scenario=="invalid-view"){
    StockSceneOcclusionMapping mapping(1);
    for(unsigned mode=0;mode<6;++mode){
      auto invalid=view(mode+1);
      if(mode==0)invalid.nearPlane=0;
      if(mode==1)invalid.farPlane=invalid.nearPlane;
      if(mode==2)invalid.viewport.Width=0;
      if(mode==3)invalid.camera.x=std::numeric_limits<float>::quiet_NaN();
      if(mode==4)invalid.viewProjection[0][0]=std::numeric_limits<float>::infinity();
      if(mode==5)invalid.nearPlane=std::numeric_limits<float>::infinity();
      mapping.beginFrame(invalid);assert(mapping.visible(0,distant));mapping.issue();
    }
    assert(fixture::creates==0);next(mapping,7);assert(mapping.visible(0,distant));mapping.issue();
  }else if(scenario=="issue-camera"){
    StockSceneOcclusionMapping mapping(1);next(mapping,1);
    assert(mapping.visible(0,distant));Moo::rc().projection_[3][0]=100;
    mapping.issue();assert(fixture::draws==0);
    next(mapping,2);assert(mapping.visible(0,distant));mapping.issue();
  }else if(scenario=="scene"){
    Scene scene,other;auto mapping=std::make_shared<StockSceneOcclusionMapping>(1);
    auto objects=[&]{IntersectionSet set;set.buckets={{{mapping,0,distant,true},{mapping,0,distant,false}}};return set;};
    auto first=objects();filterStockSceneOcclusion(scene,first,Matrix());
    assert(first.buckets[0].size()==2&&fixture::draws==0);
    fixture::events.push_back("opaque");issueStockSceneOcclusion(other);assert(fixture::draws==0);
    issueStockSceneOcclusion(scene);assert(fixture::draws==1);ready(0);
    assert(std::find(fixture::events.begin(),fixture::events.end(),"opaque") <
           std::find(fixture::events.begin(),fixture::events.end(),"query-begin"));
    Moo::rc().frame=2;auto second=objects();filterStockSceneOcclusion(scene,second,Matrix());
    assert(second.buckets[0].size()==1&&!second.buckets[0][0].eligible);
    issueStockSceneOcclusion(scene);ready(1);Moo::rc().frame=3;
    auto third=objects();filterStockSceneOcclusion(scene,third,Matrix());
    assert(third.buckets[0].size()==2);mapping->retire();issueStockSceneOcclusion(scene);
    assert(fixture::draws==2); // Weak prepared ownership cannot resurrect retired mapping.
  }else if(scenario=="scene-depth-get"||scenario=="scene-viewport-get"){
    Scene scene;auto mapping=std::make_shared<StockSceneOcclusionMapping>(1);
    IntersectionSet objects;objects.buckets={{{mapping,0,distant,true}}};
    fixture::failure=scenario.substr(6);
    filterStockSceneOcclusion(scene,objects,Matrix());issueStockSceneOcclusion(scene);
    assert(objects.buckets[0].size()==1&&fixture::creates==0&&fixture::draws==0);
    fixture::failure.clear();
  }else if(scenario=="query-get"){
    StockSceneOcclusionMapping mapping(1);next(mapping,1);
    assert(mapping.visible(0,distant));mapping.issue();fixture::failure="query-get";
    next(mapping,2);assert(mapping.visible(0,distant));fixture::failure.clear();mapping.issue();
  }else if(scenario=="depth-change"){
    StockSceneOcclusionMapping mapping(1);next(mapping,1);
    assert(mapping.visible(0,distant));IDirect3DSurface9 other;device.depth=&other;
    mapping.issue();assert(!fixture::draws);device.depth=&depth;
    next(mapping,2);assert(mapping.visible(0,distant));mapping.issue();
  }else{
    StockSceneOcclusionMapping mapping(1);next(mapping,1);
    fixture::failure=scenario;assert(mapping.visible(0,distant));mapping.issue();
    if(scenario!="draw")assert(fixture::draws==0||scenario=="query-end");
    fixture::failure.clear();next(mapping,2);assert(mapping.visible(0,distant));mapping.issue();
    ready(0);next(mapping,3);assert(!mapping.visible(0,distant));mapping.issue();
  }
  assert(visual.worldMatrix()==(paletteMode?&Matrix::identity:&originalWorld));
  assert(visual.worldMatrixPalette()==(paletteMode?originalPalette:nullptr));
  assert(visual.worldMatrixPaletteSize()==(paletteMode?6u:0u));
  assert(visual.stockPreviousWorld()==&previousWorld&&visual.stockPreviousPaletteEntries()==4);
  assert(visual.stockInstancingMatrices()==instanceMatrices&&visual.stockInstancingCount()==3);
  assert(device.states[D3DRS_ZWRITEENABLE]==1&&device.states[D3DRS_COLORWRITEENABLE]==15);
  assert(depth.refs==1);
  for(auto& value:fixtureQueries)assert(value->refs==0);
}
int main(int argc,char** argv){assert(argc==2);run(argv[1]);}
'''


class StockSceneOcclusionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('c++')
        if not compiler:
            raise RuntimeError('A C++ compiler is required for the production owner tests')
        cls.directory = tempfile.TemporaryDirectory(prefix='stock-scene-occlusion-owner-')
        cls.addClassCleanup(cls.directory.cleanup)
        path = Path(cls.directory.name)
        source = path / 'probe.cpp'
        cls.source = source
        bodies = []
        for name in ('stock_scene_occlusion.hpp', 'stock_scene_occlusion.cpp'):
            content = (ROOT / 'bigworld' / name).read_text()
            # Supply external SDK declarations; preserve every production body
            # and the production pure history kernel used by the owner.
            content = re.sub(r'^#include "(?!wot/)[^"]+"\n', '', content, flags=re.M)
            content = content.replace('#include <d3d9.h>\n', '').replace('#pragma once\n', '')
            bodies.append(content)
        source.write_text('#include "%s"\n%s\n%s' % (
            ROOT / 'tests/stock_scene_occlusion_test_engine.hpp', '\n'.join(bodies), PROBE))
        cls.binary = path / 'probe'
        command = [compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                   '-I', str(ROOT / 'include'), str(source), '-o', str(cls.binary)]
        if sys.platform != 'win32':
            command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        cls.command = command
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)

    def scenario(self, name):
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, name + '\n' + result.stdout + result.stderr)

    def test_query_receipts_near_plane_and_camera(self):
        for name in ('history', 'near', 'bounded', 'camera', 'frozen', 'issue-camera',
                     'timeout', 'timeout-ready', 'arrival-after-poll', 'invalid-view'):
            with self.subTest(name=name):
                self.scenario(name)

    def test_mapping_and_device_lifecycle(self):
        for name in ('invalidate', 'retire', 'reset', 'reset-reverse', 'depth-change'):
            with self.subTest(name=name):
                self.scenario(name)

    def test_scene_filter_opaque_issue_and_capacity(self):
        for name in ('scene', 'capacity', 'scene-depth-get', 'scene-viewport-get',
                     'off-frustum', 'shared-reuse', 'shared-retire', 'scene-off-frustum'):
            with self.subTest(name=name):
                self.scenario(name)

    def test_failures_are_local_fail_open_and_retryable(self):
        for name in ('query-create', 'query-begin', 'query-end', 'query-get', 'draw',
                     'material-load', 'material-begin', 'material-pass',
                     'state-create', 'state-capture', 'fvf', 'viewport-get', 'depth-get'):
            with self.subTest(name=name):
                self.scenario(name)
                self.scenario('palette:' + name)

    def test_negative_controls_reject_broken_stamp_pool_and_borrowed_state(self):
        original = self.source.read_text()
        mutations = (
            ('issue-frame-stamp', 'entry.history.observe(frameView->frame - 1,',
             'entry.history.observe(slot.view->frame,', 'frozen'),
            ('separate-mapping-pools', 'if (item->scene == scene) return pool;',
             'if (item->scene == scene && false) return pool;', 'shared-reuse'),
            ('lost-palette', 'if (palette) visual.worldMatrixPalette(palette, paletteSize);',
             'if (palette) visual.worldMatrix(world);', 'palette:material-pass'),
            ('reused-pending', 'if (slot.status == pending && slot.query) slot.query->discard();',
             'if (slot.status == pending && false && slot.query) slot.query->discard();', 'shared-retire'),
            ('unpolled-mapping', 'preparedMappings.push_back(mapping); mapping->beginFrame(view); ++item;',
             'preparedMappings.push_back(mapping); ++item;', 'scene-off-frustum'),
        )
        for name, before, after, scenario in mutations:
            with self.subTest(name=name):
                self.assertEqual(original.count(before), 1)
                source = Path(self.directory.name) / (name + '.cpp')
                binary = Path(self.directory.name) / name
                source.write_text(original.replace(before, after, 1))
                command = [str(source) if argument == str(self.source) else
                           str(binary) if argument == str(self.binary) else argument
                           for argument in self.command]
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run([str(binary), scenario], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0,
                                    name + ' escaped its production-body negative control')


class StockSceneOcclusionPatchTests(unittest.TestCase):
    def setUp(self):
        if not ARCHIVE.is_file():
            self.skipTest('Pinned baseline source archive is required for the patch contract')
        self.directory = tempfile.TemporaryDirectory(prefix='stock-scene-occlusion-patch-')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.original = {}
        paths = ('client/world_app.cpp', 'lib/duplo/chunk_attachment.cpp',
                 'lib/compiled_space/py_attachment_entity_embodiment.cpp')
        with tarfile.open(ARCHIVE) as archive:
            for name in paths:
                member = next(item for item in archive.getmembers()
                              if item.name.endswith('/programming/bigworld/' + name))
                self.original[name] = archive.extractfile(member).read()
        self.reset_sources()

    def reset_sources(self):
        for name, content in self.original.items():
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)

    @staticmethod
    def patcher(name):
        path = ROOT / ('bigworld/patch_stock_' + name + '.py')
        spec = importlib.util.spec_from_file_location('scene_occlusion_' + name, path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_pinned_world_seams_are_idempotent_and_reject_drift_atomically(self):
        patcher = self.patcher('scene_occlusion')
        target = self.root / 'client/world_app.cpp'
        original = target.read_text(encoding='utf-8-sig')
        patcher.patch(self.root)
        final = target.read_bytes()
        header = self.root / 'lib/stock_scene_occlusion.hpp'
        self.assertEqual(header.read_bytes(), (ROOT / 'bigworld/stock_scene_occlusion.hpp').read_bytes())
        header_mtime = header.stat().st_mtime_ns
        patcher.patch(self.root)
        self.assertEqual(target.read_bytes(), final)
        self.assertEqual(header.stat().st_mtime_ns, header_mtime)
        source = final.decode()
        self.assertEqual(source.count('filterStockSceneOcclusion(scene, visibleObjects, viewProjectionMat);'), 1)
        self.assertEqual(source.count('issueStockSceneOcclusion(pSpace->scene());'), 1)
        self.assertIn('if (pSpace && ClientChunkSpaceAdapter::getChunkSpace(pSpace) == NULL) issueStockSceneOcclusion(pSpace->scene());', source)
        self.assertLess(source.index('filterStockSceneOcclusion('),
                        source.index('DrawHelpers::drawSceneVisibilitySet('))
        self.assertLess(source.index('issueStockSceneOcclusion('), source.index('rp->endOpaqueDraw();'))
        for anchor in ('#include "world_app.hpp"',
                       '        DrawHelpers::drawSceneVisibilitySet( sceneDrawContext,',
                       '    rp->endOpaqueDraw();'):
            with self.subTest(anchor=anchor):
                target.write_text(original.replace(anchor, '// Removed reviewed owner seam', 1))
                header.write_bytes(b'Existing header must survive rejected source validation.\n')
                before = target.read_bytes()
                before_header = header.read_bytes()
                before_mtime = header.stat().st_mtime_ns
                with self.assertRaises(ValueError):
                    patcher.patch(self.root)
                self.assertEqual(target.read_bytes(), before)
                self.assertEqual(header.read_bytes(), before_header)
                self.assertEqual(header.stat().st_mtime_ns, before_mtime)
        target.write_bytes(final + b'\n#include "world_app.hpp"\n#include "stock_scene_occlusion.hpp"\n')
        before = target.read_bytes()
        with self.assertRaises(ValueError):
            patcher.patch(self.root)
        self.assertEqual(target.read_bytes(), before)
        header.unlink()
        with self.assertRaises(ValueError):
            patcher.patch(self.root)
        self.assertFalse(header.exists())

    def test_edge_preparation_and_queries_compose_in_both_patch_orders(self):
        occlusion = self.patcher('scene_occlusion')
        edge = self.patcher('edge_detection')
        target = self.root / 'client/world_app.cpp'
        for first, second in ((occlusion, edge), (edge, occlusion)):
            with self.subTest(first=first.__name__):
                self.reset_sources()
                first.patch(self.root)
                second.patch(self.root)
                once = {name: (self.root / name).read_bytes() for name in self.original}
                first.patch(self.root)
                second.patch(self.root)
                self.assertEqual({name: (self.root / name).read_bytes() for name in self.original}, once)
                source = target.read_text()
                self.assertEqual(source.count('issueStockSceneOcclusion(pSpace->scene());'), 1)
                self.assertEqual(source.count('prepareStockEdgeDetection()'), 1)
                self.assertLess(source.index('issueStockSceneOcclusion('), source.index('rp->endOpaqueDraw();'))
                self.assertLess(source.index('prepareStockEdgeDetection()'), source.index('rp->endOpaqueDraw();'))


if __name__ == '__main__':
    unittest.main()
