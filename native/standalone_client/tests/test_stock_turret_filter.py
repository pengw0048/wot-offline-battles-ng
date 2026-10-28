"""Exact turret gravity hook, SDK factory and DetachedTurret lifecycle."""
from pathlib import Path
import importlib.util
import json
import struct
import os
import shlex
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

from native_test_compile_flags import native_test_compile_flags
from test_stock_entity_filter import block, write_public_fixture, patch_module

ROOT = Path(__file__).resolve().parents[1]


def build_host(folder):
    engine = Path(os.environ.get('WOT_BIGWORLD_ROOT', '/tmp/wot-bigworld-engine-14.4.1/programming/bigworld'))
    python = Path(os.environ.get('WOT_PYTHON27', str(Path.home()/'.pyenv/versions/2.7.18/bin/python2.7')))
    config = python.with_name('python2.7-config')
    archive = Path(os.environ.get('WOT_BIGWORLD_SOURCE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not config.is_file() or not archive.is_file() or not (engine/'lib/pyscript/pyobject_plus.hpp').is_file():
        raise unittest.SkipTest('Pinned SDK archive, source and CPython 2.7 development files required')
    if not shutil.which('c++'):
        raise unittest.SkipTest('C++ compiler unavailable: c++')
    root = Path(folder)
    with mock.patch.dict(os.environ, {'WOT_BIGWORLD_SOURCE_ARCHIVE': str(archive)}):
        write_public_fixture(root)
        patch_module().patch_engine(root)
    prefix = (ROOT/'tests/stock_trajectory_drawer_host.cpp').read_text().split('struct D3DXMATRIX')[0]
    prefix = prefix.replace('#include "wot/stock_trajectory_drawer.hpp"', '#include <cfloat>\n#include <cstddef>\n#include <cstdarg>')
    prefix = prefix.replace('    float length()const', '''    static Vector3 zero(){return {};}
    void set(float a,float b,float c){x=a;y=b;z=c;}
    void setZero(){x=y=z=0;}
    float& operator[](unsigned i){return (&x)[i];}
    const float& operator[](unsigned i)const{return (&x)[i];}
    Vector3& operator*=(float value){x*=value;y*=value;z*=value;return *this;}
    Vector3& operator+=(Vector3 value){x+=value.x;y+=value.y;z+=value.z;return *this;}
    float length()const''')
    plus = (engine/'lib/pyscript/pyobject_plus.hpp').read_text()
    cpp = (engine/'lib/pyscript/pyobject_plus.cpp').read_text()
    prefix = prefix.replace('@PYOBJECT_PLUS_HEADER@', plus[plus.index('/// This must be the first line'):plus.index('// Section: Weak-referenceable macros')])
    prefix = prefix.replace('@PYOBJECT_PLUS_SOURCE@', cpp[cpp.index('// Base needs to be 0.'):cpp.index('// Section: PyObjectPlusWithWeakReference')])
    link = (engine/'lib/pyscript/py_factory_method_link.hpp').read_text()
    link_cpp = (engine/'lib/pyscript/py_factory_method_link.cpp').read_text()
    factory = '''namespace Script {
struct InitTimeJob {explicit InitTimeJob(int){};virtual ~InitTimeJob(){};virtual void init()=0;};
struct FiniTimeJob {virtual ~FiniTimeJob(){};virtual void fini()=0;};
PyObject* argCountError(const char*,int,int,...);
template<class T> PyObject* getReadOnlyData(T value){return getData(value);}
template<class T> PyObject* getRetData(T value){return getData(value);}
int setData(PyObject* value,Vector3& output,const char*){return setData(value,output);}
int setData(PyObject* value,unsigned& output,const char* =""){output=unsigned(PyLong_AsUnsignedLongMask(value));return PyErr_Occurred()?-1:0;}
int setData(PyObject* value,double& output,const char* =""){output=PyFloat_AsDouble(value);return PyErr_Occurred()?-1:0;}
}
namespace BW {using string=std::string;}
#define BW_STATIC_ASSERT(expression,message) static_assert(expression,#message)
#define bw_offsetof_from(TYPE,MEMBER,SUPERTYPE) (reinterpret_cast<char*>(&(reinterpret_cast<TYPE*>(256)->MEMBER))-reinterpret_cast<char*>(static_cast<SUPERTYPE*>(reinterpret_cast<TYPE*>(256))))
#define bw_container_of(ptr,type,member) reinterpret_cast<type*>(reinterpret_cast<char*>(ptr)-offsetof(type,member))
#define IF_NOT_MF_ASSERT_DEV(expression) if(!(expression))
'''+block(link,'class PyFactoryMethodLink :')+';\n'
    factory += '\n'.join(block(link_cpp, marker) for marker in [
        'PyFactoryMethodLink::PyFactoryMethodLink(', 'PyFactoryMethodLink::~PyFactoryMethodLink()',
        'void PyFactoryMethodLink::init()', 'void PyFactoryMethodLink::fini()'])
    factory += block((engine/'lib/pyscript/script.cpp').read_text(), 'PyObject * Script::argCountError(')
    script = (engine/'lib/pyscript/script.hpp').read_text()
    macros = script[script.index('#if !defined(_MSC_VER) || defined(__clang__)'):script.index('// Section: PyModuleLink')]
    py_header = (root/'client/py_filter.hpp').read_text()
    py_cpp = (root/'client/py_filter.cpp').read_text()
    entity_header = (ROOT/'bigworld/stock_entity_filter.hpp').read_text()
    entity_cpp = (ROOT/'bigworld/stock_entity_filter.cpp').read_text()
    methods = '\n'.join(block(entity_cpp, marker) for marker in [
        'PyWGEntityFilter::PyWGEntityFilter(', 'StockEntityFilter * PyWGEntityFilter::native()',
        'const StockEntityFilter * PyWGEntityFilter::native()', 'bool PyWGEntityFilter::isLaggingNow() const',
        'bool PyWGEntityFilter::ignoreInputs() const', 'void PyWGEntityFilter::ignoreInputs(',
        'float PyWGEntityFilter::maxTimeBetweenInputs() const', 'void PyWGEntityFilter::maxTimeBetweenInputs(',
        'void PyWGEntityFilter::cacheSettings(', 'void PyWGEntityFilter::applySettings(',
        'Filter * PyWGEntityFilter::getNewFilter()', 'StockEntityFilter::StockEntityFilter(',
        'StockEntityFilter::~StockEntityFilter()', 'void StockEntityFilter::retire()',
        'void StockEntityFilter::setScriptInputCallback(', 'void StockEntityFilter::onReset()',
        'void StockEntityFilterGeometry::invalidate()', 'bool StockEntityFilterGeometry::segmentMayHit(',
        'bool PyWGEntityFilter::segmentMayHitEntity(',
        'bool StockEntityFilter::tryCopyState(', 'void StockEntityFilter::updateVelocityReceipts('])
    host = (ROOT/'tests/stock_turret_filter_host.cpp').read_text()
    values = {'SDK_PREFIX': prefix, 'FACTORY_LINK': factory, 'AUTO_PARSE': macros,
              'WEAK_HEADER': plus[plus.index('template< class PYWEAKREFERENCABLE >'):plus.index('/**\n *\tThis macro allows a class')],
              'WEAK_SOURCE': cpp[cpp.index('PY_TYPEOBJECT_WITH_WEAKREF( PyObjectPlusWithWeakReference )'):cpp.index('BW_END_NAMESPACE', cpp.index('// Section: PyObjectPlusWithWeakReference'))],
              'PY_FILTER_CLASS': block(py_header, 'class PyFilter :'),
              'PY_FILTER_METHODS': '\n'.join(block(py_cpp, marker) for marker in ['PyFilter::PyFilter(', 'PyFilter::~PyFilter()', 'void PyFilter::reset(', 'Filter * PyFilter::getFilter()', 'void PyFilter::onFilterDestroyed(']),
              'PY_FILTER_TABLES': py_cpp[py_cpp.index('PY_TYPEOBJECT_WITH_WEAKREF( PyFilter )'):py_cpp.index('PY_SCRIPT_CONVERTERS( PyFilter )')],
              'GLOBALS': block(entity_header,'struct StockEntityFilterGlobals')+';\n'+entity_cpp[entity_cpp.index('bool StockEntityFilterGlobals::'):entity_cpp.index('bool StockEntityFilterGeometry::matches')],
              'GEOMETRY': block(entity_header,'struct StockEntityFilterGeometry'),
              'SETTINGS': block(entity_header,'struct StockEntityFilterSettings')+';',
              'ENTITY_FILTER_CLASS': block(entity_header,'class StockEntityFilter :'),
              'BASE_METHODS': methods, 'BASE_WAYPOINT': block(entity_cpp,'void StockEntityFilter::onWaypoint(')}
    for key, value in values.items():host=host.replace('@'+key+'@',value)
    (root/'host.cpp').write_text(host)
    for name in ('stock_turret_filter.hpp','stock_turret_filter.cpp'):
        (root/name).write_bytes((ROOT/'bigworld'/name).read_bytes())
    (root/'stock_entity_filter.hpp').write_text('#pragma once\n')
    (root/'pch.hpp').write_text('#pragma once\n')
    flags = [f for f in shlex.split(subprocess.check_output([str(config),'--cflags'], text=True)) if f!='-DNDEBUG']
    libs = shlex.split(subprocess.check_output([str(config),'--ldflags'], text=True))
    binary=root/'host'
    result=subprocess.run(['c++', *native_test_compile_flags('c++',sanitise=False), '-std=c++14','-ffp-contract=off','-UNDEBUG',
        '-Wno-deprecated-register','-Wno-writable-strings','-I'+str(root), *flags,str(root/'host.cpp'),*libs,'-o',str(binary)], capture_output=True,text=True)
    if result.returncode:raise AssertionError(result.stdout+result.stderr)
    return binary


class StockTurretFilterTests(unittest.TestCase):
    def test_real_factory_base_receipts_prediction_and_original_detached_turret(self):
        with tempfile.TemporaryDirectory(prefix='stock-turret-filter-') as folder:
            binary=build_host(folder)
            environment=dict(os.environ)
            client=os.environ.get('WOT_0922_CLIENT')
            if client and all(importlib.util.find_spec(name) for name in ('pefile','unicorn')):
                from stock_turret_filter_oracle import OriginalTurretFilter,receipt_cases
                cases=receipt_cases(OriginalTurretFilter(Path(client)/'WorldOfTanks.exe'))
                path=Path(folder)/'original-receipts.json';path.write_text(json.dumps(cases))
                environment['WOT_TURRET_ORACLE_CASES']=str(path)
            result=subprocess.run([str(binary),str(ROOT/'tests/stock_turret_filter_consumer.py')],capture_output=True,text=True,env=environment)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('Actual SDK turret factory',result.stdout)
            if client:
                self.assertIn('Original DetachedTurret ctor/prerequisites/enter/timer',result.stdout)
            if 'WOT_TURRET_ORACLE_CASES' in environment:
                self.assertIn('32 original turret gravity/EMA records matched',result.stdout)

    def test_original_factory_gravity_copy_and_vtable_contract(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client or not all(importlib.util.find_spec(name) for name in ('pefile','unicorn')):
            self.skipTest('Exact #1513 executable, pefile and Unicorn required')
        from stock_turret_filter_oracle import OriginalTurretFilter,OWNER,WRAPPER
        original=OriginalTurretFilter(Path(client)/'WorldOfTanks.exe')
        for requested,keywords in [(0,0),(123,456)]:
            result,events,calls=original.factory(requested_type=requested,keywords=keywords)
            self.assertEqual(result,WRAPPER+4)
            self.assertEqual(events,[['allocate',0x40],['wrapper-base',WRAPPER,0x1a0e3d8]])
            self.assertFalse(calls)
        for argc in (1,2,17):
            result,events,calls=original.factory(argc)
            self.assertEqual(result,0);self.assertFalse(events)
            self.assertEqual(calls,[['argCountError','PyWGVehicleFilter',0,0]])
        for flag in (0,1):
            result,events,reserved=original.create(flag)
            self.assertEqual(result,OWNER);self.assertEqual(reserved,flag)
            self.assertEqual(events,[['allocate',0x488],['native-base',OWNER,WRAPPER],['settings',WRAPPER,OWNER]])
        self.assertEqual(original.copy(),(True,1,[['base-copy'],['turret-rtti']]))
        self.assertEqual(original.copy(turret=False),(False,0,[['base-copy'],['turret-rtti']]))
        self.assertEqual(original.copy(base_ok=False),(False,0,[['base-copy']]))
        self.assertEqual(struct.unpack('<I',original.u.mem_read(0x14be8dc+0x1c,4))[0],0x6e6330)
        self.assertEqual(struct.unpack('<I',original.u.mem_read(0x14be8dc+0x14,4))[0],0x6e6440)
        result=original.receipt([3.]*12,[0,0,0],[1,2,3],.1,False)
        self.assertEqual([struct.unpack('<I',struct.pack('<f',v))[0]for v in result[6:]],[0,0xc11d1eb8,0,0,0,0])
        self.assertEqual(result[:6],[3.]*6)
