from __future__ import print_function
import BigWorld as B
import marshal
import os
import types
import zipfile


def expect(error, function, *args):
    try: function(*args)
    except error: return
    raise AssertionError('Expected '+error.__name__)

for args in ((), ((0,20,0),), ((0,20,0),1,None)):
    expect(TypeError,B.wg_addWaterRipples,*args)
for args in (((0,20),1),('bad',1),((0,20,0),'bad')):
    expect(TypeError,B.wg_addWaterRipples,*args)
assert B.fixture('inspect')==(0,0,0.)
assert B.wg_addWaterRipples((0,20,0),2.) is None
assert B.fixture('inspect')==(2,0,0.)
B.fixture('render')
assert B.fixture('inspect')==(0,2,200.)
# Repeated propagation/impulse passes reselect the same real material after
# End clears the active binding. No writes can land on the definition.
B.wg_addWaterRipples((1,20,1),4.)
B.fixture('render')
assert B.fixture('inspect')==(0,4,200.)
B.fixture('clear')
for mode in (1,2):
    B.fixture('mode',mode)
    B.wg_addWaterRipples((0,20,0),1)
    assert B.fixture('inspect')[0]==1
    B.fixture('clear')
B.fixture('mode',0)
for position in ((1000,20,0),(0,100,0),(100,20,100),(0,0,0)):
    B.wg_addWaterRipples(position,2)
    assert B.fixture('inspect')[0]==0
# The frame mark clears superseded impulses once, preserving every new event
# added in the same frame, including the original callback burst.
B.fixture('frame')
B.wg_addWaterRipples((0,20,0),2)
B.wg_addWaterRipples((1,20,1),2)
assert B.fixture('inspect')[0]==4
B.fixture('frame')
B.wg_addWaterRipples((2,20,2),2)
assert B.fixture('inspect')[0]==2
B.fixture('clear')
B.fixture('mode',3)
B.wg_addWaterRipples((0,20,0),2)
B.fixture('render')
assert B.fixture('inspect')==(2,0,0.)
B.fixture('mode',0)
B.fixture('render')
assert B.fixture('inspect')==(0,2,200.)
B.fixture('clear')

client=os.environ.get('WOT_0922_CLIENT')
if client:
    with zipfile.ZipFile(os.path.join(client,'res/packages/scripts.pkg')) as package:
        root=marshal.loads(package.read('scripts/client/ProjectileMover.pyc')[8:])
    def walk(code):
        for value in code.co_consts:
            if isinstance(value,types.CodeType):
                yield value
                for child in walk(value):yield child
    code=next(c for c in walk(root) if c.co_name=='__addWaterRipples')
    pending=[]
    B.callback=lambda delay,callback:pending.append((delay,callback))
    class Mover(object):pass
    original=types.FunctionType(code,{'__builtins__':__builtins__,'BigWorld':B})
    Mover._ProjectileMover__addWaterRipples=original
    original(Mover(),(0,20,0),3.,5)
    while pending:
        delay,callback=pending.pop(0)
        assert delay==0
        callback()
    assert B.fixture('inspect')[0]==12
    B.fixture('render')
    assert B.fixture('inspect')==(0,12,200.)
    print('Original ProjectileMover.pyc six-ripple callback producer passed')
B.fixture('remove')
assert B.wg_addWaterRipples((0,20,0),1) is None
assert B.fixture('inspect')[0]==0
print('Water ripple cell and shader consumer passed')
