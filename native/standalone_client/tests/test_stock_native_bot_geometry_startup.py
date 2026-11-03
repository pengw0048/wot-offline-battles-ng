"""Exercise the fitted Bot's production cold geometry preparation before installation."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from test_stock_native_bot_fitting_bridge import cpp_function

ROOT = Path(__file__).resolve().parents[1]


def build_geometry_host(folder, source, py27):
    """Build the production cold seam; resource audits can supply stock descriptors."""
    bigworld = ROOT / 'bigworld'
    round_source = (bigworld / 'stock_native_battle_bots_round.cpp').read_text()
    remote_source = (bigworld / 'stock_native_battle_remote_vehicle.cpp').read_text()
    battle_source = (bigworld / 'stock_native_battle.hpp').read_text()
    start = round_source.index('// Fitting installation is cold descriptor work.')
    start = round_source.index('p::Ref fittings(', start)
    end = round_source.index('} catch (const std::exception& e)', start)
    cold = round_source[start:end]
    py_helpers = battle_source[battle_source.index('namespace py {'):battle_source.index('enum class Phase')]
    constants = '\n'.join(line for line in round_source.splitlines()
        if line.startswith('const char* const SKILL_TIERS[]') or line.startswith('const double RATING_ANCHORS[]'))
    start = round_source.index('const std::set<std::string> SIEGE_VEHICLES')
    constants += '\n' + round_source[start:round_source.index(';', start) + 1]
    functions = '\n'.join(cpp_function(round_source, signature) for signature in (
        'p::Ref attribute(', 'std::string typeName(', 'p::Ref fieldOr(',
        'std::pair<Object, Object> descriptorPair(', 'std::string skillForRating('))
    start = remote_source.index('    for (auto& tester : b.remote->hit_testers)', remote_source.index('void destroyAll('))
    cleanup = remote_source[start:remote_source.index('    clearOverlays();', start)]
    host = r'''
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_client_state.hpp"
#include <map>
#include <memory>
#include <set>
namespace native_battle {
namespace p=stock_native_bot_python;
using Object=stock_native_client_state::Handle;
''' + cpp_function(battle_source, 'inline Object own(') + '\n' + cpp_function(battle_source, 'inline Object borrow(') + '\n' + cpp_function(battle_source, 'inline void replace(') + py_helpers + r'''
// Only the state containers are host substitutes. Calls, pair selection,
// preparation, geometry consumers and release loop come from production.
struct Remote {bool authority_geometry=true;std::map<PyObject*,Object> hit_testers,descriptors;};
struct Battle {struct {Object vehicles;}engine;std::unique_ptr<Remote>remote{new Remote};};
namespace remote {
''' + cpp_function(remote_source, 'p::Ref prepareDescriptor(') + r'''
void release(Battle&b){std::optional<std::string>first;auto describe=[](const std::exception&e){return std::string(e.what());};
''' + cleanup + r'''
if(first)throw p::Error(*first);}
}
''' + constants + '\n' + functions + r'''
p::Ref inspect(PyObject*descriptor){
    const auto s=stock_native_bot_descriptor::chassisShape(descriptor);
    const auto body=stock_native_bot_descriptor::bodyBounds(descriptor);
    const auto points=stock_native_bot_descriptor::checkpoints(descriptor);
    auto out=p::dict();
    p::set(out.get(),"shape",p::Ref(Py_BuildValue("(dddd)",s[0],s[1],s[2],s[3])));
    p::set(out.get(),"body",p::Ref(Py_BuildValue("((ddd)(ddd))",body.minimum[0],body.minimum[1],body.minimum[2],body.maximum[0],body.maximum[1],body.maximum[2])));
    auto checkpoints=p::retain(Py_None);
    if(points){replace(checkpoints,p::list());for(const auto&point:*points)p::append(checkpoints.get(),p::Ref(Py_BuildValue("(ddd)",point[0],point[1],point[2])));}
    p::set(out.get(),"checkpoints",checkpoints);return out;
}
Battle retained;
PyObject* failure(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
PyObject* prepare(PyObject*,PyObject*descriptor){try{remote::prepareDescriptor(retained,descriptor);return inspect(descriptor).release();}catch(const std::exception&e){return failure(e);}}
PyObject* release(PyObject*,PyObject*){try{remote::release(retained);Py_RETURN_NONE;}catch(const std::exception&e){return failure(e);}}
PyObject* unprepared(PyObject*,PyObject*descriptor){try{return inspect(descriptor).release();}catch(const std::exception&e){return failure(e);}}
PyObject* start(PyObject*,PyObject*args){PyObject*vehicles;const char*name;double rating;if(!PyArg_ParseTuple(args,"Osd",&vehicles,&name,&rating))return nullptr;
try{Battle b;b.engine.vehicles=borrow(vehicles);std::string vehicle_name(name);std::pair<Object,Object>pair;
''' + cold + r'''
    // The installation seam consumes the actual descriptor geometry. Repeat
    // preparation to cover the shared component owner used by subsequent Bots.
    auto travel=inspect(pair.first.get());
    auto siege=pair.second?inspect(pair.second.get()):p::retain(Py_None);
    remote::prepareDescriptor(b,pair.first.get());
    if(pair.second)remote::prepareDescriptor(b,pair.second.get());
    p::Ref result(PyTuple_Pack(2,travel.get(),siege.get()));remote::release(b);return result.release();
}catch(const std::exception&e){return failure(e);}}
}
static PyMethodDef methods[]={
{"start",native_battle::start,METH_VARARGS,nullptr},
{"prepare",native_battle::prepare,METH_O,nullptr},
{"release",native_battle::release,METH_NOARGS,nullptr},
{"unprepared",native_battle::unprepared,METH_O,nullptr},
{nullptr,nullptr,0,nullptr}};
extern "C" void initbot_geometry_startup(){Py_InitModule("bot_geometry_startup",methods);}
'''
    (folder / 'geometry_host.cpp').write_text(host)
    prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
    link = ['-bundle', '-undefined', 'dynamic_lookup'] if sys.platform == 'darwin' else ['-shared', '-fPIC']
    output = folder / 'bot_geometry_startup.so'
    command = ['c++', '-std=c++17', '-Wno-register'] + link + [
        '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(bigworld), '-I' + str(source / 'native'),
        str(folder / 'geometry_host.cpp'), str(bigworld / 'stock_native_bot_descriptor.cpp'),
        str(ROOT / 'tests/stock_native_client_state_host.cpp'), '-o', str(output)]
    subprocess.run(command, text=True, capture_output=True, check=True)
    return output


FIXTURE = r'''
import sys,types
import bot_geometry_startup as native
events=[]
class O(object):
    def __init__(self,**fields):self.__dict__.update(fields)
class Tester(object):
    def __init__(self,label,bounds):self.label=label;self.bounds=bounds;self.bbox=None;self.loads=0;self.releases=0
    def loadBspModel(self):
        assert self.loads==0,(self.label,'duplicate load')
        self.loads+=1;self.bbox=self.bounds;events.append(('load',self.label))
    def releaseBspModel(self):
        assert self.loads==1 and self.releases==0,(self.label,'unbalanced release')
        self.releases+=1;del self.bbox;events.append(('release',self.label))
class Descriptor(object):
    def __init__(self,name,suffix,shared=None):
        self.type=O(name=name)
        self.chassis=O(hitTester=Tester(suffix+'chassis',((-2.,-1.,-4.),(2.,1.,4.),'derived')),hullPosition=(0.,1.5,0.))
        self.hull=O(hitTester=Tester(suffix+'hull',((-1.,0.,-3.),(1.,2.,3.),'derived')))
        self.turret=O(hitTester=Tester(suffix+'turret',((-1.,0.,-1.),(1.,1.,1.),'derived')))
        self.gun=O(hitTester=Tester(suffix+'gun',((-.2,-.2,0.),(.2,.2,4.),'derived')))
        if shared:self.hull=shared.hull;self.turret=shared.turret;self.gun=shared.gun
        self.visibilityCheckPoints=[(float(i),1.,2.) for i in range(6)]
    def getHitTesters(self):
        return [self.chassis.hitTester,self.hull.hitTester,self.turret.hitTester,self.gun.hitTester]
class Vehicles(object):pass
fitting=types.ModuleType('gui.mods.offline_lan_0922.bot_fittings')
for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
    module=types.ModuleType(name);module.__path__=[];sys.modules[name]=module
sys.modules[fitting.__name__]=fitting
vehicles=Vehicles()
def build(actual,name,tier,level,restored):
    assert actual is vehicles and name==vehicles.name and restored is None and type(level) is int
    events.append(('build',name));return vehicles.descriptor
fitting.build=build
# Geometry is cold for every fitted module, regardless of vehicle class/tier.
# A Siege pair can share hull/turret/gun testers but has mode-specific chassis.
for name in ('japan:J24_Mi_To_130_tons','germany:G57_PzVI_Tiger_P','sweden:S11_Strv_103B'):
    for rating in (0.,.34,.67,1.):
        events[:]=[]
        travel=Descriptor(name,'travel-')
        siege=Descriptor(name,'siege-',travel) if name.startswith('sweden:') else None
        vehicles.name=name
        vehicles.descriptor=O(defaultVehicleDescr=travel,siegeVehicleDescr=siege,hasSiegeMode=True) if siege else travel
        try:native.unprepared(travel)
        except TypeError as error:assert 'NoneType' in str(error)
        else:raise AssertionError('the reported cold bbox failure was not reproduced')
        result=native.start(vehicles,name,rating)
        assert events[0]==('build',name)
        assert result[0]['shape']==(2.,4.,-1.,3.5)
        assert result[0]['body']==((-2.,-1.,-4.),(2.,3.5,4.))
        assert len(result[0]['checkpoints'])==6
        assert (result[1] is not None)==(siege is not None)
        if siege:assert result[1]['shape']==(2.,4.,-1.,3.5)
        testers=set(travel.getHitTesters()+(siege.getHitTesters() if siege else []))
        assert len(events)==1+2*len(testers),events
        assert all(t.loads==1 and t.releases==1 and not hasattr(t,'bbox') for t in testers)
        assert all(event[0]=='load' for event in events[1:1+len(testers)])
        assert all(event[0]=='release' for event in events[1+len(testers):])
native.release();native.release()
print('production fitting -> prepare travel/Siege -> cold geometry -> release passed')
'''


class GeometryStartupTests(unittest.TestCase):
    def test_fitted_descriptors_are_prepared_before_geometry_installation(self):
        source = os.environ.get('WOT_FORK_SOURCE')
        if not source:
            self.skipTest('set WOT_FORK_SOURCE to the pinned v1.1.1 checkout')
        if sys.platform == 'win32' or not shutil.which('c++'):
            self.skipTest('the CPython 2.7 extension host requires a Unix C++ compiler')
        py27 = os.environ.get('WOT_PYTHON27')
        if not py27 and shutil.which('pyenv'):
            runtime = subprocess.run(['pyenv', 'which', 'python2.7'], text=True, capture_output=True,
                env=dict(os.environ, PYENV_VERSION='2.7.18'))
            if runtime.returncode == 0:
                py27 = runtime.stdout.strip()
        if not py27:
            self.skipTest('set WOT_PYTHON27 to CPython 2.7.18, or install it through pyenv')
        with tempfile.TemporaryDirectory(prefix='bot-geometry-startup-') as directory:
            folder = Path(directory)
            build_geometry_host(folder, Path(source), py27)
            (folder / 'fixture.py').write_text(FIXTURE)
            run = subprocess.run([py27, '-B', str(folder / 'fixture.py')], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
