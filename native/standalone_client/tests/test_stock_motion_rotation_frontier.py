"""Actual production WorldFrontier rotation adapter, rotation-only macro."""
import os
import shutil
from pathlib import Path
import subprocess
import tempfile
import unittest

import test_stock_offline_query_bridge as bridge


class RotationFrontierTests(unittest.TestCase):
    def test_rotation_only_frontier(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('Fixed v0.10.1 source input is required')
        fork = Path(source)
        with tempfile.TemporaryDirectory(prefix='rotation-frontier-') as folder:
            path = Path(folder)
            binary = bridge.build_bridge(self, path, fork, rotation_query=True)
            consumer = bridge.consumer_source(fork).split(bridge.CONSUMER_CASES)[0]
            # Reuse only fixture setup/functions, never prior native-enabled
            # assertions. This binary deliberately has no ordinary-query macro.
            consumer += bridge.CONSUMER_CASES[:bridge.CONSUMER_CASES.index('reset(); surface(4,3.)')]
            consumer += CASES + prepared_consumer_cases(fork)
            (path/'consumer.py').write_text(consumer)
            if os.environ.get('WOT_ROTATION_FRONTIER_EXPORT'):
                exported=Path(os.environ['WOT_ROTATION_FRONTIER_EXPORT'])
                exported.mkdir(parents=True,exist_ok=True)
                shutil.copy2(binary,exported/'host')
                shutil.copy2(path/'consumer.py',exported/'consumer.py')
            completed = subprocess.run([str(binary), str(path/'consumer.py')],
                text=True, capture_output=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
            self.assertEqual(completed.returncode, 0, completed.stdout+completed.stderr)
            print(completed.stdout)


CASES = r'''
import offline_rotation_world_native as rotation
reports=[]
native_math=module(PREFIX+'.native_math')
native_math.report_world_failure=reports.append
# The C entry is imported from the real original numerical module initializer.
assert callable(rotation.run)
HOT=set(('_ray','_segment','_skin','_filter','_alias','_can_recast','_skipped','_scale','_normalise',
    'reject_broken_skin','_compiled_motion_skin_1513','_owned_component_skin_1513','_anonymous_original_surface_1513'))
def values(yaw=0.,end=.04,translation=(0.,0.)):
    return (0.,0.,0.,-1.,0.,-2.,1.,2.,2.,yaw,end,0.,0.,0.,translation[0],translation[1],
        math.pi/36.,1.28,16,1,1,0)

def rotation_case(generic=False,wall=False,broken=False,fail=None,yaw=0.,end=.04,translation=(0.,0.),angle=0.):
    reset();reports[:]=[];Host.visits()
    if wall:
        triangle=((-math.sin(angle),-1.,-math.cos(angle)),(0.,1.,0.),(math.sin(angle),-1.,math.cos(angle)))
        Host.addSurface(1,(71 if broken else 4)<<8,.00001,triangle,None,(10,1))
        if broken:
            authority._state['chunks'][10]['keys'].add((1,71))
            Host.addSurface(1,4<<8,.25,triangle,None,(10,2))
    q,ctx=query(True);del ctx
    keep=sensor._live_broken_collision_filter_1513([(10,1),(10,2)])
    made=[];effects=[];hot=[];traces=[];previous=[0]
    def make_world(row,bbox):
        q._live();made.append(tuple(row));trace={};traces.append(trace)
        if generic:q._native_query_binding=None
        old=not row[18]
        if old:previous[0]+=1
        child=previous[0] if old else 0
        def dispatch(op,rows):
            q._live()
            if op==1:
                if fail=='filter' or (child and (fail=='lazy_both' or
                        fail=='lazy_first' and child==1 or fail=='lazy_second' and child==2)):
                    raise ValueError('filter failure')
                return world._trace_collision_filter(keep,trace)
            if op==7:return sensor.ground_collision_filter(rows[0][0][0],rows[0][0][2])
            if op==5:
                effects.append(('resolve',plain(rows[0][2])))
                if fail=='resolve':raise RuntimeError('resolve failure')
                return (0,)
            if op==6:
                effects.append(('trace',plain(rows[0][2])))
                hit=rows[0][2];trace.update(hit=tuple(hit[0]),normal=tuple(hit[1]),reason='hard',
                    ray_start=tuple(rows[0][0]),ray_end=tuple(rows[0][1]))
                if child==1 and fail=='lazy_generation':q.owner._generation+=1
                if child==1 and fail=='lazy_binding':q._native_query_binding=None
                if child==1 and fail=='lazy_trace_failure':raise RuntimeError('lazy trace failure')
                if fail=='trace':raise RuntimeError('trace failure')
                return None
            raise AssertionError(op)
        return dispatch,trace
    def profile(frame,event,arg):
        if event=='call' and frame.f_globals.get('__name__','').startswith(PREFIX) and frame.f_code.co_name in HOT:
            hot.append(frame.f_code.co_name)
    sys.setprofile(profile)
    try:result=rotation.run(values(yaw,end,translation),q.capabilities,(4,.001,1.28),make_world)
    finally:sys.setprofile(None)
    assert result is not None
    if not generic and fail not in ('lazy_binding','lazy_generation'):assert not hot,hot
    if generic and made and fail!='filter':assert hot,(made,effects)
    assert bool(result[1])==bool(reports),(result,reports)
    # Child frames must not replace an outer retained materialized hit.
    if effects:
        for kind,hit in effects:assert hit[2]==4,(effects,made)
    return result,made,effects,len(hot),traces,len(Host.visits())

for scenario in ({}, {'wall':True}, {'wall':True,'broken':True},
        {'wall':True,'fail':'filter'}, {'wall':True,'fail':'resolve'}, {'wall':True,'fail':'trace'},
        {'translation':(.2,.1),'end':.2}, {'wall':True,'yaw':1.2,'end':1.4},
        {'wall':True,'yaw':1.2,'end':1.4,'angle':.04},
        {'wall':True,'yaw':1.2,'end':1.4,'angle':.04,'fail':'lazy_first'},
        {'wall':True,'yaw':1.2,'end':1.4,'angle':.04,'fail':'lazy_second'},
        {'wall':True,'yaw':1.2,'end':1.4,'angle':.04,'fail':'lazy_both'},
        {'wall':True,'yaw':1.2,'end':1.4,'angle':.04,'fail':'lazy_trace_failure'}):
    actual=rotation_case(**scenario)
    expected=rotation_case(generic=True,**scenario)
    assert actual[:3]==expected[:3],(scenario,actual,expected)
    if scenario.get('angle') and not scenario.get('fail'):
        assert len(actual[1])==3 and actual[5]>9,(actual,expected)
    if scenario.get('fail','').startswith('lazy_'):assert len(actual[1])==3,(scenario,actual)
    print('rotation_case',scenario,'result',actual[0][:3],'worlds',len(actual[1]),'hot native/generic',actual[3],expected[3])
# A changed owner or binding after a nested world cannot consume the old
# parent hit or replay committed filter observations. This is a local failure.
for failure in ('lazy_generation','lazy_binding'):
    changed=rotation_case(wall=True,yaw=1.2,end=1.4,angle=.04,fail=failure)
    assert changed[0][0] is False and changed[0][1]>0 and changed[0][2],changed
    assert not changed[4][0].get('hit'),changed
    assert len([event for event in changed[2] if event[0]=='resolve'])<=2,changed
# An unsupported complete operation returns None before the factory runs.
reset();q,ctx=query();del ctx;q._native_query_binding=None
called=[]
assert rotation.run(values(),q.capabilities,(4,.001,1.28),lambda *a:called.append(a)) is None
assert called==[]
# The same binary keeps ordinary translation on its generic WorldFrontier.
reset();q,ctx=query();del ctx;seen=[]
def ordinary(op,rows):
    if op==1:return None
    if op==7:return None
    if op==5:return (0,)
    if op==6:return None
    raise AssertionError(op)
def observe(frame,event,arg):
    if event=='call' and frame.f_code.co_name in HOT:seen.append(frame.f_code.co_name)
sys.setprofile(observe)
try:status=backend.world_run_native((0.,0.,0.,0.,5.,.04,0.,0.,0.,-1.,1.,2.,2.,0.,2.,0,0,0,0),ordinary,q.capabilities,(4,.001,1.28))
finally:sys.setprofile(None)
q.raise_failure();assert status==0 and seen,(status,seen)
print('rotation-only macro preserves generic ordinary world:',len(seen),'hot callbacks')
print('rotation production frontier parity passed')
'''

def prepared_consumer_cases(fork):
    import test_stock_motion_rotation_batch as rotation_laws
    scripts=fork/'src/res/scripts/client/gui/mods/offline_lan_0922'
    big=bridge.ROOT/'bigworld'
    import importlib.util
    spec=importlib.util.spec_from_file_location('rotation_bindings',big/'stock_motion_rotation_bindings.py')
    binding=importlib.util.module_from_spec(spec);spec.loader.exec_module(binding)
    original=(scripts/'battle_runtime.py').read_text()
    adapted=binding.runtime(original)
    suffix=adapted[adapted.index('\n# Prepared-copy identities for the independent rotation entry.'):]
    world_names={'_vehicle_motion_bounds','_vehicle_motion_heights','_record_hard_contact',
        '_hull_pose_y','_drivable_surface','_collide_horizontal','check_horizontal_collision','_check_horizontal_collision'}
    sensor_names={'_descriptor_value','_vehicle_hull_bbox','_vehicle_body_bbox','native_replacement_bsp_active'}
    return '\n'.join((
        'consumer=module(PREFIX+".native_rotation_world")',
        'exec(%r,consumer.__dict__)' % (big/'stock_motion_rotation_consumer.py').read_text(),
        'exec(%r,sensor.__dict__)' % rotation_laws.selected((scripts/'destructibles_sensor.py').read_text(),sensor_names),
        'world._UNPREPARED_COLLISION_FILTER=object();world._MAX_DRIVABLE_GRADIENT=1.28',
        'exec(%r,world.__dict__)' % rotation_laws.selected((scripts/'world_collision.py').read_text(),world_names),
        'world._descriptor_value=sensor._descriptor_value;world._vehicle_hull_bbox=sensor._vehicle_hull_bbox',
        'native_world=module(PREFIX+".native_world")',
        'exec(%r,native_world.__dict__)' % (scripts/'native_world.py').read_text(),
        'exec(%r,native_math.__dict__)' % rotation_laws.selected((scripts/'native_math.py').read_text(),{'world_run','world_available','call'}),
        'physics=module(PREFIX+".vehicle_physics");physics.math=math',
        'exec(%r,physics.__dict__)' % rotation_laws.selected((scripts/'vehicle_physics.py').read_text(),{'track_pivot_position'}),
        'battle=module(PREFIX+".rotation_fixture");battle.__dict__.update(math=math,native_math=native_math,world_collision=world,vehicle_physics=physics)',
        'exec(%r,battle.__dict__)' % rotation_laws.selected(original,rotation_laws.FUNCTIONS|{'_vector'}),
        'baseline=battle._native_world_rotation_is_clear',
        'exec(%r,battle.__dict__)' % rotation_laws.selected(adapted,{'_native_world_rotation_is_clear'}),
        'class BattleRuntime(object):\n    _vector=battle._vector\n    baseline=baseline\n    candidate=battle._native_world_rotation_is_clear',
        'battle.BattleRuntime=BattleRuntime',
        'exec(%r,battle.__dict__)' % suffix,
    )) + CONSUMER_PARITY


CONSUMER_PARITY = r'''
native_math._load=lambda:backend
native_math.sys=sys
native_math._calls={'world_run':1,'world_run_native':0}
native_math._fallbacks=0
native_math._report_failure=lambda error:None
world._WORLD_SOFT_RECAST_BUDGET=4;world._GROUND_HIT_EPSILON=.001
world._MAX_DRIVABLE_GRADIENT=1.28;world._UNPREPARED_COLLISION_FILTER=object()
world.ground_collision_filter=sensor.ground_collision_filter
world.collide_motion_segment=sensor.collide_motion_segment
battle.DESTRUCTIBLE_POSE_MAX_ANGLE_STEP=math.pi/36.
battle.DESTRUCTIBLE_POSE_MAX_SWEEP_STEPS=16
consumer._NATIVE_ROTATION_ENABLED=True

def consumer_case(enabled=True,wall=False,active=True,missing=False,end=.04,position=(0.,0.,0.),
        pitch=0.,roll=0.,translation=(0.,0.),pivot=0.,include_static=True,custom=False):
    reset();reports[:]=[];events=[]
    sensor.g_offh_destr_native_replacement_bsp_active=active
    if wall:
        triangle=((0.,-1.,-1.),(0.,1.,0.),(0.,-1.,1.))
        Host.addSurface(1,4<<8,.00001,triangle,None,(10,1))
    owner=BattleRuntime();owner._avatar=Namespace(spaceID=1)
    owner._runtime=Namespace(math=Math,bigworld=BigWorld);owner._generation=1;owner._bots=Namespace(round_id=1)
    owner._destructibles=sensor;owner._local_pitch=pitch;owner._local_roll=roll
    tester=Namespace(bbox=((-1.,0.,-2.),(1.,2.,2.)))
    td=None if missing else {'physics':{'test':1},'hull':{'hitTester':tester},
        'chassis':{'hitTester':tester,'hullPosition':(0.,0.,0.)}}
    if custom:
        def bbox(unused):events.append('bbox');return tester.bbox
        owner._destructibles=Namespace(_vehicle_body_bbox=bbox,native_replacement_bsp_active=lambda:True)
    keep=sensor._live_broken_collision_filter_1513([(10,1)])
    world.prepare_horizontal_collision_filter=lambda a,b:keep
    def resolve(space,a,b,hit,yaw,speed,descriptor,crush,allow,kinetic,commit,collision_filter):
        assert not allow and kinetic is None and commit is False
        events.append(('resolve',plain(hit),descriptor['physics'],descriptor['hull']['hitTester'].bbox[:2]))
        return False
    world._destroy_and_recast=resolve
    output={};before=consumer.snapshot()
    method=owner.candidate if enabled else owner.baseline
    clear=method(position,0.,end,td,pitch=pitch,roll=roll,record_local=True,pivot_offset=pivot,
        contact_trace=output,include_static=include_static,translation=translation)
    return (clear,output,getattr(owner,'_local_world_collision_trace',None),
        getattr(owner,'_local_motion_status',None),events),consumer.snapshot()['admitted']-before['admitted']
for scenario in ({},{'wall':True},{'active':False,'include_static':False},
        {'active':True,'include_static':False,'wall':True},{'missing':True}, {'end':0.},
        {'position':(.123456789,0.,-.234567891),'pitch':.2,'roll':-.15,'translation':(.2,-.1),'end':.2,'pivot':1.1},
        {'custom':True}):
    actual,admitted=consumer_case(**scenario)
    expected,unused=consumer_case(enabled=False,**scenario)
    assert actual==expected,(scenario,actual,expected)
    if not scenario or scenario.get('wall') or scenario.get('translation'):assert admitted==1,(scenario,admitted)
    if scenario.get('custom'):assert actual[-1]==['bbox'],actual
    print('prepared_consumer',scenario,'admitted',admitted,'clear',actual[0])
print('fixed BattleRuntime / prepared Python consumer parity passed')
'''
