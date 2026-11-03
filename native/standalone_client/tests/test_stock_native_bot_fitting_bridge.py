"""Run the production fitting call expressions against the real v1.1.1 builder on Python 2.7."""
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def cpp_function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


FIXTURE = r'''
import base64,os,sys,types
client = os.path.join(sys.argv[1], 'src/res/scripts/client')
sys.path.insert(0, client)
for name in ('gui', 'gui.mods'):
    module = types.ModuleType(name)
    module.__path__ = [os.path.join(client, *name.split('.'))]
    sys.modules[name] = module
# The exact builder validates the call before its first stock descriptor call.
# These imports supply no fitting/number policy; stop at that engine boundary.
items = types.ModuleType('items')
items.tankmen = types.ModuleType('items.tankmen')
items.customizations = types.ModuleType('items.customizations')
sys.modules['items'] = items
from gui.mods.offline_lan_0922 import bot_fittings as fitting
import fitting_bridge
class DescriptorBoundary(Exception): pass
class Vehicles(object):
    def VehicleDescr(self, **keywords):
        assert keywords == dict(typeName='germany:G57_PzVI_Tiger_P')
        raise DescriptorBoundary()
vehicles = Vehicles()
original = fitting.build
observed = []
def capture(*args):
    observed.append((len(args), type(args[3]).__name__, args[3],
                     None if args[4] is None else type(args[4]['crew_level']).__name__))
    assert type(args[1]) is str and type(args[2]) is str
    if args[4] is not None:
        assert type(args[4]['descriptor']) is str
        assert type(args[4]['devices']) is list
    if args[4] is not None:
        fitting.canonical(args[4])
    return original(*args)
fitting.build = capture
# Reproduce the reported failure with the untouched target guard.
for level in (50,75,100):
    try: original(vehicles, 'germany:G57_PzVI_Tiger_P', 'regular', long(level))
    except ValueError as error: assert str(error) == 'invalid Bot crew level'
    else: raise AssertionError('the original Python 2 long failure was not reproduced')
results = []
for rating, tier, level in ((0.,'rookie',50),(.34,'regular',75),(.67,'veteran',100),(1.,'elite',100)):
    plan = fitting.plan('germany:G57_PzVI_Tiger_P', tier)
    wire = dict(schema=1, vehicle='germany:G57_PzVI_Tiger_P', tier=tier,
                crew_level=level, descriptor=base64.b64encode('native fitting'),
                devices=plan['devices'], directive=plan['directive'], style=plan['style'])
    for name, call in (('fresh', lambda:fitting_bridge.fresh(vehicles,wire['vehicle'],rating)),
                       ('restored', lambda:fitting_bridge.restored(vehicles,dict(fitting=wire)))):
        try: call()
        except DescriptorBoundary: results.append((name,tier,'descriptor reached'))
        except Exception as error: results.append((name,tier,type(error).__name__+': '+str(error)))
print(repr(results))
print(repr(observed))
assert all(row[2] == 'descriptor reached' for row in results), results
assert observed == [(5,'int',level,restored) for level in (50,75,100,100)
                   for restored in (None,'int')], observed
# The shared wire projection must keep wide IDs. Consumers that accept long
# must continue receiving it; only the fitting API's small crew scalar is int.
from gui.mods.offline_lan_0922 import tactical_radio, offline_replay, replay_presentation
wide = 2**53 + 17
projected = fitting_bridge.project(dict(id=wide, quantity=3, enabled=True,
    command='RELOADING_CASSETE', details=dict(reload_time=12.5, quantity=3),
    aim=dict(aim_point=[1,2.5,-3],reload_time=0), text=u'\u5766\u514b',
    local=dict(position=[1,2.5,-3],health=100,gun_angles=4294967295,
        _local_airborne=False,visibility={'bot:2':[True,False]},
        gun=dict(clip=3,ammo=[2,5],load_started=True))))
assert type(projected['id']) is long and projected['id'] == wide
assert type(projected['quantity']) is long and type(projected['enabled']) is bool
assert type(projected['text']) is str and projected['text'].decode('utf8') == u'\u5766\u514b'
assert tactical_radio._exact_int(projected['id'],1) == wide
assert tactical_radio.validate_command_details(projected['command'],projected['details'])
assert tactical_radio.validate_command_details('SPG_AIM_AREA',projected['aim'])
assert not tactical_radio.validate_command_details('SPG_AIM_AREA',dict(aim_point=[1,2],reload_time=0))
assert not tactical_radio.validate_command_details('RELOADING_READY_CASSETE',dict(quantity=True))
local = offline_replay.validate_local(projected['local'])
assert local['gun_angles'] == 4294967295 and local['gun']['ammo'] == [2,5]
assert type(local['_local_airborne']) is bool and type(local['gun']['load_started']) is bool
assert replay_presentation._equal_values(projected['local']['position'],[1.,2.5,-3.])
assert replay_presentation._equal_values(projected['quantity'],3)
assert offline_replay.capture_message(projected)['id'] == wide
print('production fitting calls and retained Python 2 numeric/text/shape contracts passed')
'''


class FittingBridgeTests(unittest.TestCase):
    def test_production_calls_match_retained_python2_contracts(self):
        source = os.environ.get('WOT_FORK_SOURCE')
        if not source:
            self.skipTest('set WOT_FORK_SOURCE to the pinned v1.1.1 checkout')
        if sys.platform == 'win32' or not shutil.which('c++'):
            self.skipTest('the CPython 2.7 extension host requires a Unix C++ compiler')
        py27 = os.environ.get('WOT_PYTHON27')
        if not py27:
            pyenv = shutil.which('pyenv')
            if pyenv:
                runtime = subprocess.run([pyenv, 'which', 'python2.7'], text=True, capture_output=True,
                    env=dict(os.environ, PYENV_VERSION='2.7.18'))
                if runtime.returncode == 0:
                    py27 = runtime.stdout.strip()
        if not py27:
            self.skipTest('set WOT_PYTHON27 to CPython 2.7.18, or install it through pyenv')
        version = subprocess.check_output([py27, '-c', 'import sys;print(sys.version_info[:2])'], text=True).strip()
        self.assertEqual(version, '(2, 7)', 'WOT_PYTHON27 must use CPython 2.7')
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        round_source = (ROOT/'bigworld/stock_native_battle_bots_round.cpp').read_text()
        vehicles_source = (ROOT/'bigworld/stock_native_battle_vehicles.cpp').read_text()
        start = round_source.index('// Fitting installation is cold descriptor work.')
        start = round_source.index('p::Ref fittings(', start)
        end = round_source.index('pair = descriptorPair', start)
        fresh = round_source[start:end].replace('b.engine.vehicles.get()', 'vehicles')
        remote = cpp_function(vehicles_source, 'p::Ref botDescriptor(')
        remote = remote[remote.index('const auto& fitting = field(state, "fitting");'):]
        remote = remote[:remote.rfind('}')].replace('b.engine.vehicles.get()', 'vehicles')
        remote = remote.replace('return p::call(module.get(), "build", args.get());',
                                'return p::call(module.get(), "build", args.get()).release();')
        constants = '\n'.join(line for line in round_source.splitlines()
            if line.startswith('const char* const SKILL_TIERS[]') or line.startswith('const double RATING_ANCHORS[]'))
        host = r'''
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_client_state.hpp"
namespace p=stock_native_bot_python;
namespace cs=stock_native_client_state;
using Value=cs::Value;
''' + constants + '\n' + cpp_function(round_source, 'std::string skillForRating(') + '\n' + cpp_function(vehicles_source, 'const Value& field(') + '\n' + cpp_function(vehicles_source, 'std::int64_t integer(') + r'''
static PyObject* fresh(PyObject*,PyObject* parameters){PyObject*vehicles;const char*name;double rating;
    if(!PyArg_ParseTuple(parameters,"Osd",&vehicles,&name,&rating))return nullptr;
    try{std::string vehicle_name(name);
''' + fresh + r'''
    return resolved.release();}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
static PyObject* restored(PyObject*,PyObject* parameters){PyObject*vehicles,*wire;
    if(!PyArg_ParseTuple(parameters,"OO",&vehicles,&wire))return nullptr;
    try{const auto state=cs::read(wire);
''' + remote + r'''
    }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
static PyObject* project(PyObject*,PyObject* value){try{return cs::write(cs::read(value));}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
static PyMethodDef methods[]={{"project",project,METH_O,nullptr},{"fresh",fresh,METH_VARARGS,nullptr},{"restored",restored,METH_VARARGS,nullptr},{nullptr,nullptr,0,nullptr}};
extern "C" void initfitting_bridge(){Py_InitModule("fitting_bridge",methods);}
'''
        with tempfile.TemporaryDirectory(prefix='native-fitting-bridge-') as directory:
            folder=Path(directory);(folder/'host.cpp').write_text(host);(folder/'fixture.py').write_text(FIXTURE)
            link = ['-bundle','-undefined','dynamic_lookup'] if sys.platform == 'darwin' else ['-shared','-fPIC']
            command=['c++','-std=c++17','-Wno-register'] + link + [
                '-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(ROOT/'bigworld'),'-I'+str(Path(source)/'native'),
                str(folder/'host.cpp'),str(ROOT/'bigworld/stock_native_bot_descriptor.cpp'),
                str(ROOT/'tests/stock_native_client_state_host.cpp'),'-o',str(folder/'fitting_bridge.so')]
            built=subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stderr)
            run=subprocess.run([py27,'-B',str(folder/'fixture.py'),source],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

if __name__=='__main__':unittest.main()
