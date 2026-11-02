"""Differential admission and immutable transport versions on real CPython 2.7."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''# -*- coding: utf-8 -*-
from __future__ import print_function
import copy, imp, json, random, struct, sys, types
sys.path.insert(0, sys.argv[1] + '/src/res/scripts/client')
for name in ('gui','gui.mods'):
    module=types.ModuleType(name);module.__path__=[sys.path[0]+'/'+name.replace('.','/')];sys.modules[name]=module
from gui.mods.offline_lan_0922 import snapshot_delta as original, lan_client as lan, bot_state_codec as equipment
static=imp.load_source('native_static',sys.argv[2]+'/stock_snapshot_static.py')
import offline_client_transport_native as native
native.configure(static._StaticDict,static._StaticList)
def snap(**changes):
    row=dict(id=1,x=2.,y=3.,z=4.,health=100,alive=True,outfits={'1':'YWJj'},effective_params={'physics':{'mass':1000.}},critical={'devices':[{'name':'engine','hp':1.}]})
    out=dict(type='snapshot',round_id=7,authority_epoch=1,map='fjord',bot_authority_id=-1,bot_manifest_revision=2,players=[row],bots=[])
    out.update(changes);return out
def same(a,b):
    assert original._same(a,b),(a,b)
    # _same considers type but equality can hide signed zero in nested maps.
    if isinstance(a,float):assert struct.pack('d',a)==struct.pack('d',b),(a,b)
    if isinstance(a,dict):
        for k in a:same(a[k],b[k])
    if isinstance(a,(list,tuple)):
        for x,y in zip(a,b):same(x,y)
ob=nb=eb=None
random.seed(1513)
message=snap();held=[];static_version=None
for seq in range(1,161):
    message=copy.deepcopy(message)
    message['players'][0]['x']=random.uniform(-2000.,2000.)
    message['players'][0]['critical']['devices'][0]['hp']=seq
    if seq%11==0:message['players'][0]['effective_params']['physics']['mass']+=1
    if seq%13==0:message['bots'].append(dict(id=seq,x=seq,token=[seq,{'q':True}]))
    if seq%17==0 and message['bots']:message['bots'].pop(0)
    wire,eb=original.encode(message,eb,seq)
    op,ob=original.decode(wire,ob,True);np,nb=native.decode(wire,nb,True)
    actual=native.materialize(np);same(original.materialize(op),actual)
    if seq==1:static_version=actual['players'][0]['effective_params']
    if seq<11:assert actual['players'][0]['effective_params'] is static_version
    actual['players'][0]['critical']['devices'][0]['hp']=-99
    same(original.materialize(op),native.materialize(np))
    held.append((op,np))
for op,np in held:same(original.materialize(op),native.materialize(np))
# An unsuccessful patch cannot mutate either saved baseline or queued version.
full,unused=original.encode(snap(),None,1)
op,ob=original.decode(full,None,True);np,nb=native.decode(full,None,True)
good=dict(type='snapshot',round_id=7,authority_epoch=1,map='fjord',bot_authority_id=-1,bot_manifest_revision=2,snapshot_delta=True,snapshot_seq=2,snapshot_base_seq=1,players={},bots={})
bad=[]
for key,value in [('snapshot_delta',1),('snapshot_seq',True),('snapshot_seq',0),('snapshot_seq',1),('snapshot_base_seq',9),('round_id','7'),('authority_epoch',2),('map',None),('bot_authority_id',False),('bot_manifest_revision',1.5)]:
    row=copy.deepcopy(good);row[key]=value;bad.append(row)
for patch in [None,[],{'extra':[]},{'remove':[True]},{'remove':[9]},{'remove':[1,1]},{'add':[{'id':1}]},{'add':[{'id':2},{'id':2}]},{'update':[{'id':9}]},{'update':[{'id':1},{'id':1}]},{'remove':[1],'update':[{'id':1}]},{'update':[{'id':1,'set':{'id':2}}]},{'update':[{'id':1,'remove':['nope']}]},{'update':[{'id':1,'remove':['health','health']}]},{'update':[{'id':1,'remove':['health'],'set':{'health':2}}]},{'update':[{'id':1,'set':{1:2}}]}]:
    row=copy.deepcopy(good);row['players']=patch;bad.append(row)
for row in bad:
    for function,base,error in [(original.decode,ob,original.SnapshotDeltaError),(native.decode,nb,native.SnapshotDeltaError)]:
        try:function(row,base,True)
        except error:pass
        else:raise AssertionError(('invalid delta accepted',row,function))
    same(original.materialize(op),native.materialize(np))
native.decode(good,nb,True)
for values in [[True,1,1.,-0.,0.,None,u'中文'],[2**80,-2**90]]:
    row=snap();row['players'][0]['types']=values
    wire,unused=original.encode(row,None,1);pending,unused=native.decode(wire,None,True);same(row,native.materialize(pending))
plain=snap();actual,base=native.decode(plain,None,True);assert base is None;same(plain,actual);actual['players'][0]['x']=99;assert plain['players'][0]['x']==2.
# Runtime scalar canonicalization and compact-equipment recovery are compared
# against the complete fixed producer/consumer implementation.
base=dict(id=1,team=2,slot=0,vehicle='ussr:R11_MS-1',x=1.,y=2.,z=3.,yaw=.1,health=100,velocity=[1,2,3,4])
cases=[base,dict(base,velocity=(1,2,3)),dict(base,velocity=[1,2]),dict(base,stun_factors={}),None,[],{}]
for key in ('id','team','health','x','y','z','yaw','pitch','gun_pitch','speed'):
    for value in (True,None,'1','1.0',' 2 ',3,3.5,float('nan'),float('inf'),-0.,2000.1,-2000.1,1.e100):
        cases.append(dict(base,**{key:value}))
factors=dict((key,1.) for key in lan.stun_mechanics.STAT_CONFIG)
for value in (0.,.1,.1234565,.9999995,1.,1.2345675,99.9999999,100.,101.,'1',True):
    for key in ('mobility','reload'):
        row=dict(factors);row[key]=value;cases.append(dict(base,stun_factors=row))
for row in cases:same(lan._canonical_runtime_vehicle_row(row),native.canonical_row(row))
contract=dict(name='repairkit',tags=['repairkit'],cooldown=90.)
class Client(object):pass
client=Client();client.last_snapshot=dict(round_id=7,authority_epoch=1,bots=[dict(base,equipment_states=[dict(equipment=contract,usesLeft=1)])],players=[base])
message=dict(round_id=7,authority_epoch=1,bot_equipment_contracts=[contract])
rows=[dict(base,equipment_states=[dict(equipment=0,usesLeft=0)]),dict(base,equipment_states=[dict(equipment=9,usesLeft=0)]),dict(base,equipment_states=[dict(equipment=True,usesLeft=0)]),dict(base,equipment_states=[dict(equipment=0L,usesLeft=0)]),dict(base,equipment_states=[dict(equipment=contract,usesLeft=0)]),dict(base,x=float('nan')),dict(base,id=2,x=float('nan'))]
for scope in (message,dict(message,authority_epoch=2),dict(message,round_id=8)):
    for field in ('players','bots'):
        expected=lan.LANClient._canonical_runtime_rows.im_func(client,rows,field,scope)
        actual=native.canonical_rows(client,rows,field,scope);same(expected,actual)
one=native.canonical_rows(client,rows[:2],'bots',message)[0]
one[0]['equipment_states'][0]['equipment']['tags'].append('changed')
assert one[1]['equipment_states'][0]['equipment']['tags']==['repairkit']
assert contract['tags']==['repairkit']
# The generated decoder retains shared encode helpers and only native decode owners.
package_name=original.__name__.rsplit('.',1)[0]
sys.modules[package_name+'.stock_snapshot_static']=static
setattr(sys.modules[package_name],'stock_snapshot_static',static)
static_bindings=imp.load_source('test_static_bindings',sys.argv[2]+'/stock_snapshot_bindings.py')
transport_bindings=imp.load_source('test_transport_bindings',sys.argv[2]+'/stock_native_client_transport_bindings.py')
staged_source=transport_bindings.delta(static_bindings.delta(open(original.__file__.replace('.pyc','.py'),'rb').read()))
import ast
tree=ast.parse(staged_source)
declarations=set(node.name for node in tree.body if isinstance(node,(ast.FunctionDef,ast.ClassDef)))
assert not declarations.intersection(('_apply_rows','_PendingActors','SnapshotDeltaError'))
staged=types.ModuleType(original.__name__+'_native_shell');staged.__package__=package_name
exec(compile(staged_source,'staged_snapshot_delta.py','exec'),staged.__dict__)
assert not hasattr(staged,'_PENDING_ACTORS') and staged.SnapshotDeltaError is native.SnapshotDeltaError
assert staged.materialize is native.materialize
probe=snap();expected,unused=original.encode(probe,None,1);actual,unused=staged.encode(probe,None,1)
same(expected,actual)
pending,unused=staged.decode(actual,None,True);same(probe,staged.materialize(pending))
def bomb(*args,**kw):raise AssertionError('Python gameplay callback')
lan._canonical_runtime_vehicle_row=bomb;equipment.decode_snapshot_equipment=bomb;original._clone=bomb
assert native.canonical_rows(client,rows,'bots',message)[1]==2
assert native.materialize(np)['players'][0]['health']==100
print(json.dumps({'ordered_versions':len(held),'invalid_deltas':len(bad),'canonical_cases':len(cases),'python_business_callbacks':0}))
'''


class NativeClientTransportTests(unittest.TestCase):
    def test_native_decoder_and_actor_admission_match_fixed_source(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('Fixed v0.10.1 source input is required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], env=dict(os.environ, PYENV_VERSION='2.7.18'), text=True).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='native-client-transport-') as directory:
            binary = Path(directory) / 'offline_client_transport_native.so'
            command = ['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-DWOT_CLIENT_TRANSPORT_HOST_BUILD', '-bundle', '-undefined', 'dynamic_lookup', '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld'), str(ROOT / 'bigworld/stock_native_client_transport.cpp'), str(ROOT / 'tests/stock_native_client_transport_host.cpp'), '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            script = Path(directory) / 'fixture.py'
            script.write_text(FIXTURE)
            run = subprocess.run([py27, '-B', str(script), source, str(ROOT / 'bigworld')], capture_output=True, text=True, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('"python_business_callbacks": 0', run.stdout)
            print(run.stdout.strip())


if __name__ == '__main__':
    unittest.main()
