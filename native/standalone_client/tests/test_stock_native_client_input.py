"""Compare the visible input wire, queue and immutable JSON boundary on Python 2.7."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''# -*- coding: utf-8 -*-
from __future__ import print_function
import copy,json,math,random,sys,types
sys.path.insert(0,sys.argv[1]+'/src/res/scripts/client')
for name in ('gui','gui.mods'):
 m=types.ModuleType(name);m.__path__=[sys.path[0]+'/'+name.replace('.','/')];sys.modules[name]=m
from gui.mods.offline_lan_0922 import lan_client as lan
import offline_client_input_test as native
native.configure_input(lan)
native.landing_configure(lan)
def client(capabilities=True):
 c=lan.LANClient('localhost',28782,'P','ussr:R11_MS-1');c.phase='battle';c.ready=True;c.round_id=7;c.player_id=1;c.connected=True;c.running=True;c.sock=object();c._stopping=False;c._outbound_accepting=True
 c.capabilities=[lan.HUMAN_RAM_TIMELINE_CAPABILITY,lan.PLAYER_PROJECTILE_OWNER_CAPABILITY] if capabilities else []
 c.server_capabilities=list(c.capabilities);c.landing=[]
 # This differential isolates the old FIFO; the live native handoff is tested below.
 c._flush_native_outbound=lambda generation:None
 return c
def invoke(fn,args,kw):
 try:return ('ok',fn(*args,**kw))
 except Exception as e:return ('error',type(e).__name__)
def same(a,b):
 assert a==b,(a,b)
 if isinstance(a,float):
  assert math.copysign(1.,a)==math.copysign(1.,b),(a,b)
 if isinstance(a,dict):
  for k in a:same(a[k],b[k])
 if isinstance(a,(tuple,list)):
  for x,y in zip(a,b):same(x,y)
def run(kw,attrs=None,capabilities=True):
 a=client(capabilities);b=client(capabilities)
 for k,v in (attrs or {}).items():setattr(a,k,copy.deepcopy(v));setattr(b,k,copy.deepcopy(v))
 ra=invoke(a.send_input,(),copy.deepcopy(kw));rb=invoke(native.send_input,(b,),copy.deepcopy(kw));same(ra,rb)
 for k in ('_input_seq_round','_input_seq','_landing_observation_round','_landing_observation_seq','_landing_observation_pending','_landing_observation_queue','_outbound_queue','_outbound_seq','_outbound_bytes','landing'):
  same(getattr(a,k),getattr(b,k))
 same(a._outbound_event.is_set(),b._outbound_event.is_set())
 return a,b
base=dict(forward=1.,turn=-.3,position=(10.,20.,30.),yaw=7.,pitch=-7.,roll=.1,speed=20.,aim_yaw=8.,gun_pitch=-.2,shell_index=0,next_shell_index=0,shell_change_pending=False,pose_time_us=1234,gun_checkpoint=dict(reload_time=1.,reload_duration=5.,clip=1,clip_size=1,dispersion=.01),ram_contacts=[],tank_pushes=[],turret_pushes=[],destructible_contacts=[],up_cosine=.99)
random.seed(1513)
for i in range(300):
 row=copy.deepcopy(base);row.update(forward=random.uniform(-2.,2.),turn=random.uniform(-2.,2.),yaw=random.uniform(-100.,100.),aim_yaw=random.uniform(-100.,100.),gun_pitch=random.uniform(-3.,3.),fire_seq=random.randint(0,200),pose_time_us=i*40000)
 row['gun_checkpoint']['reload_time']=random.uniform(0.,5.);run(row)
cases=[]
for field,values in [('forward',[True,'1',None,float('nan'),float('inf')]),('turn',[False,'bad',[]]),('fire_seq',[True,False,-1,1.5,'4','1.5',2**64,None]),('position',[(2001.,0.,0.),(0.,1001.,0.),(0.,0.,-2001.),(float('nan'),None,'2'),(),(1.,2.)]),('up_cosine',[True,'0',-1.01,1.01,float('nan'),None]),('shell_index',[False,1.5,'2',10,-1,None]),('next_shell_index',[False,1.5,10,None]),('shell_change_pending',[1,None,True]),('pose_time_us',[False,1.5,'2',-1,10**16+1]),('gun_checkpoint',[None,{},dict(base['gun_checkpoint'],clip=True),dict(base['gun_checkpoint'],clip=2),dict(base['gun_checkpoint'],dispersion=float('nan')),dict(base['gun_checkpoint'],extra=0)]),('siege_enabled',[True,False,0,'yes']),('ram_contacts',[[dict(id=i,x=i) for i in range(20)],(dict(id=1),),[None,{},1]]),('tank_pushes',[[{'x':float('nan')}],[{'x':object()}]])]:
 for value in values:
  row=copy.deepcopy(base);row[field]=value;cases.append(row)
for row in cases:
 try:run(row)
 except Exception:print("failed input",repr(row));raise
for attributes in ({'ready':False},{'phase':'loading'},{'_input_seq_round':7,'_input_seq':lan.MAX_PROJECTILE_ID},{'_outbound_bytes':lan.MAX_OUTBOUND_BYTES},{'_outbound_queue':[(1,{},0)]*lan.MAX_OUTBOUND_MESSAGES},{'connected':False},{'_outbound_accepting':False},{'_stopping':True},{'_input_seq_round':6,'_input_seq':999,'_landing_observation_round':6,'_landing_observation_pending':{'retry_on_input':True}},{'_landing_observation_round':7,'_landing_observation_pending':{'retry_on_input':True,'sent':False,'refresh_input_on_retry':True,'wire':'old','observation_seq':1,'input_seq':1,'impact_speed':20.,'module_contacts':[]}}):run(base,attributes)
run(dict(forward=.1,turn=.2),capabilities=False)
# Generic freeze includes Unicode, Py2 byte strings, long integers, tuple order,
# signed zero, node/depth limits and caller mutation after accepted enqueue.
values=[None,False,True,2**200,-2**300,-0.,1.e300,'a\x00\\\xff',u'水\U0001f601',{'a':[1,{'b':2.}]},tuple(range(10))]
for v in values:same(lan._freeze_outbound(v,[0]),native.freeze(v))
bad=[float('nan'),float('inf'),{1:2},object(),[0]*lan.MAX_OUTBOUND_NODES]
v=0
for i in range(lan.MAX_OUTBOUND_DEPTH+2):v=[v]
bad.append(v)
for v in bad:
 assert invoke(lan._freeze_outbound,(v,[0]),{})[0]=='error'
 assert invoke(native.freeze,(v,),{})[0]=='error'
# A bounded persistent queue preserves every accepted ordered input and every
# discrete event; no latest-state coalescing is applied to a human checkpoint.
a=client();b=client()
for i in range(lan.MAX_OUTBOUND_MESSAGES+3):
 message=dict(type='event',index=i,nested={'value':[i]})
 same(a._send(message),native.send(b,message));message['nested']['value'].append(-1)
 same(a._outbound_queue,b._outbound_queue);same(a._outbound_bytes,b._outbound_bytes)
def bomb(*args,**kw):raise AssertionError('Python business callback')
b=client();b._send=b._enqueue_outbound=b.send_input=bomb
lan._freeze_outbound=lan._canonical_human_gun_checkpoint=lan._finite_float=lan._exact_int=bomb
assert native.send_input(b,**base)
assert native.send(b,{'type':'event','value':[1,2,3]})
print(json.dumps({'input_frames':300,'invalid_and_edge_cases':len(cases),'ordered_queue_messages':len(b._outbound_queue),'python_business_callbacks':0}))
'''
class NativeClientInputTests(unittest.TestCase):
    def test_native_producers_reach_tcp_without_a_python_ping(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('Fixed v0.10.1 source input is required')
        import importlib.util
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], env=dict(os.environ, PYENV_VERSION='2.7.18'), text=True).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        bindings = {}
        for name in ('stock_snapshot_bindings', 'stock_native_client_transport_bindings', 'stock_native_client_wire_bindings'):
            spec = importlib.util.spec_from_file_location(name, ROOT / 'bigworld' / (name + '.py'))
            bindings[name] = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(bindings[name])
        client = Path(source) / 'src/res/scripts/client'
        staged = (client / 'gui/mods/offline_lan_0922/lan_client.py').read_text()
        for name in bindings:
            staged = bindings[name].client(staged)
        with tempfile.TemporaryDirectory(prefix='native-input-transport-') as directory:
            folder = Path(directory)
            (folder / 'pkg').mkdir()
            (folder / 'pkg/lan_client.py').write_text(staged)
            (folder / 'pkg/stock_snapshot_static.py').write_text((ROOT / 'bigworld/stock_snapshot_static.py').read_text())
            (folder / 'offline_client_transport_native.py').write_text(
                'import offline_client_input_test as _input\n'
                'def configure_admission(*args):pass\n'
                'configure_input=_input.configure_input\nlanding_configure=_input.landing_configure\n')
            common = ['c++', '-std=c++17', '-O1', '-Wno-register', '-bundle', '-undefined', 'dynamic_lookup',
                      '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld')]
            input_sources = ['bigworld/stock_native_client_input.cpp', 'bigworld/stock_native_client_landing.cpp',
                             'tests/stock_native_client_input_host.cpp']
            result = subprocess.run(common + ['-DWOT_CLIENT_INPUT_HOST_BUILD'] + [str(ROOT / name) for name in input_sources] +
                                    ['-o', str(folder / 'offline_client_input_test.so')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            wire_sources = ['bigworld/stock_wire_codec.cpp', 'tests/stock_native_client_state_host.cpp',
                            'bigworld/stock_wire_replay.cpp', 'bigworld/stock_wire_net.cpp',
                            'bigworld/stock_wire_replica_store.cpp', 'tests/stock_wire_host_client_module.cpp']
            result = subprocess.run(common + ['-DWOT_WIRE_HOST_BUILD'] + [str(ROOT / name) for name in wire_sources] +
                                    ['-lz', '-o', str(folder / 'offline_client_wire_native.so')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([py27, '-B', str(ROOT / 'tests/stock_native_client_input_transport_fixture.py'),
                                     str(folder), str(client)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

    def test_native_input_matches_wire_and_queue(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('Fixed v0.10.1 source input is required')
        py27 = subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        prefix = subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        with tempfile.TemporaryDirectory(prefix='native-client-input-') as directory:
            binary=Path(directory)/'offline_client_input_test.so'
            command=['c++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-DWOT_CLIENT_INPUT_HOST_BUILD','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(ROOT/'bigworld'),str(ROOT/'bigworld/stock_native_client_input.cpp'), str(ROOT/'bigworld/stock_native_client_landing.cpp'),str(ROOT/'tests/stock_native_client_input_host.cpp'),'-o',str(binary)]
            result=subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            script=Path(directory)/'fixture.py';script.write_text(FIXTURE)
            result=subprocess.run([py27,'-B',str(script),source],capture_output=True,text=True,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'))
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            print(result.stdout.strip())
if __name__=='__main__':unittest.main()
