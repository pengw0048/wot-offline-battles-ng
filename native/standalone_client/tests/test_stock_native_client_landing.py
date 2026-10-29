"""Native landing queue matches the actual pinned CPython 2.7 LAN owner."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
SCRIPT=r'''
import copy,json,math,random,sys,types
sys.path.insert(0,sys.argv[1]+'/src/res/scripts/client')
for name in ('gui','gui.mods'):
    module=types.ModuleType(name);module.__path__=[sys.path[0]+'/'+name.replace('.','/')];sys.modules[name]=module
from gui.mods.offline_lan_0922 import lan_client as lan,environment_modules as environment
import client_landing_test as native
native.landing_configure(lan)
def bomb(*args,**kw):raise AssertionError('Python landing business callback ran')
fields=('_landing_observation_round','_landing_observation_seq','_landing_observation_pending','_landing_observation_queue','_input_seq','_input_seq_round','round_id','authority_epoch','_attempts')
def client(compiled=False):
    c=lan.LANClient('localhost',28782,'P','ussr:R11_MS-1');c.phase='battle';c.ready=True;c.player_id=4;c.round_id=11;c.authority_epoch=7;c._input_seq=5;c._input_seq_round=11;c.capabilities=[lan.PLAYER_ENVIRONMENT_CAPABILITY];c.server_capabilities=list(c.capabilities);c._attempts=[];c._send_ok=True
    if compiled:c._send=c._send_pending_landing_observation=c._start_queued_landing_observation=bomb
    else:
        def send(message):c._attempts.append(copy.deepcopy(message));return c._send_ok
        c._send=send
    return c
steps=[0]
def same(a,b):
    for name in fields:assert getattr(a,name)==getattr(b,name),(name,getattr(a,name),getattr(b,name))
def observe(a,b,impact,contacts=None):
    x=a.send_landing_observation(impact,contacts);y=native.landing_send_observation(b,impact,contacts);assert x==y,(impact,contacts,x,y);assert (x is False)==(y is False);same(a,b);steps[0]+=1;return x
def pending(a,b,rebind=False):
    x=a._send_pending_landing_observation(rebind);y=native.landing_send_pending(b,rebind=rebind);assert x==y;same(a,b);steps[0]+=1

def ack(a,b,accepted=True,reason='',epoch=None,committed=None):
    p=a._landing_observation_pending;seq=p['observation_seq'];input_seq=p['input_seq']
    message=dict(type='landing_observation_result',round_id=a.round_id,authority_epoch=a.authority_epoch if epoch is None else epoch,observation_seq=seq,input_seq=input_seq,committed_seq=seq if accepted else (a._landing_observation_seq if committed is None else committed),accepted=accepted,reason=reason)
    b._start_queued_landing_observation=lambda:native.landing_start_queued(b)
    b._send_pending_landing_observation=lambda rebind=False:native.landing_send_pending(b,rebind)
    try:assert a._handle_landing_observation_result(message)==b._handle_landing_observation_result(message)
    finally:b._send_pending_landing_observation=b._start_queued_landing_observation=bomb
    same(a,b);steps[0]+=1
# Numeric and contact validation, including exact int-vs-long source behavior.
cases=[None,False,0,0.,'',{},[],(),[dict(name='engineHealth',weight=.3),dict(name='engineHealth',weight=.9),dict(name='loader1Health',weight=1.)],(dict(name='gunHealth',weight=.25),)]
cases += [[dict(name='engineHealth',weight=w)] for w in (True,False,0.,-1.,1.1,float('nan'),float('inf'),long(1),'1')]
cases += [[dict(name=n,weight=.5)] for n in ('fakeHealth',[],1,None)]
cases += [[dict(name='engineHealth',weight=.5,extra=1)],[dict(name='engineHealth')],['bad'],[dict(name='driverHealth',weight=.5)]*33]
for contacts in cases:
    a,b=client(),client(True);observe(a,b,17.12345675,contacts)
for value in (False,True,None,'2',-1.,200.000001,float('nan'),float('inf'),float('-inf')):
    a,b=client(),client(True);observe(a,b,value)
# Every gate contains this observation and leaves its ledger unchanged.
for key,value in (('phase','waiting'),('ready',False),('player_id',None),('player_id',0),('capabilities',[]),('server_capabilities',[])):
    a,b=client(),client(True);setattr(a,key,value);setattr(b,key,value);observe(a,b,20.)
for key,value in (('_input_seq',0),('_input_seq_round',10),('round_id',None),('authority_epoch',None)):
    a,b=client(),client(True);setattr(a,key,value);setattr(b,key,value);observe(a,b,20.)
# An unreported failed first send is the same landing operation on retry.
a,b=client(),client(True);a._send_ok=b._send_ok=False
assert observe(a,b,19.4,[dict(name='engineHealth',weight=.4)]) is False
assert observe(a,b,25.0,[dict(name='gunHealth',weight=1.)]) is False
a._send_ok=b._send_ok=True;a._input_seq=b._input_seq=8
assert observe(a,b,29.)==1
assert a._attempts[-1]['impact_speed']==19.4 and a._attempts[-1]['input_seq']==8
# A full queue rejects one new item without discarding its existing operations.
for i in range(lan.MAX_LANDING_OBSERVATION_QUEUE+2):observe(a,b,10.+i)
assert len(a._landing_observation_queue)==lan.MAX_LANDING_OBSERVATION_QUEUE
ack(a,b);assert a._landing_observation_pending['observation_seq']==2
# Stale-input requires a newer input edge; stale authority rebinds its epoch.
ack(a,b,False,'stale_input');a._input_seq=b._input_seq=20;pending(a,b,True)
ack(a,b,False,'stale_authority',epoch=8);assert a._attempts[-1]['authority_epoch']==8
ack(a,b,False,'sequence_gap',committed=7);assert a._landing_observation_pending['observation_seq']==8
ack(a,b,False,'identity_conflict');assert a._landing_observation_pending is None and a._landing_observation_queue==[]
# Failed dequeues preserve retry_on_input and do not require another impact.
a,b=client(),client(True);observe(a,b,15.);observe(a,b,16.);a._send_ok=b._send_ok=False;ack(a,b)
assert a._landing_observation_pending['retry_on_input'] is True
observe(a,b,17.);a._send_ok=b._send_ok=True;pending(a,b,True)
# Exact CPython-2 round(impact,6), including binary-rational half ties.
rng=random.Random(1513)
values=[0.,-0.,.0078125,.0234375,.9999995,1.2345625,200.,long(17)]
values += [rng.uniform(0.,200.) for unused in range(1600)]
for value in values:
    a,b=client(),client(True);observe(a,b,value,[dict(name='driverHealth',weight=.123456789)])
# Mixed ACK, retry, queued and round-reset behavior across a long real ledger.
a,b=client(),client(True)
for i in range(420):
    op=rng.randrange(7)
    if op<3:observe(a,b,rng.uniform(0.,200.),rng.choice(cases[:10]))
    elif op==3:
        a._send_ok=b._send_ok=bool(rng.randrange(2));pending(a,b,bool(rng.randrange(2)))
    elif op==4:
        a._input_seq+=1;b._input_seq=a._input_seq
        if a._landing_observation_pending:pending(a,b,True)
    elif op==5 and a._landing_observation_pending:
        accepted=rng.random()<.7;ack(a,b,accepted,'' if accepted else rng.choice(('stale_input','stale_authority','sequence_gap')))
    elif op==6:
        a.round_id+=1;b.round_id=a.round_id;a._input_seq_round=b._input_seq_round=a.round_id;observe(a,b,18.)
    same(a,b)
# Once initialized, normalization and legacy sender helpers are forbidden.
environment.normalize_contacts=bomb
a,b=client(),client(True);assert native.landing_send_observation(b,10.,[dict(name='engineHealth',weight=.7)])==1
assert native.landing_send_pending(b,True)
b._landing_observation_queue.append(dict(input_seq=5,impact_speed=11.,module_contacts=[]))
assert native.landing_start_queued(b)
print(json.dumps(dict(landing_edges=steps[0],python_business_callbacks=0)))
'''

@unittest.skipUnless(os.environ.get('WOT_FORK_0101_SOURCE'),'Pinned gameplay source is required')
class NativeLanding(unittest.TestCase):
    def test_exact_python27_landing_queue(self):
        source=Path(os.environ['WOT_FORK_0101_SOURCE'])
        py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        with tempfile.TemporaryDirectory(prefix='native-client-landing-') as directory:
            folder=Path(directory);script=folder/'fixture.py';script.write_text(SCRIPT)
            command=['c++','-std=c++17','-O1','-ffp-contract=off','-Wno-register','-Wall','-Wextra','-Werror','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(ROOT/'bigworld'),str(ROOT/'bigworld/stock_native_client_landing.cpp'),str(ROOT/'tests/stock_native_client_landing_host.cpp'),'-o',str(folder/'client_landing_test.so')]
            subprocess.run(command,check=True);subprocess.run([py27,'-B',str(script),str(source)],check=True)

if __name__=='__main__':unittest.main()
