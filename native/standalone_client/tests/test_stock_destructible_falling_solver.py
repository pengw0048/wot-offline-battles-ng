"""Exact stock falling-body equilibrium math and actual SDK Python parser."""
import math
import os
from pathlib import Path
import random
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest
from native_test_compile_flags import native_test_compile_flags
from test_stock_ground_decals import ROOT,compiler_command,exact_client
from test_stock_screener import engine_sources,generated_source


class FallingSolverTests(unittest.TestCase):
    def test_original_iteration_and_deterioration_exit(self):
        client=exact_client(self)
        from stock_destructible_falling_solver_oracle import OriginalFallingSolver
        oracle=OriginalFallingSolver(Path(client)/'WorldOfTanks.exe')
        rng=random.Random(151322);cases=[(0.,1.,.4,-.8),(1.,1.,0.,0.),(1.,0.,0.,1.),(0.,0.,0.,0.)]
        cases += [(rng.uniform(-100,100),rng.uniform(.01,100),rng.uniform(-2,2),rng.uniform(-3,3))for n in range(4096)]
        expected=[];iterations=set()
        for values in cases:
            values=struct.unpack('<4f',struct.pack('<4f',*values))
            bits,calls,count=oracle.solve(values);expected.append(bits);iterations.add(count)
            self.assertEqual([row[2]for row in calls],[0,1,2,3])
        self.assertIn(1,iterations);self.assertIn(15,iterations)
        source='''#include "stock_destructible_falling_solver_data.hpp"
#include <cstdio>
int main(){float v[4];while(std::fread(v,4,4,stdin)==4){float result=wot::solveStockDestructibleFallPitch(v[0],v[1],v[2],v[3]);std::fwrite(&result,4,1,stdout);}}
'''
        with tempfile.TemporaryDirectory(prefix='stock-falling-solver-')as name:
            path=Path(name)/'probe.cpp';binary=Path(name)/'probe';path.write_text(source)
            subprocess.run(compiler_command(self)+['-I'+str(ROOT/'bigworld'),str(path),'-o',str(binary)],check=True)
            actual=subprocess.check_output([str(binary)],input=b''.join(struct.pack('<4f',*v)for v in cases))
        actual=struct.unpack('<%dI'%len(cases),actual)
        for index,(a,b)in enumerate(zip(actual,expected)):
            av,bv=struct.unpack('<2f',struct.pack('<2I',a,b))
            self.assertTrue(a==b or math.isnan(av)and math.isnan(bv),(index,cases[index],hex(a),hex(b)))
        print('Original falling solver: %d cases match with shared sinf boundary; observed iteration counts %s'%(len(cases),sorted(iterations)))

    def test_original_argument_count_and_conversion_failures(self):
        client=exact_client(self)
        from stock_destructible_falling_solver_oracle import OriginalFallingSolver
        oracle=OriginalFallingSolver(Path(client)/'WorldOfTanks.exe')
        for argc in (0,1,2,3,5):
            result,calls,count=oracle.solve((1,2,3,4),argc)
            self.assertIsNone(result);self.assertEqual(calls[0][0],'argCountError');self.assertEqual(count,0)
        for failed in range(4):
            result,calls,count=oracle.solve((1,2,3,4),fail_at=failed)
            self.assertIsNone(result);self.assertEqual([r[2]for r in calls],list(range(failed+1)));self.assertEqual(count,0)

    def test_complete_production_binding_over_sdk_parser(self):
        if not shutil.which('c++'):self.skipTest('Optional C++ compiler is unavailable: c++')
        engine=engine_sources(self);source=generated_source(engine)
        production=(ROOT/'bigworld/stock_destructible_falling_solver.cpp').read_text()
        production='\n'.join(line for line in production.splitlines()if not line.startswith('#include'))
        addition='''#include "stock_destructible_falling_solver_data.hpp"
namespace BW {namespace Script {PyObject* getRetData(float value){return PyFloat_FromDouble(value);}}}
'''+production
        source=source.replace('#include "stock_screener.cpp"',addition)
        begin=source.index('static PyObject * hostState(');end=source.index('// Runs every recorded',begin)
        source=source[:begin]+source[end:]
        source=source[:source.index('static PyMethodDef hostMethods[] = {')]+'''
int main(){Py_Initialize();
 PyObject* valid=Py_BuildValue("(ffff)",0.,1.,.4,-.8);PyObject* result=BW::_py_wg_solveDestructibleFallPitch(NULL,valid);
 assert(result&&PyFloat_Check(result)&&!PyErr_Occurred());Py_DECREF(result);Py_DECREF(valid);
 for(int count=0;count<=5;++count){if(count==4)continue;PyObject* args=PyTuple_New(count);
  for(int n=0;n<count;++n)PyTuple_SET_ITEM(args,n,PyFloat_FromDouble(1));
  result=BW::_py_wg_solveDestructibleFallPitch(NULL,args);assert(!result&&PyErr_ExceptionMatches(PyExc_TypeError));PyErr_Clear();Py_DECREF(args);
 }
 for(int failed=0;failed<4;++failed){PyObject* args=PyTuple_New(4);for(int n=0;n<4;++n)PyTuple_SET_ITEM(args,n,n==failed?PyString_FromString("bad"):PyFloat_FromDouble(1));
  result=BW::_py_wg_solveDestructibleFallPitch(NULL,args);assert(!result&&PyErr_ExceptionMatches(PyExc_TypeError));PyErr_Clear();Py_DECREF(args);
 }
 Py_Finalize();}
'''
        config=Path(os.environ.get('WOT_PYTHON27','/Users/peng/.pyenv/versions/2.7.18/bin/python2.7')).with_name('python2.7-config')
        if not config.is_file():self.skipTest('CPython 2.7 development files required')
        flags=[v for v in shlex.split(subprocess.check_output([str(config),'--cflags'],text=True))if v!='-DNDEBUG']
        libs=shlex.split(subprocess.check_output([str(config),'--ldflags'],text=True))
        with tempfile.TemporaryDirectory(prefix='stock-falling-solver-binding-')as name:
            path=Path(name)/'binding.cpp';binary=Path(name)/'binding';path.write_text(source)
            subprocess.run(['c++',*native_test_compile_flags('c++',sanitise=False),'-std=c++14','-UNDEBUG',*flags,
                '-Wall','-Werror','-Wno-writable-strings','-Wno-deprecated-register','-Wno-unused-function',
                '-I'+str(ROOT/'bigworld'),str(path),*libs,'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)


if __name__=='__main__':unittest.main()
