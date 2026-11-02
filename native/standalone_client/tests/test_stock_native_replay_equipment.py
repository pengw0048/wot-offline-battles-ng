"""Recorded local presentation publishes the accepted consumables ledger."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_stock_native_equipment_readiness import method

ROOT = Path(__file__).resolve().parents[1]
SOURCE = os.environ.get('WOT_FORK_0101_SOURCE')
HOST = r'''
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_client_weapon.hpp"
#include <cmath>
namespace native_battle {
struct VisibilityCore {};struct LocalCriticalOwner {};struct RemoteVehicle {};struct ReplicaBinding {};
struct FallenFoliage {};struct DestructibleCatalog {};class ReloadSound {};
Visibility::~Visibility()=default;Critical::Critical()=default;Critical::~Critical()=default;
Remote::~Remote()=default;World::~World()=default;LocalGun::LocalGun()=default;LocalGun::~LocalGun()=default;
Projectiles::~Projectiles()=default;Battle::Battle()=default;Battle::~Battle()=default;
double Battle::clock()const{return 1000.;}
@STATE@
@ECHO@
namespace params {
@EQUIPMENT_CTOR@
@PRESENTER@
}
namespace gun {
stock_native_client_weapon::Gun recorded;
stock_native_client_weapon::Gun* nativeGun(Battle&){return &recorded;}
struct ReloadPublication {double left,duration;};
std::optional<ReloadPublication> observeReplayReload(LocalGun::ReplayReload&,const Value&,double,double,bool){return {};}
bool publishKind(Battle&,const std::string&,double,bool,double,double){return true;}
// Default arguments live at the public declaration, as in the production header.
@APPLY@
}
}
namespace {
namespace b=native_battle;
PyObject* exercise(PyObject*,PyObject*args){
    PyObject *avatar,*constants,*server,*client,*commands;
    if(!PyArg_ParseTuple(args,"OOOOO",&avatar,&constants,&server,&client,&commands))return nullptr;
    try{
        b::Battle battle;battle.replay=true;battle.phase=b::Phase::Running;
        battle.startup=std::make_unique<b::Startup>();battle.gun=std::make_unique<b::LocalGun>();
        battle.engine.avatar=b::borrow(avatar);battle.engine.constants=b::borrow(constants);
        battle.engine.server=b::borrow(server);battle.engine.client=b::borrow(client);
        battle.params=std::make_unique<b::Params>();battle.params->equipment.emplace();
        auto result=b::p::list();
        for(Py_ssize_t i=0;i<b::p::size(commands);++i){
            auto raw=b::p::item(commands,i);auto command=b::cs::read(raw.get());
            if(command.get("rows").resolved().kind==b::Value::Kind::Array){
                battle.params->equipment->clear();
                for(const auto&row:command.get("rows").resolved().array){
                    b::params::EquipmentContract c;c.compact_descr=std::int64_t(row.get("compact").numeric());
                    c.reuse_count=-1;c.kind=row.get("kind").resolved().text;
                    b::params::EquipmentState e(c);e.uses_left=std::int64_t(row.get("uses").numeric(-1));
                    e.ready_at=row.get("ready_at").numeric();e.active=row.get("active").truth();
                    battle.params->equipment->push_back(std::move(e));
                }
            }
            battle.startup->client_ready_received=command.get("ready").truth();
            if(command.get("direct").truth())b::params::presentEquipment(battle,command.get("now").numeric());
            else b::gun::applyReplayAmmo(battle,command);
            b::p::append(result.get(),b::p::boolean(battle.gun->replay_publishing));
            if(command.get("new_round").truth()){
                battle.startup=std::make_unique<b::Startup>();battle.params->equipment_signature.clear();
            }
        }
        return result.release();
    }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
}
PyMethodDef methods[]={{"exercise",exercise,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initreplay_equipment_test(){Py_InitModule("replay_equipment_test",methods);}
'''
FIXTURE = r'''
import replay_equipment_test as native
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
rows=[dict(compact=n,kind=k,uses=-1) for n,k in ((507,'extinguisher'),(1019,'medkit'),(1531,'repairkit'))]
def run(commands):
 calls=[]
 avatar=O(updateVehicleAmmo=lambda *args:calls.append(args))
 constants=O(EQUIPMENT_STAGES=O(READY=7,COOLDOWN=8,EXHAUSTED=9))
 # Replay has its own presentation clock, independent of engine time.
 clock=[0.];client=O(presentation_time=lambda:clock[0])
 class Commands(list):
  pass
 commands=list(commands)
 for command in commands:command.setdefault('_replay_t',0.)
 flags=native.exercise(avatar,constants,O(vehicle_id=91),client,commands)
 assert not any(flags),flags
 return calls
# Snapshot ingress outside recorded presentation installs data without edges.
# Readiness and the following recorded local row must release all three slots.
calls=run([dict(rows=rows,ready=False,direct=True),dict(ready=True,direct=True),dict(ready=True),dict(ready=True)])
assert calls==[(91,n,1,7,0) for n in (507,1019,1531)],calls
# An initially empty ledger can receive its first snapshot after readiness.
calls=run([dict(rows=[],ready=True),dict(rows=rows,ready=True),dict(ready=True)])
assert len(calls)==3,calls
# Discrete cooldown/quantity/activation changes publish at recorded boundaries.
changed=[dict(row) for row in rows];changed[0]['ready_at']=1090.;changed[1]['uses']=0
changed.append(dict(compact=2222,kind='rpm_limiter',uses=-1,active=True))
calls=run([dict(rows=rows,ready=True),dict(rows=changed,ready=True),dict(ready=True)])
assert calls[3:]==[(91,507,1,8,90),(91,1019,0,9,0),(91,2222,1,7,-1)],calls
# A fresh round clears the presentation signature; readiness alone does not
# publish outside the scoped replay boundary, and then the local row reseeds.
calls=run([dict(rows=rows,ready=True,new_round=True),dict(ready=False),dict(ready=True,direct=True),dict(ready=True)])
assert calls==[(91,n,1,7,0) for n in (507,1019,1531)]*2,calls
assert run([dict(rows=[],ready=True)])==[]
print('Replay accepted consumables ledger publishes only at ready recorded boundaries')
'''


@unittest.skipUnless(SOURCE, 'fixed gameplay source is required')
class ReplayEquipmentTests(unittest.TestCase):
    def test_recorded_local_boundary_publishes_equipment(self):
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                      env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        params = (ROOT / 'bigworld/stock_native_battle_glue_params.cpp').read_text()
        gun = (ROOT / 'bigworld/stock_native_battle_glue_gun.cpp').read_text()
        equipment = (ROOT / 'bigworld/stock_native_battle_params_equipment.cpp').read_text()
        host = HOST.replace('@STATE@', method(params, 'Params& state(Battle& b)'))
        host = host.replace('@ECHO@', method(params, 'std::array<std::int64_t, 3> equipmentEcho('))
        host = host.replace('@PRESENTER@', method(params, 'bool presentEquipment(Battle& b,'))
        host = host.replace('@EQUIPMENT_CTOR@', method(equipment, 'EquipmentState::EquipmentState('))
        host = host.replace('@APPLY@', method(gun, 'bool applyReplayAmmo(Battle& b,'))
        with tempfile.TemporaryDirectory(prefix='replay-equipment-') as directory:
            folder = Path(directory); (folder / 'host.cpp').write_text(host)
            result = subprocess.run(['c++', '-std=c++17', '-O1', '-DWOT_NATIVE_BATTLE_HOST_BUILD', '-DWOT_CLIENT_REPLICA_HOST_BUILD',
                                     '-Wno-register', '-bundle', '-undefined', 'dynamic_lookup', '-Wl,-dead_strip',
                                     '-Wl,-exported_symbol,_initreplay_equipment_test', '-isystem', str(Path(prefix) / 'include/python2.7'),
                                     '-I' + str(ROOT / 'bigworld'), '-I' + str(Path(SOURCE) / 'native'), str(folder / 'host.cpp'),
                                     str(ROOT / 'bigworld/stock_native_client_state.cpp'), '-o', str(folder / 'replay_equipment_test.so')],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([py27, '-B', '-c', FIXTURE], cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
