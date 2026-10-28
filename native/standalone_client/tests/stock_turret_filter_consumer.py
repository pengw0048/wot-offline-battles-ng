from __future__ import print_function
import gc
import json
import marshal
import os
import struct
import sys
import types
import weakref
import zipfile
import BigWorld


def invalid(call, error=TypeError):
    try:
        call()
    except error:
        return
    raise AssertionError('Invalid filter operation was accepted')


factory=BigWorld.WGTurretFilter
assert isinstance(factory,type) and factory.__base__ is BigWorld.WGEntityFilter
f=factory()
assert f.isLaggingNow is True
assert type(f) is factory and isinstance(f,BigWorld.WGEntityFilter)
assert f.segmentMayHitEntity((0,0,0),(1,2,3),0) is False
try:
    factory(1)
except TypeError as error:
    assert str(error)=='PyWGVehicleFilter() takes no arguments.'
else:
    raise AssertionError('Turret constructor accepted an argument')
assert type(factory(unused=object())) is factory
invalid(lambda: BigWorld.fixture('attach',object()))
invalid(lambda: BigWorld.fixture('inspect',f),ValueError)
f.maxTimeBetweenInputs=.125
f.ignoreInputs=True
BigWorld.fixture('attach',f)
assert BigWorld.fixture('inspect',f)==((0.,0.,0.),(0.,0.,0.),(0.,0.,0.),1,1)
assert f.isLaggingNow is False
assert f.maxTimeBetweenInputs==.125 and f.ignoreInputs
assert f.segmentMayHitEntity((0,0,0),(1,2,3),0) is True
BigWorld.fixture('receipt',f,((0,0,0),(1,0,3),.1,1))
velocity,gravity,angular,turret,attached=BigWorld.fixture('inspect',f)
assert velocity==(4.,0.,12.) and angular==(0.,0.,0.) and turret and attached
assert struct.unpack('<I',struct.pack('<f',gravity[1]))[0]==0xc11d1eb8
position=BigWorld.fixture('predict',f,.1)
assert f.isLaggingNow is True
assert abs(position[0]-.4)<1e-6 and abs(position[1]-9.9018)<1e-5 and abs(position[2]-1.2)<1e-6
# Even an invalid base receipt sets gravity; constructor/reset do not.
BigWorld.fixture('reset',f)
assert BigWorld.fixture('inspect',f)[1]==(0.,0.,0.)
BigWorld.fixture('receipt',f,((1,2,3),(40,50,60),.1,0))
assert BigWorld.fixture('inspect',f)[0]==(0.,0.,0.)
assert BigWorld.fixture('inspect',f)[1]==gravity
# The original copies the base state before rejecting a non-turret subtype.
base=BigWorld.WGEntityFilter();BigWorld.fixture('attach',base)
BigWorld.fixture('receipt',base,((0,0,0),(2,3,4),.2,1))
assert BigWorld.fixture('copy',f,base) is False
assert BigWorld.fixture('inspect',f)[:3]==BigWorld.fixture('inspect',base)[:3]
other=factory();BigWorld.fixture('attach',other)
BigWorld.fixture('receipt',other,((0,0,0),(9,0,1),.3,1))
assert BigWorld.fixture('copy',f,other) is True
assert BigWorld.fixture('inspect',f)[:3]==BigWorld.fixture('inspect',other)[:3]
copied_settings=(f.maxTimeBetweenInputs,f.ignoreInputs)
for owner in (f,base,other):
    BigWorld.fixture('drop',owner)
    BigWorld.fixture('drop',owner)
BigWorld.fixture('attach',f)
assert BigWorld.fixture('inspect',f)[1]==(0.,0.,0.)
assert (f.maxTimeBetweenInputs,f.ignoreInputs)==copied_settings
BigWorld.fixture('drop',f)
identity=weakref.ref(f);del f;gc.collect();assert identity() is None
print('Actual SDK turret factory, inheritance, settings, weak owner and base receipt/prediction/copy passed')

cases=os.environ.get('WOT_TURRET_ORACLE_CASES')
if cases:
    probe=factory();BigWorld.fixture('attach',probe)
    records=json.load(open(cases,'rb'))
    for start,end,elapsed,valid,expected,initial in records:
        BigWorld.fixture('reset',probe)
        BigWorld.fixture('seed',probe,tuple(initial[index:index+3]for index in range(0,12,3)))
        BigWorld.fixture('receipt',probe,(start,end,elapsed,valid))
        actual=BigWorld.fixture('full',probe)
        for value,reference in zip(actual,expected):
            assert abs(value-reference)<=max(.0003,abs(reference)*.00001),(value,reference)
        assert actual[6:9]==tuple(expected[6:9]) and actual[9:12]==tuple(expected[9:12])
    BigWorld.fixture('drop',probe)
    print('%d original turret gravity/EMA records matched'%len(records))

client=os.environ.get('WOT_0922_CLIENT')
if client:
    class Bag(object):
        def __init__(self,**kwargs):self.__dict__.update(kwargs)
    callbacks={};next_callback=[0]
    def callback(delay,function):
        next_callback[0]+=1;callbacks[next_callback[0]]=(delay,function);return next_callback[0]
    def cancel(identifier):assert identifier in callbacks;del callbacks[identifier]
    BigWorld.callback=callback;BigWorld.cancelCallback=cancel;BigWorld.entity=lambda unused:None
    class Entity(object):
        @property
        def filter(self):return getattr(self,'_filter',None)
        @filter.setter
        def filter(self,value):
            old=getattr(self,'_filter',None)
            if old is not None:BigWorld.fixture('drop',old)
            self._filter=value
            if value is not None:BigWorld.fixture('attach',value)
    BigWorld.Entity=Entity
    class ComponentSystem(object):
        def __init__(self):self.components=[]
        def addComponent(self,component):self.components.append(component)
        def destroy(self):
            for component in self.components:component.destroy()
            self.components=[]
    class Effect(object):
        def __init__(self,*args):self.args=args;self.destroyed=False;self.pulls=[]
        def destroy(self):self.destroyed=True
        def notifyAboutBeingPulled(self,*args):self.pulls.append(args)
    class CallbackDelayer(object):
        def __init__(self):self.calls=[];self.destroyed=False
        def delayCallback(self,*args):self.calls.append(args)
        def destroy(self):self.destroyed=True;self.calls=[]
    module=types.ModuleType('helpers.CallbackDelayer');module.CallbackDelayer=CallbackDelayer
    sys.modules['helpers']=types.ModuleType('helpers');sys.modules['helpers'].__path__=[]
    sys.modules['helpers.CallbackDelayer']=module
    stickers=types.ModuleType('VehicleStickers');stickers.VehicleStickers=Effect;sys.modules['VehicleStickers']=stickers
    class Projectiles(object):
        entities=[]
        @classmethod
        def addEntity(cls,entity):cls.entities.append(entity)
        @classmethod
        def removeEntity(cls,entity):cls.entities.remove(entity)
    class Assembler(object):
        def __init__(self,*args):self.args=args;self.parts=[]
        def addRootPart(self,*args):self.parts.append(args)
        def emplacePart(self,*args):self.parts.append(args)
    BigWorld.CompoundAssembler=Assembler
    class Descriptor(object):
        def __init__(self,compactDescr):
            assert compactDescr==b'original compact descriptor'
            self.name='test:turret';self.kept=[];self.loads=[]
            def part(name):return Bag(models=Bag(exploded=name),turretDetachmentEffects='authored',hitTester=Bag(loadBspModel=lambda:self.loads.append(name)))
            self.turret=part('turret.model');self.gun=part('gun.model')
        def prerequisites(self):return ['authored prereq']
        def keepPrereqs(self,value):self.kept.append(value)
    with zipfile.ZipFile(os.path.join(client,'res/packages/scripts.pkg'))as package:
        code=marshal.loads(package.read('scripts/client/DetachedTurret.pyc')[8:])
    classes=dict((c.co_name,c)for c in code.co_consts if isinstance(c,types.CodeType))
    env={'__name__':'DetachedTurret','__builtins__':__builtins__,'BigWorld':BigWorld,
         'ComponentSystem':ComponentSystem,'vehicles':Bag(VehicleDescr=Descriptor),
         '_TurretDetachmentEffects':Effect,'_HitEffects':Effect,'ProjectileAwareEntities':Projectiles,
         'TankPartNames':Bag(TURRET='turret',GUN='gun'),'TankNodeNames':Bag(GUN_JOINT='HP_gunJoint'),
         'SERVER_TICK_LENGTH':.1}
    def original_class(name,bases):
        namespace=types.FunctionType(classes[name],env)()
        result=type(name,bases,namespace);env[name]=result;return result
    Timer=original_class('VehicleEnterTimer',(object,))
    Sync=original_class('SynchronousDetachment',(Timer,))
    Turret=original_class('DetachedTurret',(Entity,ComponentSystem))
    for underwater in (0,1):
        turret=Turret.__new__(Turret)
        turret.vehicleCompDescr=b'original compact descriptor';turret.vehicleID=41;turret.spaceID=7
        turret.isUnderWater=underwater;turret.isCollidingWithWorld=0;turret.matrix=object()
        Turret.__init__(turret)
        native=turret.filter
        assert type(native) is factory and BigWorld.fixture('inspect',native)[3:]==(1,1)
        timer=turret._DetachedTurret__detachConfirmationTimer
        assert type(timer) is Sync and callbacks
        prereqs=turret.prerequisites();assembler=prereqs[0]
        assert assembler.args==('test:turret',7)
        assert assembler.parts==[('turret.model','turret'),('gun.model','HP_gunJoint','gun')]
        assert prereqs[1:]==['authored prereq']
        model=Bag(matrix=None,visible=True);resolved={'test:turret':model}
        turret.onEnterWorld(resolved)
        assert model.matrix is turret.matrix and model.visible is False
        assert turret in Turret.allTurrets and turret in Projectiles.entities
        assert turret._DetachedTurret__vehDescr.loads==['turret.model','gun.model']
        # Drive the original timer to its authored timeout and consume its
        # deferred sticker callback. No copied timer or replacement stock method.
        for identifier,(delay,function)in list(callbacks.items()):
            if delay==0:del callbacks[identifier];function()
        while not timer._SynchronousDetachment__finished:
            identifier=timer._VehicleEnterTimer__callbackId
            delay,function=callbacks.pop(identifier);function()
        assert model.visible is True and not callbacks
        delayer=turret._DetachedTurret__isBeingPulledCallback
        components=list(turret.components)
        turret.onLeaveWorld()
        assert turret not in Turret.allTurrets and turret not in Projectiles.entities
        assert all(component.destroyed for component in components) and delayer.destroyed
        assert turret._DetachedTurret__detachConfirmationTimer is None
        # Entity owns the native Filter beyond onLeaveWorld; its release clears
        # SDK wrapper state and the same wrapper can be safely installed again.
        turret.filter=None
        invalid(lambda: BigWorld.fixture('inspect',native),ValueError)
        BigWorld.fixture('attach',native);BigWorld.fixture('drop',native)
        del native,timer,turret
    print('Original DetachedTurret ctor/prerequisites/enter/timer/collision guard/leave/SDK filter release passed')
