# Exact #1513 PYC, with actual SDK C++ scale wrappers.
from __future__ import print_function
import marshal,zipfile,types,sys,math
from _particle_scale_host import create
z=zipfile.ZipFile(sys.argv[1])
methods={}
for path in ('helpers/PixieBG','helpers/EffectsList'):
 def walk(code,pre=''):
  name=(pre+'.' if pre else '')+code.co_name
  methods[(path,name)]=code
  for child in code.co_consts:
   if isinstance(child,types.CodeType):walk(child,name)
 walk(marshal.loads(z.read('scripts/client/'+path+'.pyc')[8:]))
class Box(object):pass
pending=[]
pixieModule=Box()
def createBG(name,callback):
 if callback is not None:pending.append(callback)
pixieModule.createBG=createBG
restricted=Box();restricted.restrictBySpace=lambda callback:callback
namespace={'__builtins__':__builtins__,'Pixie':pixieModule,'stricted_loading':restricted,'LOG_ERROR':lambda *args:None}
class PixieBG(object):pass
for name in ('__init__','__onLoad','scale','force','destroy','stopLoading'):
 code=methods[('helpers/PixieBG','<module>.PixieBG.'+name)]
 key='_PixieBG'+name if name=='__onLoad' else name
 setattr(PixieBG,key,types.FunctionType(code,namespace,name,(None,None) if name=='__init__' else ()))
class Native(object):
 __slots__=('attached','clears')
 def __init__(self):self.attached=None;self.clears=0
 def clone(self):return type(self)()
 def force(self,value):pass
 def clear(self):self.clears+=1
def ScalableNative():return create()
# Both real SDK property bindings use the original Script float converter.
for meta in (0,1):
 native=create(meta);assert native.scale==1.0
 for value in (0,2.5,-1.0,True,17L,1e-20,1e38,float('inf'),float('-inf'),float('nan')):
  native.scale=value
  if math.isnan(float(value)):assert math.isnan(native.scale)
  elif value in (1e-20,1e38):assert abs(native.scale/value-1)<1e-6
  else:assert native.scale==float(value)
 native.scale=7.25
 class FloatLike(object):
  def __float__(self):return 2.0
 for invalid in (None,(),[],{},'3.0',FloatLike(),-1L,1L<<80):
  try:native.scale=invalid
  except TypeError:pass
  else:raise AssertionError(('invalid scalar accepted',invalid))
  assert native.scale==7.25
 if meta:
  copied=native.clone();assert copied.scale==1.0 and native.scale==7.25
print('Production SDK property: Meta/ordinary default, conversion, rejected values, clone reset passed')
class Node(object):
 def __init__(self):self.attaches=0;self.detaches=0;self.attached=set()
 def attach(self,value):assert id(value) not in self.attached;self.attached.add(id(value));self.attaches+=1
 def detach(self,value):
  if id(value) not in self.attached:raise ValueError('PyModelNode.detach(): The given Attachment is not attached here.')
  self.attached.remove(id(value));self.detaches+=1
class Desc(object):pass
node=None
import random,string
namespace.update({'PixieBG':PixieBG,'_findTargetNode':lambda *args:node,'random':random,'string':string})
for name in ('create','delete','_callbackAfterLoading','_callbackCreate'):
 code=methods[('helpers/EffectsList','<module>._PixieEffectDesc.'+name)]
 setattr(Desc,name,types.FunctionType(code,namespace,name))
def desc():
 result=Desc();result._nodeName=[];result._surfaceMatKinds=None;result._orientByClosestSurfaceNormal=False
 result._havokFiles=None;result._files=['effect.xml'];result._PixieEffectDesc__prototypePixies={};result._force=1
 return result
# Exact stock bytecode, with explicit native lifecycle/async-callback boundaries.
node=Node();effect=desc();entries=[];effect.create(object(),entries,{'scale':2.5})
assert len(entries)==1 and len(pending)==1
try:pending.pop(0)(Native())
except AttributeError:pass
else:raise AssertionError('missing native scale accepted')
assert node.attaches==0 and len(effect._PixieEffectDesc__prototypePixies)==1
try:effect.delete(entries[0],0)
except ValueError as error:assert 'not attached here' in str(error)
else:raise AssertionError('failed pre-attach creation silently detached')
assert entries[0]['pixie'].pixie is not None
try:effect.create(object(),entries,{'scale':2.5})
except AttributeError:pass
else:raise AssertionError('cache-hit scale unexpectedly succeeded')
assert len(entries)==1 and node.attaches==0
print('negative: BG scale failure prevents attach, retained entry delete fails; cached sync failure precedes append')
for value in (0.0,2.5,-1.0):
 node=Node();effect=desc();entries=[];effect.create(object(),entries,{'scale':value});pending.pop(0)(ScalableNative())
 assert node.attaches==1 and entries[0]['pixie'].pixie.scale==value
 assert effect.delete(entries[0],0);assert node.detaches==1 and entries[0]['pixie'] is None
 entries=[];effect.create(object(),entries,{'scale':value})
 assert len(entries)==1 and node.attaches==2 and entries[0]['pixie'].pixie.scale==value
 assert effect.delete(entries[0],0);assert node.detaches==2
print('positive: BG and cached scale->force->attach->delete preserve zero/positive/negative values without Python clamping')
