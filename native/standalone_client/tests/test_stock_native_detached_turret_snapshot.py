"""Native ingress/publication mirrors the fixed detached-turret wire schema."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
import ast,copy,imp,math,os,random,sys,types
import detached_snapshot_test as native
class Box(object):
    def __init__(self,**kw):self.__dict__.update(kw)
def component(weight):return Box(weight=weight,hitTester=Box(bbox=((-1.,-.5,-2.),(1.,.5,2.),3.),localHitTest=lambda a,b:None))
turret=component(1000.);turret.gunPosition=(0.,.2,1.5)
chassis=component(7000.);chassis.hullPosition=(0.,.7,0.)
hull=component(5000.);hull.turretPositions=((0.,.9,.1),)
descriptor=Box(chassis=chassis,hull=hull,turret=turret,gun=component(500.))
# Moving support geometry uses only bounds; segment-only chassis and hull
# testers satisfy the exact source contract. The third radius is ignored.
chassis.hitTester.segmentHitTest=lambda a,b:None
del chassis.hitTester.localHitTest
del hull.hitTester.localHitTest
assert native.geometry(descriptor)==(1500.,0.,.06666666666666667,.7,1.6,1.5)
assert native.launch(descriptor)==(0.,1.6,.1,.5,1,0)
turret.models=Box(exploded='stock_turret');descriptor.gun.models=Box(exploded='stock_gun')
assert native.launch(descriptor)==(0.,1.6,.1,.5,1,1)
base=os.path.join(sys.argv[1],'src/res/scripts/client/gui/mods/offline_lan_0922')
shot=imp.load_source('shot_geometry',os.path.join(base,'shot_geometry.py'))
env=dict(math=math,shot_geometry=shot)
tree=ast.parse(open(os.path.join(base,'entities/turret_obstacles.py')).read())
names=('_finite','_xyz','_add','component_bounds','_moving_components','_read_pose','_world_box','vehicle_support_boxes')
exec compile(ast.Module([n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names]),'support-boxes','exec') in env
rng=random.Random(1513)
def equal(a,b):
    if isinstance(b,(list,tuple)):
        assert len(a)==len(b)
        for x,y in zip(a,b):equal(x,y)
    else:assert abs(a-b)<=2.e-12*max(1.,abs(a),abs(b)),(a,b)
for index in range(120):
    position=[rng.uniform(-100.,100.) for unused in range(3)]
    attitude=[rng.uniform(-3.,3.) for unused in range(3)]
    hull_position=[rng.uniform(-100.,100.) for unused in range(3)]
    hull_attitude=[rng.uniform(-3.,3.) for unused in range(3)]
    separate=index%2==0;aim_yaw=rng.uniform(-3.,3.);gun_pitch=rng.uniform(-1.,1.)
    def pose(p,a):return dict(x=p[0],y=p[1],z=p[2],yaw=a[0],pitch=a[1],roll=a[2])
    source=pose(position,attitude);source.update(aim_yaw=aim_yaw,gun_pitch=gun_pitch)
    if separate:source['hull']=pose(hull_position,hull_attitude)
    expected=env['vehicle_support_boxes'](descriptor,source,index%3!=0)
    actual=native.support(descriptor,dict(position=position,attitude=attitude,hull_position=hull_position,hull_attitude=hull_attitude,separate=separate,aim_yaw=aim_yaw,gun_pitch=gun_pitch),int(index%3!=0))
    equal(actual,expected)

# Support boxes do not require localHitTest on any component. Detached
# turret/gun admission still requires it, independently of a valid bbox.
pose=dict(position=(0.,0.,0.),attitude=(0.,0.,0.),hull_position=(0.,0.,0.),hull_attitude=(0.,0.,0.),separate=False,aim_yaw=0.,gun_pitch=0.)
original=descriptor.gun.hitTester.localHitTest
del descriptor.gun.hitTester.localHitTest
assert len(native.support(descriptor,pose,1))==4
for operation in (native.geometry,native.launch):
    try:operation(descriptor)
    except (ValueError,TypeError):pass
    else:raise AssertionError('missing detached gun tester accepted')
descriptor.gun.hitTester.localHitTest=original
original_bbox=chassis.hitTester.bbox
for bad_bbox in ((), ((-1.,-1.,-1.),), ((0.,0.,0.),(0.,0.,0.),1.)):
    chassis.hitTester.bbox=bad_bbox
    try:native.support(descriptor,pose,1)
    except (ValueError,TypeError,IndexError):pass
    else:raise AssertionError('invalid support bounds accepted')
chassis.hitTester.bbox=original_bbox
row=dict(actor_kind='bot',actor_id=7,attitude=[0.,0.,0.],spin=[0.,0.,0.],created_time_ms=1000,
 flight=dict(origin=[0.,4.,0.],velocity=[0.,1.,0.],segments=[dict(origin=[0.,4.,0.],velocity=[0.,1.,0.],start=0.,duration=1.)],duration=1.,rest=[0.,0.,0.],contact=[0.,0.,0.],impact_velocity=[0.,-8.,0.],energy=32.,landed=True,rest_attitude=[0.,0.,0.]))
assert native.row(row)==row
row.update(motion_seq=1,motion_time_ms=1400,support_key='player:9')
row['flight']['body']=dict(position=[0.,3.,0.],attitude=[0.,.1,0.],centre=[0.,.2,.1],velocity=[1.,-1.,0.],angular_velocity=[.1,0.,0.],grounded=False,sleeping=False,impact_serial=2,impact=dict(point=[1.,0.,2.],normal=[0.,1.,0.],velocity=[1.,-8.,0.],energy=32.5),acks=[['player:9',2,100.,0.,0.,0.,1.,0.]])
assert native.row(row)==row
assert native.impulses(dict(id=9,turret_pushes=[['bot:7',2,100.,0.,0.,0.,1.,0.]]))==[(0,7,1,9,2,100.)]
for edit in (lambda r:r.update(actor_id=True),lambda r:r['flight'].update(body=None),lambda r:r['flight']['body'].update(grounded=1),lambda r:r['flight'].update(duration=9.),lambda r:r['flight']['body'].update(acks=[['player:9',0,0.,0.,0.,0.,0.,0.]])):
    bad=copy.deepcopy(row);edit(bad)
    try:native.row(bad)
    except (ValueError,TypeError):pass
    else:raise AssertionError('invalid row accepted')
print('detached ingress passed')
'''
class DetachedSnapshot(unittest.TestCase):
    def test_python27_ingress(self):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:self.skipTest('set WOT_FORK_0101_SOURCE')
        py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        with tempfile.TemporaryDirectory(prefix='detached-snapshot-') as directory:
            folder=Path(directory);script=folder/'fixture.py';script.write_text(FIXTURE)
            sources=[ROOT/'bigworld/stock_native_detached_turret_snapshot.cpp',ROOT/'bigworld/stock_native_detached_turret_runtime.cpp',ROOT/'bigworld/stock_native_bot_turret_obstacles.cpp',ROOT/'bigworld/stock_native_client_state.cpp',Path(source)/'native/offline_destructible_geometry.cpp',ROOT/'tests/stock_native_detached_turret_snapshot_host.cpp']
            result=subprocess.run(['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-DWOT_CLIENT_REPLICA_HOST_BUILD','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(Path(source)/'native'),'-I'+str(ROOT/'bigworld'),*[str(p) for p in sources],'-o',str(folder/'detached_snapshot_test.so')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([py27,'-B',str(script),source],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr);self.assertIn('detached ingress passed',result.stdout)
if __name__=='__main__':unittest.main()
