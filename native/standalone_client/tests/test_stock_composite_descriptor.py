"""Read the exact #1513 Composite descriptor through production native readers."""
import os
import json
import sys
import zipfile
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_stock_vehicle_physics_owner import block
from test_stock_wire_codec import python27_audit_runtime

ROOT = Path(__file__).resolve().parents[1]

NATIVE = r'''
import sys,marshal,types,zipfile,dis
import composite_descriptor_test as native
archive=zipfile.ZipFile(sys.argv[1]+'/res/packages/scripts.pkg')
def find(code,name):
 if code.co_name==name:return code
 for child in code.co_consts:
  if isinstance(child,types.CodeType):
   value=find(child,name)
   if value:return value
vehicles=marshal.loads(archive.read('scripts/common/items/vehicles.pyc')[8:])
constants=marshal.loads(archive.read('scripts/common/constants.pyc')[8:])
mode=eval(find(constants,'VEHICLE_MODE'),dict(__name__='constants'));Mode=type('VEHICLE_MODE',(object,),mode)
Siege=type('VEHICLE_SIEGE_STATE',(object,),eval(find(constants,'VEHICLE_SIEGE_STATE'),dict(__name__='constants')))
assert (Mode.DEFAULT,Mode.SIEGE)==(0,1)
ns=dict(__name__='items.vehicles',VEHICLE_MODE=Mode,VEHICLE_SIEGE_STATE=Siege,IS_CLIENT=True)
Composite=type('CompositeVehicleDescriptor',(object,),eval(find(vehicles,'CompositeVehicleDescriptor'),ns))
# The actual component reader assigns a tuple of movement and rotation factors.
reader=find(vehicles,'_readChassis')
assert reader is not None and 'shotDispersionFactors/vehicleMovement' in reader.co_consts
code=reader.co_code;store=code.index(chr(dis.opmap['STORE_ATTR'])+chr(reader.co_names.index('shotDispersionFactors'))+'\x00')
assert code[store-6:store-3]==chr(dis.opmap['BUILD_TUPLE'])+'\x02\x00'
class O(object):
 def __init__(self,**values):self.__dict__.update(values)
def descriptor(factor,center):
 pitch=O(wheelsCorrectionAngles=O(pitchMin=-.2,pitchMax=.2),wheelsCorrectionSpeed=.07,wheelCorrectionCenterZ=center,isAvailable=True,isEnabled=True)
 return O(maxHealth=2000,chassis=O(shotDispersionFactors=(factor,factor+1),hitTester=object()),hull=O(hitTester=object()),turret=O(rotationSpeed=.5,hitTester=object()),gun=O(shotDispersionFactors=dict(turretRotation=factor+2),shotDispersionAngle=.3,rotationSpeed=.6,hitTester=object(),pitchLimits=dict(absolute=(-center,center))),type=O(hullAimingParams=dict(pitch=pitch)))
normal=descriptor(.1,.2);siege=descriptor(.7,.4);proxy=Composite(normal,siege)
gun=O(loadout=dict(crew_factor=1.,gun_rotation_factor=1.),shots=[dict(shell=dict(compactDescr=1)),dict(shell=dict(compactDescr=2))])
constants=O(VEHICLE_SETTING=O(CURRENT_SHELLS=3),EQUIPMENT_STAGES=O(READY=7,COOLDOWN=8,EXHAUSTED=9))
def switch(value):
 def apply():proxy.onSiegeStateChanged(Siege.ENABLED if value else Siege.DISABLED)
 return apply
assert native.direct(proxy,'chassis') is None # Entity guard stays strict.
assert native.inspect(normal,gun,constants)==(.1,1.1,2.1)
rows=native.sequence(proxy,gun,constants,[switch(0),switch(1),switch(0)])
assert [r['factors'] for r in rows]==[(.1,1.1,2.1),(.7,1.7,2.7),(.1,1.1,2.1)],rows
assert [r['pitch'] for r in rows]==[(.2,-.2,.2),(.4,-.4,.4),(.2,-.2,.2)],rows
assert all(r['pitch_valid'] for r in rows),rows
# Resolve once: a component getter synchronously changes the same proxy mode.
class Reenter(object):
 def __init__(self,base):
  self.__dict__.update(base.__dict__);self.__dict__.pop('chassis');self.base=base
 @property
 def chassis(self):
  switch(1)();return self.base.chassis
proxy.__dict__['_CompositeVehicleDescriptor__vehicleDescr']=Reenter(normal)
switch(0)();assert native.inspect(proxy,gun,constants)==(.1,1.1,2.1)
assert native.inspect(proxy,gun,constants)==(.7,1.7,2.7)
proxy.__dict__['_CompositeVehicleDescriptor__vehicleDescr']=normal
for invalid in (None,True,1.,-1,2,'1'):
 proxy.__dict__['_CompositeVehicleDescriptor__vehicleMode']=invalid
 try:native.inspect(proxy,gun,constants)
 except RuntimeError:pass
 else:raise AssertionError('Invalid Composite mode accepted: '+repr(invalid))
proxy.__dict__['_CompositeVehicleDescriptor__vehicleMode']=0;proxy.__dict__['_CompositeVehicleDescriptor__vehicleDescr']=None
try:native.inspect(proxy,gun,constants)
except RuntimeError:pass
else:raise AssertionError('Incomplete Composite accepted')
# Arbitrary new-style objects retain their dynamic-fallback guard.
class Entity(object):
 def __getattr__(self,name):raise AssertionError('Entity dynamic fallback invoked: '+name)
assert native.direct(Entity(),'chassis') is None

# Read actual default/siege XML factors through the actual #1513 assignment and
# _xml reader, rather than guessing a new representation from packed data.
import json,copy,math
sys.path.insert(0,sys.argv[3])
from vehicle_physics_producer import load,Section
unused,scope=load(sys.argv[1])
movement=reader.co_consts.index('shotDispersionFactors/vehicleMovement')
load=chr(dis.opmap['LOAD_CONST'])+chr(movement)+'\x00'
position=code.index(load)
xml=chr(dis.opmap['LOAD_GLOBAL'])+chr(reader.co_names.index('_xml'))+'\x00'
start=code.rfind(xml,0,position)
none=reader.co_consts.index(None)
fragment=code[start:store+3]+chr(dis.opmap['LOAD_CONST'])+chr(none)+'\x00'+chr(dis.opmap['RETURN_VALUE'])
assignment=types.CodeType(3,reader.co_nlocals,reader.co_stacksize,reader.co_flags,fragment,reader.co_consts,reader.co_names,reader.co_varnames,reader.co_filename,'stock_chassis_factors',reader.co_firstlineno,'')
read_factors=types.FunctionType(assignment,scope)
for resource in json.load(open(sys.argv[2])):
 component=O();read_factors(('resource',resource['n']),Section(resource),component)
 assert isinstance(component.shotDispersionFactors,tuple) and len(component.shotDispersionFactors)==2
 value=descriptor(.3,.2);value.chassis.shotDispersionFactors=component.shotDispersionFactors
 assert native.inspect(value,gun,constants)[:2]==component.shotDispersionFactors
 broken=copy.deepcopy(resource);broken['c'][0]['c'][0]['v']='-1'
 try:read_factors(('invalid-resource',resource['n']),Section(broken),O())
 except Exception:pass
 else:raise AssertionError('Negative stock chassis factor accepted')
for bad in (None,(),(.1,),(.1,.2,.3),(.1,float('nan'))):
 normal.chassis.shotDispersionFactors=bad
 try:native.inspect(normal,gun,constants)
 except RuntimeError:pass
 else:raise AssertionError('Malformed stock factors accepted')
normal.chassis.shotDispersionFactors=(.1,1.1)
proxy.__dict__['_CompositeVehicleDescriptor__vehicleDescr']=normal
proxy.__dict__['_CompositeVehicleDescriptor__siegeDescr']=None
switch(0)();assert native.inspect(proxy,gun,constants)==(.1,1.1,2.1)

print('Actual #1513 Composite normal/siege/reentry, metadata and pitch refresh, gun frontier and invalid modes passed')
'''


class CompositeDescriptorTests(unittest.TestCase):
    def test_actual_stock_proxy_and_production_readers(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for exact Composite bytecode')
        python = python27_audit_runtime()
        prefix = Path(subprocess.check_output([python, '-c', 'import sys;print(sys.prefix)'], text=True).strip())
        with tempfile.TemporaryDirectory(prefix='stock-composite-descriptor-') as temporary:
            folder = Path(temporary)
            pitch_header = (ROOT / 'bigworld/stock_native_client_drive_presentation.hpp').read_text()
            (folder / 'descriptor_hydraulic_pitch.inc').write_text(block(pitch_header, 'struct HydraulicPitch {') + ';')
            weapon = (ROOT / 'bigworld/stock_native_client_weapon_runtime_python.cpp').read_text()
            presentation = (ROOT / 'bigworld/stock_native_client_self_presentation.cpp').read_text()
            for name, source, signature in (
                ('field', weapon, 'p::Ref field('), ('metadata', weapon, 'Metadata metadata('),
                ('equipment', weapon, 'Equipment equipmentRow('), ('replace', weapon, 'void replace('),
                ('refresh', weapon, 'void refreshOwner('), ('pitch', presentation, 'void readPitch(')):
                (folder / ('descriptor_' + name + '.inc')).write_text(block(source, signature))
            command = ['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                       '-Wno-register', '-DWOT_CLIENT_REPLICA_HOST_BUILD', '-bundle', '-undefined', 'dynamic_lookup',
                       '-isystem', str(prefix / 'include/python2.7'), '-I' + str(folder),
                       '-I' + str(ROOT / 'bigworld'), '-I' + str(ROOT.parent)]
            sources = ['tests/stock_composite_descriptor_host.cpp', 'bigworld/stock_native_client_weapon_runtime.cpp',
                       'bigworld/stock_native_client_weapon.cpp', 'bigworld/stock_native_client_state.cpp']
            result = subprocess.run(command + [str(ROOT / name) for name in sources] + ['-o', str(folder / 'composite_descriptor_test.so')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
            from packed_xml import read_packed_xml
            def json_node(name, node):
                if hasattr(node, 'value_type'):
                    if node.value_type == 0:
                        node = node.value
                    else:
                        value = node.value.decode() if isinstance(node.value, bytes) else node.value
                        return dict(n=name, v=value, c=[])
                return dict(n=name, v='', c=[json_node(key.decode(), value) for key, value in node.children])
            resources = []
            with zipfile.ZipFile(Path(client) / 'res/packages/scripts.pkg') as archive:
                for name in ('S11_Strv_103B', 'S11_Strv_103B_siege_mode', 'S22_Strv_S1', 'S22_Strv_S1_siege_mode'):
                    tree = read_packed_xml(archive.read('scripts/item_defs/vehicles/sweden/' + name + '.xml'))
                    chassis = next(row.value for key, row in tree.children if key == b'chassis')
                    for key, row in chassis.children:
                        if row.value_type != 0:
                            continue
                        factors = [json_node('shotDispersionFactors', node) for key, node in row.value.children if key == b'shotDispersionFactors']
                        if factors:
                            resources.append(dict(n=name + '/' + key.decode(), v='', c=factors))
            self.assertEqual(len(resources), 4)
            (folder / 'resources.json').write_text(json.dumps(resources))
            result = subprocess.run([python, '-B', '-c', NATIVE, client, str(folder / 'resources.json'), str(ROOT / 'tests')], cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

if __name__ == '__main__':
    unittest.main()
