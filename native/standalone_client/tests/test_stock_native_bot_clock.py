"""Compare complete callback banking to the fixed BotRuntime.update source."""
import json
import math
import os
from pathlib import Path
import random
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = os.environ.get('WOT_FORK_0101_SOURCE')

ORACLE = r'''
import json,sys,textwrap
source=open(sys.argv[1]).read()
def method(name):
    start=source.index('    def '+name+'(')
    end=source.find('\n    def ',start+1)
    chunk=source[start:end if end>=0 else len(source)]
    lines=chunk.splitlines()
    while lines and (not lines[-1].strip() or lines[-1].lstrip().startswith('@')):
        lines.pop()
    return textwrap.dedent('\n'.join(lines))
class Runtime(object): pass
scope={'MAX_CONTROL_ELAPSED_SECONDS':.2,'timed_call':lambda d,n,f,*args:f(*args)}
for name in ('_bounded_burst_step','_bank_control_step','update'):
    exec(method(name),scope)
    setattr(Runtime,name,scope[name])
class Burst(object): active=False;time_left=0.
class Navigator(object):
    def begin_frame(self,dt): pass
    def tick(self,now): pass
    def end_frame(self): pass
r=Runtime();r._accumulator=0.;r._control_refresh_accumulator=0.;r._control_seconds=.1
r._fixed_control=True;r._sample_time_us=0;r._next_publication=0.;r._burst_states={1:Burst()}
r._equipment_wire_exposed_in_update=set();r.adapter=object();r.finished=False
r.navigator=Navigator();r._native_simulation=None;r._combat_diagnostics=None
r.is_authority=lambda:True
for name in ('_begin_world_receipt_frame','_finish_world_receipt_frame','_begin_visibility_frame','_finish_visibility_frame','_apply_traffic_wait_lease','_scan_moving_destructible_bodies'):
    setattr(r,name,lambda:None)
def step(dt,now,players,neighbours,refresh,publish,evaluation):
    start=r._sample_time_us;r._sample_time_us+=max(1,int(round(dt*1000000.)))
    if r._next_publication<=0.:r._next_publication=now
    while r._next_publication<=now+1.e-9:r._next_publication+=r._control_seconds
    r.rows.append([dt,now,int(refresh),int(publish),start,r._sample_time_us])
    b=r._burst_states[1]
    if b.active:
        b.time_left-=dt
        if b.time_left<=1.e-9:b.active=False
    return []
r._run_update_once=step
for line in sys.stdin:
    dt,now,action=json.loads(line);b=r._burst_states[1]
    if action==1:b.active=True;b.time_left=.035
    if action==2:b.active=False;b.time_left=0.
    r.rows=[];r.update(dt,now)
    print(json.dumps([r.rows,r._accumulator,r._control_refresh_accumulator,r._sample_time_us,r._last_update_control_steps,r._last_update_max_control_step,r._next_publication]))
'''

@unittest.skipUnless(SOURCE, 'set WOT_FORK_0101_SOURCE to the fixed gameplay checkout')
class ClockDifferential(unittest.TestCase):
    def test_variable_callbacks_and_burst_deadlines(self):
        rng = random.Random(1513)
        cases=[]; now=10.
        for i in range(600):
            elapsed=rng.choice([0.,.001,.016,.035,.05,.1,.199999999,.2,.43,1.7])
            now+=elapsed
            cases.append([elapsed,now,1 if i%7==0 else 2 if i%13==0 else 0])
        with tempfile.TemporaryDirectory(prefix='wot-native-clock-') as directory:
            folder=Path(directory); binary=folder/'clock'; oracle=folder/'oracle.py'
            oracle.write_text(ORACLE)
            subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',
                '-I',str(ROOT/'bigworld'),str(ROOT/'bigworld/stock_native_bot_clock.cpp'),
                str(ROOT/'tests/stock_native_bot_clock_host.cpp'),'-o',str(binary)],check=True)
            actual=subprocess.check_output([str(binary)],text=True,input='\n'.join(' '.join(map(str,row)) for row in cases)+'\n')
            python=subprocess.check_output(['pyenv','which','python2.7'],text=True,env=dict(os.environ,PYENV_VERSION='2.7.18')).strip()
            reference=subprocess.check_output([python,str(oracle),str(Path(SOURCE)/'src/res/scripts/client/gui/mods/offline_lan_0922/bot_runtime.py')],
                text=True,input='\n'.join(json.dumps(row) for row in cases)+'\n',env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'))
            aa=[json.loads('['+line+']') for line in actual.splitlines()]
            ee=[json.loads(line) for line in reference.splitlines()]
            self.assertEqual(len(aa),len(cases)); self.assertEqual(len(ee),len(cases))
            def compare(a,e,path):
                if isinstance(e,list):
                    self.assertEqual(len(a),len(e),path)
                    for i,(left,right) in enumerate(zip(a,e)):compare(left,right,path+'/'+str(i))
                else:self.assertTrue(math.isclose(a,e,rel_tol=2.e-13,abs_tol=2.e-13), '%s: %r != %r'%(path,a,e))
            for i,(a,e) in enumerate(zip(aa,ee)):compare(a,e,str(i))

if __name__=='__main__':unittest.main()
