"""Real CPython 2.7 facade with explicit native physics/resource test doubles."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
import copy
import detached_python_test as native
class Box(object):
    def __init__(self,**kw):self.__dict__.update(kw)
def never(*args):raise AssertionError('Python business callback reached')
def part(mass):return Box(weight=mass,models=Box(exploded='stock'),hitTester=Box(bbox=((-1.,-.5,-1.),(1.,.5,1.),2.),localHitTest=never))
turret=part(1000.);turret.gunPosition=(0.,0.,1.)
chassis=part(5000.);chassis.hullPosition=(0.,.7,0.)
hull=part(4000.);hull.turretPositions=((0.,1.,0.),)
chassis.hitTester.segmentHitTest=never;del chassis.hitTester.localHitTest
del hull.hitTester.localHitTest
descriptor=Box(chassis=chassis,hull=hull,turret=turret,gun=part(500.),physics={'weight':10500.})
entity=Box(typeDescriptor=descriptor,isStarted=True,health=-13,_Vehicle__turretDetachmentConfirmed=True,position=(0.,1.,0.),matrix=Box(yaw=0.,pitch=0.,roll=0.),appearance=Box(turretMatrix=Box(yaw=0.)))
record=dict(kind='player',network_id=7,engine_id=77,ready=True,state=dict(x=0.,y=1.,z=0.,yaw=0.,pitch=0.,roll=0.))
battle=Box(_start_message={'round_id':9},_records={'player:7':record},_remote_factory=Box(_vehicles={77:entity}),_detached_turret_rows={},_detached_turret_proposals={},_detached_turret_retry={},_turret_bodies={},_next_turret_publish=0.,_projectile_server_time_ms=1000,_projectile_server_local_time=1.,_last_snapshot={'players':[]},_battle_result=None)
battle._queue_detached_turret=battle._server_entity=battle._collide_rigid_turret=battle._authority_players=never
# The worker's private local carrier is a real _records entry, but never a
# combat participant. Cold prefetch matches the native lifecycle participant
# filter without weakening queue or canonical packet identity checks.
battle._records.update({'player:-1':dict(kind='player',network_id=-1,local=True),
                        'bot:0':dict(kind='bot',network_id=0),
                        'observer':dict(kind='observer'), 'unused':None})
saved_box=descriptor.gun.hitTester.bbox
# Missing loaded geometry is a proved lifecycle Pending state. It is retried
# even when the resource/descriptor pointer stamp has not changed.
descriptor.gun.hitTester.bbox=None
assert native.create(battle) and native.errors()==[]
assert not native.ingress(None,1.)
assert not native.ingress({},1.)
assert not native.ingress({'round_id':9},1.)
assert not native.ingress({'round_id':9,'detached_turrets':None},1.)
assert not native.ingress({'round_id':9,'detached_turrets':[]},1.)
assert native.errors()==[]
descriptor.gun.hitTester.bbox=saved_box
assert native.queue(record,entity,None,1.)
native.clear();battle._detached_turret_proposals={}
# A present malformed bbox is a geometry error, never hidden as Pending.
descriptor.gun.hitTester.bbox=((-1.,),(1.,))
assert native.create(battle)
messages=native.errors();assert messages and any('geometry:' in text for text in messages),messages
native.clear();descriptor.gun.hitTester.bbox=saved_box
assert native.create(battle) and native.errors()==[]
try:native.queue(battle._records['player:-1'],entity,None,1.)
except ValueError as error:assert 'record identity is invalid' in str(error)
else:raise AssertionError('private carrier admitted to launch')
assert native.queue(record,entity,None,1.)
rows=native.rows();assert len(rows)==1 and 'body' not in rows[0]['flight'] and 'created_time_ms' not in rows[0]
assert battle._detached_turret_proposals['player:7']==rows[0]
assert not native.queue(record,entity,None,1.1)
before=native.counts();assert before[0]>1 and before[1]==0
assert native.advance(battle,1.2) is None and native.counts()==before
canonical=copy.deepcopy(rows[0]);canonical['created_time_ms']=1000
bad=copy.deepcopy(canonical);bad['actor_id']=True
assert native.ingress({'round_id':9,'detached_turrets':[bad,canonical]},1.2)
assert native.errors() and battle._detached_turret_proposals=={}
assert native.rows()==[canonical]
assert not native.ingress({'round_id':8,'detached_turrets':[]},1.2)
assert not native.ingress({'round_id':9},1.2)
assert native.advance(battle,5.) is None and native.counts()==before
# A failed native launch retains its own .25-second retry, then succeeds
# without reinvoking the Python queue or descriptor/business helper.
record2=dict(record,network_id=8,engine_id=78);battle._records['player:8']=record2;battle._remote_factory._vehicles[78]=entity
native.fail(True);assert not native.queue(record2,entity,None,6.)
count=native.counts();native.fail(False)
assert native.advance(battle,6.24) is None and native.counts()==count
assert native.advance(battle,6.25) is None and len(native.rows())==2
assert native.errors()
battle._battle_result={};tail=native.advance(battle,6.3);assert tail==dict(detached_turrets=native.rows())
assert native.advance(battle,6.4) is None
assert native.advance(battle,6.55)==tail
# Only explicit accepted body checkpoints enable the rigid branch. A bad
# sibling row cannot suppress this actor, and the cumulative ledger applies
# once across repeated callbacks.
body_row=copy.deepcopy(canonical);body_row.update(motion_seq=1,motion_time_ms=7000,created_time_ms=7000,support_key=None)
body_row['flight']['body']=dict(position=[0.,4.,0.],attitude=[0.,0.,0.],centre=[0.,0.,0.],velocity=[0.,0.,0.],angular_velocity=[0.,0.,0.],grounded=False,sleeping=False,impact_serial=0,impact=None,acks=[])
assert native.ingress(dict(round_id=9,detached_turrets=[body_row,bad]),7.)
battle._last_snapshot['players']=[dict(id=9,participating=True,world_pose=True,turret_pushes=[['player:7',1,1500.,0.,0.,0.,0.,0.]])]
previous=native.counts();native.advance(battle,7.)
current=native.counts();assert current[1]>previous[1]
body_update=next(row for row in native.rows() if row['actor_id']==7)
assert body_update['flight']['body']['velocity'][0]==1.
assert body_update['flight']['body']['acks']==[['player:9',1,1500.,0.,0.,0.,0.,0.]]
native.advance(battle,7.04)
assert next(row for row in native.rows() if row['actor_id']==7)['flight']['body']['velocity'][0]==1.
native.stale()
try:native.advance(battle,8.)
except ValueError:pass
else:raise AssertionError('stale native owner continued')
native.clear()
# A declared combat participant with an invalid positive identity remains an
# observable row error, rather than being hidden by the prefetch filter.
battle._records['player:2147483648']=dict(kind='player',network_id=2147483648)
assert native.create(battle)
assert native.errors()==['Detached turret record identity is invalid']
native.clear();del battle._records['player:2147483648']
# Cold handover never silently drops an already advancing stock rigid body.
battle._turret_bodies={'player:7':object()}
try:native.create(battle)
except ValueError:pass
else:raise AssertionError('legacy body was dropped')
native.clear()
print('detached facade passed')
'''
class DetachedPython(unittest.TestCase):
    def test_queue_echo_tail_and_no_business_callbacks(self):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:self.skipTest('set WOT_FORK_0101_SOURCE')
        py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        with tempfile.TemporaryDirectory(prefix='detached-python-') as directory:
            folder=Path(directory);script=folder/'fixture.py';script.write_text(FIXTURE)
            # Compile the production Pimpl body against explicit native test
            # doubles; this proves CAPI/lifecycle behavior, not BW rendering.
            facade=(ROOT/'bigworld/stock_native_detached_turret_python.cpp').read_text().replace('#include "stock_native_detached_turret_world.hpp"','#include "stock_native_detached_turret_python_stub.hpp"')
            (folder/'facade.cpp').write_text(facade)
            sources=[folder/'facade.cpp',ROOT/'bigworld/stock_native_detached_turret_snapshot.cpp',ROOT/'bigworld/stock_native_detached_turret_runtime.cpp',ROOT/'bigworld/stock_native_detached_turret_launch.cpp',ROOT/'bigworld/stock_native_bot_turret_obstacles.cpp',ROOT/'bigworld/stock_native_bot_resource_lifecycle.cpp',Path(source)/'native/offline_destructible_geometry.cpp',ROOT/'tests/stock_native_detached_turret_python_host.cpp']
            command=['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-DWOT_RESOURCE_LIFECYCLE_HOST_BUILD','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(ROOT/'tests'),'-I'+str(ROOT/'bigworld'),'-I'+str(Path(source)/'native'),*[str(p) for p in sources],'-o',str(folder/'detached_python_test.so')]
            result=subprocess.run(command,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([py27,'-B',str(script)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr);self.assertIn('detached facade passed',result.stdout)
if __name__=='__main__':unittest.main()
