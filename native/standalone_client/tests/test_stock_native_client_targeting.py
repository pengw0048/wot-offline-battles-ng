"""Whole native Expert, held outline and lock loops versus the fixed source."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
SOURCE=os.environ.get('WOT_FORK_0101_SOURCE')

FIXTURE=r"""
import ast,collections,math,random,sys,types
import client_targeting_test as native
base=sys.argv[1]+'/src/res/scripts/client/gui/mods/offline_lan_0922/'
for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
 sys.modules[name]=types.ModuleType(name)
constants=types.ModuleType('gui.mods.offline_lan_0922.battle_runtime')
for key,value in dict(EXPERT_DEVICE_DELAY_SECONDS=4.,TARGET_OUTLINE_SECONDS=.05,TARGET_MAX_DISTANCE=710.,TARGET_DESELECTION_FOV_DEGREES=80.,_SHOT_OCCLUSION_EPSILON=.001).items():setattr(constants,key,value)
sys.modules[constants.__name__]=constants
source=open(base+'battle_runtime.py').read();start=source.index('        held_seen = False\n',source.index('    def _update_target_outline('));end=source.index('        # Retail drops the target',start)
source=source[:start]+'''        pick = self._test_pick
        held_seen = pick['held_seen']
        held_reason = pick['held_reason']
        chosen = pick['chosen']
        chosen_depth = pick['depth']
        miss = pick['miss']
        decline = pick['decline']
        blocked = pick['blocked']
'''+source[end:]
tree=ast.parse(source);outer=next(n for n in tree.body if isinstance(n,ast.ClassDef)and n.name=='BattleRuntime');names=set(('_hide_expert_devices','monitor_vehicle_damaged_devices','_expert_extra_index','_tick_expert_target','_record_alive','_update_target_outline','_clear_target_outline','_refresh_native_target_outline','_publish_cursor_focus'));outer.body=[n for n in outer.body if isinstance(n,ast.FunctionDef)and n.name in names];module=ast.Module(body=[outer]);ast.fix_missing_locations(module);ns=dict(math=math,EXPERT_DEVICE_DELAY_SECONDS=4.,TARGET_OUTLINE_SECONDS=.05,TARGET_MAX_DISTANCE=710.,TARGET_DESELECTION_FOV_DEGREES=80.);exec(compile(module,base+'battle_runtime.py','exec'),ns)
compat_source=open(base+'compat.py').read();ct=ast.parse(compat_source);functions=[n for cls in ct.body if isinstance(cls,ast.ClassDef)for n in cls.body if isinstance(n,ast.FunctionDef)and n.name in ('set_target_lock_candidate','_target_lock_holds','validate_target_lock')];compat_class=ast.ClassDef(name='OldCompat',bases=[ast.Name(id='object',ctx=ast.Load())],decorator_list=[],body=functions);cm=ast.Module(body=[compat_class]);ast.fix_missing_locations(cm);exec(compile(cm,base+'compat.py','exec'),ns)
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
class Vector(tuple):
 def scale(self,n):return Vector(x*n for x in self)
 def __add__(self,o):return Vector(a+b for a,b in zip(self,o))
class Highlighter(object):
 HIGHLIGHT_OFF=0
 def __init__(self,vehicle,trap):self._Highlighter__highlightStatus=0;self._Highlighter__vehicle=vehicle;self.trap=trap
 @property
 def enabled(self):
  if self.trap:raise AssertionError('Python highlighter policy getter in hot loop')
  return self._Highlighter__highlightStatus!=self.HIGHLIGHT_OFF
class Appearance(object):
 def __init__(self,h,trap):self._CompoundAppearance__highlighter=h;self.trap=trap
 @property
 def highlighter(self):
  if self.trap:raise AssertionError('Python component property in hot loop')
  return self._CompoundAppearance__highlighter
class Vehicle(object):
 def __init__(self,id,team,log,trap):
  self.id=id;self.team=team;self.log=log;self.trap=trap;self.health=1000;self.isCrewActive=True;self._spot_visible=True;self._offlineNativeRemote=True;self._offlineLANPresentation=False;self.model=object();self.bw_entity=None;self.inWorld=True;self.owned=True;self.appearance=Appearance(Highlighter(self,trap),trap)
  extras=[O(name='engineHealth'),O(name='leftTrackHealth'),O(name='commanderHealth'),O(name='fire'),O(name='gunHealth')];self.typeDescriptor=O(extras=extras,extrasDict=dict((v.name,v)for v in extras))
 def isAlive(self):
  if self.trap:raise AssertionError('Python isAlive in native lock/expert hot loop')
  return self.isCrewActive and self.health>0
 def drawEdge(self,force):
  h=self.appearance._CompoundAppearance__highlighter
  if h._Highlighter__vehicle is None:return
  if h._Highlighter__highlightStatus:self.log.append(('delete',self.id))
  h._Highlighter__highlightStatus=1;self.log.append(('add',self.id,force))
 def removeEdge(self,force):
  h=self.appearance._CompoundAppearance__highlighter
  if h._Highlighter__vehicle is None:return
  if h._Highlighter__highlightStatus:self.log.append(('delete',self.id))
  h._Highlighter__highlightStatus=0
def bad(*args,**kwargs):raise AssertionError('Python targeting business callback in native hot loop')
def make(trap):
 log=[];b=ns['BattleRuntime']();b._generation=1;b.state='running';b._has_expert=True;b._test_skill=1;b._expert_target_id=0;b._expert_target_due=0.;b._expert_target_signature=None;b._outlined_engine_id=None;b._outlined_entity=b._outlined_vehicle=b._outlined_model=None;b._next_outline_time=0.;b._outline_blocked=False;b._projectile_server_time_ms=100;b._projectile_server_local_time=0.;b.client=O(team=1)
 vehicles={2:Vehicle(2,2,log,trap),3:Vehicle(3,1,log,trap)};b._remote_factory=O(get=lambda id:vehicles.get(id));b._server_entity=lambda id:vehicles.get(id);b._records=collections.OrderedDict();b._test_ray=((0.,0.,0.),(0.,0.,1.));b._mouse_targeting_ray=lambda:(Vector((0.,0.,0.)),Vector((0.,0.,1.)));b._clock=native.time;b._local_skill_count=lambda name:b._test_skill;b._report_skill_detail=lambda *args:None;b._report_edge=lambda *args:None;b._report_target_outline=lambda *args:None
 feedback=O(showVehicleDamagedDevices=lambda id,a,c:log.append(('show',id,tuple(a),tuple(c))),hideVehicleDamagedDevices=lambda id:log.append(('hide',id)),setTargetInFocus=lambda id,focused:log.append(('focus',id,focused)))
 b._avatar=O(spaceID=1,target=None,guiSessionProvider=O(shared=O(feedback=feedback)),_PlayerAvatar__autoAimVehID=0)
 compatibility=ns['OldCompat']();compatibility._battle_active=True;compatibility._target_lock_candidate=None;compatibility._original_avatar_getattribute=object.__getattribute__;compatibility._original_avatar_auto_aim=lambda avatar,target:log.append(('unlock',avatar._PlayerAvatar__autoAimVehID));bigworld=O(time=native.time,entity=lambda id:vehicles.get(id),wgAddEdgeDetectEntity=lambda entity,color,zero,flag:log.append(('synthetic_add',color,flag)),wgDelEdgeDetectEntity=lambda entity:log.append(('synthetic_delete',)))
 compatibility._runtime=O(bigworld=bigworld);b._runtime=O(compatibility=compatibility,bigworld=bigworld);b._native_client_query=O(vehicles=vehicles,pick=None)
 if trap:
  for name in ('_local_skill_count','_tick_expert_target','_clear_target_outline','_refresh_native_target_outline','_mouse_targeting_ray','monitor_vehicle_damaged_devices'):setattr(b,name,bad)
 return b,vehicles,log
old,ov,oldlog=make(False);new,nv,newlog=make(True);rows=[dict(key='bot:2',engine_id=2,team=2,alive=True,critical={}),dict(key='player:3',engine_id=3,team=1,alive=True,critical={})]
def update():
 old._records.clear()
 for row in rows:
  old._records[row['key']]=dict(engine_id=row['engine_id'],local=row.get('local',False),tombstone=row.get('tombstone',False),spot_visible=row.get('spot_visible',True),state=row)
 native.install(new,rows)
update();native.prepare(new);rng=random.Random(1513);now=0.
for frame in range(1100):
 now+=rng.choice([.005,.025,.05,.11,.3]);native.set_time(now)
 chosen=rng.choice([None,None,2,2,3]);pick=dict(available=True,held_seen=True,held_reason=None,chosen=chosen,depth=30. if chosen else None,miss=None,decline=None,blocked=None);old._test_pick=pick;new._native_client_query.pick=pick
 if frame%19==0:
  for id in (2,3):ov[id].model=object();nv[id].model=object();ov[id].appearance._CompoundAppearance__highlighter._Highlighter__highlightStatus=0;nv[id].appearance._CompoundAppearance__highlighter._Highlighter__highlightStatus=0
 if frame%53==0:
  for id in (2,3):ov[id].appearance._CompoundAppearance__highlighter._Highlighter__vehicle=None;nv[id].appearance._CompoundAppearance__highlighter._Highlighter__vehicle=None
 if frame%53==1:
  for id in (2,3):ov[id].appearance._CompoundAppearance__highlighter._Highlighter__vehicle=ov[id];nv[id].appearance._CompoundAppearance__highlighter._Highlighter__vehicle=nv[id]
 rows[0]['critical']=dict(devices=[dict(name='engineHealth',state=rng.choice(['normal','critical','destroyed'])),dict(name='leftTrackHealth',state='critical'),dict(name='missing-device',state='destroyed')],destroyed=['engineHealth']if frame%3==0 else [],crew_ko=['commander']if frame%7==0 else [],fire=frame%11==0)
 if frame%83==0:rows[0]['spot_visible']=False
 elif frame%83==2:rows[0]['spot_visible']=True
 if frame%97==0:rows[0]['tombstone']=True
 elif frame%97==2:rows[0]['tombstone']=False
 old._test_skill=new._test_skill=0 if frame%61==0 else 1;update()
 old._tick_expert_target(now);native.tick_expert(new,now)
 old._update_target_outline(now);native.outline(new,now)
 assert oldlog==newlog,(frame,oldlog[-12:],newlog[-12:])
 target,due,outlined=native.status(new);assert(target,due,outlined)==(old._expert_target_id,old._expert_target_due,old._outlined_engine_id),(frame,(target,due,outlined),(old._expert_target_id,old._expert_target_due,old._outlined_engine_id))
 assert new._expert_target_signature==old._expert_target_signature,(frame,new._expert_target_signature,old._expert_target_signature)
# Hold one actual target beyond the four-second deadline, then vary every
# canonical module/crew phase and descriptor extras ownership independently.
for b,vehicles in ((old,ov),(new,nv)):
 b._test_skill=1
 for id in (2,3):vehicles[id].model=object();vehicles[id].appearance._CompoundAppearance__highlighter._Highlighter__vehicle=vehicles[id]
rows[0]['alive']=True;rows[0]['spot_visible']=True;rows[0]['tombstone']=False
for frame in range(360):
 now+=.1;native.set_time(now);pick=dict(available=True,held_seen=True,held_reason=None,chosen=2,depth=30.,miss=None,decline=None,blocked=None);old._test_pick=pick;new._native_client_query.pick=pick
 phase=('normal','critical','destroyed')[(frame//7)%3]
 rows[0]['critical']=dict(devices=[dict(name='engineHealth',state=phase),dict(name='leftTrackHealth',state='critical'),dict(name='visionDeviceHealth',state='critical')],destroyed=['engineHealth']if frame%13==0 else [],crew_ko=['commanderHealth']if frame%17==0 else [],fire=frame%19==0)
 if frame in (150,210):
  for vehicles in (ov,nv):
   descriptor=vehicles[2].typeDescriptor
   if frame==150:descriptor.extrasDict['visionDeviceHealth']=descriptor.extras[-1]
   else:descriptor.extras=dict((index+7,value)for index,value in enumerate(descriptor.extras))
  rows[0]['descriptor_revision']=rows[0].get('descriptor_revision',0)+1
 update();old._tick_expert_target(now);native.tick_expert(new,now);old._update_target_outline(now);native.outline(new,now)
 assert oldlog==newlog,('stable',frame,oldlog[-8:],newlog[-8:])
 assert new._expert_target_signature==old._expert_target_signature,('stable',frame,new._expert_target_signature,old._expert_target_signature)
assert sum(1 for event in newlog if event[0]=='show')>20
# Lock validation consumes typed canonical flags and exact visual ownership.
for mask in range(128):
 values=tuple(bool(mask&(1<<i))for i in range(7));expected=values[0]and values[1]and values[2]and(not values[3]or(values[4]and values[5]and values[6]));assert native.lock(*values)is bool(expected)
for i in range(100):
 rows[0]['alive']=i%3!=0;rows[0]['spot_visible']=i%5!=0;rows[0]['tombstone']=False;update()
 for b,vehicles in ((old,ov),(new,nv)):
  v=vehicles[2];v.health=1000 if rows[0]['alive']else 0;v._spot_visible=rows[0]['spot_visible'];v._offlineLANPresentation=bool(i%2);v.bw_entity=object()if i%7 else None;v.model=object()if i%11 else None;v.inWorld=bool(i%13);b._avatar._PlayerAvatar__autoAimVehID=2
 old._runtime.compatibility.validate_target_lock(old._avatar);native.validate_lock(new._runtime.compatibility,new._avatar)
 assert oldlog==newlog,(i,oldlog[-3:],newlog[-3:])
for invalid in ('unreadable-id',2**100,object(),[2]):
 old._avatar._PlayerAvatar__autoAimVehID=invalid;new._avatar._PlayerAvatar__autoAimVehID=invalid
 old._runtime.compatibility.validate_target_lock(old._avatar);native.validate_lock(new._runtime.compatibility,new._avatar)
 assert oldlog==newlog,('invalid-lock',oldlog[-1:],newlog[-1:])
for mailbox in (None,{},[],object(),float('inf'),'bad',2**1000,-2**1000,'2',0,'-1'):
 assert old.monitor_vehicle_damaged_devices(mailbox)==native.monitor_expert(new,mailbox)
 assert oldlog==newlog,('mailbox',mailbox,oldlog[-3:],newlog[-3:])
# Reentry can retire the owner from a real GUI edge. No later model access is
# permitted, and a second cleanup must not construct a fresh owner.
native.retire(new);native.retire(new);assert getattr(new,'_native_client_targeting',None)is None
assert native.clear(new)is True and getattr(new,'_native_client_targeting',None)is None
reentered,rv,rlog=make(True);native.install(reentered,rows);native.prepare(reentered);native.set_time(now+1.);reentered._native_client_query.pick=dict(available=True,held_seen=False,held_reason=None,chosen=2,depth=30.)
feedback=reentered._avatar.guiSessionProvider.shared.feedback
feedback.setTargetInFocus=lambda id,focused:native.retire(reentered)if focused else None
native.outline(reentered,now+1.);assert getattr(reentered,'_native_client_targeting',None)is None
assert [event[0]for event in rlog]==['add','delete'],rlog
native.clear(reentered);native.retire(reentered)
# A refresh callback may clear the outline without retiring the same owner.
# The caller must not dereference or re-monitor the cleared target afterwards.
refreshed,fv,flog=make(True);native.install(refreshed,rows);native.prepare(refreshed)
refreshed._native_client_query.pick=dict(available=True,held_seen=True,held_reason=None,chosen=2,depth=30.)
native.outline(refreshed,now+2.)
fv[2].appearance._CompoundAppearance__highlighter._Highlighter__highlightStatus=0
fv[2].drawEdge=lambda force:native.clear(refreshed)
native.outline(refreshed,now+2.1)
assert native.status(refreshed)[2]is None and native.status(refreshed)[0]==0
native.retire(refreshed)
# Exact #1513 constants can live on old-style instances and classes.
class OldHighlighter:
 HIGHLIGHT_OFF=0
 def __init__(self,v):self._Highlighter__highlightStatus=0;self._Highlighter__vehicle=v
legacy,lv,llog=make(True);lv[2].appearance._CompoundAppearance__highlighter=OldHighlighter(lv[2])
native.install(legacy,rows);native.prepare(legacy);legacy._native_client_query.pick=dict(available=True,held_seen=False,held_reason=None,chosen=2,depth=30.)
native.outline(legacy,now+3.);assert native.status(legacy)[2]==2
native.retire(legacy);assert [event[0]for event in llog]==['add','focus','focus','delete','hide'],llog
native.disable_expert(legacy);native.disable_outline(legacy)
assert getattr(legacy,'_native_client_targeting',None)is None
assert not native.monitor_expert(legacy,object())
assert native.monitor_expert(legacy,0)and getattr(legacy,'_native_client_targeting',None)is None
print('1460 whole Expert/outline frames, 128 lock states, 104 lock transitions, old-style ABI and GUI reentry/retirement passed')
"""

@unittest.skipUnless(SOURCE,'fixed gameplay source is required')
class NativeClientTargeting(unittest.TestCase):
 def test_exact_stock_targeting_contract(self):
  client=os.environ.get('WOT_0922_CLIENT')
  if not client:self.skipTest('Exact #1513 client is required')
  archive=Path(client)/'res/packages/scripts.pkg';self.assertTrue(archive.is_file())
  py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
  audit=r'''
import marshal,sys,types,zipfile
def find(code,name):
 if code.co_name==name:return code
 for child in code.co_consts:
  if isinstance(child,types.CodeType):
   selected=find(child,name)
   if selected is not None:return selected
with zipfile.ZipFile(sys.argv[1])as archive:
 def load(path):return marshal.loads(archive.read(path)[8:])
 vehicle=load('scripts/client/Vehicle.pyc')
 assert set(('isCrewActive','health')).issubset(find(vehicle,'isAlive').co_names)
 assert 'drawEdge' in [value.co_name for value in find(vehicle,'Vehicle').co_consts if isinstance(value,types.CodeType)]
 for method in ('drawEdge','removeEdge'):
  assert set(('appearance','highlighter','highlight')).issubset(find(vehicle,method).co_names)
 highlighter=load('scripts/client/vehicle_systems/components/highlighter.pyc')
 assert set(('_Highlighter__highlightStatus','HIGHLIGHT_OFF')).issubset(find(highlighter,'enabled').co_names)
 assert '_Highlighter__vehicle' in find(highlighter,'highlight').co_names
 appearance=load('scripts/client/vehicle_systems/CompoundAppearance.pyc')
 assert set(('highlighter','ComponentDescriptor')).issubset(find(appearance,'CompoundAppearance').co_names)
 properties=load('scripts/client_common/svarog_script/auto_properties.pyc')
 assert 'fieldName' in find(properties,'__get__').co_names
 assert '_%s__%s' in find(properties,'__new__').co_consts
print('Exact #1513 target alive, stock outline, highlighter and component backing-field contracts passed')
'''
  result=subprocess.run([py27,'-B','-c',audit,str(archive)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
 def test_binding_seams(self):
  import ast
  sys.path.insert(0,str(ROOT/'bigworld'))
  import prepare_offline_fork as preparation
  path=preparation.NATIVE_MATH_PATH.with_name('battle_runtime.py')
  source=preparation.ADAPTERS[path]((Path(SOURCE)/path).read_text())
  tree=ast.parse(source);runtime=next(n for n in tree.body if isinstance(n,ast.ClassDef)and n.name=='BattleRuntime')
  expected={'_tick_expert_target':'tick_expert','monitor_vehicle_damaged_devices':'monitor_expert','_disable_expert_presentation':'disable_expert','_update_target_outline':'target_outline','_clear_target_outline':'clear_outline','_disable_target_outline_presentation':'disable_outline'}
  for method,entry in expected.items():
   node=next(n for n in runtime.body if isinstance(n,ast.FunctionDef)and n.name==method)
   prefix=node.body[0];self.assertIsInstance(prefix,ast.If)
   self.assertEqual(ast.unparse(prefix.test),'not self._worker_mode')
   self.assertEqual(prefix.body[1].value.func.attr,entry)
   self.assertIsInstance(prefix.body[1],ast.Return)
   self.assertGreater(len(node.body),1,'The original worker implementation must remain')
  cleanup=next(n for n in runtime.body if isinstance(n,ast.FunctionDef)and n.name=='_cleanup')
  self.assertEqual(cleanup.body[0].body[1].value.func.attr,'targeting_retire')
  self.assertEqual(sum(1 for n in ast.walk(cleanup)if isinstance(n,ast.Attribute)and n.attr=='targeting_retire'),1)
  compat=preparation.ADAPTERS[preparation.NATIVE_MATH_PATH.with_name('compat.py')]((Path(SOURCE)/preparation.NATIVE_MATH_PATH.with_name('compat.py')).read_text())
  lock=next(n for n in ast.walk(ast.parse(compat))if isinstance(n,ast.FunctionDef)and n.name=='validate_target_lock')
  self.assertIsInstance(lock.body[1],ast.Return);self.assertEqual(lock.body[1].value.func.attr,'validate_target_lock')
  module=(ROOT/'bigworld/stock_native_client_query_module.cpp').read_text()
  for entry in tuple(expected.values())+('targeting_prepare','targeting_retire','validate_target_lock'):
   self.assertEqual(module.count('{"'+entry+'",'),1)
 def test_whole_targeting_owners(self):
  py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip();prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
  with tempfile.TemporaryDirectory(prefix='native-client-targeting-')as temporary:
   folder=Path(temporary);command=['c++','-std=c++17','-bundle','-undefined','dynamic_lookup','-ffp-contract=off','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-register','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(ROOT/'bigworld'),'-I'+str(Path(SOURCE)/'native'),str(ROOT/'tests/stock_native_client_targeting_host.cpp'),'-o',str(folder/'client_targeting_test.so')]
   compiled=subprocess.run(command,capture_output=True,text=True);self.assertEqual(compiled.returncode,0,compiled.stderr);(folder/'fixture.py').write_text(FIXTURE);result=subprocess.run([py27,'-B',str(folder/'fixture.py'),SOURCE],cwd=folder,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
