"""Execute the production CPython 2.7 aiming binding and #1513 chassis target.

D3DX YPR is an explicit host boundary here. The original x86 chassis function
receives the identical recorded boundary matrix; all remaining target arithmetic
executes its original instructions, rather than a second formula in Python.
"""
from pathlib import Path
import json
import os
import struct
import subprocess
import tempfile
import unittest

from test_stock_vehicle_physics_owner import block
from test_vehicle_physics_configuration import probe_source
from test_stock_wire_codec import python27_audit_runtime

ROOT = Path(__file__).resolve().parents[1]

HOST = r'''
namespace stock_vehicle_native_math {
wot::VehicleMatrix last;
wot::VehicleMatrix hullAimingDelta(float yaw,float pitch){
 // Declared host stand-in for D3DXMatrixRotationYawPitchRoll(yaw,pitch,0).
 const float cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
 last=wot::VehicleMatrix{{{cy,0,-sy,0},{sy*sp,cp,cy*sp,0},{sy*cp,-sp,cy*cp,0},{0,0,0,1}}};
 return last;
}
}
struct PyWGVehiclePhysics {
 wot::VehiclePhysics&core_;
 PyObject*py_setHullAimingAnglesDelta(PyObject*args);
};
@METHOD@
PyObject*hullAim(PyObject*,PyObject*args){
 int handle;PyObject*arguments;
 if(!PyArg_ParseTuple(args,"iO",&handle,&arguments))return NULL;
 if(handle<0||size_t(handle)>=cores.size()||!cores[handle]){
  PyErr_SetString(PyExc_KeyError,"no core");return NULL;
 }
 PyWGVehiclePhysics object{*cores[handle]};
 return object.py_setHullAimingAnglesDelta(arguments);
}
PyObject*aimState(PyObject*,PyObject*args){
 auto*target=core(args);if(!target||!target->tank())return NULL;
 PyObject*rows=PyList_New(3);
 const wot::VehicleMatrix*matrices[]={&target->rigid_body().transform,
  &stock_vehicle_native_math::last,&target->tank()->chassis.matrix_d0};
 for(int r=0;r<3;++r){
  PyObject*values=PyList_New(16);
  for(int i=0;i<16;++i)PyList_SET_ITEM(values,i,PyFloat_FromDouble(matrices[r]->rows[i/4][i%4]));
  PyList_SET_ITEM(rows,r,values);
 }
 return rows;
}
PyObject*aimPose(PyObject*,PyObject*args){
 int handle;PyObject*rows;
 if(!PyArg_ParseTuple(args,"iO",&handle,&rows))return NULL;
 if(handle<0||size_t(handle)>=cores.size()||!cores[handle])return NULL;
 wot::VehicleMatrix pose;
 for(int i=0;i<16;++i){
  PyObjectPtr value(PySequence_GetItem(rows,i),PyObjectPtr::STEAL_REFERENCE);
  if(!value)return NULL;
  pose.rows[i/4][i%4]=float(PyFloat_AsDouble(value.get()));
  if(PyErr_Occurred())return NULL;
 }
 cores[handle]->set_tank_transform(pose,math);
 Py_RETURN_NONE;
}
'''

FIXTURE = r'''
import json,sys
import stock_physics_probe as native
from vehicle_physics_configuration_inputs import make_configuration
handle=native.create()
try:native.hull_aim(handle,(0.,0.))
except ValueError:pass
else:raise AssertionError('unconfigured physics accepted')
native.configure(handle,make_configuration())
for arguments in ((),(0.,),(0.,0.,0.),('bad',0.),(0.,object())):
 try:native.hull_aim(handle,arguments)
 except TypeError:pass
 else:raise AssertionError('malformed angle call accepted')
rows=[]
# Nonzero hull yaw, pitch, roll and translation expose order/frame mistakes.
poses=[[1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,10.,-3.,5.,1.],
       [.8,.1,-.3,0.,-.2,.9,.4,0.,.3,-.4,.7,0.,-25.,6.,31.,1.]]
for pose in poses:
 native.aim_pose(handle,pose)
 pose=native.aim_state(handle)[0]
 for yaw,pitch in ((0.,0.),(.3,0.),(0.,-.2),(-.7,.4),(True,False)):
  before=native.summary(handle)
  assert native.hull_aim(handle,(yaw,pitch)) is None
  first=native.aim_state(handle)
  assert native.hull_aim(handle,(yaw,pitch)) is None
  assert native.aim_state(handle)==first,'aiming accumulated between calls'
  assert native.summary(handle)==before,'aiming changed configure/physics state'
  assert first[0]==pose,'aiming moved the rigid body'
  rows.append(dict(yaw=yaw,pitch=pitch,hull=first[0],delta=first[1],target=first[2]))
json.dump(rows,open(sys.argv[1],'w'))
print('Production hull aiming binding: configured owner, argument shape, yaw/pitch, repeat and rigid-body ownership passed')
'''


class StockVehicleHullAimingTests(unittest.TestCase):
    def test_binding_and_original_chassis_target(self):
        python = python27_audit_runtime()
        prefix = Path(subprocess.check_output([python, '-c', 'import sys;print(sys.prefix)'], text=True).strip())
        source = (ROOT / 'bigworld/vehicle_physics_binding.cpp').read_text()
        self.assertIn('PY_METHOD( setHullAimingAnglesDelta )', source)
        host = HOST.replace('@METHOD@', block(source,
            'PyObject * PyWGVehiclePhysics::py_setHullAimingAnglesDelta('))
        text = probe_source().replace('PyMethodDef methods[]={', host + '\nPyMethodDef methods[]={'
            '{"hull_aim",hullAim,METH_VARARGS,NULL},{"aim_state",aimState,METH_VARARGS,NULL},'
            '{"aim_pose",aimPose,METH_VARARGS,NULL},')
        with tempfile.TemporaryDirectory(prefix='physics-hull-aiming-') as temporary:
            root = Path(temporary)
            (root / 'probe.cpp').write_text(text)
            (root / 'fixture.py').write_text(FIXTURE)
            command = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-register',
                       '-ffp-contract=off', '-fno-fast-math', '-fsanitize=address,undefined',
                       '-I' + str(prefix / 'include/python2.7'), '-I' + str(ROOT / 'include'),
                       '-L' + str(prefix / 'lib'), '-lpython2.7', '-Wl,-rpath,' + str(prefix / 'lib'),
                       str(root / 'probe.cpp'), '-o', str(root / 'probe')]
            command += [str(ROOT / 'src' / name) for name in
                        ('vehicle_physics.cpp', 'vehicle_dynamics.cpp', 'point_velocity.cpp')]
            subprocess.run(command, check=True)
            subprocess.run([str(root / 'probe'), str(root / 'fixture.py'), str(root / 'result.json')],
                           env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1', ASAN_OPTIONS='detect_leaks=0',
                                    PYTHONPATH=str(ROOT / 'tests')), check=True)
            rows = json.loads((root / 'result.json').read_text())
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Binding passed; WOT_0922_CLIENT required for original instruction comparison')
        from stock_tree_lod_oracle import OriginalTreeInstructionOracle
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP
        oracle = OriginalTreeInstructionOracle(Path(client) / 'WorldOfTanks.exe')
        emulator = oracle.emulator
        chassis, hull = oracle.allocate(0x148), oracle.allocate(64)
        emulator.mem_write(chassis + 0x94, struct.pack('<I', hull))
        pending = {}
        def rotation(uc, address, size, context):
            stack = uc.reg_read(UC_X86_REG_ESP)
            output, yaw, pitch, roll = struct.unpack('<4I', uc.mem_read(stack + 4, 16))
            self.assertEqual((yaw, pitch, roll), pending['angles'])
            uc.mem_write(output, struct.pack('<16f', *pending['delta']))
            uc.reg_write(UC_X86_REG_EAX, output)
            uc.reg_write(UC_X86_REG_EIP, oracle.word(stack))
            uc.reg_write(UC_X86_REG_ESP, stack + 20)
        emulator.hook_add(UC_HOOK_CODE, rotation, begin=0x11c3be2, end=0x11c3be2)
        bits = lambda x: struct.unpack('<I', struct.pack('<f', x))[0]
        for row in rows:
            pending.update(angles=(bits(row['yaw']), bits(row['pitch']), 0), delta=row['delta'])
            emulator.mem_write(hull, struct.pack('<16f', *row['hull']))
            oracle.call(0xf898d0, pending['angles'][:2], ecx=chassis)
            self.assertEqual(bytes(emulator.mem_read(chassis + 0xd0, 64)),
                             struct.pack('<16f', *row['target']))
        print('Original #1513 f898d0 target matched bitwise for %d calls' % len(rows))


if __name__ == '__main__':
    unittest.main()
