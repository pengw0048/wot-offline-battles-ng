"""Native authority/body law against exact #1513 CPython 2.7 bytecode."""
import json
import math
import os
from pathlib import Path
import random
import shutil
import subprocess
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags

ROOT = Path(__file__).resolve().parents[1]

ORACLE = r'''
import json,math,marshal,sys,types,zipfile
rows=json.load(sys.stdin)
archive=zipfile.ZipFile(sys.argv[1]+'/res/packages/scripts.pkg')
def find(code,name):
 if code.co_name==name:return code
 for child in code.co_consts:
  if isinstance(child,types.CodeType):
   result=find(child,name)
   if result is not None:return result
def load(path,name,environment):
 code=marshal.loads(archive.read(path)[8:])
 environment['__builtins__']=__builtins__
 return types.FunctionType(find(code,name),environment)
class Owner(object):
 _DestructiblesAnimator__STOP_SIMULATION_EPSILON=math.pi/500
 _DestructiblesAnimator__MAX_SIMULATION_DURATION=8.
class Physics(object):G=9.81
move=load('scripts/client/AreaDestructibles.pyc','__moveBody',dict(physics_shared=Physics,math=math))
keys=('pitch','pitchSpeed','inertiaMoment','mass','height','springAngle','springStiffnes','springResist','airResist','pitchConstr','simulationTime','buryDepth')
result=[]
for row in rows['body']:
 body=dict(zip(keys,row[:12]));touched=[]
 if row[13]:body['touchdownCallback']=lambda:touched.append(True)
 active=move(Owner(),body,row[12])
 result.append([bool(active),bool(touched),body['pitch'],body['pitchSpeed'],body['simulationTime']])
env=dict(PI=math.pi,PI_2=2.*math.pi)
encode=load('scripts/common/DestructiblesCache.pyc','encodeFallenTree',env)
decode=load('scripts/common/DestructiblesCache.pyc','decodeFallenTree',env)
angles=[]
for yaw,pitch in rows['encode']:
 value=decode(encode(1,yaw,pitch,1));angles.append([value[1],value[2]])
json.dump(dict(body=result,encode=angles),sys.stdout)
'''


class NativeDestructibleRuntimeTest(unittest.TestCase):
    def test_body_and_authority_lifecycle(self):
        fork = os.environ.get('WOT_FORK_0101_SOURCE')
        if not fork:
            self.skipTest('Fixed v0.10.1 source input is required')
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        with tempfile.TemporaryDirectory(prefix='native-destructible-runtime-') as folder:
            binary = Path(folder) / 'destructible-runtime'
            big = ROOT / 'bigworld'
            command = [compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                *native_test_compile_flags(compiler), '-I' + str(Path(fork) / 'native'),
                '-I' + str(big), str(ROOT / 'tests/test_stock_native_destructible_runtime.cpp'),
                str(big / 'stock_native_destructible_runtime.cpp'), '-o', str(binary)]
            subprocess.run(command, check=True, capture_output=True, text=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn('Native destructible runtime tests passed', result.stdout)
            client = os.environ.get('WOT_0922_CLIENT')
            if not client:
                return
            python = subprocess.run(['pyenv', 'which', 'python2.7'], env={**os.environ,
                'PYENV_VERSION': '2.7.18'}, check=True, capture_output=True, text=True).stdout.strip()
            rng = random.Random(1513)
            rows = []
            for index in range(250):
                height, mass = rng.uniform(.1, 30.), rng.uniform(0., 10000.)
                pitch_limit = rng.uniform(.1, 2.)
                rows.append([rng.uniform(-.1, pitch_limit + .1), rng.uniform(-2., 2.),
                    0. if index % 11 == 0 else mass * height * height / 3., mass, height,
                    rng.uniform(0., .5), rng.uniform(0., 10000.), rng.uniform(0., 1000.),
                    rng.uniform(0., 100.), pitch_limit, 8.01 if index % 7 == 0 else rng.uniform(0., 7.9),
                    rng.uniform(0., 1.), rng.uniform(.001, .02), index % 2])
            angles = [(rng.uniform(-50., 50.), rng.uniform(-math.pi, math.pi)) for _ in range(180)]
            angles += [(value, value) for value in (0., math.pi, -math.pi, 1.e12, -1.e12)]
            expected = json.loads(subprocess.run([python, '-c', ORACLE, client],
                input=json.dumps(dict(body=rows, encode=angles)), check=True,
                capture_output=True, text=True).stdout)
            result = subprocess.run([str(binary), 'body'], input='\n'.join(
                ' '.join(map(repr, row)) for row in rows) + '\n', check=True,
                capture_output=True, text=True)
            native = [[bool(int(fields[0])), bool(int(fields[1])), *map(float, fields[2:])]
                      for fields in map(str.split, result.stdout.splitlines())]
            for left, right in zip(expected['body'], native):
                self.assertEqual(left[:2], right[:2])
                for a, b in zip(left[2:], right[2:]):
                    self.assertAlmostEqual(a, b, delta=1.e-12 * max(1., abs(a)))
            result = subprocess.run([str(binary), 'encode'], input='\n'.join(
                ' '.join(map(repr, row)) for row in angles) + '\n', check=True,
                capture_output=True, text=True)
            self.assertEqual(expected['encode'], [list(map(float, line.split()))
                                                  for line in result.stdout.splitlines()])


if __name__ == '__main__':
    unittest.main()
