from __future__ import print_function
import __builtin__,gc,marshal,sys,types,zipfile
import _splodge_host as host
host.initScripts()
import BigWorld
class Matrix(object):
    def __init__(self,values=None):
        self.values=tuple(values) if values is not None else tuple(1. if i%5==0 else 0. for i in range(16))
local=Matrix([2,0,0,0,0,3,0,0,0,0,4,0,5,6,7,1])
world=Matrix([1,0,0,0,0,2,0,0,0,0,1,0,11,12,13,1])
a=BigWorld.Splodge(local)
assert a.maxLod==50
assert host.inspect(a)==(50.,1.)
for args in ((local,25),(local,0,-2)):
    b=BigWorld.Splodge(*args);assert host.inspect(b)==(float(args[1]),float(args[2]) if len(args)==3 else 1.)
for args in ((),(local,1,2,3),((1,2,3),),(local,'bad'),(local,4,'bad')):
    try:BigWorld.Splodge(*args)
    except TypeError:pass
    else:raise AssertionError('Invalid Splodge constructor accepted')
host.pipeline(0);assert BigWorld.Splodge(local,5,2) is None
host.pipeline(1)
start=host.counts()[0]
host.queue(a,world,50.);assert host.counts()[0]==start
host.flush();assert host.counts()[0]==start+4
assert host.counts()[3]==1
actual=host.lastWorld();expected=list(world.values);expected[13]-=2.
assert list(actual)==expected
assert host.lastLocal()==local.values
for lod,outside,pass_ in ((50.01,True,0),(1,False,0),(1,True,1)):
    before=host.counts()[0];host.queue(a,world,lod,outside,pass_);host.flush();assert host.counts()[0]==before
# maxLod<=0 is not the SDK's old infinite-LOD shortcut.
a.maxLod=0;before=host.counts()[0];host.queue(a,world,1.);host.flush();assert host.counts()[0]==before
a.maxLod=50
before=host.counts()[0];host.queue(a,world,float('nan'));host.flush();assert host.counts()[0]==before+4
a.maxLod=float('nan');before=host.counts()[0];host.queue(a,world,10.);host.flush();assert host.counts()[0]==before+4
a.maxLod=50
for operation in ('detach','leaveWorld'):
    before=host.counts()[0];host.queue(a,world,1.);host.retire(a,operation);host.flush();assert host.counts()[0]==before
before=host.counts()[0];b=BigWorld.Splodge(local);host.queue(b,world,1.);del b;gc.collect();host.flush();assert host.counts()[0]==before
before=host.counts()[0];host.queue(a,world,1.);host.changeSpace();host.flush();assert host.counts()[0]==before
host.queue(a,world,1.);host.resetDevice();host.flush();assert host.counts()[0]==before
host.queue(a,world,1.);host.flush();assert host.counts()[0]==before+4
bounds=host.bounds(a,world)
assert bounds==((14.,16.,16.),(18.,28.,24.)),bounds
host.fini();assert host.counts()[1]==0
# A second battle recreates a fresh renderer and uses the current attachment generation.
host.queue(a,world,1.);host.flush();assert host.counts()[1]==1
host.fini();assert host.counts()[1]==0
if len(sys.argv)>1:
    package=zipfile.ZipFile(sys.argv[1]+'/res/packages/scripts.pkg')
    code=marshal.loads(package.read('scripts/client/vehicle_systems/CompoundAppearance.pyc')[8:])
    cls=next(c for c in code.co_consts if isinstance(c,types.CodeType) and c.co_name=='CompoundAppearance')
    fs=dict((c.co_name,c) for c in cls.co_consts if isinstance(c,types.CodeType))
    class Fields(object):
        def __init__(self,**kw):self.__dict__.update(kw)
    class Node(object):
        def attach(self,item):self.attached=item
    node=Node()
    app=Fields(_CompoundAppearance__currentDamageState=Fields(isCurrentModelDamaged=True),
               _CompoundAppearance__typeDesc=Fields(chassis=Fields(AODecals=(local,),hullPosition=Fields(y=2.5))),
               _CompoundAppearance__splodge=None,
               _CompoundAppearance__compoundModel=Fields(node=lambda part:node))
    g={'BigWorld':BigWorld,'MAX_DISTANCE':37.,'__builtins__':__builtin__,'TankPartNames':Fields(HULL='hull')}
    app._CompoundAppearance__attachSplodge=types.MethodType(types.FunctionType(fs['__attachSplodge'],g),app)
    for damaged in (True, False):
        app._CompoundAppearance__currentDamageState.isCurrentModelDamaged=damaged
        app._CompoundAppearance__computeVehicleHeight=lambda:(0.,2.)
        app._CompoundAppearance__setupHavok=lambda:None
        app._CompoundAppearance__typeDesc.shot=Fields(shell=Fields(caliber=100.))
        app._CompoundAppearance__splodge=None
        types.FunctionType(fs['__setupModels'],g)(app)
        assert host.inspect(node.attached)==(37.,2.5)
        assert node.attached is app._CompoundAppearance__splodge
    host.pipeline(0)
    node=Node();app._CompoundAppearance__splodge=None
    types.FunctionType(fs['__setupModels'],g)(app)
    assert app._CompoundAppearance__splodge is None and not hasattr(node,'attached')
    host.pipeline(1)
print('Splodge factory, actual draw/attachment retirement owner and original PYC passed')
