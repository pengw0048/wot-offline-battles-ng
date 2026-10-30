"""Opt-in host stage benchmark; numbers are not Windows client evidence."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
STAGES=r'''
namespace {
PyObject*benchmarkReceipts(PyObject*,PyObject*args){int loops;if(!PyArg_ParseTuple(args,"i",&loops))return nullptr;if(loops<=0){PyErr_SetString(PyExc_ValueError,"append count must be positive");return nullptr;}
using Clock=std::chrono::steady_clock;cs::ReceiptHistory ring;std::vector<cs::Sample> legacy;legacy.reserve(513);cs::Sample row;row.state.kind=cs::Value::Kind::Shared;row.state.reference=std::make_shared<const cs::Value>();
for(int i=0;i<512;++i){row.time=i;row.revision=i;legacy.push_back(row);ring.push_back(row);}
const auto begin=Clock::now();for(int i=0;i<loops;++i){row.time=i+512;row.revision=i+512;legacy.push_back(row);legacy.erase(legacy.begin());}const auto middle=Clock::now();
for(int i=0;i<loops;++i){row.time=i+512;row.revision=i+512;ring.push_back(row);}const auto end=Clock::now();
if(ring.front().revision!=legacy.front().revision||ring.back().revision!=legacy.back().revision){PyErr_SetString(PyExc_AssertionError,"receipt benchmark order changed");return nullptr;}
return Py_BuildValue("{s:i,s:d,s:d,s:i}","appends",loops,"vector_erase_ns",std::chrono::duration<double>(middle-begin).count()*1.e9/loops,"ring_ns",std::chrono::duration<double>(end-middle).count()*1.e9/loops,"retained",int(ring.size()));}
PyObject*benchmarkSnapshot(PyObject*,PyObject*args){PyObject*raw;int loops,projected=0,edges=0;if(!PyArg_ParseTuple(args,"Oi|ii",&raw,&loops,&projected,&edges))return nullptr;try{using Clock=std::chrono::steady_clock;double parse=0,core=0,pack=0,release=0;std::uint64_t count=0;Owner o;o.state->local_player_id=1;
for(int i=0;i<loops;++i){if(edges){auto*bots=PyDict_GetItemString(raw,"bots");for(Py_ssize_t j=0;bots&&j<std::min<Py_ssize_t>(edges,PySequence_Size(bots));++j){auto row=p::item(bots,j);p::set(row.get(),"fire_seq",p::integer(i));}}const auto t0=Clock::now();auto t3=t0;
{cs::Value data;if(!projected)data=readMessage(o,raw);else data=readStateMessage(o,raw,false);
auto&seq=data.object["server_tick"];seq.kind=cs::Value::Kind::Integer;seq.integer=i+1;auto&revision=data.object["bot_state_revision"];revision=seq;auto&stamp=data.object["motion_time_us"];stamp=seq;stamp.integer*=50000;data.object["bot_state_time_us"]=stamp;
const auto t1=Clock::now();auto events=cs::snapshot(*o.state,data,i*.05);const auto t2=Clock::now();auto output=p::list();for(const auto&e:events)p::append(output.get(),p::Ref(cs::write(e.payload)));t3=Clock::now();count+=events.size();parse+=std::chrono::duration<double>(t1-t0).count();core+=std::chrono::duration<double>(t2-t1).count();pack+=std::chrono::duration<double>(t3-t2).count();}
release+=std::chrono::duration<double>(Clock::now()-t3).count();}
return Py_BuildValue("{s:d,s:d,s:d,s:d,s:K}","parse_ms",parse*1000/loops,"core_ms",core*1000/loops,"pack_ms",pack*1000/loops,"release_ms",release*1000/loops,"events",static_cast<unsigned long long>(count));}catch(const std::exception&e){PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
}
'''
SCRIPT=r'''
import json,sys
import client_replica_test as native
print(json.dumps(dict(value_allocations=native.value_contract())))
message=json.load(open(sys.argv[1]));loops=int(sys.argv[2]);edges=int(sys.argv[3]);receipt_appends=int(sys.argv[4])
if receipt_appends:
 print(json.dumps(dict(boundary='Host receipt bookkeeping only; both histories share one immutable Value, excluding codec and old-row destruction',receipt_stages=native.benchmark_receipts(receipt_appends))))
class _StaticDict(dict):pass
for row in message.get('players',[]):
 for key in ('outfits','effective_params'):
  if isinstance(row.get(key),dict):row[key]=_StaticDict(row[key])
for projected in (0,1):
 print(json.dumps(dict(projected=bool(projected),stages=native.benchmark_snapshot(message,loops,projected,edges))))
'''
def fixture():
    import sys
    sys.path.insert(0,str(ROOT.parents[1]/'tests'))
    from test_port_0922_equipment_mechanics import _defaults,equipment_mechanics
    equipment=[equipment_mechanics.EquipmentState(equipment_mechanics.project_equipment(d)).snapshot(0.) for d in _defaults()]
    sys.path.insert(0,str(ROOT.parents[1]/'server'))
    from lan_battle_server import BattleState,Player
    names=['engineHealth','fuelTankHealth','ammoBayHealth','gunHealth','turretRotatorHealth','radioHealth','surveyingDeviceHealth','leftTrackHealth','rightTrackHealth']
    critical=dict(fire=False,ammo_rack_death=False,crew_roster=['commander','gunner','driver','radio','loader'],crew_ko=[],destroyed=[],devices=[dict(name=n,hp=100.,max_hp=100.,regen_hp=50.,state='normal') for n in names])
    bots=[]
    for i in range(2,31):
        raw=dict(id=i,x=i*3.,y=0.,z=0.,health=1000,alive=True,critical=critical,equipment_states=equipment,reload_time=3.,reload_duration=3.)
        bots.append(BattleState._sanitize_bot_state(raw,dict(id=i,team=1 if i<=15 else 2,slot=(i-1)%15,name='Vehicle',vehicle='ussr:T-34'),None,trusted=True))
    human=BattleState._public_player(Player(1,None,('127.0.0.1',0)))
    # The manifest retains the pinned producer's full profile/route/equipment
    # fields, and admission rejoins it even for lean snapshots.
    manifest=[]
    for row in bots:
        version=dict(row);version.update(profile=dict(class_tag='mediumTank',preferred_range=200.,parking_radius=4.),terminal_critical=critical,route=dict(id='map_route',waypoints=[dict(x=j*20.,y=0.,z=j*20.,hold=False) for j in range(16)]));manifest.append(version)
    return dict(round_id=1,server_tick=1,server_time_ms=1,bot_state_revision=1,motion_time_us=50000,bot_state_time_us=50000,players=[human],bots=bots,bot_manifest=manifest,destructibles=[dict(chunk_id=i,item_index=j,kind='tree',fallen=True,direction=[1.,0.,0.]) for i in range(8) for j in range(18)])
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--loops',type=int,default=120);parser.add_argument('--optimization',choices=['0','2'],default='0');parser.add_argument('--input',type=Path);parser.add_argument('--legacy-copies',action='store_true');parser.add_argument('--eager-object',action='store_true');parser.add_argument('--edge-actors',type=int,default=0);parser.add_argument('--receipt-appends',type=int,default=0);args=parser.parse_args()
    fork=Path(os.environ['WOT_FORK_0101_SOURCE']);py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip();prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
    with tempfile.TemporaryDirectory(prefix='native-replica-benchmark-') as directory:
        folder=Path(directory);raw=json.loads(args.input.read_text()) if args.input else fixture();(folder/'input.json').write_text(json.dumps(raw));(folder/'run.py').write_text(SCRIPT)
        law=(fork/'native/offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0];(folder/'offline_release_drive_law.hpp').write_text('#pragma once\n#include <algorithm>\n#include <cmath>\n'+law)
        host='#include <chrono>\n'+(ROOT/'tests/stock_native_client_replica_host.cpp').read_text();host=host.replace('"../bigworld/', '"'+str(ROOT/'bigworld')+'/');
        if args.legacy_copies:
            state=(ROOT/'bigworld/stock_native_client_state.cpp').read_text();begin=state.index('    if(state.reference)a.state=state;');end=state.index('a.revision=',begin);state=state[:begin]+'    a.state=state.resolved();'+state[end:];(folder/'experiment_state.cpp').write_text(state);host=host.replace(str(ROOT/'bigworld/stock_native_client_state.cpp'),str(folder/'experiment_state.cpp'))
            replica=(ROOT/'bigworld/stock_native_client_replica.cpp').read_text();begin=replica.index('cs::Value readActorRows(');end=replica.index('// Admission retains',begin);replica=replica[:begin]+'cs::Value readActorRows(Owner&o,PyObject*raw){return readMessage(o,raw); }\n'+replica[end:];(folder/'experiment_replica.cpp').write_text(replica);host=host.replace(str(ROOT/'bigworld/stock_native_client_replica.cpp'),str(folder/'experiment_replica.cpp'))
        if args.eager_object:
            # Restore the R18 member only in this temporary experiment. Keep
            # actor sharing, envelope projection and array capacity identical.
            header=(ROOT/'bigworld/stock_native_client_state.hpp').read_text().replace('ObjectFields object;','std::map<std::string,Value> object;')
            header='\n'.join(line for line in header.splitlines() if not line.startswith('static_assert('))+'\n'
            (folder/'stock_native_client_state.hpp').write_text(header)
            for name in ('state','replica'):
                path=folder/('experiment_'+name+'.cpp');text=path.read_text() if path.exists() else (ROOT/('bigworld/stock_native_client_'+name+'.cpp')).read_text()
                path.write_text(text);host=host.replace(str(ROOT/('bigworld/stock_native_client_'+name+'.cpp')),str(path))
        host=host.replace('PyMethodDef methods[]={',STAGES+'\nPyMethodDef methods[]={{"benchmark_receipts",benchmarkReceipts,METH_VARARGS,""},{"benchmark_snapshot",benchmarkSnapshot,METH_VARARGS,""},',1);(folder/'benchmark.cpp').write_text(host)
        subprocess.run(['c++','-std=c++17','-O'+args.optimization,'-ffp-contract=off','-Wno-register','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(ROOT/'bigworld'),'-I'+str(fork/'native'),'-I'+str(folder),str(folder/'benchmark.cpp'),'-o',str(folder/'client_replica_test.so')],check=True)
        print(json.dumps(dict(boundary=('Captured JSON with accepted static wrappers restored' if args.input else 'Synthetic server-canonical schema')+' on host CPython 2.7; no Windows performance claim',bytes=len(json.dumps(raw)),actors=len(raw['players'])+len(raw['bots']),loops=args.loops,optimization=args.optimization,legacy_copies_experiment=args.legacy_copies,eager_object_experiment=args.eager_object,edge_actors=args.edge_actors)),flush=True)
        subprocess.run([py27,'-B',str(folder/'run.py'),str(folder/'input.json'),str(args.loops),str(args.edge_actors),str(args.receipt_appends)],check=True)
if __name__=='__main__':main()
