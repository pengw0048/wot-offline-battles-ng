"""Run fixed upstream checks against the actual embedded builtin module.

Only their extension loader is redirected: the returned object is the real
compiled module. Numerical algorithms, typed owners and engine capability
callbacks are unchanged. Each selected check runs in a fresh interpreter.
"""
from __future__ import print_function
import imp
import os
import re
import runpy
import shutil
import stat
import struct
import sys
import threading

sys.dont_write_bytecode = True
root, expected_path, selected = sys.argv[1:]
assert sys.version_info[:3] == (2,7,7), sys.version
import offline_math_batch_native as backend

expected = tuple(open(expected_path,'rb').read().splitlines())
actual = tuple(sorted(name for name in dir(backend) if not name.startswith('_')))
assert len(expected) == 103 and actual == tuple(sorted(expected)), (actual,expected)
for name in expected:
    assert callable(getattr(backend,name)), name
assert isinstance(backend.thread_cpu_seconds(),float)
first = backend.thread_cpu_seconds()
assert first >= 0.0
assert backend.thread_cpu_seconds() >= first
window = backend.window_state()
assert isinstance(window,tuple) and len(window) == 3, window
assert all(type(value) is int and -1 <= value <= 1 for value in window), window
assert window[2] in (0,1), window
assert backend.window_state(1) is None

load_dynamic = imp.load_dynamic
def builtin_loader(name,path,*args):
    if name == 'offline_math_batch_native':
        return backend
    if name == 'native_chunk_vector':
        return __import__('native_chunk_vector')
    return load_dynamic(name,path,*args)
imp.load_dynamic = builtin_loader
os.environ['WOT_NATIVE_CHUNK_VECTOR'] = os.path.join(root,'builtin-vector.pyd')

def tool_fixtures(source):
    """Retain actual nonfinite double inputs despite this runtime's text parser.

    The embedded build currently parses 'nan' as -0 and 'inf' as 1e300.
    These private check/reference copies only replace literal constructors
    with their intended IEEE bytes. Original sources and algorithms stay intact.
    """
    fixture = os.path.join(os.path.dirname(source),'check-fixtures')
    tools = os.path.join(fixture,'tools')
    if not os.path.isdir(tools):
        os.makedirs(tools)
    if not os.path.isdir(os.path.join(fixture,'src')):
        shutil.copytree(os.path.join(source,'src'),os.path.join(fixture,'src'))
    pattern = re.compile(r"float\(\s*(['\"])([+-]?(?:nan|inf(?:inity)?))\1\s*\)", re.I)
    changes = [0]
    def encoded(match):
        name = match.group(2).lower().replace('infinity','inf').lstrip('+')
        bits = {'nan':'000000000000f87f','-nan':'000000000000f8ff',
                'inf':'000000000000f07f','-inf':'000000000000f0ff'}[name]
        changes[0] += 1
        return "__import__('struct').unpack('<d',%r.decode('hex'))[0]" % bits
    for name in os.listdir(os.path.join(source,'tools')):
        if not name.endswith('.py'):
            continue
        data = open(os.path.join(source,'tools',name),'rb').read()
        data = pattern.sub(encoded,data)
        with open(os.path.join(tools,name),'wb') as output:
            output.write(data)
    # Reference routines also use literal infinity as a distance sentinel.
    # Preserve that actual value instead of comparing native Inf to a finite
    # 1e300 created by this build's text parser. Never alter the source tree.
    for base, unused_dirs, names in os.walk(os.path.join(source,'src')):
        for name in names:
            if not name.endswith('.py'):
                continue
            path = os.path.join(base,name)
            original = open(path,'rb').read()
            data = pattern.sub(encoded,original)
            if data == original:
                continue
            target = os.path.join(fixture,os.path.relpath(path,source))
            os.chmod(target,os.stat(target).st_mode | stat.S_IWRITE)
            with open(target,'wb') as output:
                output.write(data)
    graph = os.path.join(fixture,'navgraphs')
    if not os.path.isdir(graph):
        os.makedirs(graph)
    for name in ('100_thepit.json','08_ruinberg.json'):
        shutil.copyfile(os.path.join(source,'navgraphs',name),os.path.join(graph,name))
    print('Private upstream check fixture IEEE literal substitutions:',changes[0])
    return tools

tools = os.path.join(root,'tools') if selected == 'boundaries' else tool_fixtures(root)
reference_root = root if selected == 'boundaries' else os.path.dirname(tools)
scripts = os.path.join(reference_root,'src','res','scripts','client')
sys.path.insert(0,tools)
sys.path.insert(0,scripts)
sys.path.insert(0,os.path.join(root,'tests'))
if selected == 'boundaries':
    for text in ('nan','inf','-inf'):
        print('Embedded float text input:',text,'bits',struct.pack('<d',float(text)).encode('hex'))
    assert backend.sim_open(-1,1) is None
    assert backend.sim_open(1,-1) is None
    first = backend.sim_open(17,23)
    assert first > 0 and backend.sim_lifetime(first) == (17,23)
    assert backend.sim_close(first) == 1
    assert backend.sim_close(first) == 1
    assert backend.sim_lifetime(first) is None
    second = backend.sim_open(17,23)
    assert second > 0 and second != first
    assert backend.sim_lifetime(second) == (17,23)
    assert backend.sim_weapon_pending(first) is None
    assert backend.sim_weapon_pending(second) == ()
    assert backend.sim_close(second) == 1
    print('Actual CPython 2.7.7: 103 exports / typed identity / stale-handle retirement / Win32 window and thread clock PASS')
else:
    path = os.path.join(tools,selected)
    assert os.path.isfile(path), path
    # The projectile cursor's third argument means a prior-source comparison,
    # whereas the other selected checks use it as CLIENT_SCRIPTS.
    sys.argv = [path,os.path.join(root,'builtin.pyd')]
    if selected != 'check_native_projectile_cursor.py':
        sys.argv.append(scripts)
    if selected == 'check_native_navigation_query.py':
        sys.argv = sys.argv[:2] + ['--pairs','1']
    elif selected == 'check_native_simulation_control.py':
        sys.argv = [path,'--extension',os.path.join(root,'builtin.pyd'),'--client-scripts',scripts]
    elif selected == 'check_native_simulation_motion.py':
        sys.argv = sys.argv[:2]
    runpy.run_path(path,run_name='__main__')
    print('Actual embedded builtin check PASS:',selected,'caller thread',threading.current_thread().ident)
