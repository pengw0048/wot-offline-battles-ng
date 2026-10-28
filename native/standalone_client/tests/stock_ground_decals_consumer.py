from __future__ import print_function
import math
import marshal
import random
import sys
import types
import zipfile
import _screener_host as native

# Tuple-backed vectors exercise the real public Script::setData tuple parser.
class Vector(tuple):
    def __new__(cls,*values):return tuple.__new__(cls,values)
    def __add__(self,other):return Vector(*(a+b for a,b in zip(self,other)))
    def __sub__(self,other):return Vector(*(a-b for a,b in zip(self,other)))
    def __mul__(self,value):return Vector(*(a*value for a in getattr(self,'normalised',self)))
    __rmul__=__mul__
    @property
    def length(self):return math.sqrt(sum(a*a for a in self))
    def normalise(self):self.normalised=tuple(a/self.length for a in self)
    def scale(self,value):return self*value
class Value(object):
    def __init__(self,**values):self.__dict__.update(values)

def child(code,name):
    return next(value for value in code.co_consts if isinstance(value,types.CodeType)and value.co_name==name)
package=zipfile.ZipFile(sys.argv[1])
module=marshal.loads(package.read('scripts/client/helpers/EffectsList.pyc')[8:])
create=child(child(module,'_DecalEffectDesc'),'create')
world=Value(isForwardPipeline=lambda:False,wg_addDecal=native.wg_addDecal)
producer=types.FunctionType(create,{'__builtins__':__builtins__,'True':True,'False':False,'BigWorld':world,'random':random,'DecalMap':Value(g_instance=Value(getIndex=lambda name:{'d':0,'n':1,'s':-1}[name]))})
effect=Value(_size=Vector(1.,2.),_variation=0.,_randomYaw=False,_groupName='impact',_texName='d',_bumpTexName='n',_smTexName='s')
assert set(('_texName','_bumpTexName','_smTexName')).issubset(create.co_names)
args={'start':Vector(0.,1.,0.),'end':Vector(0.,-1.,0.),'yaw':.3}
count=native.emitted();assert producer(effect,None,None,args) is None and native.emitted()==count+1
args['showDecal']=False;assert producer(effect,None,None,args) is None and native.emitted()==count+1
args['showDecal']=True;world.isForwardPipeline=lambda:True
assert producer(effect,None,None,args) is None and native.emitted()==count+1
world.isForwardPipeline=lambda:False;del world.wg_addDecal
try:producer(effect,None,None,args)
except AttributeError:pass
else:raise AssertionError('Missing interface did not reproduce original PYC failure')
print('Original EffectsList._DecalEffectDesc.create: exact 8-argument native call and guards passed')
