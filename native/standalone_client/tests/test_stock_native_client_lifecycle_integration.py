"""Exercise the composed startup methods and real cold facades on Python 2.7."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = r'''
import ast,imp,os,sys,types
base=sys.argv[1];events=[];fail_at=[None]
sys.path.insert(0,sys.argv[3]);import offline_client_lifecycle_native
class Box(object):
 def __init__(self,**kw):self.__dict__.update(kw)
class Handle(Box):
 def __init__(self,kind,battle=None):Box.__init__(self,kind=kind,battle=battle,closed=False)
def module(name,**methods):
 m=types.ModuleType(name);m.__dict__.update(methods);sys.modules[name]=m;return m
def created(kind,battle=None):
 events.append('create:'+kind)
 if fail_at[0]==kind:raise RuntimeError('fixture rejected '+kind+' installation')
 return Handle(kind,battle)
def close(h):
 assert not h.closed,'duplicate native close: '+h.kind
 events.append('close:'+h.kind);h.closed=True
pkg=module('gui');pkg.__path__=[];pkg=module('gui.mods');pkg.__path__=[]
pkg=module('gui.mods.offline_lan_0922');pkg.__path__=[]
entitypkg=module(pkg.__name__+'.entities');entitypkg.__path__=[]
module(entitypkg.__name__+'.remote_vehicle',_SegmentCollisionResultExt=object)
module('offline_client_transport_native')
module('offline_client_weapon_native',create=lambda *a,**kw:Box())

def replica_create():
 h=created('replica');h.state=Box(ready=False,current=False,resources=False,retired=False);return h
def refresh(h,b):
 h.state.ready=b._records['player:4']['ready'];events.append('refresh:replica');return []
def recv(h,s,msg,now):events.append('receive:'+msg['type']);return []
replica=module('offline_client_replica_native',create=replica_create,state=lambda h:h.state,manifest=recv,snapshot=recv,refresh=refresh)
def replica_close(h):h.state.retired=True;close(h)
replica.close=replica_close
module('offline_client_drive_native',create=lambda b:created('drive',b),close=close)
def query_create(b,t):
 assert b._native_client_state is not None
 h=created('query',b);h.state=b._native_client_state;return h
def query_ingest(h,b):
 assert not h.closed and h.state is b._native_client_state
 h.state.resources=bool(h.state.ready and b.model_ready)
 events.append('ingest:query:'+str(h.state.resources));return []
def targeting_prepare(b):
 assert b._native_client_state is not None and not b._native_client_state.retired
 assert b._avatar is not None and b._native_client_query is not None
 assert not b._native_client_query.closed
 b._native_client_targeting=created('targeting',b)
def targeting_retire(b):
 h=getattr(b,'_native_client_targeting',None)
 if h is None:return
 assert b._native_client_state is not None and not b._native_client_state.retired,'targeting released after accepted state'
 assert not b._native_client_query.closed,'targeting released after its query resources'
 close(h);b._native_client_targeting=None
module('offline_client_query_native',create=query_create,ingest=query_ingest,close=close,targeting_prepare=targeting_prepare,targeting_retire=targeting_retire)
module('offline_client_environment_native',create=lambda b:created('environment',b),ingest=lambda h,b:[],close=close,retire_survival=lambda b:None)
def projectile_create(b,now):
 assert b._native_client_drive_owner and not b._native_client_drive_owner.closed
 assert b._native_client_query and not b._native_client_query.closed
 return created('projectile',b)
module('offline_client_projectile_native',now=lambda b:1.,create=projectile_create,observe=lambda *a:True,resources=lambda *a:True,close=close)
def weapon_advance(b,e,now):
 s=b._native_client_state
 assert s.ready and s.current and s.resources,'native weapon descriptor/pose unavailable'
 assert not b._native_client_drive_owner.closed and not b._native_client_query.closed
 if getattr(b,'_native_client_weapon_runtime',None) is None:b._native_client_weapon_runtime=created('weapon',b)
 events.append('advance:weapon');return b._gun_state
def weapon_close(b):close(b._native_client_weapon_runtime)
module('offline_client_weapon_runtime_native',advance=weapon_advance,close=weapon_close)
module('offline_client_critical_native',close=lambda b:None)
pkg.native_bot_runtime=module(pkg.__name__+'.native_bot_runtime',enabled=lambda bots:False)
facades={}
for name,file in [('replica','replica'),('drive','drive_facade'),('query','query'),('environment','environment'),('weapon_runtime','weapon_runtime'),('projectile','projectile'),('critical','critical')]:
 full=pkg.__name__+'.native_client_'+name
 facades[name]=imp.load_source(full,os.path.join(base,'stock_native_client_'+file+'.py'));setattr(pkg,'native_client_'+name,facades[name])
source=ast.parse(open(sys.argv[2]).read());runtime=next(n for n in source.body if isinstance(n,ast.ClassDef) and n.name=='BattleRuntime')
names=('_finish_entity_startup','_observe_projectile_message','_ammo_tick','_advance_local_gun_to')
nodes=[n for n in runtime.body if isinstance(n,ast.FunctionDef) and n.name in names]
# Execute the exact composed native cleanup prefix. Stock map/GUI teardown is
# independently covered; this test fences cross-owner release before it starts.
cleanup=next(n for n in runtime.body if isinstance(n,ast.FunctionDef) and n.name=='_cleanup')
body=[]
for n in cleanup.body:
 if isinstance(n,ast.ImportFrom) or (isinstance(n,ast.Expr) and isinstance(n.value,ast.Call) and isinstance(n.value.func,ast.Attribute) and isinstance(n.value.func.value,ast.Name) and n.value.func.value.id.startswith('native_client_')):body.append(n)
 elif isinstance(n,ast.If) and any(isinstance(item,ast.Import) and any(alias.name=='offline_client_query_native' for alias in item.names) for item in n.body):body.append(n)
 else:break
cleanup.body=body;nodes.append(cleanup)
class Sync(object):
 def __init__(self,*args,**kw):self._now=lambda:1.
 def manifest(self,m):return facades['replica'].receive(self,m,True)
 def snapshot(self,m):return facades['replica'].receive(self,m)
class Bots(object):
 def __init__(self,*args,**kw):
  # Native resources may finish after early observation. Final query.bind must
  # refresh this same query owner before running and the first ammo tick.
  current_battle[0].model_ready=True
 def battle_start(self,msg):return []
 def apply_snapshot(self,msg):pass
current_battle=[None]
env=dict(__name__=pkg.__name__+'.battle_runtime',SnapshotSync=Sync,BotRuntime=Bots,reset_pose_animation_writes=lambda:None,_offline_lifecycle_native=offline_client_lifecycle_native,track_damage=Box(reset_diagnostics=lambda:None),FRAME_SECONDS=.016,AMMO_SECONDS=.1)
exec compile(ast.Module(nodes),'composed-client-startup','exec') in env
class Battle(object):
 def __getattr__(self,name):
  if name.startswith('_') and not name.startswith('_native_'):return lambda *a,**kw:None
  raise AttributeError(name)
 def _clock(self):return 1.
 def _wreck_prewarm_ready_for_startup(self):return True
 def _local_loadout(self,d):return []
 def _local_ammo_layout(self):return []
 def _garage_loadout_snapshot(self):return dict(shell_order=[])
 def _run_optional_feature(self,name,func,args=(),**kw):return func(*args)
 def _reconcile_detached_turret_snapshot(self,msg):return False
 def _bind_local_arcade_camera(self):
  assert self._native_client_state.resources,'camera descriptor resources unavailable'
  self._native_client_state.current=True;events.append('camera:pose')
 def _schedule(self,*args,**kw):events.append('schedule')
 def _server_entity(self,id):return Box(id=id)
 def _fail(self,error):self.failure=error
for name in names+('_cleanup',):setattr(Battle,name,env[name])
def battle(latest=False):
 b=Battle();b.state='loading_entities';b.failure=None;b.model_ready=False
 b._worker_mode=b._replay_mode=False;b._config={};b._disabled_optional_features=set();b._local_descriptor=object();b._records={'player:4':dict(ready=False,engine_id=4)}
 b._runtime=Box(compatibility=Box(set_control_mode_listener=lambda f:None),vehicle_view_state=None)
 b._avatar=Box(guiSessionProvider=None);b._detached_turret_obstacles=None
 b._start_message=dict(type='battle_start',round_id=9,players=[],bots=[]);b._last_snapshot=None;b._projectile_epoch=1
 b.client=Box(player_id=4,last_snapshot=dict(type='snapshot',round_id=9) if latest else None,send_battle_ready=lambda:None)
 b._spawn_planner=Box(bounds=None);b._arena_bounds=None;b._navigation_graph=None;b._combat_diagnostics=None;b._server=Box(vehicle_id=4);b._battle_result=None
 current_battle[0]=b;return b
for latest in (False,True):
 b=battle(latest);b._finish_entity_startup();assert b.failure is None,b.failure
 assert b.state=='running' and b._native_client_state.current
 assert events.count('create:query')==1,events
 assert events.index('create:replica')<events.index('create:drive')<events.index('create:query')<events.index('create:projectile')<events.index('camera:pose')<events.index('advance:weapon'),events
 assert 'ingest:query:False' in events and 'ingest:query:True' in events
 saved=b._native_client_state;b._cleanup();b._cleanup();assert saved.retired
 assert b._native_client_state is None and b._native_client_drive_owner is None and b._native_client_projectile is None
 assert not hasattr(b,'_native_client_query') and not hasattr(b,'_native_client_environment') and not hasattr(b,'_native_client_weapon_runtime')
 assert [e for e in events if e.startswith('close:') and e!='close:targeting']==['close:projectile','close:weapon','close:environment','close:replica','close:drive','close:query'],events
 assert events.index('create:query')<events.index('create:targeting')<events.index('advance:weapon'),events
 assert events.index('close:targeting')<events.index('close:query'),events
 assert events.index('close:targeting')<events.index('close:replica'),events
 # Reentry uses a fresh state and fresh native owners on the same round facade.
 b.state='loading_entities';b.model_ready=False;b._last_snapshot=None;b.failure=None;b._records['player:4']['ready']=False
 events[:]=[];current_battle[0]=b;b._finish_entity_startup();assert b.failure is None,b.failure;assert b._native_client_state is not saved
 b._cleanup();events[:]=[]
# Failure before any native owner is created must not make cleanup create one.
b=battle();b._local_descriptor=None;b._finish_entity_startup();assert b.failure is not None
b._cleanup();b._cleanup();assert not any(e.startswith('create:') for e in events),events

# Every partial cold-install boundary is independently releasable, including
# failures before the projectile capsule is published on the battle object.
for point in ('replica','drive','query','projectile','environment','targeting','weapon'):
 events[:]=[];fail_at[0]=point;b=battle();b._finish_entity_startup();assert b.failure is not None,point
 fail_at[0]=None;b._cleanup();b._cleanup()
 for key in ('_native_client_state','_native_client_drive_owner','_native_client_query','_native_client_projectile','_native_client_environment','_native_client_weapon_runtime','_native_client_targeting'):
  assert getattr(b,key,None) is None,(point,key)
# Sensitivity controls reproduce both real installation-order regressions.
# Omitting the final refresh keeps the early query cache empty at ammo_tick.
actual=facades['query'].bind
def stale_bind(b):
 if getattr(b,'_native_client_query',None) is not None:return True
 return actual(b)
facades['query'].bind=stale_bind;events[:]=[];b=battle();b._finish_entity_startup()
assert b.failure is not None and 'resources' in str(b.failure),b.failure
b._cleanup();facades['query'].bind=actual
# Omitting the early scene/query dependency makes the battle_start observe
# fail before running, exactly where the original generated chain observes it.
actual=facades['projectile'].bind
def scene_less_bind(b,now=None):return projectile_create(b,now)
facades['projectile'].bind=scene_less_bind;events[:]=[];b=battle();b._finish_entity_startup();assert b.failure is not None
b._cleanup();facades['projectile'].bind=actual
print('Composed native startup/late resources/pose/ammo/partial install/double teardown/reentry passed')

'''

@unittest.skipUnless(os.environ.get('WOT_FORK_0101_SOURCE'), 'Pinned gameplay source is required')
class NativeClientLifecycleIntegration(unittest.TestCase):
    def test_composed_startup(self):
        sys.path.insert(0, str(ROOT/'bigworld'))
        import prepare_offline_fork as preparation
        fork=Path(os.environ['WOT_FORK_0101_SOURCE'])
        source=(fork/preparation.NATIVE_MATH_PATH.with_name('battle_runtime.py')).read_text()
        staged=preparation.ADAPTERS[preparation.NATIVE_MATH_PATH.with_name('battle_runtime.py')](source)
        py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        with tempfile.TemporaryDirectory(prefix='native-client-lifecycle-') as directory:
            folder=Path(directory);(folder/'battle.py').write_text(staged);(folder/'fixture.py').write_text(SCRIPT)
            # Routed methods call the real lifecycle ports.
            subprocess.run(['c++','-std=c++17','-O1','-Wno-unused-parameter','-Wno-register','-Wno-missing-field-initializers',
                            '-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),
                            '-I'+str(ROOT/'bigworld'),str(ROOT/'tests/stock_native_client_lifecycle_host.cpp'),
                            '-o',str(folder/'offline_client_lifecycle_native.so')],check=True)
            subprocess.run([py27,'-B',str(folder/'fixture.py'),str(ROOT/'bigworld'),str(folder/'battle.py'),str(folder)],check=True)

if __name__=='__main__':unittest.main()
