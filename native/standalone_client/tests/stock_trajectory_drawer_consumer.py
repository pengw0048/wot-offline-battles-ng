from __future__ import print_function
import BigWorld as B
import gc
import marshal
import os
import types
import weakref
import zipfile


def expect(error,function,*args):
    try:function(*args)
    except error:return
    raise AssertionError('Expected '+error.__name__)

expect(RuntimeError,B.wg_trajectory_drawer)
expect(MemoryError,B.fixture,'fail_init')
expect(RuntimeError,B.wg_trajectory_drawer)
B.fixture('init')
a=B.wg_trajectory_drawer()
assert a is B.wg_trajectory_drawer()
assert type(a).__name__ == 'WGTrajectoryDrawer'
assert type(a).__base__.__name__ == 'PyObjectPlus'
expect(TypeError,type(a))
expect(TypeError,weakref.ref,a)
expect(AttributeError,setattr,a,'unregistered',True)
# These lists and their read-only setters execute the public SDK macros.
assert isinstance(a.__members__,list) and isinstance(a.__methods__,list)
expect(TypeError,setattr,a,'__members__',[])
expect(TypeError,setattr,a,'__methods__',[])
assert a.visible is True
for name,good in [('setParams',(1000.,(0,-10,0),(0,0))),
                  ('setColors',((0,255,0,255),(255,0,0,255),(128,128,128,255))),
                  ('setGetDynamicCollidersCallback',(None,)),('setStrategicMode',(True,)),
                  ('update',((0,0,500),(0,10,0),(0,20,100),.1))]:
    method=getattr(a,name)
    expect(TypeError,method)
    expect(TypeError,method,*(good+(None,)))
    method(*good)
    method(*good,ignored_keyword=True)
expect(TypeError,a.setParams,'bad',(0,-10,0),(0,0))
expect(TypeError,a.setParams,1000,(0,-10),(0,0))
expect(TypeError,a.setParams,1000,(0,-10,0),(0,0,0))
expect(TypeError,a.setColors,(1,2,3),(4,5,6),(7,8,9))
expect(TypeError,a.update,(0,0,500),(0,10,0),(0,20,100),'bad')
a.visible=False
assert not a.visible
B.fixture('tick');B.fixture('draw')
assert B.fixture('inspect')[4:]==(0,0,0)
a.visible=True
B.fixture('tick');B.fixture('draw')
assert B.fixture('inspect')[4:]==(2,2,2)
B.fixture('reset');B.fixture('draw')
assert B.fixture('inspect')[4:]==(4,4,4)

calls=[]
def collider(start,end,filter):
    assert filter is None
    calls.append((start,end))
    return (0.1,None)
def selection(start,end):
    assert start==(0,10,0)
    return [collider]
a.setGetDynamicCollidersCallback(selection)
a.update((0,0,500),(0,10,0),(0,20,100),.1)
assert calls
assert B.fixture('callback') is selection
# The native owner snapshots and retains the selected colliders. A callback
# may clear the producer's list without invalidating the remaining callbacks.
selected=[]
visited=[]
def clears_selection(*unused):
    visited.append('clear')
    del selected[:]
    return None
class RetainedCollider(object):
    def __call__(self,*unused):
        visited.append('retained')
        return (0.1,None)
selected.extend([clears_selection,RetainedCollider()])
a.setGetDynamicCollidersCallback(lambda *unused:selected)
a.update((0,0,500),(0,10,0),(0,20,100),.1)
assert not selected
assert visited[:2]==['clear','retained']
assert visited.count('clear')==visited.count('retained')
# A selection/collider error is local, prints and clears the Python error;
# the scene query and the next update still work.
a.setGetDynamicCollidersCallback(lambda start,end:[None,lambda *args:None,lambda *args:()])
a.update((0,0,500),(0,10,0),(0,20,100),.1)
a.setGetDynamicCollidersCallback(lambda start,end:None)
a.update((0,0,500),(0,10,0),(0,20,100),.1)
a.setGetDynamicCollidersCallback(None)
a.update((0,0,500),(0,10,0),(0,20,100),.1)

# Mutating parameters or recursively updating from a Python collider retires
# the outer calculation; it cannot overwrite the newer interpolation state.
before=B.fixture('inspect')[:4]
def change_parameters(*unused):
    a.setParams(1000,(0,-30,0),(0,0))
    return []
a.setGetDynamicCollidersCallback(change_parameters)
a.update((0,0,200),(0,20,0),(0,10,100),.1)
assert B.fixture('inspect')[:4]==before
def change_from_collider(*unused):
    a.setParams(1000,(0,-10,0),(0,0))
    return (.1,)
a.setGetDynamicCollidersCallback(lambda *unused:[change_from_collider])
a.update((0,0,200),(0,20,0),(0,10,100),.1)
assert B.fixture('inspect')[:4]==before
def recursive_selection(*unused):
    a.setGetDynamicCollidersCallback(None)
    a.update((0,0,100),(0,30,0),(0,10,100),.1)
    return []
a.setGetDynamicCollidersCallback(recursive_selection)
a.update((0,0,500),(0,10,0),(0,20,100),.1)
recursive=B.fixture('inspect')[:4]
a.update((0,0,100),(0,30,0),(0,10,100),.1)
assert B.fixture('inspect')[:4]==recursive

# Drive the original #1513 producer functions, rather than copied Python calls.
client=os.environ.get('WOT_0922_CLIENT')
if client:
    z=zipfile.ZipFile(os.path.join(client,'res/packages/scripts.pkg'))
    root=marshal.loads(z.read('scripts/client/AvatarInputHandler/control_modes.pyc')[8:])
    def walk(code):
        for value in code.co_consts:
            if isinstance(value,types.CodeType):
                yield value
                for child in walk(value):yield child
    codes=list(walk(root))
    changed=next(c for c in codes if c.co_name=='__onGunShotChanged')
    class Obj(object):pass
    player=Obj();descriptor=Obj();descriptor.shot=Obj()
    descriptor.shot.maxDistance=1000;descriptor.shot.gravity=10
    player.getVehicleDescriptor=lambda:descriptor
    math=Obj();math.Vector3=lambda *args:args
    self=Obj();self._TrajectoryControlMode__trajectoryDrawer=a;self._aimOffset=(0,0)
    B.player=lambda:player
    types.FunctionType(changed,{'BigWorld':B,'Math':math})(self)
    print('Original control_modes.pyc shot producer passed')
    root=marshal.loads(z.read('scripts/client/AvatarInputHandler/AimingSystems/ArtyAimingSystem.pyc')[8:])
    update_matrix=next(c for c in walk(root) if c.co_name=='_updateMatrix')
    import math as maths
    class Vec(object):
        def __init__(self,*args):
            if len(args)==1:args=tuple(args[0])
            self.x,self.y,self.z=map(float,args)
        def __len__(self):return 3
        def __getitem__(self,i):return (self.x,self.y,self.z)[i]
        def __sub__(self,b):return Vec(self.x-b.x,self.y-b.y,self.z-b.z)
        def __add__(self,b):return Vec(self.x+b.x,self.y+b.y,self.z+b.z)
        def __mul__(self,f):return Vec(self.x*f,self.y*f,self.z*f)
        @property
        def lengthSquared(self):return self.x*self.x+self.y*self.y+self.z*self.z
        def normalise(self):
            length=maths.sqrt(self.lengthSquared)
            if length:self.x/=length;self.y/=length;self.z/=length
        @property
        def yaw(self):return maths.atan2(self.x,self.z)
        @property
        def pitch(self):return maths.atan2(self.y,maths.sqrt(self.x*self.x+self.z*self.z))
    attached=Obj();attached.position=Vec(0,0,0)
    player.getVehicleAttached=lambda:attached
    player.spaceID=1
    player.gunRotator=Obj()
    player.gunRotator.getShotParams=lambda *args:(Vec(0,10,0),Vec(10,10,100),Vec(0,-10,0))
    B.wg_collideSegment=lambda *args:None
    aiming=Obj();aiming._planePosition=Vec(100,0,300);aiming._clampToArenaBB=lambda:None
    aiming._ArtyAimingSystem__aimMatrix=Obj()
    aiming._ArtyAimingSystem__aimMatrix.setTranslate=lambda value:None
    utility=Obj();utility.createRTMatrix=lambda angles,position:(angles,position)
    types.FunctionType(update_matrix,{'__builtins__':__builtins__,'BigWorld':B,'Vector3':Vec,'_MINIMAL_AIMING_RADIUS_SQ':1.,
        '_MINIMAL_AIMING_RADIUS':1.,'SERVER_TICK_LENGTH':.1,'SHELL_TRAJECTORY_EPSILON_CLIENT':.03,
        'mathUtils':utility})(aiming)
    assert abs(aiming._matrix[1][1])<.0001
    print('Original ArtyAimingSystem.pyc simulation consumer passed')

# Python aliases cannot outlive and resurrect the GUI's native owner.
args=((0,10,0),(0,10,100),(0,-10,0),.1,.03)
for count in (0,4,8):expect(TypeError,B.wg_simulateProjectileTrajectory,*((args+(128,0,None))[:count]))
result=B.wg_simulateProjectileTrajectory(*args)
assert len(result)==3 and 2.7<result[0]<2.8
assert abs(result[1][1])<.0001 and result[2][1]<0
assert B.wg_simulateProjectileTrajectory(*(args+(1,))) is None
assert B.wg_simulateProjectileTrajectory(*(args+(128,1))) is None
expect(ValueError,B.wg_simulateProjectileTrajectory,*(args[:3]+(0,.03)))
expect(ValueError,B.wg_simulateProjectileTrajectory,*(args[:3]+(.1,0)))
expect(TypeError,B.wg_simulateProjectileTrajectory,*(args+(None,)))
B.fixture('miss')
assert B.wg_simulateProjectileTrajectory(*args) is None
print('Projectile simulation CPython consumer passed')

B.fixture('fini');B.fixture('fini')
expect(RuntimeError,lambda:a.visible)
expect(RuntimeError,a.setParams,1000,(0,-10,0),(0,0))
expect(RuntimeError,B.wg_trajectory_drawer)
B.fixture('init')
assert a is not B.wg_trajectory_drawer()
# Re-entrant teardown from the callback must not free the running owner.
b=B.wg_trajectory_drawer();b.setParams(1000,(0,-10,0),(0,0))
b.setGetDynamicCollidersCallback(lambda *args:B.fixture('fini'))
b.update((0,0,500),(0,10,0),(0,20,100),.1)
expect(RuntimeError,lambda:b.visible)
del a,b;gc.collect()
print('TrajectoryDrawer CPython consumer passed')
