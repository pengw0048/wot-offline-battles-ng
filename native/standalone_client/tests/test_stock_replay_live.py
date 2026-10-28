"""Ordinary battle replay calls use real presentation owners and native guards.

The CPython2 extension compiles the production methods over observable
connection/clock owners. Vector/scalar conversions are seams; the original
x86 oracle separately checks the exact #1513 wrapper control and field flow.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_stock_cursor_camera import CLIENT, PY27, ROOT, run_oracle, requirements

HOST = Path(__file__).with_name('stock_replay_live_host.cpp')


def source_text():
    source = (ROOT / 'bigworld/stock_replay_controller.cpp').read_text()
    fields = source[source.index('    // #1513 6fd780'):source.index('    std::vector<std::pair<int,int>> streamFilters;')]
    text = source[source.index('bool text('):source.index('void notify(')]
    methods = source[source.index('// Live battles share'):source.index('// The exact setters')]
    properties = source[source.index('PyObject* getDuration('):source.index('PyObject* getError(')]
    names = {'serverAim', 'getArcadeSize', 'setArcadeSize', 'getSPGParams', 'setSPGParams',
             'setAimPosition', 'getReloadAmount', 'setReloadTime', 'getConsumableAmount',
             'setConsumableSlot', 'resetPeriod', 'effectNeeded', 'ammoPressed', 'cruiseMode',
             'equipmentID', 'sniperMode', 'lockTarget', 'chatMessage', 'serializeData'}
    bindings = '\n'.join('METHOD("%s",%s),' % (name, method) for name, method in
                         re.findall(r'REPLAY_METHOD\("([^"]+)",(\w+)\)', source) if method in names)
    return HOST.read_text().replace('@FIELDS@', fields).replace('@TEXT@', text).replace(
        '@METHODS@', methods).replace('@PROPERTIES@', properties).replace('@BINDINGS@', bindings)


DRIVER = r'''
import math,sys
sys.path.insert(0,sys.argv[1])
from replay_live_probe import Controller
owner=Controller()
assert owner.isServerAim is False
for value in (True,False,True):
 assert owner.onServerAim(value) is None
 assert owner.isServerAim is value
owner.isServerAim=False
assert owner.isServerAim is False
assert abs(owner.gunMarkerDiameter-.01)<1e-8
assert owner.turretYaw==owner.gunPitch==-100000
assert owner.getArcadeGunMarkerSize()==-1 and owner.getSPGGunMarkerParams()==(-1,-1)
owner.setArcadeGunMarkerSize(.75);assert owner.getArcadeGunMarkerSize()==.75
owner.setSPGGunMarkerParams((.25,2));assert owner.getSPGGunMarkerParams()==(.25,2)
for field,value in [('gunRotatorTargetPoint',(3.,5.,7.)),('gunMarkerPosition',(7.,5.,3.)),
                    ('gunMarkerDirection',(1.,0.,0.)),('gunMarkerDiameter',1.25),
                    ('turretYaw',-2.5),('gunPitch',.75),('fps',-15),('ping',60),('isLaggingNow',True)]:
 setattr(owner,field,value);assert getattr(owner,field)==value
owner.gunRotatorTargetPoint=(float('nan'),3.,4.)
assert owner.gunRotatorTargetPoint==(3.,5.,7.)
owner.gunRotatorTargetPoint=(float('inf'),2.,4.)
assert math.isinf(owner.gunRotatorTargetPoint[0])
owner.gunMarkerPosition=(float('nan'),2.,4.)
assert math.isnan(owner.gunMarkerPosition[0])
assert owner.setAimClipPosition((.5,-.5)) is None
assert owner.getGunReloadAmountLeft()==1
owner.setGunReloadTime(2,8);owner.setActiveConsumableSlot(3)
assert owner.getGunReloadAmountLeft()==1 and owner.getConsumableSlotCooldownAmount(3)==0
owner.configure(1,5);owner.setGunReloadTime(2,8);owner.setActiveConsumableSlot(3)
assert owner.getGunReloadAmountLeft()==-.25 and owner.getConsumableSlotCooldownAmount(3)==-.25
assert owner.getConsumableSlotCooldownAmount(4)==0
owner.configure(1,15)
assert owner.getGunReloadAmountLeft()==owner.getConsumableSlotCooldownAmount(3)==1
owner.setGunReloadTime(-4,-1);assert owner.getGunReloadAmountLeft()==0
owner.setGunReloadTime(5,0);assert owner.getGunReloadAmountLeft()==1
owner.configure(0,0)
assert owner.arenaPeriod==0 and owner.arenaLength==0
owner.arenaLength=60;assert owner.arenaLength==0
owner.resetArenaPeriod();owner.arenaLength=60;assert owner.arenaLength==60
owner.arenaLength=120;assert owner.arenaLength==60
owner.arenaPeriod=3;owner.arenaLength=120;assert owner.arenaLength==120
owner.arenaPeriod=3;owner.arenaLength=240;assert owner.arenaLength==120
owner.configure(1,5);assert owner.arenaLength==115
assert owner.isEffectNeedToPlay(77) is False
owner.entity(77,1);assert owner.isEffectNeedToPlay(77) is False
owner.entity(77,0);assert owner.isEffectNeedToPlay(77) is True
owner.entity(77,-1);assert owner.isEffectNeedToPlay(77) is False
calls=[]
for index in (2,5,6,7,8,10):
 owner.callback(index,lambda *args:calls.append(args))
owner.configure(0,0)
def events():
 owner.onAmmoButtonPressed(7);owner.onBattleChatMessage('text',True)
 owner.onLockTarget(44,False);owner.onSetCruiseMode(2);owner.onSetEquipmentID(9);owner.onSniperMode(True)
events();assert calls==[]
owner.configure(1,0);events()
assert calls==[(7,),('text',True),(44,False),(2,),(9,),(True,)]
# Callbacks may replace themselves synchronously; the invoking reference stays alive.
owner.callback(2,lambda v:owner.callback(2,lambda n:calls.append(('changed',n))))
owner.onAmmoButtonPressed(1);owner.onAmmoButtonPressed(2);assert calls[-1]==('changed',2)
reduced=[]
class Payload(object):
 def __reduce__(self):reduced.append(True);return (str,('payload',))
owner.configure(0,0);owner.serializeCallbackData('event',(Payload(),))
assert reduced==[True]
try:owner.serializeCallbackData('event',(lambda:None,))
except Exception:pass
else:raise AssertionError('Pickle failure was hidden')
for method,args in [(owner.onServerAim,()),(owner.onServerAim,(True,False)),
 (owner.setArcadeGunMarkerSize,('wrong',)),(owner.setSPGGunMarkerParams,((1,),)),
 (owner.setAimClipPosition,((1,),)),(owner.setGunReloadTime,(1,)),
 (owner.setGunReloadTime,(1,'wrong')),(owner.setActiveConsumableSlot,(2**40,)),
 (owner.getConsumableSlotCooldownAmount,(-1,)),(owner.isEffectNeedToPlay,('wrong',)),
 (owner.onLockTarget,(1,)),(owner.onSniperMode,()),(owner.serializeCallbackData,('only',))]:
 try:method(*args)
 except (TypeError,OverflowError):pass
 else:raise AssertionError('Invalid ABI arguments accepted: %r'%(args,))
for field in ('isServerAim','gunMarkerPosition','gunMarkerDiameter','fps','arenaPeriod','arenaLength'):
 try:delattr(owner,field)
 except TypeError:pass
 else:raise AssertionError('Deleted native state')
# Execute the actual pinned producer that used to abort AvatarInputHandler.start.
import marshal,types,zipfile
z=zipfile.ZipFile(sys.argv[2]);code=marshal.loads(z.read('scripts/client/BattleReplay.pyc')[8:])
cls=next(c for c in code.co_consts if isinstance(c,types.CodeType) and c.co_name=='BattleReplay')
funcs=dict((c.co_name,c) for c in cls.co_consts if isinstance(c,types.CodeType))
class LiveBattle(object):pass
live=LiveBattle();live._BattleReplay__replayCtrl=owner
fn=types.FunctionType(funcs['setUseServerAim'],{})
assert fn(live,True) is None and owner.isServerAim is True
for name,args in [('setArcadeGunMarkerSize',(.5,)),('setSPGGunMarkerParams',(.25,4.)),
                  ('setAimClipPosition',((.1,.2),)),('setGunReloadTime',(0.,10.)),
                  ('setEquipmentID',(3,))]:
 types.FunctionType(funcs[name],{})(live,*args)
print('Production replay live methods and #1513 Python battle producers passed')
'''


class ReplayLiveTests(unittest.TestCase):
    def test_original_x86_live_playback_and_recording_guards(self):
        requirements(self)
        script = r'''
import sys
sys.path.insert(0,sys.argv[1])
from stock_replay_live_oracle import OriginalReplayLive
from stock_cursor_camera_oracle import bits
e=OriginalReplayLive(sys.argv[2])
for value in (False,True,False):
 assert e.method(0x6ff240,value) is None and bool(e.byte(e.owner+0x21d)) is value
assert e.events==[]
e.mode(recording=True)
for value in (False,True,True,False):e.method(0x6ff240,value)
assert e.events==[('packet','serverAim'),('packet','serverAim')]
e.events[:]=[];e.mode()
assert e.method(0x6fe5b0)==-1 and e.method(0x6feaf0)==(-1,-1)
e.method(0x6ffa00,.75);e.method(0x700050,(.25,2.))
assert e.method(0x6fe5b0)==.75 and e.method(0x6feaf0)==(.25,2.)
e.method(0x6ff960,(.5,-.5));assert e.floats(e.codec+0xa4,2)==[0,0]
for setter,getter,value in ((0x703810,0x700cf0,(3.,5.,7.)),(0x703720,0x700c60,(7.,5.,3.)),
 (0x7036b0,0x700c00,(1.,0.,0.)),(0x703670,0x700bd0,1.25),
 (0x703d70,0x701000,-2.5),(0x703790,0x700cc0,.75),
 (0x703630,0x700bb0,-15),(0x703ae0,0x700ea0,60),(0x7039a0,0x700db0,True)):
 assert e.setter(setter,value) is None and e.method(getter)==value
e.setter(0x703810,(float('nan'),3.,4.));assert e.method(0x700cf0)==(3.,5.,7.)
e.setter(0x703810,(float('inf'),2.,4.));assert e.method(0x700cf0)[0]==float('inf')
assert e.method(0x6fea90)==1
e.method(0x6ffdc0,2.,8.);e.method(0x6ff8b0,3)
assert e.method(0x6fea90)==1 and e.method(0x6fe990,3)==0
e.mode(playing=True,now=5);e.method(0x6ffdc0,2.,8.);e.method(0x6ff8b0,3)
assert e.method(0x6fea90)==e.method(0x6fe990,3)==-.25
assert e.method(0x6fe990,4)==0
e.mode(playing=True,now=15);assert e.method(0x6fea90)==1
e.method(0x6ffdc0,-4.,-1.);assert e.method(0x6fea90)==0
e.method(0x6ffdc0,5.,0.);assert e.method(0x6fea90)==1
e.mode()
e.setter(0x7032e0,60.);assert e.method(0x700a30)==0
e.method(0x6ff610);e.setter(0x7032e0,60.);assert e.method(0x700a30)==60
e.setter(0x7032e0,120.);assert e.method(0x700a30)==60
e.setter(0x703370,3);e.setter(0x7032e0,120.);assert e.method(0x700a30)==120
e.setter(0x703370,3);e.setter(0x7032e0,240.);assert e.method(0x700a30)==120
e.mode(playing=True,now=5);assert e.method(0x700a30)==115
assert e.method(0x6febe0,77) is False
entity=e.alloc(0x144);e.entities[77]=entity
e.uc.mem_write(entity+0x140,b'\x01');assert e.method(0x6febe0,77) is False
e.uc.mem_write(entity+0x140,b'\x00');assert e.method(0x6febe0,77) is True
e.entities.clear();assert e.method(0x6febe0,77) is False
events=((0x6fed60,(7,)),(0x6ff100,(44,False)),(0x6ff2e0,(2,)),
        (0x6ff3d0,(9,)),(0x6ff4c0,(True,)))
e.mode()
for function,args in events:assert e.method(function,*args) is None
assert e.events==[]
e.mode(playing=True)
for function,args in events:e.method(function,*args)
assert e.events==[('callback','callback-170',[7]),('callback','callback-180',[44,False]),
 ('callback','callback-184',[2]),('callback','callback-188',[9]),('callback','callback-18c',[True])]
for address,args in ((0x6ff240,()),(0x6ffa00,('wrong',)),(0x700050,((1,),)),
 (0x6ffdc0,(1.,)),(0x6ffdc0,(1.,'wrong')),(0x6fe990,(-1,)),(0x6ff100,(1,))):
 assert e.method(address,*args)=='error'
print('Original replay live guards, presentation state and callbacks passed')
'''
        self.assertIn('callbacks passed', run_oracle(script))

    def test_production_wrappers_and_pinned_live_producers(self):
        if not PY27.exists() or not (CLIENT / 'res/packages/scripts.pkg').exists():
            self.skipTest('CPython 2.7 and pinned client bytecode required')
        with tempfile.TemporaryDirectory(prefix='replay-live-') as directory:
            path = Path(directory)
            cpp = path / 'probe.cpp'
            cpp.write_text(source_text())
            result = subprocess.run(['c++', '-std=c++14', '-Wall', '-Werror', '-Wno-missing-field-initializers',
                                     '-Wno-write-strings', '-shared', '-undefined', 'dynamic_lookup',
                                     '-isystem', str(PY27.parents[1] / 'include/python2.7'), str(cpp),
                                     '-o', str(path / 'replay_live_probe.so')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(PY27), '-c', DRIVER, directory,
                                     str(CLIENT / 'res/packages/scripts.pkg')],
                                    capture_output=True, text=True,
                                    env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('producers passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
