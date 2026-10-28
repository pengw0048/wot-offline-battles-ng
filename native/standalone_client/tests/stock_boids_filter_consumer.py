from __future__ import print_function
import BigWorld, _boids, os, marshal, zipfile, types, struct

class Vector3(tuple):
    def __new__(cls,*values):
        if not values: values=(0,0,0)
        elif len(values)==1: values=values[0]
        return tuple.__new__(cls,values)
    x=property(lambda self:self[0])
    y=property(lambda self:getattr(self,'_y',self[1]),lambda self,value:setattr(self,'_y',value))
    z=property(lambda self:self[2])
class Physics(object):
    def teleport(self,point): self.point=point
class Entity(object):
    @property
    def filter(self):return self._filter
    @filter.setter
    def filter(self,value):self._filter=value;_boids.attach(value)
    @property
    def physics(self):return self._physics
    @physics.setter
    def physics(self,value):assert value==0;self._physics=Physics()
BigWorld.Entity=Entity
archive=zipfile.ZipFile(os.path.join(os.environ['WOT_0922_CLIENT'],'res/packages/scripts.pkg'))
code=marshal.loads(archive.read('scripts/client/Flock.pyc')[8:])
code=next(c for c in code.co_consts if isinstance(c,types.CodeType) and c.co_name=='Flock')
env={'__name__':'Flock','__builtins__':__builtins__,'BigWorld':BigWorld,'Vector3':Vector3,
     'Math':type('Math',(),{'Vector3':Vector3}),
     'BattleReplay':type('Replay',(),{'g_replayCtrl':type('Controller',(),{'isPlaying':False})()})}
namespace=types.FunctionType(code,env)()
Flock=type('Flock',(Entity,),namespace);env['Flock']=Flock
env['BattleReplay'].g_replayCtrl.isPlaying=True
assert Flock.__new__(Flock).onEnterWorld(None) is None
env['BattleReplay'].g_replayCtrl.isPlaying=False
for rates,centre,radius in [((0.,0.,0.),(0.,0.,0.),0.),((1.,.2,.5),(10.,50.,10.),8.)]:
    value=Flock.__new__(Flock)
    value.models=[];value._loadModels=lambda prereqs:None
    value.flyAroundCenter=Flock.STRATEGY_USUAL_FLY
    value.speedAtBottom=1.25
    value.yawSpeed,value.pitchSpeed,value.rollSpeed=rates
    value.position=Vector3(centre);value.deadZoneRadius=radius;value.height=30
    value._Flock__makeDecision=lambda:None
    value.onEnterWorld(None)
    assert value.filter.yprSpeed==tuple(struct.unpack("<f",struct.pack("<f",v))[0] for v in rates)
    assert value.filter.deadZonePosition==centre
    assert value.filter.deadZoneRadius==radius
    result=_boids.tick(value.filter)
    if rates==(0.,0.,0.):assert result==(0.,0.,0.),result
    else:assert any(v!=0 for v in result),result
    _boids.checkOwner(value.filter)
# Failed typed writes preserve the actual cached/attached values.
p=BigWorld.BoidsFilter()
assert p.yprSpeed==tuple(struct.unpack('<f',struct.pack('<f',v))[0] for v in (1.,.02,.05))
assert p.deadZonePosition==(0.,0.,0.) and p.deadZoneRadius==0.
_boids.attach(p)
for name,bad in [('yprSpeed',None),('yprSpeed',(1,2)),('deadZonePosition','bad'),('deadZoneRadius',None)]:
    previous=getattr(p,name)
    try:setattr(p,name,bad)
    except TypeError:pass
    else:raise AssertionError((name,bad))
    assert getattr(p,name)==previous
p.yprSpeed=(-1.,2.,-3.);p.deadZoneRadius=-1.
assert p.yprSpeed==(-1.,2.,-3.) and p.deadZoneRadius==-1.
_boids.checkOutput()
print('Original Flock.onEnterWorld / actual SDK cache, attach, copy and model consumers PASS')
