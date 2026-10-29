"""Native current-pose query ownership and exact pinned adapter seam tests."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import sys
import types
from unittest import mock
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''
import collections,gc,imp,sys,types,weakref
import client_query_test as native
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
class Entity(O):
 reads=0
 def __getattribute__(self,key):
  if key in ('position','matrix','filter','isAlive','typeDescriptor'):
   Entity.reads+=1
  return object.__getattribute__(self,key)
material=O(armor=37.,extra=O(name='original'))
component=O(materials={7:material})
def descriptor(**kw):
 result=O(minimum=(-1.,-1.,-1.),maximum=(1.,1.,1.),chassis=component,hull=component,turret=component,gun=component)
 result.__dict__.update(kw);return result
def pose(x=0.,z=0.):return (x,0.,z,0.,0.,0.,0.,0.)
def actor(key,id,z,**kw):
 row=dict(key=key,engine_id=id,pose=pose(z=z));row.update(kw);return row
rows=[actor('bot:1',11,10.),actor('bot:2',12,20.),actor('player:3',13,30.,local=True)]
vehicles={id:Entity(owned=True,typeDescriptor=descriptor(),appearance=O()) for id in (11,12,13)}
records={row['key']:dict(kind='bot',network_id=i+1,engine_id=row['engine_id'],ready=True,spot_visible=True) for i,row in enumerate(rows)}
battle=O(_records=records,_remote_factory=O(_vehicles=vehicles),_runtime=O(bigworld=O(entity=lambda id:vehicles.get(id))))
native.state(battle,rows)
Collision=collections.namedtuple('Collision','dist hitAngleCos matInfo compName')
owner=native.create(battle,Collision)
assert not native.ingest(owner,battle)
Entity.reads=0
# Native state and cached bounds are the only hot vehicle inputs.
assert native.candidates(owner,[],(0.,0.,0.),(0.,0.,100.),[])==[vehicles[11],vehicles[12],vehicles[13]]
assert native.candidates(owner,[11],(0.,0.,0.),(0.,0.,100.),[])==[vehicles[12],vehicles[13]]
assert not native.segment_may_hit(owner,vehicles[11],(5.,0.,0.),(5.,0.,100.))
assert native.segment_may_hit(owner,O(),(0.,0.,0.),(0.,0.,100.)) is None
hit=native.collisions(owner,vehicles[11],(0.,0.,0.),(0.,0.,100.))[0]
assert hit.dist==9. and hit.matInfo is material and hit.compName=='vehicleHull'
assert Entity.reads==0,Entity.reads
assert native.stats(owner)[0]==3
# The same actor pointer advances without reinstallation or spawn-pose reads.
rows[0]['pose']=pose(40.,10.);native.state(battle,rows)
assert not native.segment_may_hit(owner,vehicles[11],(0.,0.,0.),(0.,0.,100.))
rows[0]['pose']=pose(z=10.);rows[0]['visible']=False;native.state(battle,rows)
assert vehicles[11] not in native.candidates(owner,[],None,None,[])
# Hidden dead wrecks are drawn and remain physical/picker obstacles.
rows[0]['alive']=False;native.state(battle,rows)
assert vehicles[11] in native.candidates(owner,[],None,None,[])
rows[0]['alive']=True;rows[0]['visible']=True;native.state(battle,rows)
# Exact stock extras have no vehicle exceptIDs gate; unknown filters accept.
class F(object):
 def segmentMayHitEntity(self,a,b,skip):assert skip==1;return True
extra=O(filter=F());unknown=O(filter=O())
assert native.candidates(owner,[11,12,13],(0.,0.,0.),(0.,0.,100.),[extra,unknown])==[extra,unknown]
def pick(held=None,server=100.):return native.outline(owner,(0.,0.,0.),(0.,0.,1.),100.,1.,held,1,server,.01)
assert pick()['chosen']==11 and pick()['depth']==9.
assert native.stats(owner)[0]==3 and Entity.reads==0
native.world(5.);result=pick(11);assert result['chosen'] is None and result['held_reason']=='is behind scenery'
native.world(None)
rows[0]['alive']=False;native.state(battle,rows)
assert pick()['chosen'] is None and pick()['blocked']==(12,'is behind a wreck')
rows[0]['pose']=pose(40.,10.);native.state(battle,rows)
assert pick()['chosen']==12
assert not native.turrets(owner,{'bot:1':dict(target_bounds=((-1.,-1.,5.),(1.,1.,6.)),settles_at_ms=150.)})
assert pick(server=100.)['chosen']==12
assert pick(server=150.)['blocked']==(12,'is behind a thrown turret')
native.turrets(owner,{})
# Replacement and missing engine ownership never reuse a stale entity.
vehicles[12].owned=False;assert pick()['chosen'] is None
vehicles[12].owned=True
old=records['bot:2'];records['bot:2']=dict(old)
assert not native.ingest(owner,battle)
old['spot_visible']=False;native.flags(owner,old);assert pick()['chosen']==12
records['bot:2']['spot_visible']=False;native.flags(owner,records['bot:2']);assert pick()['chosen'] is None
records['bot:2']['spot_visible']=True;native.flags(owner,records['bot:2']);assert pick()['chosen']==12
# An unloaded BSP remains a broadphase candidate, never a fabricated hit.
vehicles[12].typeDescriptor=descriptor(unloaded=True);rows[1]['descriptor_revision']=1;native.state(battle,rows)
errors=native.ingest(owner,battle);assert len(errors)==1,errors
assert vehicles[12] in native.candidates(owner,[],None,None,[])
assert native.collisions(owner,vehicles[12],(0.,0.,0.),(0.,0.,100.)) is None
assert pick()['chosen'] is None
vehicles[12].typeDescriptor=descriptor();assert not native.ingest(owner,battle);assert pick()['chosen']==12
# A bad vehicle query is isolated; a second actor still resolves.
vehicles[11].typeDescriptor=descriptor(broken=True);rows[0].update(alive=True,pose=pose(z=10.));native.state(battle,rows)
assert not native.ingest(owner,battle);result=pick();assert result['chosen']==12 and len(result['errors'])==1,result
# Round fences apply before ingress, and close is idempotent.
native.reset(battle);assert native.candidates(owner,[],None,None,[]) is None
native.state(battle,rows);assert not native.ingest(owner,battle)
native.close(owner);native.close(owner);assert native.segment_may_hit(owner,vehicles[12],(0.,0.,0.),(0.,0.,100.)) is None
owner2=native.create(battle,Collision);native.reset(battle,1)
assert native.outline(owner2,(0.,0.,0.),(0.,0.,1.),100.,1.,None,1,100.,.01) is None
native.close(owner2)
# Projectile observation precedes Vehicle readiness. Rebinding before camera
# alignment must install real descriptor/BSP resources without inventing pose.
package='gui.mods.offline_lan_0922'
for name in ('gui','gui.mods',package,package+'.entities'):
 module=types.ModuleType(name);module.__path__=[];sys.modules[name]=module
remote=types.ModuleType(package+'.entities.remote_vehicle')
remote._SegmentCollisionResultExt=Collision
sys.modules[remote.__name__]=remote
sys.modules['offline_client_query_native']=native
facade=imp.load_source(package+'.native_client_query',sys.argv[1])
earlyrow=actor('player:7',17,999.,local=True,ready=False,has_current=False)
actual=Entity(owned=True,typeDescriptor=descriptor(),appearance=O())
earlyrecord=dict(kind='player',network_id=7,engine_id=17,ready=False,spot_visible=True)
warnings=[]
early=O(_worker_mode=False,_records={'player:7':earlyrecord},
 _remote_factory=O(_vehicles={17:actual}),_detached_turret_obstacles=None,
 _runtime=O(bigworld=O(entity=lambda id:actual if id==17 else None),compatibility=O()),
 _warn_optional_failure=lambda *args,**kw:warnings.append((args,kw)))
native.state(early,[earlyrow]);assert facade.bind(early)
earlyowner=early._native_client_query
assert native.stats(earlyowner)[0]==0 and native.resource(early,'player:7')==(0,0,None)
# This is the stock ready record transition, before any subsequent snapshot or
# driver/camera pose admission. Weapon refreshOwner uses this exact accessor.
earlyrecord['ready']=True
assert facade.bind(early) and early._native_client_query is earlyowner
assert native.stats(earlyowner)[0]==1 and native.resource(early,'player:7')==(1,0,actual.typeDescriptor)
assert not native.candidates(earlyowner,[],None,None,[])
# The camera/driver then donates the actual matrix pose, never the row's stale
# placeholder. First ammo advancement can already read its descriptor cache.
earlyrow.update(ready=True,has_current=True,pose=pose(40.,10.));native.state(early,[earlyrow])
assert native.resource(early,'player:7')==(1,1,actual.typeDescriptor)
assert native.candidates(earlyowner,[],(40.,0.,0.),(40.,0.,100.),[])==[actual]
assert not native.candidates(earlyowner,[],(0.,0.,0.),(0.,0.,100.),[])
assert facade.bind(early) and native.stats(earlyowner)[0]==1
# Readiness does not excuse a missing native BSP or lost native entity lease.
actual.typeDescriptor=descriptor(unloaded=True);earlyrow['descriptor_revision']=1;native.state(early,[earlyrow])
assert facade.bind(early) and warnings
assert native.resource(early,'player:7')==(0,1,None)
actual.typeDescriptor=descriptor();assert facade.bind(early)
assert native.resource(early,'player:7')==(1,1,actual.typeDescriptor)
actual.owned=False;assert facade.bind(early)
assert native.resource(early,'player:7')==(0,1,None)
facade.retire(early);facade.retire(early)
# #1513 matrixProvider(kind) allocates a new wrapper on every filter getter.
# Its stable owner, not those transient wrappers or matrix values, fences
# geometry shared with both the player drive and projectile resource owners.
class FreshFilter(O):
 reads=0
 @property
 def bodyMatrix(self):
  FreshFilter.reads+=1;return O(owner=self,kind='body')
 @property
 def groundPlacingMatrix(self):
  FreshFilter.reads+=1;return O(owner=self,kind='ground')
def installed_descriptor():
 d=descriptor()
 for name in ('chassis','hull','turret','gun'):
  setattr(d,name,O(materials={7:material},hitTester=O(_ModelHitTester__bspModel=O())))
 return d
f=FreshFilter();assert f.bodyMatrix is not f.bodyMatrix
assert f.groundPlacingMatrix is not f.groundPlacingMatrix
FreshFilter.reads=0
app=O(compoundModel=O(),turretMatrix=O(value=.1),gunMatrix=O(value=.2))
v=Entity(owned=True,typeDescriptor=installed_descriptor(),appearance=app,filter=f,isStarted=True,health=100,_Vehicle__turretDetachmentConfirmed=False)
row=actor('player:19',29,10.,local=True)
steady=O(_records={'player:19':dict(kind='player',network_id=19,engine_id=29,ready=True)},_remote_factory=O(_vehicles={29:v}),_runtime=O(bigworld=O(entity=lambda id:v)))
native.state(steady,[row]);stable=native.create(steady,Collision)
assert not native.ingest(stable,steady)
drive_geometry=native.borrow(steady,'player:19');projectile_geometry=native.borrow(steady,'player:19')
for i in range(1000):
 app.turretMatrix.value=i*.01;app.gunMatrix.value=-i*.03
 # Native accepted motion and provider output change while resource owners do
 # not. The consumers' exact Actor.geometry shared_ptr fence stays satisfied.
 row['pose']=pose(i*.01,10.);native.state(steady,[row])
 assert not native.ingest(stable,steady)
 assert native.same_resource(drive_geometry,steady,'player:19')
 assert native.same_resource(projectile_geometry,steady,'player:19')
assert native.statistics(stable)==dict(cold_installs=1,entries=1,queries=0,bounds_tests=0,bsp_tests=0,closed=False)
assert FreshFilter.reads==0,FreshFilter.reads
def replacement(change):
 global drive_geometry,projectile_geometry
 before=native.statistics(stable)['cold_installs'];change()
 assert not native.ingest(stable,steady)
 assert native.statistics(stable)['cold_installs']==before+1
 assert not native.same_resource(drive_geometry,steady,'player:19')
 assert not native.same_resource(projectile_geometry,steady,'player:19')
 drive_geometry=native.borrow(steady,'player:19');projectile_geometry=native.borrow(steady,'player:19')
 for i in range(5):assert not native.ingest(stable,steady)
 assert native.statistics(stable)['cold_installs']==before+1
# Old comparison owners remain retained until the next ingress, even when the
# stock appearance no longer points to them. This prevents an ABA cache hit.
old_filter=weakref.ref(v.filter);del f;v.filter=FreshFilter()
gc.collect();assert old_filter() is not None
replacement(lambda:None);gc.collect();assert old_filter() is None
old_compound=weakref.ref(app.compoundModel)
app.compoundModel=O();gc.collect();assert old_compound() is not None
replacement(lambda:None);gc.collect();assert old_compound() is None
old_turret=weakref.ref(app.turretMatrix);app.turretMatrix=O(value=0.)
gc.collect();assert old_turret() is not None
replacement(lambda:None);gc.collect();assert old_turret() is None
replacement(lambda:setattr(app,'gunMatrix',O(value=0.)))
replacement(lambda:setattr(v,'typeDescriptor',installed_descriptor()))
replacement(lambda:setattr(v.typeDescriptor.hull.hitTester,'_ModelHitTester__bspModel',O()))
replacement(lambda:setattr(v,'_offline_critical_descriptor',O()))
replacement(lambda:setattr(v,'isStarted',False))
replacement(lambda:setattr(v,'isStarted',True))
def detach():v.health=-13;v._Vehicle__turretDetachmentConfirmed=True
replacement(detach)
assert FreshFilter.reads==0
assert native.statistics(stable)['cold_installs']==11
native.close(stable);assert native.statistics(stable)['entries']==0 and native.statistics(stable)['closed']
print('native client query ownership/current-pose fixture passed')
'''


class NativeClientQueryTest(unittest.TestCase):
    def test_pinned_bindings(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('fixed gameplay source is required')
        spec = importlib.util.spec_from_file_location('query_bindings', ROOT / 'bigworld/stock_native_client_query_bindings.py')
        bindings = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(bindings)
        folder = Path(source) / 'src/res/scripts/client/gui/mods/offline_lan_0922'
        for name, transform in (('battle_runtime.py', bindings.battle), ('compat.py', bindings.compat), ('entities/turret_obstacles.py', bindings.turrets)):
            prepared = transform((folder / name).read_text())
            compile(prepared, name, 'exec')
            if name == 'battle_runtime.py':
                startup = prepared[prepared.index('    def _finish_entity_startup('):prepared.index('    def _bind_local_arcade_camera(')]
                ready = startup.index("            record['ready'] = True")
                query = startup.index('            native_client_query.bind(self)')
                running = startup.index("            self.state = 'running'")
                camera = startup.index('                self._bind_local_arcade_camera()')
                ammo = startup.index('                self._ammo_tick()')
                self.assertLess(ready, query)
                self.assertLess(query, running)
                self.assertLess(running, camera)
                self.assertLess(camera, ammo)
                begin = prepared.index('    def _update_target_outline(')
                end = prepared.index('    def _publish_cursor_focus(', begin)
                self.assertNotIn('for record in self._records', prepared[begin:end])
                self.assertNotIn('_wreck_blocks_target_outline(', prepared[begin:end])

    def test_exact_import_alias_ownership(self):
        native = types.ModuleType('offline_client_query_native')
        with mock.patch.dict(sys.modules, {'offline_client_query_native': native}):
            spec = importlib.util.spec_from_file_location('native_query_facade', ROOT / 'bigworld/stock_native_client_query.py')
            facade = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(facade)
        compatibility = types.SimpleNamespace()
        original, replacement, other = object(), object(), object()
        controls = types.SimpleNamespace(getCollidableEntities=original)
        marker = types.SimpleNamespace(getCollidableEntities=other)
        rotator = types.SimpleNamespace(getCollidableEntities=original)
        unrelated = types.SimpleNamespace(getCollidableEntities=original)
        modules = {'AvatarInputHandler.control_modes': controls,
                   'AvatarInputHandler.gun_marker_ctrl': marker,
                   'VehicleGunRotator': rotator, 'unrelated_module': unrelated}
        with mock.patch.dict(sys.modules, modules):
            facade.install_aliases(compatibility, original, replacement)
            self.assertIs(controls.getCollidableEntities, replacement)
            self.assertIs(rotator.getCollidableEntities, replacement)
            self.assertIs(marker.getCollidableEntities, other)
            self.assertIs(unrelated.getCollidableEntities, original)
            controls.getCollidableEntities = other
            facade.restore_aliases(compatibility)
            self.assertIs(controls.getCollidableEntities, other)
            self.assertIs(rotator.getCollidableEntities, original)
            facade.restore_aliases(compatibility)
            self.assertEqual(compatibility._native_query_aliases, [])

    def test_native_current_pose_and_lifecycle(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('Pinned native geometry source is required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True, env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='client-query-') as directory:
            folder = Path(directory)
            (folder / 'fixture.py').write_text(FIXTURE)
            fork = Path(source)/'native'
            names = ['stock_native_bot_resource_lifecycle', 'stock_native_bot_turret_obstacles', 'stock_native_detached_turret_snapshot',
                     'stock_native_detached_turret_runtime']
            sources = [str(ROOT/'bigworld'/(name+'.cpp')) for name in names]
            sources += [str(fork/'offline_destructible_geometry.cpp')]
            result = subprocess.run(['c++', '-std=c++17', '-DWOT_RESOURCE_LIFECYCLE_HOST_BUILD', '-Wno-register', '-bundle', '-undefined', 'dynamic_lookup', '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld'), '-I'+str(fork), str(ROOT / 'tests/stock_native_client_query_host.cpp')]+sources+['-o', str(folder / 'client_query_test.so')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([py27, '-B', str(folder / 'fixture.py'), str(ROOT / 'bigworld/stock_native_client_query.py')], cwd=str(folder), capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
