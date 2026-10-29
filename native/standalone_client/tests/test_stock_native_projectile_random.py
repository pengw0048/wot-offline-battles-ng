"""Native draws share CPython 2.7's MT and Gaussian spare with Python draws."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
import random,types,sys
import projectile_random_test as native
assert type(random._inst.random) is types.BuiltinMethodType
native.install(random._inst)
for seed in (0,1513,144619,2**63):
    random.seed(seed)
    state=random.getstate()
    actual=[]
    for i in xrange(1000):
        if i%5==0:actual.append(random.gauss(20.,3.))
        elif i%5==1:actual.append(native.gauss(25.,4.))
        elif i%5==2:actual.append(random.random())
        elif i%5==3:actual.append(native.uniform())
        else:actual.append(native.gauss(-7.,-.5))
    final=random.getstate()
    random.setstate(state)
    expected=[]
    for i in xrange(1000):
        if i%5==0:expected.append(random.gauss(20.,3.))
        elif i%5==1:expected.append(random.gauss(25.,4.))
        elif i%5==2:expected.append(random.random())
        elif i%5==3:expected.append(random.random())
        else:expected.append(random.gauss(-7.,-.5))
    assert actual==expected,(seed,[(a,b) for a,b in zip(actual,expected) if a!=b][:3])
    assert random.getstate()==final
class Scripted(object):
    gauss_next=None
    def random(self):raise AssertionError('Python callback must not run')
try:native.install(Scripted())
except TypeError:pass
else:raise AssertionError('Python business callback accepted')
# The failed replacement does not disturb the already bound native owner.
state=random.getstate();actual=native.uniform();random.setstate(state);assert actual==random.random()
native.close()
print('4000 shared-stream draws matched exact CPython 2.7')
'''
class NativeProjectileRandom(unittest.TestCase):
    def test_shared_python27_stream(self):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:self.skipTest('Fixed source input is required')
        py27=os.environ.get('WOT_PYTHON27') or subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        with tempfile.TemporaryDirectory(prefix='projectile-random-') as folder:
            folder=Path(folder);binary=folder/'projectile_random_test.so';script=folder/'fixture.py';script.write_text(FIXTURE)
            command=['c++','-std=c++17','-O0','-ffp-contract=off','-Wall','-Wextra','-Werror','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(Path(source)/'native'),'-I'+str(ROOT/'bigworld'),str(ROOT/'bigworld/stock_native_projectile_random.cpp'),str(ROOT/'tests/stock_native_projectile_random_host.cpp'),'-o',str(binary)]
            subprocess.run(command,check=True)
            subprocess.run([py27,'-B',str(script)],check=True)
if __name__=='__main__':unittest.main()
