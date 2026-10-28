from __future__ import print_function
import json, marshal, math, os, sys, types, zipfile
import BigWorld, _zones


def name_id(text):
    if not text: return 0
    value=2166136261
    for byte in text.lower(): value=((value*16777619)^ord(byte))&0xffffffff
    return value


_zones.reset()
# Conversion and exact arity go through the unchanged SDK PY_AUTO wrapper.
for bad in [(), ((0,0),(10,10),0,'','',''), ((0,), (10,10),0,'','','',1),
            ((0,0),(10,10),'angle','','','',1), ((0,0),(10,10),0,None,'','',1)]:
    try: BigWorld.addSoundZoneTrigger(*bad)
    except (TypeError, ValueError): pass
    else: raise AssertionError(('accepted invalid zone',bad))
assert BigWorld.addSoundZoneTrigger((0,0),(20,20),0,'enter','exit','AUX_zone',.75) is None
assert BigWorld.addSoundZoneTrigger((0,0),(30,30),0,'second','leave_second','AUX_second',.5) is None
_zones.mapping('spaces/test_map','AUX_default',.25)
assert _zones.trace()==[('aux',1,name_id('AUX_default'),.25)]
_zones.tick(0,10000,0,.05)
assert _zones.trace()==[]
_zones.tick(0,-10000,0,.11)
assert _zones.trace()==[('event',name_id('enter')),('aux',1,name_id('AUX_zone'),.75)]
_zones.tick(0,0,0,.22)
assert _zones.trace()==[]
_zones.tick(12,0,0,.33)
assert _zones.trace()==[('event',name_id('exit')),('event',name_id('second')),('aux',1,name_id('AUX_second'),.5)]
_zones.tick(100,0,0,.44)
assert _zones.trace()==[('event',name_id('leave_second')),('aux',1,name_id('AUX_default'),.25)]
_zones.tick(0,0,0,.55)
assert _zones.trace()[0]==('event',name_id('enter'))
_zones.clearSpace()
assert _zones.trace()==[]  # destruction cancels, without synthesizing an exit
_zones.tick(0,0,0,10)
assert _zones.trace()==[]
_zones.mapping('spaces/next_map','',0.)
assert _zones.trace()==[('aux',1,0,0.)]
BigWorld.addSoundZoneTrigger((0,0),(10,10),0,'again','','AUX_next',1)
_zones.tick(0,0,0,10.05)
assert _zones.trace()==[]
_zones.tick(0,0,0,10.11)
assert _zones.trace()==[('event',name_id('again')),('aux',1,name_id('AUX_next'),1.)]
assert _zones.geometry(sys.argv[1]) > 2000

# Execute the original UDO class body and constructor, with every authored
# property supplied by the real UDOS/PackedXML records collected by the audit.
class Vector2(tuple):
    def __new__(cls,*args): return tuple.__new__(cls,args[0] if len(args)==1 else args)
    def __add__(self,other): return Vector2(self[0]+other[0],self[1]+other[1])
class UDO(object):
    def __init__(self): pass
BigWorld.UserDataObject=UDO
archive=zipfile.ZipFile(os.path.join(os.environ['WOT_0922_CLIENT'],'res/packages/scripts.pkg'))
module=marshal.loads(archive.read('scripts/client/SoundZoneTrigger.pyc')[8:])
code=next(c for c in module.co_consts if isinstance(c,types.CodeType) and c.co_name=='SoundZoneTrigger')
namespace=types.FunctionType(code,{'__name__':'SoundZoneTrigger','BigWorld':BigWorld,
    'Math':types.ModuleType('Math'),'math':math,'cos':math.cos,'sin':math.sin,
    '__builtins__':__builtins__})()
# __init__ reads module math, not a replacement producer.
namespace['__init__'].func_globals['Math'].Vector2=Vector2
SoundZone=type('SoundZoneTrigger',(UDO,),namespace)
rows=json.load(open(os.environ['WOT_SOUND_ZONE_RECORDS']))
for row in rows:
    _zones.reset()
    value=SoundZone.__new__(SoundZone)
    value.direction=type('Direction',(),{'x':row['angle']})()
    value.position=type('Position',(),dict(zip(('x','y','z'),row['position'])))()
    value.Size=tuple(row['size']); value.ZoneEnter=row['enter'];value.ZoneExit=row['exit']
    value.Reverb=row['reverb'];value.ReverbLevel=row['gain']
    value.__init__()
    # Constructor's exact shifted centre is passed into the real native owner.
    alpha=value.direction.x
    x=value.position.x-value.Size[1]*.5*math.sin(alpha)
    z=value.position.z+value.Size[1]*.5*math.cos(alpha)
    _zones.tick(x,999999,z,.11)
    assert _zones.trace()==[('aux',1,name_id(row['reverb']),row['gain'])],row
    value.destroy()
    _zones.clearSpace()
print('Original SoundZoneTrigger constructors:',len(rows))
