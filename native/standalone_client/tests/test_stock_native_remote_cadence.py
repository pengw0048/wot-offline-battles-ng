"""Run actual replica admission/interpolation with separate worker and render consumers."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from test_stock_native_bot_fitting_bridge import cpp_function
from test_stock_wire_codec import python27_audit_runtime

ROOT = Path(__file__).resolve().parents[1]

HOST = r'''
#include "stock_native_bot_python.hpp"
#include "stock_native_client_state.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
namespace p=stock_native_bot_python;
namespace cs=stock_native_client_state;
namespace native_battle {
using cs::Actor;using cs::Pose;using cs::Sample;using cs::Value;
using Bracket=std::optional<std::array<std::int64_t,4>>;
constexpr double pi=3.141592653589793;
@MATH@
// Engine binding containers are host substitutes. State admission, receive
// ownership and confirmed-cursor interpolation below are production code.
struct Remote {
    std::map<std::string,int>bindings,static_versions;
    std::map<std::string,Value>pending_creates;
    std::optional<double>last_advance;
    unsigned received=0,lifecycle_events=0;
};
struct Battle {
    bool worker=false;std::int64_t local_player=0;
    std::unique_ptr<Remote>remote{new Remote};
    std::shared_ptr<cs::State>vehicles{std::make_shared<cs::State>()};
};
namespace vehicles {void apply(Battle&b,const Value&e){if(auto*a=b.vehicles->find(e.get("entity").text))a->engine_id=a->id;}}
void refreshBindings(Battle&,bool){}
@INTERPOLATION@
@RECEIVE@
Battle battle;
PyObject* reset(PyObject*,PyObject*worker){battle=Battle();battle.worker=worker==Py_True;Py_RETURN_NONE;}
PyObject* feed(PyObject*,PyObject*args){PyObject*row;double now;if(!PyArg_ParseTuple(args,"Od",&row,&now))return nullptr;
    try{receive(battle,cs::read(row),now,false);Py_RETURN_NONE;}
    catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyObject* stats(PyObject*,PyObject*){
    auto out=p::list();for(const auto&entry:battle.vehicles->actors){const auto&a=*entry.second;
        p::append(out.get(),p::Ref(Py_BuildValue("(kkkkkdddd)",(unsigned long)a.samples.size(),(unsigned long)a.samples.capacity(),
            (unsigned long)a.receipts.size(),(unsigned long)(a.receipts.empty()?0:a.receipts.front().revision.value_or(0)),
            (unsigned long)(a.receipts.empty()?0:a.receipts.back().revision.value_or(0)),a.target[0],a.state.get("health").numeric(),
            a.receipts.empty()?0.:a.receipts.front().state.get("x").numeric(),
            a.receipts.empty()?0.:a.receipts.front().state.get("health").numeric())));}
    return out.release();}
PyObject* render(PyObject*,PyObject*args){double now,dt;if(!PyArg_ParseTuple(args,"dd",&now,&dt))return nullptr;
    if(!battle.worker)for(const auto&entry:battle.vehicles->actors){auto&a=*entry.second;Bracket bracket;bool snapped=false;
        interpolate(a,now,dt,1.-std::exp(-20.*dt),bracket,snapped);}
    const auto*a=battle.vehicles->find("bot:1");return Py_BuildValue("(ddd)",a->current[0],a->cursor.value_or(-1),a->delay);}
}
PyMethodDef methods[]={
{"reset",native_battle::reset,METH_O,nullptr},{"feed",native_battle::feed,METH_VARARGS,nullptr},
{"stats",native_battle::stats,METH_NOARGS,nullptr},{"render",native_battle::render,METH_VARARGS,nullptr},
{nullptr,nullptr,0,nullptr}};
extern "C" void initremote_cadence(){Py_InitModule("remote_cadence",methods);}
'''

FIXTURE = r'''
import math,sys
import remote_cadence as native

def message(count,revision,motion,source):
    rows=[]
    for bot_id in range(1,count+1):
        rows.append(dict(id=bot_id,alive=True,health=100-revision%3,
            x=source/100000.,y=0.,z=0.,yaw=0.,speed=10.,
            critical=dict(crew_ko=[],devices=dict(engineHealth=dict(hp=1.,max_hp=1.))),
            ammo_remaining=[30,20,5],equipment_states=[]))
    return dict(round_id=1,sequence=motion+1,bot_state_revision=revision,
        motion_time_us=motion,bot_state_time_us=source,bots=rows,players=[],timing=dict(phase='battle'))

mode=sys.argv[1]
if mode=='worker':
    # Five minutes of 29 simultaneous Bots: every accepted revision still has
    # its complete independent contact receipt, including its frozen row.
    for count in (1,29):
        native.reset(True)
        for tick in range(3000):
            native.feed(message(count,tick,tick*100000,tick*100000),tick*.1)
            if tick%100==0 or tick==2999:
                rows=native.stats();assert len(rows)==count
                for samples,capacity,receipts,oldest,newest,target,health,old_x,old_health in rows:
                    assert samples==0 and capacity<=1,(tick,samples,capacity)
                    assert receipts==min(tick+1,512),(tick,receipts)
                    assert oldest==max(0,tick-511) and newest==tick,(tick,oldest,newest)
                    assert abs(target-tick)<1e-8 and health==100-tick%3
                    assert old_x==oldest and old_health==100-oldest%3,(oldest,old_x,old_health)
        # Rendering is intentionally absent on a worker; a render callback
        # cannot be required to clean up its display-only history.
    print('worker 5min x 29 actors bounded; all 512 contact revisions retained')
else:
    # 10 Hz authority, 30 Hz snapshot exposure, 60 Hz presentation and 10 ms
    # transport, with varied producer jitter. No collision/proof extrapolation.
    for jitter,source_origin,local_origin in ((0.,0,0.),(.003,123000000,6789.25),(.01,987000000,17000.5)):
        native.reset(False);poses=[]
        for frame in range(1800):
            now=frame/60.
            if frame%2==0:
                source=max(0.,now-.01-jitter*math.sin(frame*.37))
                tick=int(source/.1)
                row=message(1,tick,source_origin+int(now*1000000),source_origin+tick*100000)
                # The network clock supplies an elapsed queue delay. Its
                # epoch is independent of BigWorld.time and authority us.
                row['_client_dispatch_delay']=.007
                native.feed(row,local_origin+now+.007)
            poses.append(native.render(local_origin+now+.007,1./60.)[0])
        speeds=[(poses[i]-poses[i-1])*60 for i in range(301,len(poses))]
        assert all(9.99<v<10.01 for v in speeds),(jitter,min(speeds),max(speeds))
        samples,capacity,receipts,oldest,newest,target,health,old_x,old_health=native.stats()[0]
        assert 2<=samples<=5 and receipts==300,(samples,receipts)
    print('10Hz source -> 60Hz confirmed presentation continuous for all jitter cases')
'''


class RemoteCadenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if sys.platform == 'win32' or not shutil.which('c++'):
            raise unittest.SkipTest('The CPython 2.7 extension requires a Unix C++ compiler')
        cls.py27 = os.environ.get('WOT_PYTHON27') or python27_audit_runtime()
        cls.directory = tempfile.TemporaryDirectory(prefix='remote-cadence-')
        cls.addClassCleanup(cls.directory.cleanup)
        cls.folder = Path(cls.directory.name)
        source = (ROOT / 'bigworld/stock_native_battle_remote_replica.cpp').read_text()
        helpers = (ROOT / 'bigworld/stock_native_battle_remote_internal.hpp').read_text()
        interpolation = '\n'.join(cpp_function(source, signature) for signature in (
            'Pose blend(', 'std::optional<Pose> blendBody(', 'Bracket bracket(', 'bool interpolate('))
        math = '\n'.join(cpp_function(helpers, signature) for signature in ('inline double wrap(', 'inline double clamp('))
        host = HOST.replace('@MATH@', math).replace('@INTERPOLATION@', interpolation).replace(
            '@RECEIVE@', cpp_function(source, 'void receive(Battle& b, const Value& data,'))
        (cls.folder / 'host.cpp').write_text(host)
        (cls.folder / 'fixture.py').write_text(FIXTURE)
        prefix = subprocess.check_output([cls.py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        link = ['-bundle', '-undefined', 'dynamic_lookup'] if sys.platform == 'darwin' else ['-shared', '-fPIC']
        command = ['c++', '-std=c++17', '-O1', '-Wno-register'] + link + [
            '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld'),
            str(cls.folder / 'host.cpp'), str(ROOT / 'tests/stock_native_client_state_host.cpp'),
            '-o', str(cls.folder / 'remote_cadence.so')]
        built = subprocess.run(command, capture_output=True, text=True)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)

    def check_fixture(self, mode):
        result = subprocess.run([self.py27, '-B', str(self.folder / 'fixture.py'), mode], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_worker_five_minute_history_is_bounded_without_rendering(self):
        self.check_fixture('worker')

    def test_visible_ten_hz_source_remains_continuous_at_sixty_hz(self):
        self.check_fixture('visible')


if __name__ == '__main__':
    unittest.main()
