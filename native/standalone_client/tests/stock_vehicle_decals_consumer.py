from __future__ import print_function
import marshal
import sys
import types
import zipfile
import _vehicle_decals_host as host
host.initScripts()
import BigWorld

class Event(object):
    def __init__(self): self.handlers=[]
    def __iadd__(self,value): self.handlers.append(value); return self
    def __isub__(self,value):
        if value in self.handlers: self.handlers.remove(value)
        return self
class Value(object):
    def __init__(self,**values): self.__dict__.update(values)
settings=Value(onSettingsChanged=Event())
for name in ('helpers','skeletons','skeletons.account_helpers','skeletons.account_helpers.settings_core',
             'vehicle_systems','vehicle_systems.tankStructure'):
    module=types.ModuleType(name);module.__path__=[];sys.modules[name]=module
sys.modules['helpers'].dependency=Value(descriptor=lambda _:settings)
sys.modules['skeletons.account_helpers.settings_core'].ISettingsCore=object
sys.modules['vehicle_systems.tankStructure'].TankPartNames=Value(HULL='hull',TURRET='turret')
BigWorld.isForwardPipeline=lambda:False
BigWorld.isShadowsEnabled=lambda:True
BigWorld.isSSAOEnabled=lambda:True
package=zipfile.ZipFile(sys.argv[1])
source=marshal.loads(package.read('scripts/client/vehicle_systems/components/vehicleDecal.pyc')[8:])
module=types.ModuleType('vehicleDecal');exec source in module.__dict__
VehicleDecal=module.VehicleDecal
class Node(object):
    def __init__(self):self.items=[]
    def attach(self,value):
        assert type(value).__name__ in ('WGOcclusionDecal','WGShadowForwardDecal')
        self.items.append(value)
    def detach(self,value):self.items.remove(value)
class Model(object):
    def __init__(self):self.root=Node();self.hull=Node();self.turret=Node()
    def node(self,name):return getattr(self,name)
transform=host.matrix()
reader_code=marshal.loads(package.read('scripts/common/items/vehicles.pyc')[8:])
reader_code=next(code for code in reader_code.co_consts if isinstance(code,types.CodeType) and code.co_name=='_readAODecals')
section=Value(has_key=lambda key:key=='AODecals')
subsection=Value(readMatrix=lambda name:transform if name=='transform' else None)
reader=types.FunctionType(reader_code,{'tuple':tuple,'_xml':Value(getChildren=lambda *args:[('decal',subsection)])})
authored=reader(None,section,'AODecals');assert authored==(transform,)
# Missing native factory aborts the exact producer before attachment.
original_factory=BigWorld.WGOcclusionDecal;del BigWorld.WGOcclusionDecal
model=Model();part=Value(AODecals=authored);appearance=Value(compoundModel=model,typeDescriptor=Value(chassis=part,hull=part,turret=part))
partial=VehicleDecal(appearance)
try:partial.create()
except AttributeError:pass
else:raise AssertionError('Missing API did not reproduce the producer failure')
partial.destroy();assert not settings.onSettingsChanged.handlers
BigWorld.WGOcclusionDecal=original_factory
for shadows,ssao,expected in ((True,True,2),(False,True,1),(True,False,0)):
    BigWorld.isShadowsEnabled=lambda:shadows
    BigWorld.isSSAOEnabled=lambda:ssao
    model=Model();part=Value(AODecals=authored);appearance=Value(compoundModel=model,
        typeDescriptor=Value(chassis=part,hull=part,turret=part))
    effect=VehicleDecal(appearance);assert VehicleDecal.getDecalType()==expected
    effect.create();effect.attach()
    assert len(model.root.items)==(1 if expected else 0)
    assert len(model.hull.items)==len(model.root.items)==len(model.turret.items)
    effect.detach();effect.detach();assert not model.root.items
    effect.attach();effect.destroy();assert not model.root.items and not settings.onSettingsChanged.handlers
# Production macro parser: strict positional counts and ignored keyword args.
for factory in (BigWorld.WGOcclusionDecal,BigWorld.WGShadowForwardDecal):
    try:factory(1)
    except TypeError:pass
    else:raise AssertionError('Factory accepted arguments')
ao=BigWorld.WGOcclusionDecal();shadow=BigWorld.WGShadowForwardDecal()
assert ao.create('','','','texture',0,4,30,4294967295L,2) is True
assert shadow.setup('texture',6,0,62) is True
assert ao.setLocalTransform(transform) is None
assert shadow.collideWithScene(True) is None
assert shadow.setup('texture',6,0,62,ignored=True) is True
for target,name,count in ((ao,'create',9),(ao,'setLocalTransform',1),(shadow,'setup',4),(shadow,'collideWithScene',1)):
    for argc in (0,count+1):
        try:getattr(target,name)(*([None]*argc))
        except TypeError:pass
        else:raise AssertionError('Wrong arity accepted')
for method,args in ((ao.setLocalTransform,(None,)),(shadow.setup,('texture',object(),0,62))):
    try:method(*args)
    except TypeError:pass
    else:raise AssertionError('Wrong type accepted')
events=host.events()
assert any(event==('configure','maps/spots/TankOcclusion/TankOcclusionMap.dds',2,0,30) for event in events)
assert any(event==('configure','maps/spots/TankOcclusion/TankOcclusionMap.dds',2,0,62) for event in events)
assert any(event==('configure','maps/spots/TankOcclusion/TankOcclusionMap.dds',6,0,62) for event in events)
print('Original VehicleDecal PYC creation, part attachment, detach and destroy passed')
